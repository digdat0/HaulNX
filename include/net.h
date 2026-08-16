#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up libnx sockets + curl global state. Safe to call more than once. */
bool net_init(void);
void net_exit(void);

/* Progress callback: return non-zero to abort the transfer. */
typedef int (*net_progress_cb)(void *userdata, uint64_t now, uint64_t total);

/* Rate-limit provider: return the current maximum download rate for THIS
 * transfer in bytes/sec (0 = unlimited). Polled repeatedly during the download
 * so the cap can track a live setting change and, for a shared global budget,
 * the current number of active transfers. May be NULL for no limit. */
typedef uint64_t (*net_rate_cb)(void *userdata);

/*
 * HTTP GET into a heap buffer (NUL-terminated). Caller frees the result.
 * Returns NULL on transport error. *http_code / *out_len are filled if non-NULL.
 */
char *http_get(const char *url, long *http_code, size_t *out_len);

/*
 * A private connection for parallel GETs (e.g. bulk metadata refresh). Each
 * handle owns its own TLS connection, so several workers can fetch at once
 * without serializing on http_get()'s shared handle. Not thread-shared: use one
 * handle per worker thread. Free with net_conn_free.
 */
void *net_conn_new(void);
void net_conn_free(void *conn);
char *http_get_on(void *conn, const char *url, long *http_code, size_t *out_len);

/*
 * True if a URL's host is archive.org or a *.archive.org subdomain. The one
 * gate on where the archive.org S3 credential may travel, so it lives here
 * rather than in a caller: http_download enforces it on the initial URL and on
 * every redirect, and queue.c uses it to decide whether to attach the header at
 * all. Parses the authority the way curl does — userinfo before the last '@' is
 * discarded, and '\' terminates the authority — so a URL like
 * "https://archive.org@evil.com/" is correctly seen as evil.com.
 */
bool net_is_archive_org_url(const char *url);

/*
 * True if url's host exactly equals host (case-insensitive, ignoring a
 * trailing dot and the port on either side). Used to scope a non-archive.org
 * provider credential (e.g. a self-hosted RomM instance) to exactly its
 * configured host, the same way net_is_archive_org_url scopes the
 * archive.org one — parsed the same way curl resolves the authority (userinfo
 * before the last '@' discarded, '\' terminates it like '/').
 */
bool net_url_host_eq(const char *url, const char *host);

/*
 * Like http_get, but for a provider with its own credentials scoped to a
 * single configured host (e.g. a self-hosted RomM instance) rather than
 * archive.org's. Attaches HTTP Basic Auth (user/pass, via CURLOPT_USERPWD) or
 * a literal header line (auth_header, e.g. "authorization: Bearer rmm_xxx")
 * -- pass one or the other, not both; NULL/empty both means unauthenticated.
 * The credential is attached only when url's host exactly matches auth_host
 * (net_url_host_eq) -- over http or https alike, since a self-hosted instance
 * may have no TLS at all, unlike the archive.org credential, which is
 * https-only. Redirects are never followed: a JSON API has no legitimate
 * reason to redirect, and not following one closes off any question of where
 * the credential could end up. verify_tls=false skips TLS certificate
 * verification, for a self-signed/LAN certificate. Returns NULL on transport
 * error; *http_code / *out_len are filled if non-NULL.
 */
char *http_get_authed(const char *url, const char *auth_host,
                      const char *user, const char *pass,
                      const char *auth_header, bool verify_tls,
                      long *http_code, size_t *out_len);

/*
 * Stream an HTTP GET to a file on disk. Returns true on a 2xx download.
 * Set extra_header (e.g. "authorization: LOW key:secret") or NULL. A header is
 * only ever sent to archive.org over HTTPS: the initial URL is re-checked here,
 * and any redirect that would carry it elsewhere aborts the transfer.
 * If resume_from > 0, the file is opened for append and a Range request is made
 * to continue from that byte offset (the progress callback's now/total include
 * the offset). A 416 reply with resume_from > 0 is treated as success (the file
 * already holds everything the server has).
 * rate_cb (with rate_ud) supplies a live download-rate cap in bytes/sec; pass
 * NULL for an unthrottled transfer.
 */
bool http_download(const char *url, const char *dest_path,
                   const char *extra_header,
                   net_progress_cb cb, void *userdata,
                   net_rate_cb rate_cb, void *rate_ud,
                   uint64_t resume_from,
                   long *http_code);

/*
 * Like http_download, but for a provider with its own credentials scoped to
 * a single configured host (e.g. a self-hosted RomM instance) rather than
 * archive.org's. extra_header is sent only when url's host exactly matches
 * auth_host (net_url_host_eq) -- over http or https alike, since a
 * self-hosted instance may have no TLS at all. Redirects are never followed:
 * a file-content endpoint has no legitimate reason to redirect, and not
 * following one closes off any question of where the header could end up.
 * verify_tls=false skips TLS certificate verification (a self-signed/LAN
 * certificate). Other params behave exactly as http_download's.
 */
bool http_download_authed(const char *url, const char *dest_path,
                          const char *auth_host, const char *extra_header,
                          bool verify_tls,
                          net_progress_cb cb, void *userdata,
                          net_rate_cb rate_cb, void *rate_ud,
                          uint64_t resume_from,
                          long *http_code);

/* Which half of the speed test is running (see SpeedProg). */
typedef enum {
    SP_IDLE = 0,
    SP_DOWNLOAD = 1,
    SP_UPLOAD = 2,
    SP_DONE = 3,
} SpeedPhase;

/*
 * Live state for the diagnostics speed test. net_speedtest_live() (worker
 * thread) writes these every progress tick; the UI thread reads them each
 * frame to draw the download/upload meters and sets `cancel` to abort. Plain
 * scalars shared without a lock — a torn read only mis-draws a single frame,
 * and `cancel`/`phase` are single-writer. Zero-initialize before starting.
 */
typedef struct {
    volatile int phase;          /* SpeedPhase: current transfer, SP_DONE at end */
    volatile int cancel;         /* UI sets 1 to abort the transfer */
    volatile uint64_t dl_now;    /* bytes downloaded so far */
    volatile uint64_t dl_total;  /* download size (from Content-Length) */
    volatile uint64_t ul_now;    /* bytes uploaded so far */
    volatile uint64_t ul_total;  /* upload size (fixed payload) */
    volatile double dl_bps;      /* live download rate, bytes/sec */
    volatile double ul_bps;      /* live upload rate, bytes/sec */
    void *handle;                /* current CURL*, for getinfo in the callback */
} SpeedProg;

/*
 * Two-phase speed test: a timed HTTPS download of a fixed filler payload
 * followed by a timed upload, both discarded, updating *p live throughout.
 * Returns true only if both phases finished cleanly (2xx, not cancelled); on
 * success p->dl_bps / p->ul_bps hold the final average rates. Blocks for the
 * length of both transfers, so run it off the UI thread. Set p->cancel from
 * another thread to stop early.
 */
bool net_speedtest_live(SpeedProg *p);

#ifdef __cplusplus
}
#endif

#endif /* NET_H */
