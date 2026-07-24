#include "net.h"
#include "config.h"
#include "fsutil.h"

#include <switch.h>
#include <curl/curl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define USER_AGENT "HaulNX/1.0 (libnx)"

static bool g_ready = false;

/* One reused easy handle for the small metadata/API GETs (http_get). curl keeps
 * its live connections in the handle's cache, so reusing it across sequential
 * fetches skips the TLS handshake to archive.org/github when browsing repo to
 * repo. Guarded by a mutex because metadata and update-check run on separate
 * worker threads and a curl handle is not safe to share concurrently. The bulk
 * file transfers (http_download) keep their own per-call handle. */
static CURL *g_get_handle = NULL;
static Mutex g_get_mtx;

/* Append a line to the debug log so failures are diagnosable on-device. This is
 * the busiest writer in the app (two lines per HTTP request, from several worker
 * threads), so the size check is sampled rather than run every call — a stat per
 * 64 lines is nothing, one per line would contend with the downloads for the SD
 * card. Racy across threads by design: a missed sample only delays a rotation. */
static void net_log(const char *fmt, ...) {
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

bool net_init(void) {
    if (g_ready) {
        return true;
    }
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        return false;
    }
    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        socketExit();
        return false;
    }
    mutexInit(&g_get_mtx);
    /* One guaranteed size check per session, before anything appends: net_log
     * only samples, and the other writers of debug.log (extract, queue, the
     * updater) don't check at all. */
    fs_log_rotate(LOG_PATH, LOG_ROTATE_DEBUG);
    g_ready = true;
    return true;
}

void net_exit(void) {
    if (!g_ready) {
        return;
    }
    if (g_get_handle) {
        curl_easy_cleanup(g_get_handle);
        g_get_handle = NULL;
    }
    curl_global_cleanup();
    socketExit();
    g_ready = false;
}

/*
 * devkitPro's curl uses the libnx ssl-service backend (built --with-libnx,
 * --without-mbedtls), which performs TLS through the console's `ssl` system
 * service and verifies against the console's own certificate store. So no
 * cacert.pem / mbedtls is involved; leaving curl's defaults (VERIFYPEER on)
 * uses that store. This works on real hardware; emulators that stub the ssl
 * service (e.g. Ryujinx) will fail the handshake regardless.
 *
 * There is nothing per-request to configure, so this only records which backend
 * is in play — once per session, not once per request: the line is a constant,
 * and writing it on every GET meant an SD open/write/close competing with the
 * downloads for the card on every metadata fetch.
 */
static void apply_tls(CURL *c) {
    static bool logged = false;
    (void)c;
    if (!logged) {
        logged = true;
        net_log("TLS: libnx ssl backend (console cert store)");
    }
}

/* Restrict what a URL is allowed to be. curl speaks far more than HTTP, and a
 * download URL can come from an imported dl_sources.json, so pin both the
 * initial request and any redirect to http/https — otherwise a crafted
 * collection could make a "download" read file:// off the SD card. */
static void pin_protocols(CURL *c) {
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS,
                     CURLPROTO_HTTP | CURLPROTO_HTTPS);
}

bool net_is_archive_org_url(const char *url) {
    if (!url) {
        return false;
    }
    const char *p = strstr(url, "://");
    if (!p) {
        return false;
    }
    p += 3;
    /* The authority runs to the first path/query/fragment delimiter. Backslash
     * counts as one: several URL parsers fold it to '/', and our idea of the
     * host must not be able to differ from the one curl actually connects to. */
    size_t alen = 0;
    while (p[alen] && p[alen] != '/' && p[alen] != '\\' && p[alen] != '?' &&
           p[alen] != '#') {
        alen++;
    }
    /* Everything up to the LAST '@' is userinfo, not the host — the host in
     * "https://archive.org@evil.com/" is evil.com. */
    size_t hs = 0;
    for (size_t i = 0; i < alen; i++) {
        if (p[i] == '@') {
            hs = i + 1;
        }
    }
    const char *h = p + hs;
    size_t hl = alen - hs;
    for (size_t i = 0; i < hl; i++) {
        if (h[i] == ':') { /* drop the port */
            hl = i;
            break;
        }
    }
    /* "archive.org." is the same name to DNS as "archive.org". */
    while (hl > 0 && h[hl - 1] == '.') {
        hl--;
    }

    static const char dom[] = "archive.org";
    const size_t dl = sizeof(dom) - 1;
    if (hl == dl && strncasecmp(h, dom, dl) == 0) {
        return true;
    }
    return hl > dl && h[hl - dl - 1] == '.' &&
           strncasecmp(h + hl - dl, dom, dl) == 0;
}

/* Ceiling for an in-memory GET. The biggest legitimate response is an
 * archive.org /metadata/ listing for a huge item — single-digit MB. Anything
 * beyond this is a broken or hostile server, and letting it realloc without
 * bound would take the whole app down on a console with this little RAM. */
#define HTTP_GET_MAX (32u * 1024 * 1024)

struct mem_buf {
    char *data;
    size_t len;
};

static size_t mem_write(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t add = size * nmemb;
    struct mem_buf *m = (struct mem_buf *)ud;
    if (add > HTTP_GET_MAX - m->len) {
        return 0; /* over budget: short write makes curl abort the transfer */
    }
    char *np = (char *)realloc(m->data, m->len + add + 1);
    if (!np) {
        return 0;
    }
    m->data = np;
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

/* Run one GET on handle c (already reset, caller owns exclusivity). */
static char *http_get_impl(CURL *c, const char *url, long *http_code,
                           size_t *out_len) {
    struct mem_buf m;
    m.data = (char *)malloc(1);
    m.len = 0;
    if (m.data) {
        m.data[0] = '\0';
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    pin_protocols(c);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    /* archive.org's /metadata/ JSON (whole file listing of an item) is large
     * and highly compressible; ask for gzip so the transfer is ~5-10x smaller.
     * curl is built --with-zlib and decompresses transparently. "" advertises
     * every encoding curl supports. Only for these API/metadata GETs — the bulk
     * file downloads are already-compressed archives. */
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    /* Keep the connection alive between fetches so it stays in the cache. */
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    apply_tls(c);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (http_code) {
        *http_code = code;
    }

    net_log("GET %s -> curl=%d(%s) http=%ld len=%lu", url, (int)rc,
            curl_easy_strerror(rc), code, (unsigned long)m.len);

    if (rc != CURLE_OK) {
        free(m.data);
        return NULL;
    }
    if (out_len) {
        *out_len = m.len;
    }
    return m.data;
}

char *http_get(const char *url, long *http_code, size_t *out_len) {
    /* Serialize on the shared handle: reset clears per-request options but keeps
     * the connection cache, so a warm connection is reused across calls. */
    mutexLock(&g_get_mtx);
    if (!g_get_handle) {
        g_get_handle = curl_easy_init();
    } else {
        curl_easy_reset(g_get_handle);
    }
    CURL *c = g_get_handle;
    if (!c) {
        mutexUnlock(&g_get_mtx);
        return NULL;
    }
    /* Handle is NOT cleaned up here: it lives on for reuse (freed in net_exit). */
    char *r = http_get_impl(c, url, http_code, out_len);
    mutexUnlock(&g_get_mtx);
    return r;
}

/* Private per-worker connections for parallel GETs (bulk metadata refresh).
 * Each has its own TLS connection, so several fetches overlap instead of
 * serializing on g_get_mtx. One worker per handle — not shared. */
void *net_conn_new(void) {
    return curl_easy_init();
}

void net_conn_free(void *conn) {
    if (conn) {
        curl_easy_cleanup((CURL *)conn);
    }
}

char *http_get_on(void *conn, const char *url, long *http_code, size_t *out_len) {
    CURL *c = (CURL *)conn;
    if (!c) {
        return NULL;
    }
    curl_easy_reset(c);
    return http_get_impl(c, url, http_code, out_len);
}

struct dl_ctx {
    FILE *fp;
    net_progress_cb cb;
    void *ud;
    uint64_t base; /* resume offset, added to curl's session-relative counts */
    CURL *handle;         /* the transfer's own handle, for live re-limiting */
    net_rate_cb rate_cb;  /* live rate-cap provider (bytes/sec, 0 = unlimited) */
    void *rate_ud;
    uint64_t last_cap;    /* last cap applied, so we only setopt on a change */
};

static size_t file_write(void *ptr, size_t size, size_t nmemb, void *ud) {
    struct dl_ctx *d = (struct dl_ctx *)ud;
    /* curl expects the number of BYTES handled; fwrite returns item count. */
    return fwrite(ptr, size, nmemb, d->fp) * size;
}

static int xfer_info(void *ud, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    struct dl_ctx *d = (struct dl_ctx *)ud;
    /* Track a live download-rate cap. curl reads MAX_RECV_SPEED_LARGE on the fly,
     * and setopt on a handle from inside its own progress callback is allowed, so
     * re-applying here lets the cap follow a settings change or a change in how
     * many transfers share a global budget — without restarting the download. */
    if (d->rate_cb) {
        uint64_t cap = d->rate_cb(d->rate_ud);
        if (cap != d->last_cap) {
            curl_easy_setopt(d->handle, CURLOPT_MAX_RECV_SPEED_LARGE,
                             (curl_off_t)cap);
            d->last_cap = cap;
        }
    }
    if (d->cb) {
        uint64_t now = d->base + (uint64_t)dlnow;
        uint64_t total = dltotal > 0 ? d->base + (uint64_t)dltotal : 0;
        return d->cb(d->ud, now, total);
    }
    return 0;
}

/* ---- credential redirect guard ---------------------------------------- */

/* True if `u` starts with a URL scheme ("https:", "file:", ...) rather than
 * being a path relative to the URL we are already on. */
static bool has_scheme(const char *u) {
    size_t i = 0;
    while (u[i] && (isalnum((unsigned char)u[i]) || u[i] == '+' ||
                    u[i] == '-' || u[i] == '.')) {
        i++;
    }
    return i > 0 && u[i] == ':';
}

/* May the credential follow this Location? */
static bool redirect_ok(const char *loc) {
    /* "//host/path" keeps the scheme and changes the host: resolve it against
     * https (REDIR_PROTOCOLS already forbids anything else) and check the host. */
    if (loc[0] == '/' && loc[1] == '/') {
        char abs[2048];
        int n = snprintf(abs, sizeof(abs), "https:%s", loc);
        return n > 0 && (size_t)n < sizeof(abs) && net_is_archive_org_url(abs);
    }
    /* No scheme at all: relative to the URL we are on, which has already been
     * checked — so the host cannot change. */
    if (!has_scheme(loc)) {
        return true;
    }
    return strncasecmp(loc, "https://", 8) == 0 && net_is_archive_org_url(loc);
}

struct auth_guard {
    long code; /* status of the response whose headers are arriving */
};

/*
 * Keep the archive.org credential from riding a redirect off archive.org.
 *
 * curl is told to carry the Authorization header across hosts
 * (CURLOPT_UNRESTRICTED_AUTH) because archive.org's /download/ URL redirects to
 * a data node (ia######.us.archive.org) and, without it, the node sees an
 * unauthenticated request and 401s a restricted item. curl has no host allowlist
 * to bound that trust, so we read the Location of each 3xx as it arrives:
 * returning a short count aborts the transfer *before* curl issues the
 * redirected request, so a redirect off archive.org fails the download rather
 * than handing the S3 secret to whoever it points at.
 */
static size_t auth_hdr_check(char *buf, size_t size, size_t nitems, void *ud) {
    struct auth_guard *g = (struct auth_guard *)ud;
    size_t len = size * nitems;

    if (len >= 5 && strncasecmp(buf, "HTTP/", 5) == 0) {
        /* Status line: a new response starts here (curl replays this callback
         * for every hop, so the code must be re-read each time). */
        const char *sp = memchr(buf, ' ', len);
        g->code = sp ? strtol(sp + 1, NULL, 10) : 0;
        return len;
    }
    if (g->code != 301 && g->code != 302 && g->code != 303 &&
        g->code != 307 && g->code != 308) {
        return len;
    }
    static const char key[] = "location:";
    const size_t kl = sizeof(key) - 1;
    if (len <= kl || strncasecmp(buf, key, kl) != 0) {
        return len;
    }
    const char *v = buf + kl;
    size_t vl = len - kl;
    while (vl > 0 && (*v == ' ' || *v == '\t')) {
        v++;
        vl--;
    }
    while (vl > 0 && (v[vl - 1] == '\r' || v[vl - 1] == '\n' ||
                      v[vl - 1] == ' ' || v[vl - 1] == '\t')) {
        vl--;
    }
    char loc[2048];
    if (vl == 0 || vl >= sizeof(loc)) {
        net_log("SEC redirect refused: unusable Location (%lu bytes)",
                (unsigned long)vl);
        return 0; /* can't vet it, so don't follow it */
    }
    memcpy(loc, v, vl);
    loc[vl] = '\0';
    if (!redirect_ok(loc)) {
        net_log("SEC redirect refused, credential withheld: %s", loc);
        return 0;
    }
    return len;
}

bool http_download(const char *url, const char *dest_path,
                   const char *extra_header,
                   net_progress_cb cb, void *userdata,
                   net_rate_cb rate_cb, void *rate_ud,
                   uint64_t resume_from,
                   long *http_code) {
    /* Append when resuming so the existing partial file is preserved. */
    FILE *fp = fopen(dest_path, resume_from > 0 ? "ab" : "wb");
    if (!fp) {
        return false;
    }
    /* Batch curl's ~16KB chunks into large SD writes: the SD card is a shared,
     * serializing resource, and many small writes here stall the UI thread's
     * own SD reads (icons/config/cache) → visible hitches during a download.
     * Mirrors the extractor's write buffering. Freed after fclose. */
    char *iobuf = (char *)malloc(512 * 1024);
    if (iobuf) {
        setvbuf(fp, iobuf, _IOFBF, 512 * 1024);
    }
    CURL *c = curl_easy_init();
    if (!c) {
        fclose(fp);
        free(iobuf);
        return false;
    }

    struct dl_ctx d;
    d.fp = fp;
    d.cb = cb;
    d.ud = userdata;
    d.base = resume_from;
    d.handle = c;
    d.rate_cb = rate_cb;
    d.rate_ud = rate_ud;
    d.last_cap = 0;

    /* A credential goes to archive.org over TLS or it does not go at all. The
     * caller already gates this, but the rule is the whole reason the header
     * exists, so it is re-checked at the point of use rather than trusted from
     * a distance: an unauthenticated attempt is the correct fallback. */
    struct curl_slist *hdrs = NULL;
    if (extra_header && extra_header[0]) {
        if (strncasecmp(url, "https://", 8) == 0 && net_is_archive_org_url(url)) {
            hdrs = curl_slist_append(hdrs, extra_header);
        } else {
            net_log("SEC credential withheld, not an archive.org https URL");
        }
    }
    struct auth_guard guard = {0};

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    pin_protocols(c);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_info);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    /* Abort a transfer that stalls (<30 B/s for 30s), e.g. Wi-Fi dropped
     * mid-download; otherwise a dead connection hangs the worker forever. */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 30L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L); /* treat 4xx/5xx as errors */
    /* Seed the rate cap before the first byte so a limit is honoured from the
     * start; xfer_info keeps it current as the setting / active count change. */
    if (rate_cb) {
        d.last_cap = rate_cb(rate_ud);
        curl_easy_setopt(c, CURLOPT_MAX_RECV_SPEED_LARGE,
                         (curl_off_t)d.last_cap);
    }
    if (resume_from > 0) {
        curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE,
                         (curl_off_t)resume_from);
    }
    if (hdrs) {
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        /* archive.org's /download/ URL redirects to a data node
         * (ia######.us.archive.org); by default curl drops a custom
         * Authorization header across that host change, so the node sees an
         * unauthenticated request and 401s a restricted item. Keep the header
         * across the redirect (curl's --location-trusted) — and then bound that
         * trust ourselves, since curl has no host allowlist: auth_hdr_check
         * vets every Location before it is followed, and every hop must be TLS
         * so the header can't be downgraded onto plain http. */
        curl_easy_setopt(c, CURLOPT_UNRESTRICTED_AUTH, 1L);
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, auth_hdr_check);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &guard);
    }
    apply_tls(c);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (http_code) {
        *http_code = code;
    }
    if (hdrs) {
        curl_slist_free_all(hdrs);
    }
    curl_easy_cleanup(c);
    /* Up to 512KB of the tail is still sitting in the stdio buffer here, so a
     * full or ejected card surfaces at this fclose rather than at any write.
     * Discarding the result would hand the caller a short file that only the
     * size/md5 checks catch — and an item that declares neither would be
     * installed truncated. */
    bool flush_ok = (fclose(fp) == 0);
    free(iobuf); /* only after fclose flushes through it */

    net_log("DL  %s (resume=%llu) -> curl=%d(%s) http=%ld%s", url,
            (unsigned long long)resume_from, (int)rc, curl_easy_strerror(rc),
            code, flush_ok ? "" : " FLUSH FAILED");

    if (!flush_ok) {
        return false; /* the .part is short; the caller resumes from it */
    }

    /* 416 on a resumed transfer means the server has nothing past our offset:
     * the partial file already holds the whole thing. Treat as success. */
    if (rc == CURLE_HTTP_RETURNED_ERROR && code == 416 && resume_from > 0) {
        return true;
    }
    return rc == CURLE_OK;
}
