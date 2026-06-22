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

/*
 * HTTP GET into a heap buffer (NUL-terminated). Caller frees the result.
 * Returns NULL on transport error. *http_code / *out_len are filled if non-NULL.
 */
char *http_get(const char *url, long *http_code, size_t *out_len);

/*
 * Stream an HTTP GET to a file on disk. Returns true on a 2xx download.
 * Set extra_header (e.g. "authorization: LOW key:secret") or NULL.
 * If resume_from > 0, the file is opened for append and a Range request is made
 * to continue from that byte offset (the progress callback's now/total include
 * the offset). A 416 reply with resume_from > 0 is treated as success (the file
 * already holds everything the server has).
 */
bool http_download(const char *url, const char *dest_path,
                   const char *extra_header,
                   net_progress_cb cb, void *userdata,
                   uint64_t resume_from,
                   long *http_code);

#ifdef __cplusplus
}
#endif

#endif /* NET_H */
