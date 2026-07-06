#include "queue.h"
#include "net.h"
#include "extract.h"
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
#define MAX_DL_THREADS 5
static Thread g_dl_threads[MAX_DL_THREADS];
static int g_dl_count = 0;
/* How many downloads may run at once. All MAX_DL_THREADS workers are always
 * started; idle ones just don't pick work. That way changing the setting
 * takes effect immediately instead of on the next launch. */
static volatile int g_max_dl = 1;
static Thread g_ex_thread;
static volatile bool g_run = false;
static const char *g_roms_root = "sdmc:/tico/roms";

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

/* ---- worker-side processing ----------------------------------------- */

typedef struct {
    QueueItem *it;
    uint64_t last_now;
    uint64_t last_tick;
} DlCtx;

static int dl_progress(void *ud, uint64_t now, uint64_t total) {
    DlCtx *ctx = (DlCtx *)ud;
    QueueItem *it = ctx->it;
    it->now = now;
    it->total = total;
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

static bool ex_progress(void *ud, const char *entry, int done) {
    QueueItem *it = (QueueItem *)ud;
    (void)entry;
    (void)done;
    return !it->cancel && g_run;
}

/* Append a download outcome to the history log shown in Settings. */
static void log_download(const QueueItem *it, const char *status) {
    fs_mkdir_p(CONFIG_DIR);
    char ts[32] = "";
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *tm = localtime_r(&t, &tmv);
    if (tm) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    }

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
    if (!in || strstr(in, "..")) {
        return false;
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

/* True if a URL's host is archive.org or a *.archive.org subdomain. Used to
 * avoid sending archive.org S3 credentials to any other host. */
static bool is_archive_org_url(const char *url) {
    const char *p = strstr(url, "://");
    if (!p) {
        return false;
    }
    p += 3;
    char host[256];
    size_t i = 0;
    for (; p[i] && p[i] != '/' && p[i] != ':' && i + 1 < sizeof(host); i++) {
        host[i] = p[i];
    }
    host[i] = '\0';
    size_t hl = strlen(host);
    static const char dom[] = "archive.org";
    size_t dl = sizeof(dom) - 1;
    if (hl == dl && strcasecmp(host, dom) == 0) {
        return true;
    }
    if (hl > dl && host[hl - dl - 1] == '.' &&
        strcasecmp(host + hl - dl, dom) == 0) {
        return true;
    }
    return false;
}

/* ---- persistence ----------------------------------------------------- */

/* Write the still-pending items to disk so the queue survives an app restart.
 * Assumes g_mtx is held. Credentials (it->auth) are deliberately NOT persisted;
 * they're re-derived from credentials.json on load so the S3 secret lives in
 * only one place. */
static void save_locked(void) {
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(QUEUE_STATE_PATH, "wb");
    if (!f) {
        return;
    }
    fputs("{\"items\":[", f);
    bool first = true;
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
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
        fputs(",\"md5\":", f);
        json_write_escaped(f, it->md5);
        fprintf(f, ",\"size\":%llu,\"is_archive\":%s,\"seq\":%u}",
                (unsigned long long)it->size, it->is_archive ? "true" : "false",
                it->seq);
    }
    fputs("]}", f);
    fclose(f);
}

/* Reload a previously-saved queue. Outstanding items come back as Q_QUEUED so
 * the worker re-attempts them (resuming from any .part already on disk). */
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
                json_copy(body, tok, json_obj_get(body, tok, child, "md5"),
                          it->md5, sizeof(it->md5));
                it->size =
                    json_u64(body, tok, json_obj_get(body, tok, child, "size"));
                it->is_archive = json_bool(
                    body, tok, json_obj_get(body, tok, child, "is_archive"));
                it->seq = (uint32_t)json_u64(
                    body, tok, json_obj_get(body, tok, child, "seq"));
                snprintf(it->auth, sizeof(it->auth), "%s", auth);
                it->status = it->url[0] ? Q_QUEUED : Q_FREE;
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

static void process_item(QueueItem *it) {
    /* Never trust the remote filename as a filesystem path. */
    char safe[600];
    if (!safe_rel(it->name, safe, sizeof(safe))) {
        set_fail(it, "bad name");
        return;
    }

    /* Key the temp file by target too, so two same-named files headed to
     * different consoles can download concurrently without sharing a .part. */
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s/%s_%s.part", DL_TMP_DIR, it->target, safe);
    fs_ensure_parent(tmp);

    char destdir[1200];
    snprintf(destdir, sizeof(destdir), "%s/%s", g_roms_root, it->target);

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
    DlCtx ctx = { it, have, armGetSystemTick() };

    /* Bail out before writing if the SD card can't hold what's still to fetch. */
    uint64_t need = (it->size > have) ? (it->size - have) : 0;
    if (need > 0 && fs_free_bytes("sdmc:/") < need) {
        set_fail(it, "no space");
        return;
    }

    /* Only send archive.org S3 credentials to archive.org hosts, and only
     * over HTTPS — never leak the secret in cleartext. */
    const char *auth =
        (it->auth[0] && strncasecmp(it->url, "https://", 8) == 0 &&
         is_archive_org_url(it->url))
            ? it->auth
            : NULL;
    long code = 0;
    bool ok = false;
    /* Transient server errors (5xx/429, e.g. archive.org throttling a burst of
     * new connections) and transport drops get a couple of automatic retries
     * with a short backoff, resuming from the .part instead of failing. */
    for (int attempt = 0;; attempt++) {
        ok = http_download(it->url, tmp, auth, dl_progress, &ctx, have, &code);
        it->http_code = code;
        if (ok || it->cancel || it->pause || !g_run) {
            break;
        }
        if (!net_is_up_fresh()) {
            break; /* connection is down: park instead of burning retries */
        }
        bool transient = (code == 0 || code == 429 || code >= 500);
        if (!transient || attempt >= 2) {
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

    if (!g_run) {
        /* Shutting down: keep the .part and leave the item pending (status
         * unchanged) so it's saved and resumes on the next launch. */
        return;
    }
    if (it->cancel) {
        remove(tmp);
        it->status = Q_CANCELLED;
        log_download(it, "cancelled");
        return;
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
        char got[33];
        if (!md5_file(tmp, got, &it->cancel)) {
            remove(tmp);
            if (it->cancel) {
                it->status = Q_CANCELLED;
                log_download(it, "cancelled");
            } else {
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
        it->status = Q_DONE;
        log_download(it, "done");
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
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s/%s_%s.part", DL_TMP_DIR, it->target, safe);
    char destdir[1200];
    snprintf(destdir, sizeof(destdir), "%s/%s", g_roms_root, it->target);

    fs_mkdir_p(destdir);
    int ow = 0;
    int n = extract_archive(tmp, destdir, ex_progress, it, &ow);
    if (it->cancel || !g_run) {
        remove(tmp);
        if (it->cancel) {
            it->status = Q_CANCELLED;
            log_download(it, "cancelled");
        }
        return;
    }
    if (n > 0) {
        remove(tmp);
        it->overwrote = ow;
        it->status = Q_DONE;
        log_download(it, "done");
        return;
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
        int active = 0;
        for (int i = 0; i < QUEUE_MAX; i++) {
            QStatus s = g_items[i].status;
            if (s == Q_DOWNLOADING || s == Q_VERIFYING) {
                active++;
            }
        }
        if (online && active < g_max_dl) {
            for (int i = 0; i < QUEUE_MAX; i++) {
                QStatus s = g_items[i].status;
                if ((s == Q_QUEUED || s == Q_PAUSED) && g_items[i].seq < best) {
                    best = g_items[i].seq;
                    pick = i;
                }
            }
        }
        if (pick >= 0) {
            g_items[pick].pause = false;
            g_items[pick].status = Q_DOWNLOADING;
        }
        mutexUnlock(&g_mtx);

        if (pick < 0) {
            svcSleepThread(150000000ULL); /* 150 ms idle */
            continue;
        }
        process_item(&g_items[pick]);
        mutexLock(&g_mtx);
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
    /* insertion sort by seq (cnt <= 5) */
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

void queue_init(const char *roms_root, int max_dl) {
    if (roms_root && roms_root[0]) g_roms_root = roms_root;
    memset(g_items, 0, sizeof(g_items));
    mutexInit(&g_mtx);
    mutexInit(&g_net_mtx);
    queue_load(); /* restore any downloads pending from a previous session */
    g_run = true;
    queue_set_max_dl(max_dl);
    /* cpuid -2 = default core (–1 is invalid for svcCreateThread). */
    g_dl_count = 0;
    for (int i = 0; i < MAX_DL_THREADS; i++) {
        Result rc = threadCreate(&g_dl_threads[i], dl_worker, NULL, NULL, 0x40000, 0x2C, -2);
        if (R_SUCCEEDED(rc)) {
            if (R_SUCCEEDED(threadStart(&g_dl_threads[i]))) {
                g_dl_count++;
            } else {
                threadClose(&g_dl_threads[i]);
            }
        }
    }
    bool ex_ok = false;
    Result rc2 = threadCreate(&g_ex_thread, ex_worker, NULL, NULL, 0x40000, 0x2C, -2);
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
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(LOG_PATH, "a");
    if (f) {
        fprintf(f, "queue: %d/%d dl_workers started (limit %d), ex_worker %s\n",
                g_dl_count, MAX_DL_THREADS, (int)g_max_dl,
                ex_ok ? "started" : "FAILED");
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
    /* Persist after both threads have stopped so interrupted downloads and
     * pending extractions survive a restart/update and resume next launch. */
    mutexLock(&g_mtx);
    save_locked();
    mutexUnlock(&g_mtx);
}

bool queue_add(const char *url, const char *name, const char *target,
               const char *auth, uint64_t size, bool is_archive,
               const char *md5) {
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
        return false;
    }
    QueueItem *it = &g_items[slot];
    memset(it, 0, sizeof(*it));
    snprintf(it->url, sizeof(it->url), "%s", url);
    snprintf(it->name, sizeof(it->name), "%s", name);
    snprintf(it->target, sizeof(it->target), "%s", target);
    snprintf(it->auth, sizeof(it->auth), "%s", auth ? auth : "");
    snprintf(it->md5, sizeof(it->md5), "%s", md5 ? md5 : "");
    it->size = size;
    it->is_archive = is_archive;
    it->seq = g_seq++;
    it->status = Q_QUEUED;
    save_locked();
    mutexUnlock(&g_mtx);
    return true;
}

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
    if (s == Q_FAILED || s == Q_CANCELLED) {
        QueueItem *it = &g_items[slot];
        it->cancel = false;
        it->pause = false;
        it->now = 0;
        it->total = 0;
        it->speed = 0;
        it->http_code = 0;
        it->fail_reason[0] = '\0';
        /* Keep the original seq so the item resumes in its current list
         * position (and is picked promptly) instead of jumping to the bottom. */
        it->status = Q_QUEUED;
        save_locked();
    }
    mutexUnlock(&g_mtx);
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
        if (!found && (s == Q_DOWNLOADING || s == Q_VERIFYING ||
                       s == Q_EXTRACTING)) {
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

int queue_cancel_by_part(const char *partname, bool do_cancel) {
    int hits = 0;
    mutexLock(&g_mtx);
    for (int i = 0; i < QUEUE_MAX; i++) {
        QStatus s = g_items[i].status;
        if (s == Q_FREE || s == Q_DONE || s == Q_SAVED ||
            s == Q_FAILED || s == Q_CANCELLED)
            continue;
        char safe[600];
        if (!safe_rel(g_items[i].name, safe, sizeof(safe)))
            continue;
        char expect[700];
        snprintf(expect, sizeof(expect), "%s_%s.part", g_items[i].target, safe);
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
