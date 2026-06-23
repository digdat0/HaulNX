#include "net.h"
#include "config.h"
#include "fsutil.h"

#include <switch.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_AGENT "TicoDL+/0.1 (libnx)"

static bool g_ready = false;

/* Append a line to the debug log so failures are diagnosable on-device. */
static void net_log(const char *fmt, ...) {
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
    g_ready = true;
    return true;
}

void net_exit(void) {
    if (!g_ready) {
        return;
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
 */
static void apply_tls(CURL *c) {
    (void)c;
    net_log("TLS: libnx ssl backend (console cert store)");
}

struct mem_buf {
    char *data;
    size_t len;
};

static size_t mem_write(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t add = size * nmemb;
    struct mem_buf *m = (struct mem_buf *)ud;
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

char *http_get(const char *url, long *http_code, size_t *out_len) {
    CURL *c = curl_easy_init();
    if (!c) {
        return NULL;
    }
    struct mem_buf m;
    m.data = (char *)malloc(1);
    m.len = 0;
    if (m.data) {
        m.data[0] = '\0';
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    apply_tls(c);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (http_code) {
        *http_code = code;
    }
    curl_easy_cleanup(c);

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

struct dl_ctx {
    FILE *fp;
    net_progress_cb cb;
    void *ud;
    uint64_t base; /* resume offset, added to curl's session-relative counts */
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
    if (d->cb) {
        uint64_t now = d->base + (uint64_t)dlnow;
        uint64_t total = dltotal > 0 ? d->base + (uint64_t)dltotal : 0;
        return d->cb(d->ud, now, total);
    }
    return 0;
}

bool http_download(const char *url, const char *dest_path,
                   const char *extra_header,
                   net_progress_cb cb, void *userdata,
                   uint64_t resume_from,
                   long *http_code) {
    /* Append when resuming so the existing partial file is preserved. */
    FILE *fp = fopen(dest_path, resume_from > 0 ? "ab" : "wb");
    if (!fp) {
        return false;
    }
    CURL *c = curl_easy_init();
    if (!c) {
        fclose(fp);
        return false;
    }

    struct dl_ctx d;
    d.fp = fp;
    d.cb = cb;
    d.ud = userdata;
    d.base = resume_from;

    struct curl_slist *hdrs = NULL;
    if (extra_header && extra_header[0]) {
        hdrs = curl_slist_append(hdrs, extra_header);
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_info);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L); /* treat 4xx/5xx as errors */
    if (resume_from > 0) {
        curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE,
                         (curl_off_t)resume_from);
    }
    if (hdrs) {
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
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
    fclose(fp);

    net_log("DL  %s (resume=%llu) -> curl=%d(%s) http=%ld", url,
            (unsigned long long)resume_from, (int)rc, curl_easy_strerror(rc),
            code);

    /* 416 on a resumed transfer means the server has nothing past our offset:
     * the partial file already holds the whole thing. Treat as success. */
    if (rc == CURLE_HTTP_RETURNED_ERROR && code == 416 && resume_from > 0) {
        return true;
    }
    return rc == CURLE_OK;
}
