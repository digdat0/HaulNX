#include "queue.h"
#include "net.h"
#include "extract.h"
#include "rar3.h"
#include "fsutil.h"
#include "config.h"
#include "jsonutil.h"
#include "md5.h"

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>

static QueueItem g_items[QUEUE_MAX];
static uint32_t g_seq = 1;
static Mutex g_mtx;
#define MAX_DL_THREADS 10 /* matches the 1–10 range the settings UI offers */
static Thread g_dl_threads[MAX_DL_THREADS];
static int g_dl_count = 0;
/* How many downloads may run at once. All MAX_DL_THREADS workers are always
 * started; idle ones just don't pick work. That way changing the setting
 * takes effect immediately instead of on the next launch. */
static volatile int g_max_dl = 1;
/* When true, a downloaded archive is kept compressed instead of being
 * extracted: queue_add clears the item's is_archive flag so it lands as a plain
 * file. Off by default (extract as usual). Set from the Downloads setting. */
static bool g_keep_archives = false;
/* Download-rate limits in bytes/sec (0 = unlimited). g_rate_all_bps is the
 * combined budget across all active downloads; g_rate_item_bps caps each one.
 * g_active_dl tracks how many transfers are running right now, so the global
 * budget can be split fair-share between them. All read live from the worker
 * threads' rate callback; g_active_dl is touched from several workers at once,
 * hence the atomic ops. */
static volatile int64_t g_rate_all_bps = 0;
static volatile int64_t g_rate_item_bps = 0;
static volatile int g_active_dl = 0;
/* Never cap a single transfer below this: curl aborts a download that averages
 * under CURLOPT_LOW_SPEED_LIMIT (30 B/s) for 30s, so a tiny global budget split
 * across many downloads must still leave each comfortably above that floor. The
 * floor can let the combined rate slightly exceed a very small global cap — a
 * deliberate trade so throttling never kills a transfer outright. */
#define RATE_FLOOR_BPS 4096
static Thread g_ex_thread;
static volatile bool g_run = false;
/* User "pause all": a persistent latch the scheduler checks before picking any
 * new work. Unlike a per-item Q_PAUSED (which the scheduler auto-resumes), this
 * keeps everything parked until a "retry all paused" (resume) clears it. */
static volatile bool g_paused = false;
/* Set while a worker has declined to start an item because the card is nearly
 * full. The item stays queued — the hold clears by itself once space appears. */
static volatile bool g_space_hold = false;
static const char *g_roms_root = "sdmc:/roms"; /* overwritten by queue_init() */

/* Cores this process may use OTHER than core 0 (where the UI/render thread
 * lives). The CPU-heavy workers (extraction, md5) are pinned here so they run
 * truly in parallel with rendering instead of preempting it on a shared core.
 * Homebrew under hb-loader typically has cores 0..2; filled by detect_worker_cores. */
static int g_worker_cores[4];
static int g_worker_core_count = 0;

static void detect_worker_cores(void) {
    g_worker_core_count = 0;
    u64 mask = 0;
    if (R_SUCCEEDED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        for (int c = 1; c < 4; c++) { /* skip core 0: reserved for the UI */
            if (mask & (1ull << c)) {
                g_worker_cores[g_worker_core_count++] = c;
            }
        }
    }
}

/* Pick a core for download worker i: round-robin across the available non-zero
 * cores, leaving the highest one free for extraction when we have >=2. Returns
 * -2 (default/creation core) as a fallback when no non-zero core is available. */
static int dl_worker_core(int i) {
    if (g_worker_core_count == 0) {
        return -2;
    }
    if (g_worker_core_count == 1) {
        return g_worker_cores[0]; /* share with extract: downloads are I/O-bound */
    }
    return g_worker_cores[i % (g_worker_core_count - 1)];
}

/* The extract worker (pure-CPU decompression, the worst UI offender) gets the
 * highest available non-zero core to itself where possible. */
static int ex_worker_core(void) {
    return g_worker_core_count > 0 ? g_worker_cores[g_worker_core_count - 1] : -2;
}

/* ---- network state --------------------------------------------------- */

/* Cached connectivity check (nifm polled at most every ~2s) so the workers can
 * pause downloads when the connection drops and resume when it returns,
 * instead of failing item after item while offline. */
static Mutex g_net_mtx;
static bool g_net_up = true;
static u64 g_net_last = 0;

static bool net_is_up(void) {
    mutexLock(&g_net_mtx);
    u64 now = armGetSystemTick();
    if (g_net_last == 0 || armTicksToNs(now - g_net_last) >= 2000000000ULL) {
        NifmInternetConnectionType t = (NifmInternetConnectionType)0;
        u32 w = 0;
        NifmInternetConnectionStatus st = (NifmInternetConnectionStatus)0;
        g_net_up = R_SUCCEEDED(nifmGetInternetConnectionStatus(&t, &w, &st)) &&
                   st == NifmInternetConnectionStatus_Connected;
        g_net_last = now;
    }
    bool up = g_net_up;
    mutexUnlock(&g_net_mtx);
    return up;
}

/* Failure-path check: force a fresh nifm query. The cached result can be up
 * to ~2s stale, which would misclassify an abrupt network drop (sockets die
 * instantly in airplane mode) as a download failure. */
static bool net_is_up_fresh(void) {
    mutexLock(&g_net_mtx);
    g_net_last = 0;
    mutexUnlock(&g_net_mtx);
    return net_is_up();
}

/* ---- CPU boost (verify / extract) ------------------------------------ */

/* MD5 verification and archive decompression are CPU/memory-bound. Nintendo's
 * FastLoad boost profile raises the CPU (~1785MHz) and memory (~1600MHz) clocks
 * — the same firmware-sanctioned profile games use on loading screens; it is NOT
 * an overclock. It throttles the GPU to minimum, so we hold it ONLY across the
 * actual verify/extract calls and drop back to Normal immediately after.
 *
 * The mode is system-global, and several downloads can verify while an extract
 * runs, so it's reference-counted: the first worker to enter a heavy phase turns
 * it on, the last to leave turns it off. */
static Mutex g_boost_mtx;
static int g_boost_refs = 0;

static void boost_acquire(void) {
    mutexLock(&g_boost_mtx);
    if (g_boost_refs++ == 0) {
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    }
    mutexUnlock(&g_boost_mtx);
}

static void boost_release(void) {
    mutexLock(&g_boost_mtx);
    if (g_boost_refs > 0 && --g_boost_refs == 0) {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    }
    mutexUnlock(&g_boost_mtx);
}

/* ---- worker-side processing ----------------------------------------- */

typedef struct {
    QueueItem *it;
    uint64_t last_now;
    uint64_t last_tick;
} DlCtx;

/* Live per-transfer rate cap (bytes/sec, 0 = unlimited), passed to
 * http_download and polled throughout the download. Fair-share: the global
 * budget is divided across the transfers currently active, then clamped by the
 * per-item cap and lifted to the stall floor. Reads the limits and active count
 * without locking — each is a single word, and an occasional stale read only
 * nudges the cap slightly for one progress tick before the next corrects it. */
static uint64_t dl_rate_cb(void *ud) {
    (void)ud;
    int64_t all = g_rate_all_bps;
    int64_t item = g_rate_item_bps;
    if (all <= 0 && item <= 0) {
        return 0; /* unthrottled */
    }
    int64_t eff;
    if (all > 0) {
        int n = __atomic_load_n(&g_active_dl, __ATOMIC_RELAXED);
        if (n < 1) {
            n = 1;
        }
        eff = all / n;
        if (item > 0 && item < eff) {
            eff = item; /* per-item cap is tighter than this transfer's share */
        }
    } else {
        eff = item; /* only a per-item cap is set */
    }
    if (eff > 0 && eff < RATE_FLOOR_BPS) {
        eff = RATE_FLOOR_BPS;
    }
    return (uint64_t)eff;
}

static int dl_progress(void *ud, uint64_t now, uint64_t total) {
    DlCtx *ctx = (DlCtx *)ud;
    QueueItem *it = ctx->it;
    it->now = now;
    it->total = total;
    /* Forward progress since the last sample proves the retried attempt is
     * actually moving bytes again -- clear the stall flag right away rather
     * than waiting for the 500ms speed-sample gate below, so "reconnecting"
     * doesn't linger after the connection has already recovered. */
    if (it->stalled && now > ctx->last_now) {
        it->stalled = false;
    }
    uint64_t tick = armGetSystemTick();
    uint64_t dt_ns = armTicksToNs(tick - ctx->last_tick);
    if (dt_ns >= 500000000ULL) {
        uint64_t db = (now >= ctx->last_now) ? (now - ctx->last_now) : 0;
        it->speed = db * 1000000000ULL / dt_ns;
        ctx->last_now = now;
        ctx->last_tick = tick;
    }
    return (it->cancel || it->pause || !g_run) ? 1 : 0;
}

/* Optional post-import hook (see queue.h). NULL unless an add-on module sets it. */
void (*queue_post_import)(QueueItem *it, const char *path, bool is_dir) = NULL;

/* "An item just landed" notification (see queue.h) -- independent of the
 * post-import hook above. NULL unless some subsystem registers it. */
void (*queue_on_landed)(const char *name, const char *path, bool is_dir) = NULL;

/* Runtime enable flag for the post-import hook (user preference). */
static bool g_post_import_enabled = true;
void queue_set_post_import_enabled(bool on) { g_post_import_enabled = on; }

static bool ex_progress(void *ud, const char *entry, int done,
                        uint64_t bytes_read) {
    QueueItem *it = (QueueItem *)ud;
    (void)entry;
    /* Live progress for the UI: archive bytes consumed (vs it->total, the
     * archive size, for a percent bar) and entries completed. The byte-based
     * bar moves even inside one huge entry. */
    it->now = bytes_read;
    it->ex_files = done;
    return !it->cancel && g_run;
}

/* Progress during md5 verification: bytes hashed so far vs the file size, so a
 * large uncompressed ISO (no extract step to follow) shows a moving bar instead
 * of sitting on "vrfy" long enough to look hung. */
static void md5_progress(void *ud, uint64_t done, uint64_t total) {
    QueueItem *it = (QueueItem *)ud;
    it->now = done;
    if (total) {
        it->total = total;
    }
}


/* Append a download outcome to the history log shown in Settings. */
static void log_download(const QueueItem *it, const char *status) {
    fs_mkdir_p(LOGS_DIR);
    char ts[32] = "";
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *tm = localtime_r(&t, &tmv);
    if (tm) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    }

    /* Both history files are things the user reads (and re-downloads from), so
     * they get a generous ceiling before the oldest generation is dropped —
     * but a ceiling all the same, since nothing else ever trims them. One line
     * per completed download makes this cheap to check every time. */
    fs_log_rotate(DLLOG_PATH, LOG_ROTATE_HISTORY);
    fs_log_rotate(DLLOG_JSON, LOG_ROTATE_HISTORY);

    /* Human-readable text log. */
    FILE *f = fopen(DLLOG_PATH, "a");
    if (f) {
        fprintf(f, "%s  %-9s  [%s]  %s", ts, status, it->target, it->name);
        if (it->overwrote == 1) {
            fputs("  (overwrote existing)", f);
        } else if (it->overwrote > 1) {
            fprintf(f, "  (overwrote %d files)", it->overwrote);
        }
        fputc('\n', f);
        fclose(f);
    }

    /* Structured JSON-lines log for re-download. */
    FILE *jf = fopen(DLLOG_JSON, "a");
    if (jf) {
        fputs("{\"ts\":", jf); json_write_escaped(jf, ts);
        fputs(",\"st\":", jf); json_write_escaped(jf, status);
        fputs(",\"name\":", jf); json_write_escaped(jf, it->name);
        fputs(",\"target\":", jf); json_write_escaped(jf, it->target);
        fputs(",\"url\":", jf); json_write_escaped(jf, it->url);
        fprintf(jf, ",\"size\":%llu", (unsigned long long)it->size);
        fputs(",\"md5\":", jf); json_write_escaped(jf, it->md5);
        fprintf(jf, ",\"arc\":%s}\n", it->is_archive ? "true" : "false");
        fclose(jf);
    }
}

/* Turn a remote filename into a safe relative path under our own folders:
 * reject path traversal, drop leading slashes, normalize separators and replace
 * FAT-illegal characters. Returns false if nothing usable remains. */
static bool safe_rel(const char *in, char *out, size_t out_sz) {
    if (!in) {
        return false;
    }
    /* Reject ".." only when it is a whole path segment (real traversal), not as
     * a substring: a substring test also drops legit names like
     * "Zelda..Oracle.zip". Mirrors sanitize_rel() in extract.c. */
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

/* ---- persistence ----------------------------------------------------- */

/* Write the still-pending items to disk so the queue survives an app restart.
 * Assumes g_mtx is held. Credentials (it->auth) are deliberately NOT persisted;
 * they're re-derived from credentials.json on load so the S3 secret lives in
 * only one place. */
/* Batch depth and "a save was skipped" flag, both guarded by g_mtx. */
static int g_batch_depth = 0;
static bool g_batch_dirty = false;

static void save_locked(void) {
    if (g_batch_depth > 0) {
        g_batch_dirty = true; /* one write when the batch closes */
        return;
    }
    fs_mkdir_p(DATA_DIR);
    FILE *f = fopen(QUEUE_STATE_PATH, "wb");
    if (!f) {
        return;
    }
    fputs("{\"items\":[", f);
    bool first = true;
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (g_items[i].external) {
            continue; /* external transfers can't resume across a launch */
        }
        if (s != Q_QUEUED && s != Q_PAUSED && s != Q_DOWNLOADING &&
            s != Q_VERIFYING && s != Q_AWAIT_EXTRACT && s != Q_EXTRACTING) {
            continue; /* only persist outstanding work */
        }
        QueueItem *it = &g_items[i];
        if (!first) {
            fputc(',', f);
        }
        first = false;
        fputs("{\"url\":", f);
        json_write_escaped(f, it->url);
        fputs(",\"name\":", f);
        json_write_escaped(f, it->name);
        fputs(",\"target\":", f);
        json_write_escaped(f, it->target);
        fputs(",\"dest\":", f);
        json_write_escaped(f, it->dest);
        fputs(",\"md5\":", f);
        json_write_escaped(f, it->md5);
        /* "downloaded": the file is fully on disk (past the download phase),
         * so on reload it skips straight to extraction — no re-download, and
         * no network needed. */
        bool downloaded = (s == Q_VERIFYING || s == Q_AWAIT_EXTRACT ||
                           s == Q_EXTRACTING);
        fprintf(f, ",\"size\":%llu,\"is_archive\":%s,\"seq\":%u,"
                   "\"downloaded\":%s}",
                (unsigned long long)it->size, it->is_archive ? "true" : "false",
                it->seq, downloaded ? "true" : "false");
    }
    fputs("]}", f);
    fclose(f);
}

/* This item's .part filename (no directory), and its full path. Every caller
 * that needs to know where an item's temp file lives goes through these two,
 * so they can never drift out of agreement with each other again -- they used
 * to be four hand-copied snprintf calls, and a previous drift between two of
 * them (raw vs sanitized target) already caused a live-download-deleted-out-
 * from-under-itself bug (see queue_cancel_by_part's comment).
 *
 * The name is keyed by target+name+seq, not just target+name: two queue
 * entries that happen to share a name and target (e.g. the same file queued
 * twice, or duplicated in a source collection's metadata) used to collide on
 * the identical .part path. Downloaded concurrently, both sides fopen() it in
 * "wb" and each truncates the other's in-flight write -- symptoms exactly
 * like "download restarts and drops its progress" plus a "write error" on
 * whichever side loses the race to extract a file the other side already
 * consumed. seq is a monotonic per-item counter, persisted in queue.json, so
 * it's unique for the item's whole lifetime including across a relaunch. */
static bool item_part_name(const QueueItem *it, char *out, size_t out_sz) {
    char safe[600], safet[80];
    if (!safe_rel(it->name, safe, sizeof(safe)) ||
        !safe_rel(it->target, safet, sizeof(safet))) {
        return false;
    }
    snprintf(out, out_sz, "%s_%s_%u.part", safet, safe, (unsigned)it->seq);
    return true;
}

static bool item_part_path(const QueueItem *it, char *out, size_t out_sz) {
    char name[700];
    if (!item_part_name(it, name, sizeof(name))) {
        return false;
    }
    snprintf(out, out_sz, "%s/%s", DL_TMP_DIR, name);
    return true;
}

/* Bytes of this item already staged in its .part file (0 if there is none). */
static uint64_t part_size(const QueueItem *it) {
    char tmp[1200];
    if (!item_part_path(it, tmp, sizeof(tmp))) {
        return 0;
    }
    struct stat st;
    if (stat(tmp, &st) != 0 || st.st_size <= 0) {
        return 0;
    }
    return (uint64_t)st.st_size;
}

/* What this item still has to download. Unknown size (0) reports 0 — callers
 * treat the total as a lower bound. */
static uint64_t item_remaining(const QueueItem *it) {
    if (it->size == 0) {
        return 0;
    }
    uint64_t have = it->now;
    if (have == 0) {
        have = part_size(it);
    }
    return it->size > have ? it->size - have : 0;
}

/* True if this item's .part is on disk and complete (matches the expected
 * size when known). Lets a persisted "downloaded" flag be trusted before we
 * skip the download phase on reload. */
static bool part_is_complete(const QueueItem *it) {
    char tmp[1200];
    if (!item_part_path(it, tmp, sizeof(tmp))) {
        return false;
    }
    struct stat st;
    if (stat(tmp, &st) != 0 || st.st_size <= 0) {
        return false;
    }
    if (it->size > 0 && (uint64_t)st.st_size != it->size) {
        return false; /* partial/corrupt: fall back to download + resume */
    }
    return true;
}

/* Reload a previously-saved queue. Items still needing download come back as
 * Q_QUEUED (resuming from any .part on disk); an archive whose download had
 * already finished comes back as Q_AWAIT_EXTRACT so only the unzip is retried. */
static void queue_load(void) {
    size_t len = 0;
    char *body = json_read_file(QUEUE_STATE_PATH, &len);
    if (!body) {
        return;
    }
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (!tok || tok[0].type != JSMN_OBJECT) {
        free(tok);
        free(body);
        return;
    }
    int arr = json_obj_get(body, tok, 0, "items");
    if (arr < 0 || tok[arr].type != JSMN_ARRAY) {
        free(tok);
        free(body);
        return;
    }

    /* Rebuild the auth header from the current credentials. */
    Credentials creds;
    creds_load(&creds);
    char auth[320];
    creds_auth_header(&creds, auth, sizeof(auth));

    int count = tok[arr].size;
    int child = arr + 1;
    uint32_t maxseq = 0;
    for (int i = 0; i < count; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            int slot = -1;
            for (int j = 0; j < QUEUE_MAX; j++) {
                if (g_items[j].status == Q_FREE) {
                    slot = j;
                    break;
                }
            }
            if (slot >= 0) {
                QueueItem *it = &g_items[slot];
                memset(it, 0, sizeof(*it));
                json_copy(body, tok, json_obj_get(body, tok, child, "url"),
                          it->url, sizeof(it->url));
                json_copy(body, tok, json_obj_get(body, tok, child, "name"),
                          it->name, sizeof(it->name));
                json_copy(body, tok, json_obj_get(body, tok, child, "target"),
                          it->target, sizeof(it->target));
                /* Absent "dest" (older queue.json) leaves it empty, so the item
                 * falls back to the default <roms_root>/<target>. */
                json_copy(body, tok, json_obj_get(body, tok, child, "dest"),
                          it->dest, sizeof(it->dest));
                json_copy(body, tok, json_obj_get(body, tok, child, "md5"),
                          it->md5, sizeof(it->md5));
                /* queue.json is ours, but the size in it originally came off the
                 * network — a restored queue must not reintroduce a value the
                 * space guard can't handle. */
                it->size = json_u64_size(body, tok,
                                         json_obj_get(body, tok, child, "size"));
                it->is_archive = json_bool(
                    body, tok, json_obj_get(body, tok, child, "is_archive"));
                it->seq = (uint32_t)json_u64(
                    body, tok, json_obj_get(body, tok, child, "seq"));
                snprintf(it->auth, sizeof(it->auth), "%s", auth);
                bool downloaded = json_bool(
                    body, tok, json_obj_get(body, tok, child, "downloaded"));
                if (!it->url[0]) {
                    it->status = Q_FREE;
                } else if (downloaded && it->is_archive &&
                           part_is_complete(it)) {
                    /* Download already finished last session; only the unzip
                     * is left. Go straight to the extract worker — no
                     * re-download, and it works with no network. */
                    it->total = it->size; /* so the extract progress bar shows */
                    it->status = Q_AWAIT_EXTRACT;
                } else {
                    it->status = Q_QUEUED;
                }
                if (it->seq > maxseq) {
                    maxseq = it->seq;
                }
            }
        }
        child = json_tok_skip(tok, child);
    }
    if (maxseq + 1 > g_seq) {
        g_seq = maxseq + 1;
    }
    free(tok);
    free(body);
}

/* Mark an item failed with a short reason (shown in the UI and the log).
 * Write the reason and status together under the mutex so a UI snapshot that
 * sees Q_FAILED also sees the matching reason (no torn/stale read). Log outside
 * the lock so disk I/O doesn't stall the UI thread. */
static void set_fail(QueueItem *it, const char *reason) {
    mutexLock(&g_mtx);
    snprintf(it->fail_reason, sizeof(it->fail_reason), "%s", reason);
    it->status = Q_FAILED;
    mutexUnlock(&g_mtx);
    log_download(it, reason);
}

/* The owning worker was asked to stop this item (cancel set). If a requeue was
 * also requested (queue_requeue: Retry/Redownload from a stuck verify/unzip),
 * reset it to QUEUED so the picker runs it again — dropping the .part `tmp`
 * only for a full redownload — instead of marking it CANCELLED. Returns true if
 * the item was requeued (caller should just return), false if it was cancelled.
 * Call from a worker that owns `it` and holds no lock. */
static bool finish_cancel(QueueItem *it, const char *tmp) {
    if (it->requeue) {
        if (it->requeue == 2) {
            remove(tmp); /* redownload: pull the file again from scratch */
        }
        mutexLock(&g_mtx);
        it->requeue = 0;
        it->cancel = false;
        it->pause = false;
        it->now = 0;
        it->total = 0;
        it->speed = 0;
        it->ex_files = 0;
        it->http_code = 0;
        it->fail_reason[0] = '\0';
        it->status = Q_QUEUED; /* keep seq: re-runs in its current position */
        save_locked();
        mutexUnlock(&g_mtx);
        return true;
    }
    remove(tmp);
    it->status = Q_CANCELLED;
    log_download(it, "cancelled");
    return false;
}

/* Final install directory for an item: its snapshotted custom `dest` when set,
 * otherwise the default <roms_root>/<sanitized target>. `safet` is the already
 * traversal-checked target. A custom dest is a locally-entered path (not from an
 * imported collection), so it is trusted as-is like the roms root itself. */
static void item_destdir(const QueueItem *it, const char *safet, char *out,
                         size_t out_sz) {
    if (it->dest[0]) {
        snprintf(out, out_sz, "%s", it->dest);
    } else {
        snprintf(out, out_sz, "%s/%s", g_roms_root, safet);
    }
}

static void process_item(QueueItem *it) {
    /* Never trust the remote filename or the console folder as a filesystem
     * path: the folder ("target") comes from the imported collection, which can
     * arrive over the LAN, so it must be traversal-checked just like the name. */
    char safe[600];
    if (!safe_rel(it->name, safe, sizeof(safe))) {
        set_fail(it, "bad name");
        return;
    }
    char safet[80];
    if (!safe_rel(it->target, safet, sizeof(safet))) {
        set_fail(it, "bad target");
        return;
    }

    char tmp[1200];
    if (!item_part_path(it, tmp, sizeof(tmp))) {
        set_fail(it, "bad name");
        return;
    }
    fs_ensure_parent(tmp);

    char destdir[1200];
    item_destdir(it, safet, destdir, sizeof(destdir));

    /* Resume from whatever's already on disk from a prior attempt/session. */
    uint64_t have = 0;
    struct stat pst;
    if (stat(tmp, &pst) == 0) {
        have = (uint64_t)pst.st_size;
    }
    /* A partial bigger than the expected size is corrupt; start over. */
    if (it->size > 0 && have > it->size) {
        remove(tmp);
        have = 0;
    }

    it->now = have;
    it->total = it->size;
    it->speed = 0;
    it->stalled = false;
    DlCtx ctx = { it, have, armGetSystemTick() };

    /* Bail out before writing if the SD card can't hold what's still to fetch. */
    uint64_t need = (it->size > have) ? (it->size - have) : 0;
    if (need > 0 && fs_free_bytes("sdmc:/") < need) {
        set_fail(it, "no space");
        return;
    }

    /* Archive.org public items need NO credentials — a browser sends none, and
     * the data node that /download/ redirects to (ia######.us.archive.org)
     * rejects a LOW S3 header on a public file with 401/403. So download
     * browser-style: start unauthenticated, and only if the item turns out to
     * be restricted retry once WITH the credentials. In practice a restricted
     * item's data node doesn't reliably answer 401/403 for that first,
     * unauthenticated request — some come back 404 (access-denied masked as
     * not-found) and a loaded/unhappy node can 502 instead — so both are
     * treated as "might be restricted" too, not just 401/403. The credential
     * is still only ever sent to archive.org over HTTPS, and only after an
     * unauthenticated attempt actually failed — never wasted on public files. */
    const char *cred_auth =
        (it->auth[0] && strncasecmp(it->url, "https://", 8) == 0 &&
         net_is_archive_org_url(it->url))
            ? it->auth
            : NULL;
    const char *auth = NULL; /* unauthenticated first, like a browser */
    bool tried_auth = false;
    long code = 0;
    bool ok = false;
    bool transport_err = false;
    /* Count this transfer among the active downloads so the fair-share rate
     * split (dl_rate_cb) divides the global budget by the true in-flight count.
     * Balanced by the decrement after the retry loop. */
    __atomic_add_fetch(&g_active_dl, 1, __ATOMIC_RELAXED);
    /* Transient server errors (5xx/429, e.g. archive.org throttling a burst of
     * new connections) and transport drops get a couple of automatic retries
     * with a short backoff, resuming from the .part instead of failing. */
    for (int attempt = 0;; attempt++) {
        ok = http_download(it->url, tmp, auth, dl_progress, &ctx,
                           dl_rate_cb, NULL, have, &code, &transport_err);
        it->http_code = code;
        if (ok || it->cancel || it->pause || !g_run) {
            break;
        }
        if (!net_is_up_fresh()) {
            break; /* connection is down: park instead of burning retries */
        }
        /* Every path below this point retries rather than giving up: flag the
         * item as stalled so the UI can show "reconnecting" instead of a
         * blank speed field for the gap. dl_progress clears it the moment the
         * retried attempt actually moves a byte. */
        it->stalled = true;
        /* Likely-restricted response to an unauthenticated request: 401/403
         * are the textbook auth errors, but a restricted item's data node
         * doesn't always answer with those (see the comment above) — 404 gets
         * the same benefit of the doubt. If we have credentials, retry once
         * WITH them (no backoff, doesn't burn a transient-retry slot). If the
         * authenticated attempt also fails, the creds are wrong or lack
         * access — that falls through to the normal fail handling below. */
        if ((code == 401 || code == 403 || code == 404) && cred_auth &&
            !tried_auth) {
            tried_auth = true;
            auth = cred_auth;
            struct stat ast;
            have = (stat(tmp, &ast) == 0) ? (uint64_t)ast.st_size : 0;
            if (it->size > 0 && have > it->size) {
                remove(tmp);
                have = 0;
            }
            it->now = have;
            ctx.last_now = have;
            ctx.last_tick = armGetSystemTick();
            continue;
        }
        /* A stall/reset (e.g. the LOW_SPEED_TIME abort in http_download) commonly
         * happens *after* a 200 status line already arrived and was streaming --
         * CURLINFO_RESPONSE_CODE still reads 200 in that case, so `code` alone
         * would misclassify a live-but-broken connection as a clean-but-bad
         * response and skip straight to giving up instead of the backoff/resume
         * retry this whole loop exists for. transport_err is the actual signal:
         * curl itself didn't finish the exchange, regardless of what the last
         * status line said. */
        bool transient =
            (transport_err || code == 0 || code == 429 || code >= 500);
        if (!transient || attempt >= 2) {
            /* Giving up on the unauthenticated side (a persistent 5xx/502,
             * not just a blip) — if credentials are available and untried,
             * take one authenticated shot before failing outright. A loaded
             * data node can 502 an access-denied request instead of 401/403
             * just like it can 404 one; without this a restricted item behind
             * a flaky node would fail with valid credentials sitting unused. */
            if (cred_auth && !tried_auth) {
                tried_auth = true;
                auth = cred_auth;
                struct stat ast;
                have = (stat(tmp, &ast) == 0) ? (uint64_t)ast.st_size : 0;
                if (it->size > 0 && have > it->size) {
                    remove(tmp);
                    have = 0;
                }
                it->now = have;
                ctx.last_now = have;
                ctx.last_tick = armGetSystemTick();
                continue;
            }
            break;
        }
        svcSleepThread(2000000000ULL); /* 2s backoff */
        if (it->cancel || it->pause || !g_run) {
            break; /* state changed during the backoff: don't re-attempt */
        }
        /* Resume from whatever the failed attempt managed to write. */
        struct stat rst;
        have = (stat(tmp, &rst) == 0) ? (uint64_t)rst.st_size : 0;
        if (it->size > 0 && have > it->size) {
            remove(tmp);
            have = 0;
        }
        it->now = have;
        ctx.last_now = have;
        ctx.last_tick = armGetSystemTick();
    }
    it->stalled = false; /* loop is over one way or another; don't let it linger */
    __atomic_sub_fetch(&g_active_dl, 1, __ATOMIC_RELAXED);

    if (!g_run) {
        /* Shutting down: keep the .part and leave the item pending (status
         * unchanged) so it's saved and resumes on the next launch. */
        return;
    }
    if (it->cancel) {
        finish_cancel(it, tmp); /* requeue (Retry/Redownload) or mark cancelled */
        return;
    }
    /* Salvage a transfer that delivered the whole file but then errored while
     * waiting for the connection to close (e.g. curl timeout with http 200,
     * seen on some servers and under emulators): if the .part already matches
     * the expected size, the download is genuinely complete. The MD5 check
     * below still guards against a truncated/corrupt file. Done before the
     * pause check so a completed-but-timed-out item finishes rather than
     * needlessly parking. */
    if (!ok && it->size > 0) {
        struct stat cst;
        if (stat(tmp, &cst) == 0 && (uint64_t)cst.st_size == it->size) {
            ok = true;
        }
    }
    if (it->pause) {
        it->pause = false;
        /* Preempted (download limit lowered): keep the .part and park the item
         * as PAUSED — the picker resumes it, in seq order, when a slot frees.
         * If the download managed to finish before the pause took effect,
         * ignore the request and continue to verify/install as normal. */
        if (!ok) {
            mutexLock(&g_mtx);
            it->status = Q_PAUSED;
            mutexUnlock(&g_mtx);
            return;
        }
    }
    if (!ok) {
        /* A failure while the connection is down isn't the item's fault —
         * curl reports the *last* HTTP code (e.g. 200 when the transfer died
         * mid-download), so don't try to classify by code: if the network is
         * gone, park the item as PAUSED (keeping the .part). It — and
         * everything still queued — resumes when the connection returns. */
        if (!net_is_up_fresh()) {
            mutexLock(&g_mtx);
            it->status = Q_PAUSED;
            mutexUnlock(&g_mtx);
            return;
        }
        /* Keep the .part so a retry or relaunch can resume from here. */
        char rb[20];
        if (it->http_code >= 400 && it->http_code <= 999) {
            snprintf(rb, sizeof(rb), "HTTP %d", (int)it->http_code);
        } else {
            snprintf(rb, sizeof(rb), "network");
        }
        set_fail(it, rb);
        return;
    }

    /* Integrity checks before we trust the file. */
    if (it->size > 0) {
        struct stat ds;
        if (stat(tmp, &ds) != 0 || (uint64_t)ds.st_size != it->size) {
            remove(tmp); /* wrong size: corrupt, force a clean re-download */
            set_fail(it, "bad size");
            return;
        }
    }
    if (it->md5[0]) {
        it->status = Q_VERIFYING;
        /* Reset the bar for the verify phase: it->now/it->total held the
         * download's final bytes (100%); md5_progress refills them with
         * hashed/total so the UI bar tracks the hash pass from zero. */
        it->now = 0;
        char got[33];
        boost_acquire(); /* CPU-bound hash: boost only across the md5 pass */
        bool md5ok = md5_file(tmp, got, &it->cancel, md5_progress, it);
        boost_release();
        if (!md5ok) {
            if (it->cancel) {
                finish_cancel(it, tmp); /* requeue or mark cancelled */
            } else {
                /* We couldn't read the file back to hash it — an SD hiccup,
                 * not a verdict on the bytes, which already passed the size
                 * check above. Keep the .part: a retry re-verifies in seconds
                 * (the resumed transfer 416s immediately) instead of pulling
                 * gigabytes down again. Only a real mismatch below deletes. */
                set_fail(it, "md5 error");
            }
            return;
        }
        if (strcasecmp(got, it->md5) != 0) {
            remove(tmp);
            set_fail(it, "bad md5");
            return;
        }
    }

    if (it->is_archive) {
        /* Hand off to the extract thread so the download thread can start
         * the next queued item immediately. */
        mutexLock(&g_mtx);
        it->status = Q_AWAIT_EXTRACT;
        mutexUnlock(&g_mtx);
        return;
    }

    char dest[2048];
    snprintf(dest, sizeof(dest), "%s/%s", destdir, safe);
    bool existed = fs_exists(dest);
    if (fs_move(tmp, dest)) {
        it->overwrote = existed ? 1 : 0;
        log_download(it, "done");
        if (g_post_import_enabled && queue_post_import) queue_post_import(it, dest, false);
        if (queue_on_landed) queue_on_landed(it->name, dest, false);
        it->status = Q_DONE;
    } else {
        set_fail(it, "write error");
    }
}

/* Called by the extract thread for items that finished downloading. */
static void install_item(QueueItem *it) {
    char safe[600];
    if (!safe_rel(it->name, safe, sizeof(safe))) {
        set_fail(it, "bad name");
        return;
    }
    char safet[80];
    if (!safe_rel(it->target, safet, sizeof(safet))) {
        set_fail(it, "bad target");
        return;
    }
    char tmp[1200];
    if (!item_part_path(it, tmp, sizeof(tmp))) {
        set_fail(it, "bad name");
        return;
    }
    char destdir[1200];
    item_destdir(it, safet, destdir, sizeof(destdir));

    fs_mkdir_p(destdir);
    /* it->now switches meaning here: downloaded bytes -> archive bytes
     * consumed by the extractor (it->total stays the archive size, so the
     * UI's percent bar works for both phases). */
    it->now = 0;
    it->ex_files = 0;
    int ow = 0;
    boost_acquire(); /* CPU-bound decompression: boost only across the extract */
    int n = extract_archive(tmp, destdir, ex_progress, it, &ow);
    boost_release();
    if (!g_run && !it->cancel) {
        /* Shutting down mid-extract: KEEP the complete .part and leave the
         * item pending (status EXTRACTING is persisted, and reloads straight
         * to AWAIT_EXTRACT) so it resumes extraction next launch instead of
         * re-downloading the whole archive. */
        return;
    }
    if (it->cancel) {
        finish_cancel(it, tmp); /* requeue (Retry/Redownload) or mark cancelled */
        return;
    }
    if (n > 0) {
        remove(tmp);
        it->overwrote = ow;
        log_download(it, "done");
        if (g_post_import_enabled && queue_post_import) queue_post_import(it, destdir, true);
        if (queue_on_landed) queue_on_landed(it->name, destdir, true);
        it->status = Q_DONE;
        return;
    }
    /* libarchive failed and this is a .rar: try the fallback RAR3 decoder
     * before giving up. It exists for exactly one gap -- RAR3's "programmable
     * filter" entries, which libarchive's RAR reader has never implemented
     * (see rar3.h) -- so it only fires here, after the primary path has
     * already had its shot. Anything rar3_extract() also can't handle
     * (password-protected, split volumes, a genuinely custom filter program)
     * falls through to the raw-save below exactly as it always has. */
    size_t safelen = strlen(safe);
    if (safelen > 4 && strcasecmp(safe + safelen - 4, ".rar") == 0) {
        it->now = 0;
        it->ex_files = 0;
        ow = 0; /* separate attempt from the failed extract_archive() call above */
        boost_acquire();
        int n2 = rar3_extract(tmp, destdir, ex_progress, it, &ow);
        boost_release();
        if (it->cancel) {
            finish_cancel(it, tmp);
            return;
        }
        if (n2 > 0) {
            remove(tmp);
            it->overwrote = ow;
            log_download(it, "done");
            if (g_post_import_enabled && queue_post_import) queue_post_import(it, destdir, true);
            it->status = Q_DONE;
            return;
        }
    }
    /* Couldn't extract: keep the raw archive as a plain file. */
    char dest[2048];
    snprintf(dest, sizeof(dest), "%s/%s", destdir, safe);
    bool existed = fs_exists(dest);
    if (fs_move(tmp, dest)) {
        it->overwrote = existed ? 1 : 0;
        it->status = Q_SAVED;
        log_download(it, "saved-raw");
    } else {
        set_fail(it, "write error");
    }
}

static void dl_worker(void *arg) {
    (void)arg;
    while (g_run) {
        int pick = -1;
        uint32_t best = 0xFFFFFFFFu;
        /* No network: start nothing. Queued/paused items wait and resume
         * automatically (checked every ~2s) once the connection returns. */
        bool online = net_is_up();
        mutexLock(&g_mtx);
        /* Respect the (live) concurrent-download limit: only pick new work
         * while fewer than g_max_dl items are being downloaded/verified. */
        int active = 0, blocked = 0;
        for (int i = 0; i < QUEUE_MAX; i++) {
            QStatus s = g_items[i].status;
            if (g_items[i].external) {
                continue; /* a receive/update isn't a queue download slot */
            }
            if (s == Q_DOWNLOADING || s == Q_VERIFYING) {
                active++;
            } else if ((s == Q_QUEUED || s == Q_PAUSED) &&
                       g_items[i].no_space) {
                blocked++;
            }
        }
        if (online && !g_paused && active < g_max_dl) {
            for (int i = 0; i < QUEUE_MAX; i++) {
                QStatus s = g_items[i].status;
                /* Items parked for want of space are skipped, not blocking:
                 * one 40 GB file at the head of the queue must not stop the
                 * 200 small ones behind it. They're retried below. */
                if ((s == Q_QUEUED || s == Q_PAUSED) && !g_items[i].no_space &&
                    g_items[i].seq < best) {
                    best = g_items[i].seq;
                    pick = i;
                }
            }
        }
        QStatus prev = Q_QUEUED;
        if (pick >= 0) {
            prev = g_items[pick].status;
            g_items[pick].pause = false;
            g_items[pick].status = Q_DOWNLOADING;
        }
        mutexUnlock(&g_mtx);

        /* Marks the exact moment the picker hands an item to process_item(),
         * so a "sat queued for N seconds before anything happened" report is
         * a visible gap in the debug log rather than something only guessable
         * from the DL line that follows once the HTTP attempt itself starts. */
        if (pick >= 0) {
            net_log_event("DL pickup [%s] %s (was %s)", g_items[pick].target,
                          g_items[pick].name,
                          prev == Q_PAUSED ? "paused" : "queued");
        }

        if (pick < 0) {
            if (blocked > 0) {
                /* Everything left is waiting on space. Pause, then clear the
                 * flags so the next pass re-measures — a finished download or
                 * a deletion may have freed room. */
                g_space_hold = true;
                svcSleepThread(3000000000ULL);
                mutexLock(&g_mtx);
                for (int i = 0; i < QUEUE_MAX; i++) {
                    g_items[i].no_space = false;
                }
                mutexUnlock(&g_mtx);
            } else {
                g_space_hold = false;
                svcSleepThread(150000000ULL); /* 150 ms idle */
            }
            continue;
        }
        /* Don't start a download the card can't hold. Failing item after item
         * as the disk fills would burn through a big queue in seconds and leave
         * the user a screen of errors; instead park this one and move on. The
         * stat/statvfs happen outside the mutex — they touch the SD card.
         * Concurrent downloads each re-check as free space actually shrinks,
         * so the reserve is the only headroom that has to be guessed. */
        uint64_t need = item_remaining(&g_items[pick]);
        uint64_t freeb = fs_free_bytes("sdmc:/");
        /* Subtraction, not addition: `need` comes from the size declared in
         * dl_sources.json, which arrives over the LAN receiver and is not ours
         * to trust. A declared size within QUEUE_SPACE_RESERVE of UINT64_MAX
         * made `need + QUEUE_SPACE_RESERVE` wrap to something tiny, the test
         * read as "plenty of room", and the guard let the download start. */
        if (need > 0 && freeb != UINT64_MAX &&
            (freeb < QUEUE_SPACE_RESERVE ||
             freeb - QUEUE_SPACE_RESERVE < need)) {
            mutexLock(&g_mtx);
            g_items[pick].no_space = true;
            /* Only un-claim it if we still own it: a cancel that landed while
             * we were statting sets .cancel, and other states aren't ours. */
            if (g_items[pick].status == Q_DOWNLOADING) {
                g_items[pick].status = prev; /* still waiting, not failed */
            }
            mutexUnlock(&g_mtx);
            g_space_hold = true;
            continue; /* try the next candidate immediately */
        }
        g_space_hold = false;
        process_item(&g_items[pick]);
        mutexLock(&g_mtx);
        /* This download either consumed or released space, so anything parked
         * deserves a fresh look. */
        for (int i = 0; i < QUEUE_MAX; i++) {
            g_items[i].no_space = false;
        }
        save_locked();
        mutexUnlock(&g_mtx);
    }
}

static void ex_worker(void *arg) {
    (void)arg;
    while (g_run) {
        int pick = -1;
        uint32_t best = 0xFFFFFFFFu;
        mutexLock(&g_mtx);
        for (int i = 0; i < QUEUE_MAX; i++) {
            if (g_items[i].status == Q_AWAIT_EXTRACT && g_items[i].seq < best) {
                best = g_items[i].seq;
                pick = i;
            }
        }
        if (pick >= 0) {
            g_items[pick].status = Q_EXTRACTING;
        }
        mutexUnlock(&g_mtx);

        if (pick < 0) {
            svcSleepThread(150000000ULL);
            continue;
        }
        install_item(&g_items[pick]);
        mutexLock(&g_mtx);
        save_locked();
        mutexUnlock(&g_mtx);
    }
}

/* ---- public API ------------------------------------------------------ */

void queue_set_max_dl(int n) {
    if (n < 1) n = 1;
    if (n > MAX_DL_THREADS) n = MAX_DL_THREADS;
    g_max_dl = n;

    /* Apply the new limit to in-flight downloads: keep the n oldest (by seq)
     * running and ask the rest to pause (they keep their .part and resume in
     * order when a slot frees). Raising the limit clears any pending pause
     * requests that haven't been acted on yet. */
    mutexLock(&g_mtx);
    int order[QUEUE_MAX];
    int cnt = 0;
    for (int i = 0; i < QUEUE_MAX; i++) {
        if (g_items[i].status == Q_DOWNLOADING) {
            order[cnt++] = i;
        }
    }
    /* insertion sort by seq (cnt <= MAX_DL_THREADS) */
    for (int a = 1; a < cnt; a++) {
        int key = order[a];
        uint32_t ks = g_items[key].seq;
        int b = a - 1;
        while (b >= 0 && g_items[order[b]].seq > ks) {
            order[b + 1] = order[b];
            b--;
        }
        order[b + 1] = key;
    }
    for (int a = 0; a < cnt; a++) {
        g_items[order[a]].pause = (a >= n);
    }
    mutexUnlock(&g_mtx);
}

void queue_set_keep_archives(bool on) { g_keep_archives = on; }

void queue_set_rate_limits(int all_bps, int item_bps) {
    if (all_bps < 0) all_bps = 0;
    if (item_bps < 0) item_bps = 0;
    /* In-flight downloads pick the new caps up on their next progress tick via
     * dl_rate_cb — no restart, no lost .part. */
    g_rate_all_bps = all_bps;
    g_rate_item_bps = item_bps;
}

void queue_init(const char *roms_root, int max_dl) {
    if (roms_root && roms_root[0]) g_roms_root = roms_root;
    memset(g_items, 0, sizeof(g_items));
    mutexInit(&g_mtx);
    mutexInit(&g_net_mtx);
    mutexInit(&g_boost_mtx);
    queue_load(); /* restore any downloads pending from a previous session */
    g_run = true;
    queue_set_max_dl(max_dl);
    /* Pin the workers to cores OTHER than core 0 so their CPU work runs in
     * parallel with rendering instead of contending for the UI's core. cpuid
     * -2 = default/creation core (–1 is invalid for svcCreateThread) is the
     * fallback when the process only has core 0.
     * Worker priorities also sit BELOW the main/UI thread (0x2C; higher number =
     * lower priority) so rendering always preempts them and the UI stays
     * responsive while downloads/extraction run. Downloads are mostly I/O-bound
     * (they block on the socket), so a small step down; extraction is pure-CPU
     * decompression and the worst offender, so it drops much further. */
    detect_worker_cores();
    g_dl_count = 0;
    for (int i = 0; i < MAX_DL_THREADS; i++) {
        Result rc = threadCreate(&g_dl_threads[i], dl_worker, NULL, NULL, 0x40000, 0x30, dl_worker_core(i));
        if (R_SUCCEEDED(rc)) {
            if (R_SUCCEEDED(threadStart(&g_dl_threads[i]))) {
                g_dl_count++;
            } else {
                threadClose(&g_dl_threads[i]);
            }
        }
    }
    bool ex_ok = false;
    Result rc2 = threadCreate(&g_ex_thread, ex_worker, NULL, NULL, 0x40000, 0x3B, ex_worker_core());
    if (R_SUCCEEDED(rc2)) {
        if (R_SUCCEEDED(threadStart(&g_ex_thread))) {
            ex_ok = true;
        } else {
            threadClose(&g_ex_thread);
        }
    }
    if (g_dl_count == 0 && !ex_ok) {
        g_run = false;
    }
    fs_mkdir_p(LOGS_DIR);
    FILE *f = fopen(LOG_PATH, "a");
    if (f) {
        char cores[32] = "";
        for (int i = 0; i < g_worker_core_count; i++) {
            char t[6];
            snprintf(t, sizeof(t), "%s%d", i ? "," : "", g_worker_cores[i]);
            strncat(cores, t, sizeof(cores) - strlen(cores) - 1);
        }
        fprintf(f, "queue: %d/%d dl_workers started (limit %d), ex_worker %s; "
                   "worker cores [%s] (ex core %d)\n",
                g_dl_count, MAX_DL_THREADS, (int)g_max_dl,
                ex_ok ? "started" : "FAILED",
                cores[0] ? cores : "0(fallback)", ex_worker_core());
        fclose(f);
    }
}

void queue_exit(void) {
    if (!g_run) {
        return;
    }
    /* Clearing g_run aborts both the in-flight download (via dl_progress) and
     * any in-flight extraction (via ex_progress). Both threads exit promptly.
     * Downloads interrupted this way keep their .part and stay pending so they
     * resume next launch. */
    g_run = false;
    for (int i = 0; i < g_dl_count; i++) {
        threadWaitForExit(&g_dl_threads[i]);
        threadClose(&g_dl_threads[i]);
    }
    threadWaitForExit(&g_ex_thread);
    threadClose(&g_ex_thread);
    /* Both workers are stopped: make sure we didn't leave the system in boost
     * mode (e.g. a phase interrupted by shutdown). */
    if (g_boost_refs != 0) {
        g_boost_refs = 0;
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    }
    /* Persist after both threads have stopped so interrupted downloads and
     * pending extractions survive a restart/update and resume next launch. */
    mutexLock(&g_mtx);
    save_locked();
    mutexUnlock(&g_mtx);
}

bool queue_add(const char *url, const char *name, const char *target,
               const char *auth, uint64_t size, bool is_archive,
               const char *md5, const char *dest) {
    mutexLock(&g_mtx);
    /* Same name + target already live in the queue: treat as already queued
     * rather than adding a second entry. Without this, a source collection
     * that (legitimately, or via a UI double-tap) lists/selects the same file
     * twice queues two items that fight over the identical .part file path --
     * item_part_name's seq keying stops that from corrupting each other, but
     * they'd still burn bandwidth downloading and extracting the same file
     * twice, and to the user it looks like "the download restarted". Terminal
     * states (done/saved/failed/cancelled) are excluded so retrying/re-adding
     * a finished or given-up item still works normally. */
    for (int i = 0; i < QUEUE_MAX; i++) {
        QueueItem *ex = &g_items[i];
        if (ex->external || ex->status == Q_FREE || ex->status == Q_DONE ||
            ex->status == Q_SAVED || ex->status == Q_FAILED ||
            ex->status == Q_CANCELLED) {
            continue;
        }
        if (strcmp(ex->name, name) == 0 && strcmp(ex->target, target) == 0) {
            mutexUnlock(&g_mtx);
            return true; /* already in flight; nothing more to do */
        }
    }
    int slot = -1;
    for (int i = 0; i < QUEUE_MAX; i++) {
        if (g_items[i].status == Q_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        mutexUnlock(&g_mtx);
        return false;
    }
    QueueItem *it = &g_items[slot];
    memset(it, 0, sizeof(*it));
    snprintf(it->url, sizeof(it->url), "%s", url);
    snprintf(it->name, sizeof(it->name), "%s", name);
    snprintf(it->target, sizeof(it->target), "%s", target);
    snprintf(it->dest, sizeof(it->dest), "%s", dest ? dest : "");
    snprintf(it->auth, sizeof(it->auth), "%s", auth ? auth : "");
    snprintf(it->md5, sizeof(it->md5), "%s", md5 ? md5 : "");
    /* Backstop: every download enters here, whatever route the caller took to
     * find a size, so the space guard's arithmetic is bounded at one place. */
    it->size = size > JSON_SIZE_MAX ? JSON_SIZE_MAX : size;
    /* "Keep archives compressed": drop the archive flag so this file skips the
     * extract worker and lands as-is (the raw .zip/.7z) in the console folder. */
    it->is_archive = is_archive && !g_keep_archives;
    it->seq = g_seq++;
    it->status = Q_QUEUED;
    save_locked();
    mutexUnlock(&g_mtx);
    return true;
}

/* ---- external transfers ---------------------------------------------------
 * See queue.h. These entries share the item array (so the Queue tab renders
 * them for free) but carry external=true, so the worker scheduler, the space
 * accounting, and the persistence all skip them (guards above). The owning code
 * drives now/total/status through the calls below. */

int queue_ext_add(const char *name, const char *target, uint8_t xkind) {
    mutexLock(&g_mtx);
    int slot = -1;
    for (int i = 0; i < QUEUE_MAX; i++) {
        if (g_items[i].status == Q_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        mutexUnlock(&g_mtx);
        return -1;
    }
    QueueItem *it = &g_items[slot];
    memset(it, 0, sizeof(*it));
    snprintf(it->name, sizeof(it->name), "%s", name ? name : "");
    snprintf(it->target, sizeof(it->target), "%s", target ? target : "");
    it->external = true;
    it->xkind = xkind;
    it->seq = g_seq++;
    it->status = Q_DOWNLOADING; /* "in progress"; xkind picks the shown verb */
    mutexUnlock(&g_mtx);
    return slot;
}

void queue_ext_set_kind(int slot, uint8_t xkind) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    mutexLock(&g_mtx);
    QueueItem *it = &g_items[slot];
    if (it->external && it->status == Q_DOWNLOADING) {
        it->xkind = xkind;
    }
    mutexUnlock(&g_mtx);
}

void queue_ext_progress(int slot, uint64_t now, uint64_t total, uint64_t speed) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    mutexLock(&g_mtx);
    QueueItem *it = &g_items[slot];
    if (it->external && it->status == Q_DOWNLOADING) {
        it->now = now;
        it->total = total;
        if (speed) {
            it->speed = speed; /* owner supplied a rate: trust it */
        } else {
            /* Derive bytes/sec from the delta between samples, on the same ~2 Hz
             * cadence dl_progress uses, so a receive shows a live rate just like a
             * download. (A byte-count total, e.g. the DAT sync's console counts,
             * isn't shown as speed by the renderer, so a bogus value is harmless.) */
            uint64_t tick = armGetSystemTick();
            if (it->x_last_tick == 0) {
                it->x_last_tick = tick;
                it->x_last_now = now;
            } else {
                uint64_t dt = armTicksToNs(tick - it->x_last_tick);
                if (dt >= 500000000ULL) {
                    uint64_t db =
                        (now >= it->x_last_now) ? now - it->x_last_now : 0;
                    it->speed = db * 1000000000ULL / dt;
                    it->x_last_now = now;
                    it->x_last_tick = tick;
                }
            }
        }
    }
    mutexUnlock(&g_mtx);
}

void queue_ext_finish(int slot, bool ok, const char *fail_reason) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    mutexLock(&g_mtx);
    QueueItem *it = &g_items[slot];
    /* Only finish an item still in progress: makes repeated calls idempotent
     * (a per-frame mirror can call this every frame once a transfer reports
     * done) and stops a teardown "fail all" from downgrading an already-done
     * item to failed. */
    if (it->external && it->status == Q_DOWNLOADING) {
        it->speed = 0;
        if (ok) {
            it->status = Q_DONE;
        } else {
            snprintf(it->fail_reason, sizeof(it->fail_reason), "%s",
                     fail_reason ? fail_reason : "");
            it->status = Q_FAILED;
        }
    }
    mutexUnlock(&g_mtx);
}

void queue_ext_cancel(int slot) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    mutexLock(&g_mtx);
    if (g_items[slot].external) {
        g_items[slot].cancel = true; /* owner polls queue_ext_cancelled */
    }
    mutexUnlock(&g_mtx);
}

bool queue_ext_cancelled(int slot) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return false;
    }
    mutexLock(&g_mtx);
    bool c = g_items[slot].external && g_items[slot].cancel;
    mutexUnlock(&g_mtx);
    return c;
}

void queue_batch_begin(void) {
    mutexLock(&g_mtx);
    g_batch_depth++;
    mutexUnlock(&g_mtx);
}

void queue_batch_end(void) {
    mutexLock(&g_mtx);
    if (g_batch_depth > 0 && --g_batch_depth == 0 && g_batch_dirty) {
        g_batch_dirty = false;
        save_locked(); /* depth is 0 again, so this really writes */
    }
    mutexUnlock(&g_mtx);
}

int queue_free_slots(void) {
    int n = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        if (g_items[i].status == Q_FREE) {
            n++;
        }
    }
    mutexUnlock(&g_mtx);
    return n;
}

uint64_t queue_pending_bytes(void) {
    /* Deliberately memory-only: the UI calls this while composing a dialog, and
     * statting up to QUEUE_MAX .part files would hitch the render thread. An
     * item that hasn't started yet counts its full size even when a resumable
     * .part exists, so the total can overshoot — the safe direction for a
     * "will this fit?" question. The workers do the exact check per item. */
    uint64_t total = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (g_items[i].external) {
            continue; /* not bytes the queue itself has to fetch */
        }
        if (s == Q_FREE || s == Q_DONE || s == Q_SAVED || s == Q_FAILED ||
            s == Q_CANCELLED) {
            continue; /* finished or failed: no longer needs space */
        }
        if (g_items[i].size > g_items[i].now) {
            total += g_items[i].size - g_items[i].now;
        }
    }
    mutexUnlock(&g_mtx);
    return total;
}

bool queue_space_hold(void) { return g_space_hold; }

static int cmp_view(const void *a, const void *b) {
    const QueueView *x = (const QueueView *)a;
    const QueueView *y = (const QueueView *)b;
    if (x->item.seq < y->item.seq) {
        return -1;
    }
    return x->item.seq > y->item.seq ? 1 : 0;
}

int queue_snapshot(QueueView *out, int max) {
    mutexLock(&g_mtx);
    int c = 0;
    for (int i = 0; i < QUEUE_MAX && c < max; i++) {
        if (g_items[i].status != Q_FREE) {
            out[c].item = g_items[i];
            out[c].slot = i;
            c++;
        }
    }
    mutexUnlock(&g_mtx);
    qsort(out, c, sizeof(QueueView), cmp_view);
    return c;
}

void queue_cancel(int slot) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    bool log_cxl = false;
    QueueItem snap;
    mutexLock(&g_mtx);
    QStatus s = g_items[slot].status;
    if (s == Q_QUEUED || s == Q_PAUSED) {
        g_items[slot].status = Q_CANCELLED;
        snap = g_items[slot];
        log_cxl = true; /* no worker owns it, so the worker won't log it */
        save_locked(); /* drop it from the persisted pending set */
    } else if (s == Q_DOWNLOADING || s == Q_VERIFYING || s == Q_EXTRACTING) {
        g_items[slot].cancel = true; /* worker marks + logs it CANCELLED */
    } else if (s == Q_AWAIT_EXTRACT) {
        g_items[slot].cancel = true; /* extract thread will see it */
    }
    mutexUnlock(&g_mtx);
    if (log_cxl) {
        log_download(&snap, "cancelled");
    }
}

void queue_retry(int slot) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    mutexLock(&g_mtx);
    QStatus s = g_items[slot].status;
    if (s == Q_FAILED || s == Q_CANCELLED || s == Q_PAUSED) {
        QueueItem *it = &g_items[slot];
        it->cancel = false;
        it->pause = false;
        /* A paused item resumes in place from its .part (progress preserved);
         * a failed/cancelled one restarts from zero. */
        if (s != Q_PAUSED) {
            it->now = 0;
            it->total = 0;
            it->speed = 0;
            it->ex_files = 0;
            it->http_code = 0;
            it->fail_reason[0] = '\0';
        }
        /* Keep the original seq so the item resumes in its current list
         * position (and is picked promptly) instead of jumping to the bottom. */
        it->status = Q_QUEUED;
        /* An explicit per-item retry also lifts a global "pause all" latch —
         * otherwise the re-queued item stays stuck behind the pause and never
         * gets picked. (The scheduler resumes any remaining paused items too;
         * there is no per-item bypass of the global latch.) */
        g_paused = false;
        save_locked();
    }
    mutexUnlock(&g_mtx);
}

void queue_requeue(int slot, bool wipe) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return;
    }
    mutexLock(&g_mtx);
    QueueItem *it = &g_items[slot];
    QStatus s = it->status;
    if (s == Q_DOWNLOADING || s == Q_VERIFYING || s == Q_AWAIT_EXTRACT ||
        s == Q_EXTRACTING) {
        /* Owned by a download/extract worker (or waiting for the extract
         * thread): ask it to hand the item back via the cancel handoff. The
         * worker resets it to QUEUED in finish_cancel — dropping the .part only
         * for a redownload — so we don't touch its files from here. */
        it->requeue = wipe ? 2 : 1;
        it->cancel = true;
    } else if (s == Q_QUEUED || s == Q_PAUSED || s == Q_FAILED ||
               s == Q_CANCELLED) {
        /* No worker owns it: reset it in place. A resume re-validates the .part,
         * so a "redownload" here just restarts from zero on the next run. */
        it->cancel = false;
        it->pause = false;
        it->requeue = 0;
        it->now = 0;
        it->total = 0;
        it->speed = 0;
        it->ex_files = 0;
        it->http_code = 0;
        it->fail_reason[0] = '\0';
        it->status = Q_QUEUED;
        save_locked();
    }
    mutexUnlock(&g_mtx);
}

int queue_retry_all(void) {
    int n = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        if (g_items[i].status == Q_FAILED) {
            QueueItem *it = &g_items[i];
            it->cancel = false;
            it->pause = false;
            it->now = 0;
            it->total = 0;
            it->speed = 0;
            it->ex_files = 0;
            it->http_code = 0;
            it->fail_reason[0] = '\0';
            it->status = Q_QUEUED; /* keep seq: resumes in place */
            n++;
        }
    }
    if (n > 0) {
        save_locked();
    }
    mutexUnlock(&g_mtx);
    return n;
}

void queue_pause_all(void) {
    mutexLock(&g_mtx);
    g_paused = true; /* latch the scheduler off until an explicit resume */
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_QUEUED) {
            g_items[i].status = Q_PAUSED;
        } else if (s == Q_DOWNLOADING) {
            /* Park the in-flight transfer (keeps its .part), like max-dl=0. */
            g_items[i].pause = true;
        }
    }
    save_locked();
    mutexUnlock(&g_mtx);
}

void queue_cancel_all(void) {
    /* Slots whose cancel we own (nothing else will log them). */
    int to_log[QUEUE_MAX];
    int nlog = 0;
    bool changed = false;
    mutexLock(&g_mtx);
    g_paused = false; /* cancelling clears any global pause */
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_QUEUED || s == Q_PAUSED) {
            g_items[i].status = Q_CANCELLED; /* no worker owns it -> log below */
            to_log[nlog++] = i;
            changed = true;
        } else if (s == Q_DOWNLOADING || s == Q_VERIFYING ||
                   s == Q_EXTRACTING || s == Q_AWAIT_EXTRACT) {
            g_items[i].cancel = true; /* worker marks + logs it CANCELLED */
        }
    }
    if (changed) {
        save_locked();
    }
    mutexUnlock(&g_mtx);
    /* Log outside the lock so the file IO never blocks the workers/UI. */
    for (int k = 0; k < nlog; k++) {
        QueueItem snap;
        mutexLock(&g_mtx);
        snap = g_items[to_log[k]];
        mutexUnlock(&g_mtx);
        log_download(&snap, "cancelled");
    }
}

/* Bulk retry/resume by current status. Paused items resume in place keeping
 * their .part (progress preserved); failed/cancelled items restart from zero.
 * Returns how many items were re-queued. */
int queue_retry_status(QStatus want) {
    int n = 0;
    mutexLock(&g_mtx);
    if (want == Q_PAUSED) {
        g_paused = false; /* resume: let the scheduler pick work again */
    }
    for (int i = 0; i < QUEUE_MAX; i++) {
        if (g_items[i].status == want) {
            QueueItem *it = &g_items[i];
            it->cancel = false;
            it->pause = false;
            if (want != Q_PAUSED) {
                it->now = 0;
                it->total = 0;
                it->speed = 0;
                it->ex_files = 0;
                it->http_code = 0;
                it->fail_reason[0] = '\0';
            }
            it->status = Q_QUEUED; /* keep seq: resumes in place */
            n++;
        }
    }
    if (n > 0 || want == Q_PAUSED) {
        save_locked();
    }
    mutexUnlock(&g_mtx);
    return n;
}

static bool q_is_active(QStatus s) {
    return s == Q_DOWNLOADING || s == Q_VERIFYING ||
           s == Q_AWAIT_EXTRACT || s == Q_EXTRACTING;
}

bool queue_move(int slot, int dir) {
    if (slot < 0 || slot >= QUEUE_MAX || (dir != -1 && dir != 1)) {
        return false;
    }
    bool moved = false;
    mutexLock(&g_mtx);
    if (g_items[slot].status != Q_FREE) {
        /* Build the on-screen order (non-FREE items sorted by seq). */
        int order[QUEUE_MAX];
        int cnt = 0;
        for (int i = 0; i < QUEUE_MAX; i++) {
            if (g_items[i].status != Q_FREE) {
                order[cnt++] = i;
            }
        }
        for (int a = 1; a < cnt; a++) {
            int key = order[a];
            uint32_t ks = g_items[key].seq;
            int b = a - 1;
            while (b >= 0 && g_items[order[b]].seq > ks) {
                order[b + 1] = order[b];
                b--;
            }
            order[b + 1] = key;
        }
        int pos = -1;
        for (int a = 0; a < cnt; a++) {
            if (order[a] == slot) {
                pos = a;
                break;
            }
        }
        int j = pos + dir;
        /* The active item can't be moved, and nothing may move above it. */
        if (pos >= 0 && j >= 0 && j < cnt &&
            !q_is_active(g_items[slot].status) &&
            !q_is_active(g_items[order[j]].status)) {
            uint32_t tmp = g_items[slot].seq;
            g_items[slot].seq = g_items[order[j]].seq;
            g_items[order[j]].seq = tmp;
            save_locked();
            moved = true;
        }
    }
    mutexUnlock(&g_mtx);
    return moved;
}

bool queue_move_end(int slot, bool to_bottom) {
    if (slot < 0 || slot >= QUEUE_MAX) {
        return false;
    }
    bool moved = false;
    mutexLock(&g_mtx);
    if (g_items[slot].status != Q_FREE &&
        !q_is_active(g_items[slot].status)) {
        /* On-screen order: non-FREE items sorted by seq. */
        int order[QUEUE_MAX], cnt = 0;
        for (int i = 0; i < QUEUE_MAX; i++) {
            if (g_items[i].status != Q_FREE) {
                order[cnt++] = i;
            }
        }
        for (int a = 1; a < cnt; a++) {
            int key = order[a];
            uint32_t ks = g_items[key].seq;
            int b = a - 1;
            while (b >= 0 && g_items[order[b]].seq > ks) {
                order[b + 1] = order[b];
                b--;
            }
            order[b + 1] = key;
        }
        /* boundary = first non-active slot (top of the waiting section, so a
         * move-to-top lands just below the active download, never above it). */
        int boundary = 0;
        while (boundary < cnt && q_is_active(g_items[order[boundary]].status)) {
            boundary++;
        }
        int pos = -1;
        for (int a = 0; a < cnt; a++) {
            if (order[a] == slot) {
                pos = a;
                break;
            }
        }
        int target = to_bottom ? cnt - 1 : boundary;
        if (pos >= 0 && pos != target) {
            int rest[QUEUE_MAX], r = 0;
            for (int a = 0; a < cnt; a++) {
                if (order[a] != slot) rest[r++] = order[a];
            }
            int fin[QUEUE_MAX], f = 0;
            for (int a = 0; a < r; a++) {
                if (f == target) fin[f++] = slot;
                fin[f++] = rest[a];
            }
            if (f == target) fin[f++] = slot;
            /* Renumber in the new order; keep g_seq above these so later adds
             * still append to the bottom. */
            for (int a = 0; a < cnt; a++) {
                g_items[fin[a]].seq = (uint32_t)(a + 1);
            }
            if (g_seq <= (uint32_t)cnt) {
                g_seq = (uint32_t)cnt + 1;
            }
            save_locked();
            moved = true;
        }
    }
    mutexUnlock(&g_mtx);
    return moved;
}

bool queue_active_info(char *name, size_t name_sz, QStatus *status,
                       uint64_t *now, uint64_t *total, uint64_t *speed,
                       int *index, int *count) {
    bool found = false;
    uint32_t active_seq = 0;
    int total_items = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_FREE) {
            continue;
        }
        total_items++;
        if (!found &&
            (s == Q_DOWNLOADING || s == Q_VERIFYING || s == Q_EXTRACTING)) {
            const QueueItem *it = &g_items[i];
            if (name && name_sz) {
                snprintf(name, name_sz, "%s", it->name);
            }
            if (status) {
                *status = s;
            }
            if (now) {
                *now = it->now;
            }
            if (total) {
                *total = it->total;
            }
            if (speed) {
                *speed = it->speed;
            }
            active_seq = it->seq;
            found = true;
        }
    }
    /* 1-based position of the active item among all queued items, by FIFO seq. */
    int idx = 0;
    if (found) {
        for (int i = 0; i < QUEUE_MAX; i++) {
            if (g_items[i].status != Q_FREE && g_items[i].seq <= active_seq) {
                idx++;
            }
        }
    }
    mutexUnlock(&g_mtx);
    if (index) {
        *index = idx;
    }
    if (count) {
        *count = total_items;
    }
    return found;
}

void queue_clear_finished(void) {
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_DONE || s == Q_SAVED || s == Q_FAILED || s == Q_CANCELLED) {
            g_items[i].status = Q_FREE;
        }
    }
    mutexUnlock(&g_mtx);
}

int queue_active_count(void) {
    int c = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_QUEUED || s == Q_PAUSED || s == Q_DOWNLOADING ||
            s == Q_VERIFYING || s == Q_AWAIT_EXTRACT || s == Q_EXTRACTING) {
            c++;
        }
    }
    mutexUnlock(&g_mtx);
    return c;
}

bool queue_io_active(void) {
    bool r = false;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        /* Only the states that actually touch the SD card or network right now
         * (this includes every external receive/USB/live-link item, which ride
         * as Q_DOWNLOADING). Q_QUEUED/Q_PAUSED are intentionally excluded so a
         * parked queue can't block the inventory refresh indefinitely. */
        if (s == Q_DOWNLOADING || s == Q_VERIFYING ||
            s == Q_AWAIT_EXTRACT || s == Q_EXTRACTING) {
            r = true;
            break;
        }
    }
    mutexUnlock(&g_mtx);
    return r;
}

int queue_cancel_by_part(const char *partname, bool do_cancel) {
    int hits = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_FREE || s == Q_DONE || s == Q_SAVED ||
            s == Q_FAILED || s == Q_CANCELLED)
            continue;
        /* Rebuild the .part name exactly as item_part_name does, so this
         * always agrees with wherever the file actually lives (see its
         * comment: a prior drift here already deleted a live download out
         * from under itself once). */
        char expect[700];
        if (!item_part_name(&g_items[i], expect, sizeof(expect)))
            continue;
        if (strcasecmp(expect, partname) == 0) {
            hits++;
            if (do_cancel) {
                if (s == Q_QUEUED || s == Q_PAUSED) {
                    g_items[i].status = Q_CANCELLED;
                } else {
                    g_items[i].cancel = true;
                }
            }
        }
    }
    if (do_cancel && hits > 0) save_locked();
    mutexUnlock(&g_mtx);
    return hits;
}
