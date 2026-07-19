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
 * Stream an HTTP GET to a file on disk. Returns true on a 2xx download.
 * Set extra_header (e.g. "authorization: LOW key:secret") or NULL.
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

#ifdef __cplusplus
}
#endif

#endif /* NET_H */
