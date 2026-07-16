#ifndef HTTPSRV_H
#define HTTPSRV_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single-purpose HTTP server for receiving one file from a PC on the same
 * LAN. It serves a small upload page on GET and captures the posted file on
 * POST, then it is done. There is no routing, no keep-alive and no TLS: it
 * exists for the seconds the Import screen is open and is closed immediately
 * after. */

#define HTTPSRV_PORT     8080
#define HTTPSRV_MAX_BODY (1024 * 1024) /* refuse anything larger than 1 MB */

typedef struct {
    int listen_fd;   /* -1 when closed */
    char *body;      /* received file, NUL-terminated, owned; NULL until then */
    size_t body_len;
} HttpSrv;

/* The console's LAN address as a dotted quad, e.g. "192.168.1.42".
 * False if there is no network connection (nothing to advertise). */
bool httpsrv_local_ip(char *out, size_t out_sz);

/* Bind HTTPSRV_PORT on all interfaces. False if the port is unavailable. */
bool httpsrv_open(HttpSrv *s);

/* Service at most one pending connection and return immediately: this is
 * called once per frame from the UI thread, so it never waits for a client.
 *   0  nothing arrived, or the upload form was served — keep polling
 *   1  a file was received; s->body / s->body_len hold it
 *   2  the browser fetched the post-upload page, so it is parked somewhere a
 *      reload is harmless and the caller can stop serving
 *   3  the current config was handed to the browser (an export)
 *  -1  the server is not open
 * Once this returns 1 the caller owns s->body until httpsrv_close. */
int httpsrv_poll(HttpSrv *s);

/* Close the socket and free any received body. Safe on an unopened server. */
void httpsrv_close(HttpSrv *s);

#ifdef __cplusplus
}
#endif

#endif /* HTTPSRV_H */
