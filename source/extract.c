#include "extract.h"
#include "fsutil.h"
#include "config.h"

#include <switch.h>
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>

/* Append a line to the shared debug log so extraction problems are diagnosable
 * on-device (e.g. an unsupported RAR compression method). */
static void ex_log(const char *fmt, ...) {
    /* Sampled, not per-line: a pathological archive can log per entry, and the
     * stat is the expensive half on an SD card that is also being written to. */
    static unsigned tick = 0;
    if ((tick++ & 63u) == 0) {
        fs_log_rotate(LOG_PATH, LOG_ROTATE_DEBUG);
    }
    fs_mkdir_p(LOGS_DIR);
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* Room for the destination directory + '/' + a full-length sanitized entry
 * path. Anything longer is skipped rather than truncated, so this only needs to
 * be big enough that no realistic archive entry gets dropped. */
#define EX_PATH_MAX 2048

/* The pipeline hands the SD card work to a second thread so decompression (CPU)
 * and writing (I/O) overlap instead of taking turns on one thread. Data is
 * coalesced into fixed-size chunks before it crosses to the writer, because the
 * card's per-write overhead dominates: 1 MB writes are the single biggest
 * extraction win and we want to keep hitting that size. EX_WQ_SLOTS buffers let
 * the decompressor run ahead of the card by a few chunks. The chunk size is the
 * runtime `ex_chunk_mb` knob (EX_SLOT_DFLT_MB by default, capped at
 * EX_SLOT_MAX_MB so the ring — nslots * chunk — stays bounded); the whole ring
 * is allocated once per archive and freed at the end. */
#define EX_SLOT_DFLT_MB 1
#define EX_SLOT_MAX_MB  4
#define EX_WQ_SLOTS 4

/* Runtime knobs, snapshotted per archive (see extract.h). Only the single
 * extract worker reads them and only the UI thread writes them; a torn read at
 * worst makes one archive run with a mixed setting, harmless for a perf toggle,
 * so no lock. Defaults reproduce the shipped behavior. */
static ExtractTunables g_tun = { false, true, EX_SLOT_DFLT_MB };

void extract_set_tunables(const ExtractTunables *t) {
    if (!t) {
        return;
    }
    g_tun.bench = t->bench;
    g_tun.prealloc = t->prealloc;
    /* Accept only the sizes the UI offers; anything else falls back to 1 MB so a
     * stray prefs value can't blow up the ring allocation. */
    g_tun.chunk_mb =
        (t->chunk_mb == 2 || t->chunk_mb == EX_SLOT_MAX_MB) ? t->chunk_mb : 1;
}

static bool ends_with_ci(const char *s, const char *suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcasecmp(s + ls - lf, suffix) == 0;
}

bool is_archive_name(const char *filename) {
    static const char *exts[] = {".zip",     ".7z",      ".rar",
                                 ".tar",     ".tgz",     ".tbz",
                                 ".tbz2",    ".txz",     ".tar.gz",
                                 ".tar.bz2", ".tar.xz",  NULL};
    for (int i = 0; exts[i]; i++) {
        if (ends_with_ci(filename, exts[i])) {
            return true;
        }
    }
    return false;
}

/* Sanitize an archive entry path into a safe relative SD path: drop leading
 * slashes, normalize backslashes, replace FAT-illegal chars, keep '/' as the
 * separator. Returns false if the entry should be skipped (e.g. traversal).
 * Declared in extract.h and shared with source/rar3/rar3_extract.c. */
bool sanitize_rel(const char *in, char *out, size_t out_sz) {
    /* Refuse path traversal, but only when ".." is a whole path segment: a
     * substring test would also drop legitimate names like "Zelda..Oracle.zip". */
    for (const char *p = in; *p; p++) {
        if (p[0] != '.' || p[1] != '.') {
            continue;
        }
        bool at_start = (p == in) || p[-1] == '/' || p[-1] == '\\';
        bool at_end = (p[2] == '\0') || p[2] == '/' || p[2] == '\\';
        if (at_start && at_end) {
            return false;
        }
    }
    while (*in == '/' || *in == '\\') {
        in++;
    }
    size_t o = 0;
    for (; *in && o + 1 < out_sz; in++) {
        char c = *in;
        if (c == '\\') {
            c = '/';
        }
        if (c != '/' &&
            (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
             c == '>' || c == '|' || (unsigned char)c < 0x20)) {
            c = '_';
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return o > 0;
}

/* ---- write pipeline -------------------------------------------------------
 * A bounded ring of full-size chunks feeds a writer thread pinned to a core
 * other than the decompressor's. The producer (extract_archive) fills the slot
 * at `head`, publishes it, and moves on; the writer drains from `tail` to the
 * SD card. When no second core or no memory is available we degrade to writing
 * synchronously on the producer thread, which matches the pre-pipeline speed. */
typedef struct {
    char  *data;    /* points into ExWriter.buf */
    size_t len;     /* bytes to write */
    off_t  offset;  /* file offset this chunk begins at */
} ExSlot;

typedef struct {
    ExSlot  slots[EX_WQ_SLOTS];
    char   *buf;          /* single allocation backing every slot */
    size_t  slot_sz;      /* bytes per slot (the runtime chunk size) */
    int     nslots;       /* 0 = no buffer (write each block straight through) */
    int     head, tail, count;
    Mutex   mtx;
    CondVar can_push;     /* a slot freed */
    CondVar can_pop;      /* a slot filled, or stop requested */
    FILE   *f;            /* current output file; producer opens, writer writes */
    off_t   pos;          /* writer's position in f */
    bool    disk_fail;    /* a seek/write/close failed: the run is doomed */
    bool    stop;         /* producer asked the writer to exit */
    Thread  thread;
    bool    started;      /* writer thread is running (pipelined mode) */
} ExWriter;

/* The one place bytes actually reach the card. Called by the writer thread in
 * pipelined mode, or directly by the producer in synchronous mode — never both
 * at once, since the producer drains the ring before it touches f again. */
static void ex_do_write(ExWriter *w, const void *data, size_t len, off_t offset) {
    if (w->disk_fail || !w->f) {
        return;
    }
    if (offset != w->pos) {
        /* An unchecked seek would leave the position where it was and drop the
         * chunk at the wrong place — a silently mis-assembled file. */
        if (fseeko(w->f, offset, SEEK_SET) != 0) {
            w->disk_fail = true;
            return;
        }
        w->pos = offset;
    }
    if (len && fwrite(data, 1, len, w->f) != len) {
        w->disk_fail = true;
        return;
    }
    w->pos += (off_t)len;
}

static void ex_writer_main(void *arg) {
    ExWriter *w = (ExWriter *)arg;
    for (;;) {
        mutexLock(&w->mtx);
        while (w->count == 0 && !w->stop) {
            condvarWait(&w->can_pop, &w->mtx);
        }
        if (w->count == 0 && w->stop) {
            mutexUnlock(&w->mtx);
            return;
        }
        int idx = w->tail;
        mutexUnlock(&w->mtx);

        /* disk_fail is set here and only read by the producer after it re-locks
         * (drain / begin), so the unlock below publishes it. On failure we keep
         * popping so the ring still empties and the producer's drain returns. */
        ex_do_write(w, w->slots[idx].data, w->slots[idx].len, w->slots[idx].offset);

        mutexLock(&w->mtx);
        w->tail = (w->tail + 1) % w->nslots;
        w->count--;
        condvarWakeOne(&w->can_push);
        mutexUnlock(&w->mtx);
    }
}

/* Wait until the slot at head is free to fill. Returns false if the writer has
 * reported a disk failure. */
static bool ex_begin_slot(ExWriter *w) {
    mutexLock(&w->mtx);
    while (w->count == w->nslots && !w->disk_fail) {
        condvarWait(&w->can_push, &w->mtx);
    }
    bool ok = !w->disk_fail;
    mutexUnlock(&w->mtx);
    return ok;
}

/* Hand the freshly-filled head slot to the writer. */
static void ex_publish(ExWriter *w, size_t len, off_t offset) {
    w->slots[w->head].len = len;
    w->slots[w->head].offset = offset;
    mutexLock(&w->mtx);
    w->head = (w->head + 1) % w->nslots;
    w->count++;
    condvarWakeOne(&w->can_pop);
    mutexUnlock(&w->mtx);
}

/* Block until every queued chunk has been written. */
static void ex_drain(ExWriter *w) {
    mutexLock(&w->mtx);
    while (w->count > 0) {
        condvarWait(&w->can_push, &w->mtx);
    }
    mutexUnlock(&w->mtx);
}

/* Flush the chunk currently accumulating in the head slot. In pipelined mode it
 * goes to the writer and we wait for the next free slot (usually immediate); in
 * synchronous mode we write it here. */
static bool ex_flush(ExWriter *w, size_t *fill, off_t run_off, bool threaded) {
    if (*fill == 0) {
        return true;
    }
    if (threaded) {
        ex_publish(w, *fill, run_off);
        *fill = 0;
        return ex_begin_slot(w);
    }
    ex_do_write(w, w->slots[w->head].data, *fill, run_off);
    *fill = 0;
    return !w->disk_fail;
}

/* Accumulate one decompressed block into full-size chunks, flushing whenever a
 * chunk fills or the offset jumps (sparse entry). With no buffer at all, write
 * the block straight through. */
static bool ex_put(ExWriter *w, const void *buff, size_t size, off_t offset,
                   size_t *fill, off_t *run_off, bool *have, bool threaded) {
    if (w->nslots == 0) {
        ex_do_write(w, buff, size, offset);
        return !w->disk_fail;
    }
    const char *src = (const char *)buff;
    off_t at = offset;
    size_t rem = size;
    while (rem > 0) {
        if (*have && at != *run_off + (off_t)*fill) {
            if (!ex_flush(w, fill, *run_off, threaded)) {
                return false;
            }
            *have = false;
        }
        if (!*have) {
            *run_off = at;
            *fill = 0;
            *have = true;
        }
        size_t space = w->slot_sz - *fill;
        size_t n = rem < space ? rem : space;
        memcpy(w->slots[w->head].data + *fill, src, n);
        *fill += n;
        src += n;
        rem -= n;
        at += (off_t)n;
        if (*fill == w->slot_sz) {
            if (!ex_flush(w, fill, *run_off, threaded)) {
                return false;
            }
            *have = false;
        }
    }
    return true;
}

/* Pick a core for the writer thread that is NOT the one the decompressor is
 * running on — same-core "overlap" would just add a memcpy and serialize, so
 * returning -1 (no distinct core) tells the caller to stay synchronous. */
static int ex_pick_writer_core(void) {
    u64 mask = 0;
    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return -1;
    }
    u32 self = svcGetCurrentProcessorNumber();
    for (int c = 1; c < 4; c++) { /* skip core 0: the UI/render thread */
        if ((mask & (1ull << c)) && (u32)c != self) {
            return c;
        }
    }
    return -1;
}

/* Open the output file. Replaces a pre-open fs_exists() stat: O_EXCL creates the
 * file and, on the fresh-extract common case, that is the only syscall; if it
 * already exists we learn so (for the overwrite count) and reopen truncating.
 *
 * When the entry's decompressed size is known (prealloc > 0) we grow the file to
 * its final size up front with ftruncate. On FAT/exFAT that lets the filesystem
 * reserve the cluster run in one go instead of extending the chain — and dirtying
 * the FAT/allocation bitmap — on every write as the file grows. That cuts write
 * overhead on large ROMs and keeps the file contiguous, which also speeds the
 * emulator's later reads. It is best-effort: if ftruncate fails we just fall back
 * to a growing file, and because the writer overwrites every byte sequentially
 * (0..prealloc) the reservation never leaves a visible gap. A short/corrupt entry
 * that never fills the reservation is removed by the caller's error path. */
static FILE *ex_open_out(const char *path, off_t prealloc, bool *existed) {
    *existed = false;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0 && errno == EEXIST) {
        *existed = true;
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    if (fd < 0) {
        return NULL;
    }
    if (prealloc > 0) {
        (void)ftruncate(fd, prealloc);
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return NULL;
    }
    return f;
}

int extract_archive(const char *src, const char *dest_dir, extract_cb cb,
                    void *userdata, int *out_overwrites) {
    if (out_overwrites) {
        *out_overwrites = 0;
    }
    struct archive *a = archive_read_new();
    if (!a) {
        return -1;
    }
    archive_read_support_filter_all(a); /* gzip/bzip2/xz/lzma/zstd/lz4 */
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    archive_read_support_format_tar(a);

    /* 1MB read buffer: SD card access has high per-call overhead on Switch,
     * so fewer, larger reads are significantly faster than many small ones. */
    if (archive_read_open_filename(a, src, 1024 * 1024) != ARCHIVE_OK) {
        ex_log("extract: cannot open %s: %s", src, archive_error_string(a));
        archive_read_free(a);
        return -1; /* not a (supported) archive */
    }

    /* Bring up the write pipeline. It engages only when there is a distinct core
     * to run the writer on AND room for the ring; otherwise we fall back to a
     * single reusable chunk written synchronously (== the old 1 MB batching), or
     * as a last resort no buffer at all. Any of these still extracts correctly. */
    /* Snapshot the runtime knobs once so a mid-archive prefs change can't shift
     * the chunk size out from under the ring we're about to size. */
    ExtractTunables tun = g_tun;
    size_t slot_sz = (size_t)tun.chunk_mb * 1024 * 1024;

    ExWriter w;
    memset(&w, 0, sizeof(w));
    mutexInit(&w.mtx);
    condvarInit(&w.can_push);
    condvarInit(&w.can_pop);
    w.slot_sz = slot_sz;
    int wcore = ex_pick_writer_core();
    int want = (wcore >= 0) ? EX_WQ_SLOTS : 1;
    w.buf = (char *)malloc((size_t)want * slot_sz);
    if (!w.buf && want > 1) {
        want = 1;
        w.buf = (char *)malloc(slot_sz);
    }
    if (w.buf) {
        w.nslots = want;
        for (int i = 0; i < want; i++) {
            w.slots[i].data = w.buf + (size_t)i * slot_sz;
        }
    }
    bool threaded = false;
    if (w.nslots >= 2 && wcore >= 0) {
        if (R_SUCCEEDED(threadCreate(&w.thread, ex_writer_main, &w, NULL,
                                     0x20000, 0x3B, wcore)) &&
            R_SUCCEEDED(threadStart(&w.thread))) {
            threaded = true;
            w.started = true;
        }
    }

    int count = 0;
    int overwrites = 0;
    int skipped = 0;        /* entries the filesystem wouldn't take — see below */
    bool disk_fail = false; /* couldn't write to SD (full?) — don't claim done */
    char last_dir[EX_PATH_MAX] = {0}; /* cache: skip mkdir_p when dir repeats */
    /* Benchmark accounting: decompressed bytes actually handed to the writer, and
     * a wall-clock start for the whole archive. Cheap to keep even when the bench
     * toggle is off; only the final log write is gated on it. */
    uint64_t bench_bytes = 0;
    u64 bench_start = armGetSystemTick();
    struct archive_entry *entry;
    for (;;) {
        int rc = archive_read_next_header(a, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            ex_log("extract: header rc=%d in %s: %s", rc, src,
                   archive_error_string(a));
            break;
        }
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            continue;
        }
        const char *name = archive_entry_pathname(entry);
        if (!name) {
            continue;
        }
        char rel[1024];
        if (!sanitize_rel(name, rel, sizeof(rel))) {
            continue;
        }
        char out[EX_PATH_MAX];
        int on = snprintf(out, sizeof(out), "%s/%s", dest_dir, rel);
        if (on < 0 || (size_t)on >= sizeof(out)) {
            /* Too long to represent. Silently truncating would map two entries
             * onto one path and have the second overwrite the first, so drop
             * this one and keep going. */
            ex_log("extract: path too long, skipped '%s'", rel);
            skipped++;
            continue;
        }

        /* Only call mkdir_p when the directory actually changes. */
        char dir[EX_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", out);
        char *sl = strrchr(dir, '/');
        if (sl) {
            *sl = '\0';
            if (strcmp(dir, last_dir) != 0) {
                fs_mkdir_p(dir);
                snprintf(last_dir, sizeof(last_dir), "%s", dir);
            }
        }

        /* The entry's declared decompressed size, used twice below: to
         * preallocate the output file, and to bound sparse-block offsets. Only a
         * positive size means anything: -1 is "not declared" (streamed tar) and
         * 0 would wrongly reject a format that reports the size only after the
         * data, so both mean "no size available" (no preallocation, no bound). */
        la_int64_t emax = archive_entry_size_is_set(entry)
                              ? (la_int64_t)archive_entry_size(entry)
                              : -1;

        bool existed = false;
        FILE *f = ex_open_out(
            out, (off_t)(tun.prealloc && emax > 0 ? emax : 0), &existed);
        if (!f) {
            /* One entry the filesystem won't accept: a name FAT rejects, a path
             * too deep for fs_mkdir_p, a reserved name. That is this entry's
             * problem, not the card's — skip it and carry on, because treating
             * it as a disk failure threw away every remaining file in an
             * otherwise fine archive over one awkward name. A real card failure
             * still stops the run: it shows up as a short write below. */
            ex_log("extract: cannot write %s (skipped)", out);
            skipped++;
            continue;
        }
        /* We hand the writer whole chunks already (slot_sz each), so stdio
         * buffering would only add a second copy — write straight through. */
        setvbuf(f, NULL, _IONBF, 0);
        w.f = f;
        w.pos = 0;

        /* For a sparse entry libarchive reports each block's offset straight
         * from the archive, so it is attacker-influenceable. The bound check
         * below trusts it only as far as emax (computed above): FAT has no
         * sparse files, so an out-of-range offset would materialize the whole
         * hole and fill the card. */
        const void *buff;
        size_t size;
        la_int64_t offset;
        size_t fill = 0;    /* bytes accumulated in the current chunk */
        off_t run_off = 0;  /* file offset that chunk begins at */
        bool have = false;
        bool write_ok = true;
        bool cancelled = false;
        for (;;) {
            int r = archive_read_data_block(a, &buff, &size, &offset);
            if (r == ARCHIVE_EOF) {
                break;
            }
            if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
                ex_log("extract: '%s' data rc=%d: %s", rel, r,
                       archive_error_string(a));
                write_ok = false;
                break;
            }
            if (size > 0) {
                if (offset < 0 ||
                    (emax > 0 && ((la_int64_t)size > emax ||
                                  offset > emax - (la_int64_t)size))) {
                    ex_log("extract: '%s' block outside declared size, dropped",
                           rel);
                    write_ok = false; /* bad archive, not a bad card */
                    break;
                }
                if (!ex_put(&w, buff, size, (off_t)offset, &fill, &run_off,
                            &have, threaded)) {
                    write_ok = false; /* writer hit a disk failure */
                    break;
                }
                bench_bytes += size;
            }
            /* Per-block progress so one huge entry still visibly moves (and
             * a cancel takes effect mid-file, not only between entries). */
            if (cb && !cb(userdata, rel, count,
                          (uint64_t)archive_filter_bytes(a, -1))) {
                write_ok = false;
                cancelled = true;
                break;
            }
        }
        /* Push the last partial chunk only if the entry was good, then wait for
         * the writer to finish so f is ours to close. Draining even on the error
         * path keeps the writer off f before fclose. */
        if (write_ok) {
            if (!ex_flush(&w, &fill, run_off, threaded)) {
                write_ok = false;
            }
        }
        if (threaded) {
            ex_drain(&w);
        }
        if (w.disk_fail) {
            write_ok = false;
            disk_fail = true;
        }
        /* A short chunk can still be unwritten in f until close on some paths,
         * so a full card can surface here rather than at any write above. */
        if (fclose(f) != 0 && write_ok) {
            write_ok = false;
            disk_fail = true;
            w.disk_fail = true;
        }
        w.f = NULL;
        if (!write_ok) {
            remove(out);
            if (cancelled || disk_fail) {
                break; /* cancelled, or SD failed: the rest would fail too */
            }
            continue;
        }
        count++;
        if (existed) {
            overwrites++;
        }
        if (cb && !cb(userdata, rel, count,
                      (uint64_t)archive_filter_bytes(a, -1))) {
            break;
        }
    }

    /* Tear the pipeline down: the ring is already drained (every entry drains
     * before it closes its file), so the writer just sees stop and exits. */
    if (w.started) {
        mutexLock(&w.mtx);
        w.stop = true;
        condvarWakeOne(&w.can_pop);
        mutexUnlock(&w.mtx);
        threadWaitForExit(&w.thread);
        threadClose(&w.thread);
    }
    free(w.buf);

    if (out_overwrites) {
        *out_overwrites = overwrites;
    }
    ex_log("extract: %s -> %d file(s) (%d overwritten, %d skipped) into %s%s",
           src, count, overwrites, skipped, dest_dir,
           disk_fail ? " [DISK WRITE FAILED]" : "");

    /* One line per archive so A/B runs diff cleanly. Records the settings in
     * effect alongside the number, so a log left over from an earlier config is
     * self-describing. Skipped entirely when the toggle is off. */
    if (tun.bench) {
        double secs = (double)armTicksToNs(armGetSystemTick() - bench_start) / 1e9;
        double mbps =
            secs > 0.0 ? ((double)bench_bytes / (1024.0 * 1024.0)) / secs : 0.0;
        const char *base = strrchr(src, '/');
        fs_mkdir_p(LOGS_DIR);
        FILE *bf = fopen(EXBENCH_PATH, "a");
        if (bf) {
            fprintf(bf,
                    "archive=%s files=%d bytes=%llu secs=%.2f MBps=%.1f "
                    "prealloc=%d chunk=%dMB threaded=%d%s\n",
                    base ? base + 1 : src, count,
                    (unsigned long long)bench_bytes, secs, mbps,
                    tun.prealloc ? 1 : 0, tun.chunk_mb, threaded ? 1 : 0,
                    disk_fail ? " DISK_FAIL" : "");
            fclose(bf);
        }
    }
    archive_read_free(a);
    /* A disk write failure means the extraction is incomplete even if some
     * files landed: report failure so the caller keeps the archive instead of
     * deleting it and claiming success. */
    return disk_fail ? -1 : count;
}
