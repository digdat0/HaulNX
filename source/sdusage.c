#include "sdusage.h"
#include "config.h"   /* DATA_DIR */
#include "fsutil.h"   /* fs_mkdir_p */
#include "jsonutil.h" /* json_write_escaped */

#include <switch.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <inttypes.h>

/* Kept per scan: every top-level folder (plus one bucket for files sitting in
 * the root), each with its own subfolders (plus one for its loose files), and
 * the largest files anywhere on the card. Anything past the caps is folded
 * into the last bucket rather than dropped, so totals always add up. */
#define SDU_MAX_TOP   96
#define SDU_MAX_SUB   64
#define SDU_MAX_BIG   200
#define SDU_NAME      96
#define SDU_PATH      512
#define SDU_MAX_DEPTH 32
#define SDU_KNAME     64
#define SDU_KID_PER   64   /* biggest entries kept inside each subfolder */
#define SDU_MAX_KID   16384 /* pool: room for 256 subfolders' worth */
#define SDU_STACK     0x20000
#define SDU_PRIO      0x3B /* below the UI and the transfer workers */

typedef struct {
    char name[SDU_NAME];
    unsigned long long bytes;
    unsigned long long files;
    unsigned items; /* direct entries (subfolders only): games, apps, ... */
    short kbase;    /* first slot of this subfolder's block in kid[], -1 = none */
    short nkid;
} SduBucket;

/* One entry directly inside a subfolder (roms/snes/<this>), for the
 * Storage page's treemap drill-down. Only the biggest SDU_KID_PER per
 * subfolder are kept; the page shows the rest as one remainder tile. */
typedef struct {
    char name[SDU_KNAME];
    unsigned long long bytes;
    unsigned long long files;
    bool dir;
} SduKid;

typedef struct {
    char path[SDU_PATH];
    unsigned long long size;
} SduBig;

typedef struct {
    SduBucket top[SDU_MAX_TOP];
    int ntop;
    SduBucket sub[SDU_MAX_TOP][SDU_MAX_SUB];
    int nsub[SDU_MAX_TOP];
    SduBig big[SDU_MAX_BIG];
    int nbig;
    SduKid kid[SDU_MAX_KID];
    int nkblk; /* kid[] blocks handed out so far */
    unsigned long long total_bytes;
    unsigned long long files;
    unsigned long long dirs;
    long long when; /* epoch seconds the scan finished */
    char scope[1024]; /* the folder list scanned ("" = whole card) */
} SduResult;

static Mutex s_mx;                 /* guards s_done and the progress text */
static Mutex s_reqmx;              /* serializes sdusage_request (Wi-Fi + USB threads) */
static SduResult *s_done = NULL;   /* last finished result, published */
static SduResult *s_work = NULL;   /* the running scan's scratch */
static Thread s_thr;
static bool s_thr_live = false;
static volatile bool s_running = false;
static volatile bool s_cancel = false;
static volatile unsigned long long s_scanned = 0;
static char s_current[SDU_NAME];
static char s_path[SDU_PATH * 2];  /* the walk's shared path buffer */
static char s_dirs[1024];          /* this scan's folders, comma-separated ("" = whole card) */

/* Copy src into dst[cap], cutting it short if it doesn't fit (names and
 * paths here are display-only, so a truncated one is fine). */
static void copy_trunc(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void bucket_add(SduBucket *arr, int *n, int cap, const char *name,
                       int *idx_out) {
    if (*n < cap) {
        copy_trunc(arr[*n].name, SDU_NAME, name);
        arr[*n].bytes = 0;
        arr[*n].files = 0;
        arr[*n].items = 0;
        arr[*n].kbase = -1;
        arr[*n].nkid = 0;
        *idx_out = (*n)++;
    } else {
        /* Past the cap: everything else shares the last bucket. */
        copy_trunc(arr[cap - 1].name, SDU_NAME, "(other folders)");
        *idx_out = cap - 1;
    }
}

static void big_add(SduResult *r, const char *path, unsigned long long size) {
    if (r->nbig == SDU_MAX_BIG && size <= r->big[SDU_MAX_BIG - 1].size) {
        return;
    }
    int i = r->nbig < SDU_MAX_BIG ? r->nbig++ : SDU_MAX_BIG - 1;
    /* Keep the array sorted largest-first: shift smaller ones down. */
    while (i > 0 && r->big[i - 1].size < size) {
        r->big[i] = r->big[i - 1];
        i--;
    }
    copy_trunc(r->big[i].path, SDU_PATH, path);
    r->big[i].size = size;
}

/* Record one entry of subfolder r->sub[ti][si], keeping that subfolder's
 * biggest SDU_KID_PER sorted largest-first. Blocks come from the shared
 * pool on first use; once it runs dry the rest just get no drill-down. */
static void kid_add(SduResult *r, int ti, int si, const char *name,
                    unsigned long long bytes, unsigned long long files, bool dir) {
    SduBucket *b = &r->sub[ti][si];
    b->items++;
    if (!strcmp(b->name, "(other folders)")) {
        return;
    }
    if (b->kbase < 0) {
        if ((r->nkblk + 1) * SDU_KID_PER > SDU_MAX_KID) {
            return;
        }
        b->kbase = (short)(r->nkblk++ * SDU_KID_PER);
        b->nkid = 0;
    }
    SduKid *k = &r->kid[b->kbase];
    if (b->nkid == SDU_KID_PER && bytes <= k[SDU_KID_PER - 1].bytes) {
        return;
    }
    int i = b->nkid < SDU_KID_PER ? b->nkid++ : SDU_KID_PER - 1;
    while (i > 0 && k[i - 1].bytes < bytes) {
        k[i] = k[i - 1];
        i--;
    }
    copy_trunc(k[i].name, SDU_KNAME, name);
    k[i].bytes = bytes;
    k[i].files = files;
    k[i].dir = dir;
}

/* Walk s_path (length `len`) recursively, charging every file to top bucket
 * `ti` and its sub bucket `si` (-1 = not yet below a top-level folder). */
static void walk(SduResult *r, size_t len, int depth, int ti, int si) {
    if (s_cancel || depth > SDU_MAX_DEPTH) {
        return;
    }
    DIR *d = opendir(s_path);
    if (!d) {
        return;
    }
    struct dirent *e;
    while (!s_cancel && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        int n = snprintf(s_path + len, sizeof(s_path) - len, "%s%s",
                         (len && s_path[len - 1] == '/') ? "" : "/", e->d_name);
        if (n <= 0 || len + (size_t)n >= sizeof(s_path)) {
            s_path[len] = '\0';
            continue;
        }
        size_t nlen = len + (size_t)n;
        struct stat st;
        if (stat(s_path, &st) != 0) {
            s_path[len] = '\0';
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            r->dirs++;
            int nti = ti, nsi = si;
            if (depth == 0) {
                bucket_add(r->top, &r->ntop, SDU_MAX_TOP, e->d_name, &nti);
                mutexLock(&s_mx);
                copy_trunc(s_current, sizeof(s_current), e->d_name);
                mutexUnlock(&s_mx);
            } else if (depth == 1) {
                bucket_add(r->sub[ti], &r->nsub[ti], SDU_MAX_SUB, e->d_name, &nsi);
            }
            unsigned long long b0 = r->total_bytes, f0 = r->files;
            walk(r, nlen, depth + 1, nti, nsi);
            if (depth == 2 && si >= 0) {
                kid_add(r, ti, si, e->d_name, r->total_bytes - b0, r->files - f0, true);
            }
        } else {
            unsigned long long sz = (unsigned long long)st.st_size;
            int fti = ti, fsi = si;
            if (depth == 0) {
                /* A file sitting in the root: its own top bucket. */
                int k;
                for (k = 0; k < r->ntop; k++) {
                    if (!strcmp(r->top[k].name, "(files in root)")) break;
                }
                if (k == r->ntop) bucket_add(r->top, &r->ntop, SDU_MAX_TOP, "(files in root)", &k);
                fti = k;
            } else if (depth == 1) {
                /* A file directly inside a top-level folder. */
                int k;
                for (k = 0; k < r->nsub[ti]; k++) {
                    if (!strcmp(r->sub[ti][k].name, "(files)")) break;
                }
                if (k == r->nsub[ti]) bucket_add(r->sub[ti], &r->nsub[ti], SDU_MAX_SUB, "(files)", &k);
                fsi = k;
            }
            r->top[fti].bytes += sz;
            r->top[fti].files++;
            if (fsi >= 0 && depth >= 1) {
                r->sub[fti][fsi].bytes += sz;
                r->sub[fti][fsi].files++;
            }
            r->total_bytes += sz;
            r->files++;
            s_scanned = r->files;
            big_add(r, s_path, sz);
            if (depth == 2 && si >= 0) {
                kid_add(r, ti, si, e->d_name, sz, 1, false);
            }
        }
        s_path[len] = '\0';
    }
    closedir(d);
}

static int bucket_cmp(const void *a, const void *b) {
    unsigned long long x = ((const SduBucket *)a)->bytes;
    unsigned long long y = ((const SduBucket *)b)->bytes;
    return x < y ? 1 : x > y ? -1 : 0;
}

static void scan_thread(void *arg) {
    (void)arg;
    SduResult *r = s_work;
    memset(r, 0, sizeof(*r));
    copy_trunc(r->scope, sizeof(r->scope), s_dirs);
    if (!s_dirs[0]) {
        snprintf(s_path, sizeof(s_path), "%s", "sdmc:/");
        walk(r, strlen(s_path), 0, -1, -1);
    } else {
        /* Each listed folder is a top-level bucket of its own; walking it
         * from depth 1 makes its subfolders the sub buckets, exactly as if it
         * had been found at the root. A missing folder is just skipped. */
        char list[sizeof(s_dirs)];
        copy_trunc(list, sizeof(list), s_dirs);
        for (char *tok = strtok(list, ","); tok && !s_cancel; tok = strtok(NULL, ",")) {
            int n = snprintf(s_path, sizeof(s_path), "sdmc:/%s", tok);
            struct stat st;
            if (n <= 0 || (size_t)n >= sizeof(s_path) || stat(s_path, &st) != 0 ||
                !S_ISDIR(st.st_mode)) {
                continue;
            }
            int ti;
            bucket_add(r->top, &r->ntop, SDU_MAX_TOP, tok, &ti);
            mutexLock(&s_mx);
            copy_trunc(s_current, sizeof(s_current), tok);
            mutexUnlock(&s_mx);
            r->dirs++;
            walk(r, (size_t)n, 1, ti, -1);
        }
    }
    if (!s_cancel) {
        /* Largest first, subfolders moving with their parent. */
        for (int i = 0; i < r->ntop; i++) {
            qsort(r->sub[i], (size_t)r->nsub[i], sizeof(SduBucket), bucket_cmp);
        }
        int order[SDU_MAX_TOP];
        for (int i = 0; i < r->ntop; i++) order[i] = i;
        for (int i = 1; i < r->ntop; i++) {
            int k = order[i], j = i;
            while (j > 0 && r->top[order[j - 1]].bytes < r->top[k].bytes) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = k;
        }
        SduResult *out = s_done ? s_done : (SduResult *)malloc(sizeof(SduResult));
        if (out && out != r) {
            mutexLock(&s_mx);
            out->ntop = r->ntop;
            for (int i = 0; i < r->ntop; i++) {
                out->top[i] = r->top[order[i]];
                out->nsub[i] = r->nsub[order[i]];
                memcpy(out->sub[i], r->sub[order[i]], sizeof(SduBucket) * (size_t)r->nsub[order[i]]);
            }
            memcpy(out->big, r->big, sizeof(r->big));
            out->nbig = r->nbig;
            memcpy(out->kid, r->kid, sizeof(SduKid) * (size_t)r->nkblk * SDU_KID_PER);
            out->nkblk = r->nkblk;
            out->total_bytes = r->total_bytes;
            out->files = r->files;
            out->dirs = r->dirs;
            out->when = (long long)time(NULL);
            copy_trunc(out->scope, sizeof(out->scope), r->scope);
            s_done = out;
            mutexUnlock(&s_mx);
        }
    }
    s_running = false;
}

/* Normalize a folder list into s_dirs: split on commas/newlines, trim spaces
 * and slashes, drop an "sdmc:" prefix, and skip anything empty or containing
 * ".." so a request can never walk outside the card. */
static void set_dirs(const char *dirs) {
    s_dirs[0] = '\0';
    if (!dirs) {
        return;
    }
    size_t out = 0;
    const char *p = dirs;
    while (*p) {
        const char *e = p;
        while (*e && *e != ',' && *e != '\n' && *e != '\r') e++;
        const char *a = p, *b = e;
        while (a < b && (*a == ' ' || *a == '\t')) a++;
        if ((size_t)(b - a) >= 5 && strncmp(a, "sdmc:", 5) == 0) a += 5;
        while (a < b && *a == '/') a++;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '/')) b--;
        size_t len = (size_t)(b - a);
        bool bad = len == 0;
        for (const char *q = a; !bad && q + 1 < b; q++) {
            if (q[0] == '.' && q[1] == '.') bad = true;
        }
        if (!bad && out + len + 2 < sizeof(s_dirs)) {
            if (out) s_dirs[out++] = ',';
            memcpy(s_dirs + out, a, len);
            out += len;
            s_dirs[out] = '\0';
        }
        p = *e ? e + 1 : e;
    }
}

void sdusage_request(const char *dirs) {
    mutexLock(&s_reqmx);
    if (s_running) {
        mutexUnlock(&s_reqmx);
        return;
    }
    set_dirs(dirs);
    if (s_thr_live) {
        threadWaitForExit(&s_thr);
        threadClose(&s_thr);
        s_thr_live = false;
    }
    if (!s_work) {
        s_work = (SduResult *)malloc(sizeof(SduResult));
        if (!s_work) {
            mutexUnlock(&s_reqmx);
            return;
        }
    }
    s_cancel = false;
    s_scanned = 0;
    mutexLock(&s_mx);
    s_current[0] = '\0';
    mutexUnlock(&s_mx);
    s_running = true;
    if (R_FAILED(threadCreate(&s_thr, scan_thread, NULL, NULL, SDU_STACK,
                              SDU_PRIO, -2)) ||
        R_FAILED(threadStart(&s_thr))) {
        s_running = false;
        mutexUnlock(&s_reqmx);
        return;
    }
    s_thr_live = true;
    mutexUnlock(&s_reqmx);
}

static void write_bucket(FILE *f, const SduBucket *b) {
    fputs("{\"name\":", f);
    json_write_escaped(f, b->name);
    fprintf(f, ",\"bytes\":%llu,\"files\":%llu", b->bytes, b->files);
}

/* ---- Card health -------------------------------------------------------
 * Hardware facts straight from the SD controller: the CID register (maker,
 * product name, revision, serial, manufacture date -- sent raw, the desktop
 * decodes it), the bus speed mode, the physical area sizes, and its error
 * counters. The counters are get-and-clear, so every read is added to a
 * running total kept on the card itself (SDU_HEALTH_PATH), reset whenever
 * the CID differs (a cloned card). */
#define SDU_HEALTH_PATH DATA_DIR "/sd_health.txt"

static void write_card(FILE *f) {
    FsDeviceOperator op;
    if (R_FAILED(fsOpenDeviceOperator(&op))) {
        fputs(",\"card\":{\"ok\":false}", f);
        return;
    }
    bool inserted = false;
    s64 speed = -1, user = 0, prot = 0;
    u8 cid[16];
    memset(cid, 0, sizeof(cid));
    FsStorageErrorInfo ei;
    memset(&ei, 0, sizeof(ei));
    s64 logsz = 0;
    char logbuf[0x200];
    fsDeviceOperatorIsSdCardInserted(&op, &inserted);
    bool hs = R_SUCCEEDED(fsDeviceOperatorGetSdCardSpeedMode(&op, &speed));
    bool hc = R_SUCCEEDED(fsDeviceOperatorGetSdCardCid(&op, cid, sizeof(cid), sizeof(cid)));
    if (R_FAILED(fsDeviceOperatorGetSdCardUserAreaSize(&op, &user))) user = 0;
    if (R_FAILED(fsDeviceOperatorGetSdCardProtectedAreaSize(&op, &prot))) prot = 0;
    bool he = R_SUCCEEDED(fsDeviceOperatorGetAndClearSdCardErrorInfo(
        &op, &ei, &logsz, logbuf, sizeof(logbuf), sizeof(logbuf)));
    fsDeviceOperatorClose(&op);

    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", cid[i]);
    }
    /* Running totals: "<cid> <af> <ac> <rf> <rc> <since>" */
    unsigned long long tot[4] = {0, 0, 0, 0};
    long long since = 0;
    char ocid[40] = "";
    FILE *h = fopen(SDU_HEALTH_PATH, "rb");
    if (h) {
        if (fscanf(h, "%39s %llu %llu %llu %llu %lld", ocid, &tot[0], &tot[1],
                   &tot[2], &tot[3], &since) != 6 || strcmp(ocid, hex) != 0) {
            memset(tot, 0, sizeof(tot));
            since = 0;
            ocid[0] = '\0';
        }
        fclose(h);
    }
    unsigned long long add[4] = {ei.num_activation_failures, ei.num_activation_error_corrections,
                                 ei.num_read_write_failures, ei.num_read_write_error_corrections};
    bool dirty = !ocid[0] || !since;
    for (int i = 0; i < 4; i++) {
        if (he && add[i]) {
            tot[i] += add[i];
            dirty = true;
        }
    }
    if (!since) {
        since = (long long)time(NULL);
    }
    if (dirty && hc) {
        fs_mkdir_p(DATA_DIR);
        h = fopen(SDU_HEALTH_PATH, "wb");
        if (h) {
            fprintf(h, "%s %llu %llu %llu %llu %lld\n", hex, tot[0], tot[1], tot[2], tot[3], since);
            fclose(h);
        }
    }
    fprintf(f, ",\"card\":{\"ok\":true,\"inserted\":%s,\"cid\":\"%s\",\"speed\":%lld,"
               "\"user_area\":%lld,\"protected_area\":%lld,\"errors_ok\":%s,"
               "\"act_fail\":%llu,\"act_fix\":%llu,\"rw_fail\":%llu,\"rw_fix\":%llu,\"since\":%lld}",
            inserted ? "true" : "false", hc ? hex : "", hs ? (long long)speed : -1LL,
            (long long)user, (long long)prot, he ? "true" : "false",
            tot[0], tot[1], tot[2], tot[3], since);
}

bool sdusage_write_json(const char *path) {
    fs_mkdir_p(DATA_DIR);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    mutexLock(&s_mx);
    fprintf(f, "{\"running\":%s,\"scanned\":%llu,\"current\":",
            s_running ? "true" : "false", (unsigned long long)s_scanned);
    json_write_escaped(f, s_current);
    fputs(",\"scanning\":", f); /* the running scan's folder list */
    json_write_escaped(f, s_dirs);
    write_card(f);
    const SduResult *r = s_done;
    if (r) {
        fputs(",\"scope\":", f);
        json_write_escaped(f, r->scope);
        fprintf(f, ",\"when\":%lld,\"total\":%llu,\"files\":%llu,\"dirs\":%llu,\"folders\":[",
                r->when, r->total_bytes, r->files, r->dirs);
        for (int i = 0; i < r->ntop; i++) {
            if (i) fputc(',', f);
            write_bucket(f, &r->top[i]);
            fputs(",\"subs\":[", f);
            for (int k = 0; k < r->nsub[i]; k++) {
                const SduBucket *b = &r->sub[i][k];
                if (k) fputc(',', f);
                write_bucket(f, b);
                fprintf(f, ",\"items\":%u", b->items);
                if (b->kbase >= 0 && b->nkid > 0) {
                    fputs(",\"kids\":[", f);
                    for (int j = 0; j < b->nkid; j++) {
                        const SduKid *kd = &r->kid[b->kbase + j];
                        fputs(j ? ",{\"name\":" : "{\"name\":", f);
                        json_write_escaped(f, kd->name);
                        fprintf(f, ",\"bytes\":%llu,\"files\":%llu,\"dir\":%s}",
                                kd->bytes, kd->files, kd->dir ? "true" : "false");
                    }
                    fputc(']', f);
                }
                fputc('}', f);
            }
            fputs("]}", f);
        }
        fputs("],\"large\":[", f);
        for (int i = 0; i < r->nbig; i++) {
            fputs(i ? ",{\"path\":" : "{\"path\":", f);
            json_write_escaped(f, r->big[i].path);
            fprintf(f, ",\"size\":%llu}", r->big[i].size);
        }
        fputc(']', f);
    }
    fputc('}', f);
    mutexUnlock(&s_mx);
    return fclose(f) == 0;
}

void sdusage_shutdown(void) {
    if (!s_thr_live) {
        return;
    }
    s_cancel = true;
    threadWaitForExit(&s_thr);
    threadClose(&s_thr);
    s_thr_live = false;
}
