#include "extract.h"
#include "fsutil.h"
#include "config.h"

#include <archive.h>
#include <archive_entry.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

/* Append a line to the shared debug log so extraction problems are diagnosable
 * on-device (e.g. an unsupported RAR compression method). */
static void ex_log(const char *fmt, ...) {
    /* Sampled, not per-line: a pathological archive can log per entry, and the
     * stat is the expensive half on an SD card that is also being written to. */
    static unsigned tick = 0;
    if ((tick++ & 63u) == 0) {
        fs_log_rotate(LOG_PATH, LOG_ROTATE_DEBUG);
    }
    fs_mkdir_p(CONFIG_DIR);
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
 * separator. Returns false if the entry should be skipped (e.g. traversal). */
static bool sanitize_rel(const char *in, char *out, size_t out_sz) {
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

    /* 512KB read buffer: SD card access has high per-call overhead on Switch,
     * so fewer, larger reads are significantly faster than many 64KB ones. */
    if (archive_read_open_filename(a, src, 512 * 1024) != ARCHIVE_OK) {
        ex_log("extract: cannot open %s: %s", src, archive_error_string(a));
        archive_read_free(a);
        return -1; /* not a (supported) archive */
    }

    int count = 0;
    int overwrites = 0;
    int skipped = 0;        /* entries the filesystem wouldn't take — see below */
    bool disk_fail = false; /* couldn't write to SD (full?) — don't claim done */
    char last_dir[EX_PATH_MAX] = {0}; /* cache: skip mkdir_p when dir repeats */
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
        bool existed = fs_exists(out);

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

        FILE *f = fopen(out, "wb");
        if (!f) {
            /* One entry the filesystem won't accept: a name FAT rejects, a path
             * too deep for fs_mkdir_p, a reserved name. That is this entry's
             * problem, not the card's — skip it and carry on, because treating
             * it as a disk failure threw away every remaining file in an
             * otherwise fine archive over one awkward name. A real card failure
             * still stops the run: it shows up as the short fwrite below. */
            ex_log("extract: cannot write %s (skipped)", out);
            skipped++;
            continue;
        }
        /* 128KB write buffer: batches small fwrite calls into fewer SD card
         * writes, which is the single biggest extraction bottleneck. */
        char *wbuf = (char *)malloc(128 * 1024);
        if (wbuf) {
            setvbuf(f, wbuf, _IOFBF, 128 * 1024);
        }
        /* For a sparse entry libarchive reports each block's offset straight
         * from the archive, so it is attacker-influenceable. Trust it only as
         * far as the entry's own declared size: FAT has no sparse files, so an
         * out-of-range offset would materialize the whole hole and fill the
         * card. Only a positive size bounds anything: -1 is "not declared"
         * (streamed tar) and 0 would wrongly reject a format that reports the
         * size only after the data, so both mean "no bound available". */
        la_int64_t emax = archive_entry_size_is_set(entry)
                              ? (la_int64_t)archive_entry_size(entry)
                              : -1;
        const void *buff;
        size_t size;
        la_int64_t offset;
        la_int64_t pos = 0;
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
                if (offset != pos) {
                    /* An unchecked seek leaves the position where it was and
                     * puts the block at the wrong place — a silently
                     * mis-assembled file that still counts as extracted. */
                    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) {
                        write_ok = false;
                        disk_fail = true;
                        break;
                    }
                }
                if (fwrite(buff, 1, size, f) != size) {
                    write_ok = false;
                    disk_fail = true;
                    break;
                }
                pos = offset + (la_int64_t)size;
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
        /* 128KB of this entry can still be unwritten, so a full card shows up
         * at fclose rather than at any fwrite above. */
        if (fclose(f) != 0 && write_ok) {
            write_ok = false;
            disk_fail = true;
        }
        free(wbuf);
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

    if (out_overwrites) {
        *out_overwrites = overwrites;
    }
    ex_log("extract: %s -> %d file(s) (%d overwritten, %d skipped) into %s%s",
           src, count, overwrites, skipped, dest_dir,
           disk_fail ? " [DISK WRITE FAILED]" : "");
    archive_read_free(a);
    /* A disk write failure means the extraction is incomplete even if some
     * files landed: report failure so the caller keeps the archive instead of
     * deleting it and claiming success. */
    return disk_fail ? -1 : count;
}
