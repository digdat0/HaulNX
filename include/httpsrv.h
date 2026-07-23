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
 * after.
 *
 * The received file is either a dl_sources.json collection or a HaulNX .nro
 * build to install; the server is a plain transport and the caller tells them
 * apart (an NRO carries "NRO0" at offset 0x10). */

#define HTTPSRV_PORT     8080
/* Big enough for an app build, small enough to refuse runaway uploads. */
#define HTTPSRV_MAX_BODY (16 * 1024 * 1024)
/* One-time code carried in the URL path (e.g. http://ip:8080/a1b2c3). It only
 * ever appears on the console's screen, so anything that knows it was shown
 * the address by the user — a web page loaded on some other LAN device can
 * neither POST a file nor fetch the config export without it. */
#define HTTPSRV_TOKEN_LEN 6

typedef struct {
    int listen_fd;   /* -1 when closed */
    bool nro_page;   /* serve the app-update page instead of the collection
                        one (set by the caller after open; transport-neutral —
                        either kind of file is still accepted on POST) */
    char token[HTTPSRV_TOKEN_LEN + 1]; /* set by open; caller shows it in the URL */
    char ip[46];     /* our own address, for the Host-header check (may be "") */
    /* One in-flight connection, read a slice per poll so the UI thread never
     * blocks and can show receive progress. All owned/reset internally. */
    int client_fd;      /* -1 when idle */
    char *head;         /* request head while it is still arriving */
    size_t head_len;
    char *cbody;        /* POST body being filled */
    size_t cbody_len;   /* bytes received so far */
    size_t cbody_total; /* Content-Length */
    char ctype[192];    /* Content-Type (for multipart slicing at the end) */
    unsigned long long last_data_ns; /* watchdog for a client that went quiet */
    char *body;      /* received file, NUL-terminated, owned; NULL until then */
    size_t body_len;
} HttpSrv;

/* The console's LAN address as a dotted quad, e.g. "192.168.1.42".
 * False if there is no network connection (nothing to advertise). */
bool httpsrv_local_ip(char *out, size_t out_sz);

/* Bind HTTPSRV_PORT on all interfaces. False if the port is unavailable. */
bool httpsrv_open(HttpSrv *s);

/* Service the connection a slice at a time and return immediately: this is
 * called once per frame from the UI thread, so it never waits for a client —
 * a large upload simply spans many polls (see httpsrv_receiving).
 *   0  nothing finished this poll — keep polling
 *   1  a file was received; s->body / s->body_len hold it
 *   2  the browser fetched the post-upload page, so it is parked somewhere a
 *      reload is harmless and the caller can stop serving
 *   3  the current config was handed to the browser (an export)
 *  -1  the server is not open
 * Once this returns 1 the caller owns s->body until httpsrv_close. */
int httpsrv_poll(HttpSrv *s);

/* True while a POST body is arriving; *now / *total (either may be NULL)
 * report the bytes so far and the Content-Length, for a progress line. */
bool httpsrv_receiving(const HttpSrv *s, size_t *now, size_t *total);

/* Close the socket and free any received body. Safe on an unopened server. */
void httpsrv_close(HttpSrv *s);

#ifdef __cplusplus
}
#endif

#endif /* HTTPSRV_H */
