#ifndef HTTPSRV_H
#define HTTPSRV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
 * apart (an NRO carries "NRO0" at offset 0x10).
 *
 * The one exception is ROM mode (HTTPSRV_MODE_ROM): a game file can be hundreds
 * of MB to several GB, far too large to hold in RAM, so it is streamed straight
 * to a file under a caller-set folder rather than buffered into s->body. */

#define HTTPSRV_PORT     8080
/* The always-on read-only inventory server (HTTPSRV_MODE_INVENTORY) binds its
 * own port so it can coexist with the transfer server on 8080 — the transfer
 * screens open/close 8080 per-session while this one stays up. */
#define HTTPSRV_INV_PORT 8081
/* Buffered-in-RAM modes (collection/nro): big enough for an app build, small
 * enough to refuse runaway uploads. ROM mode streams to disk and is bounded by
 * HTTPSRV_MAX_ROM (and, ultimately, free space on the card) instead. */
#define HTTPSRV_MAX_BODY (16 * 1024 * 1024)
/* A single game file. exFAT-formatted cards go past 4 GB, so this is a sanity
 * ceiling on the declared Content-Length, not a real expected size; the card
 * filling up is the true limit and surfaces as a write failure mid-stream. */
#define HTTPSRV_MAX_ROM  (64ULL * 1024 * 1024 * 1024)
/* One-time code carried in the URL path (e.g. http://ip:8080/k7m2xq9r). It only
 * ever appears on the console's screen, so anything that knows it was shown
 * the address by the user — a web page loaded on some other LAN device can
 * neither POST a file nor fetch the config export without it.
 *
 * It is also the only thing standing between a LAN peer and replacing the app
 * binary. It is deliberately a short 4-digit numeric code: it lives only on a
 * trusted home LAN, the receive screen is open for a moment at a time, and the
 * code is regenerated every time that screen opens, so the value is being read
 * off a TV and typed on a phone rather than defended against sustained guessing.
 * Brevity is the point. */
#define HTTPSRV_TOKEN_LEN 4

/* Which one-way task the open server is serving. It only picks the page shown
 * in a browser and whether the config export is offered; POST is still routed
 * by content, so a file meant for the wrong screen is forgiven. Defaults to
 * IMPORT because httpsrv_open zeroes the struct. */
typedef enum {
    HTTPSRV_MODE_IMPORT = 0, /* receive a collection from a PC (upload page) */
    HTTPSRV_MODE_EXPORT,     /* hand this console's collection to a PC (GET) */
    HTTPSRV_MODE_NRO,        /* receive an app .nro build (update page) */
    HTTPSRV_MODE_ROM,        /* receive a game file, streamed into dest_dir */
    HTTPSRV_MODE_DAT,        /* receive a verification DAT (buffered like import) */
    HTTPSRV_MODE_INVENTORY,  /* read-only: serve inventory.json / dl_sources.json (GET only) */
} HttpSrvMode;

typedef struct {
    int listen_fd;   /* -1 when closed */
    uint16_t port;   /* the port this instance bound; set by open */
    HttpSrvMode mode; /* set by the caller after open; see HttpSrvMode */
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
    unsigned long long last_data_ns;  /* watchdog for a client that went quiet */
    unsigned long long conn_start_ns; /* accept time, for the head deadline */
    char *body;      /* received file, NUL-terminated, owned; NULL until then */
    size_t body_len;
    /* ROM mode only. dest_dir is set by the caller before serving; an upload is
     * streamed to <dest_dir>/<recv_name>.part (part_path) rather than into body.
     * On completion httpsrv_poll returns 1 with body == NULL, part_path holding
     * the finished temp file and recv_name the sanitized name it should take —
     * the caller confirms any overwrite and moves it into place. sink is the
     * open temp file mid-transfer (closed and removed by httpsrv_close if the
     * user backs out before it finishes). */
    char dest_dir[768];
    FILE *sink;
    char part_path[1088];
    char recv_name[256];
    /* Inventory mode only: the target app name from an X-App-Target header on a
     * streamed push (the Emulators-tab one-click update). Empty for a normal game
     * push. When set, the caller installs the finished body as that app instead of
     * filing it in the inbox. Sanitized like recv_name. */
    char recv_app[256];
    /* Inventory mode only: the exact device path (sdmc:/switch/.../x.nro) from an
     * X-App-Path header. When set it takes precedence over recv_app, so a same-named
     * .nro in a different subfolder can be updated independently. Validated to stay
     * under sdmc:/switch. Empty for a normal game push or a name-only update. */
    char recv_app_path[768];
    /* Inventory mode only: X-App-Install was set alongside recv_app_path — the
     * companion's Emulators-tab "Install" for an app not yet on the device, so the
     * caller writes the body as a NEW app at recv_app_path rather than rejecting it
     * as a missing update target. False for updates and game pushes. */
    bool recv_app_new;
    /* Inventory mode only: an X-Dest-Folder header on a streamed game push (the
     * Library tab's per-game "send to Switch") names the console it came from,
     * e.g. "switch" or "3ds" — the same short target key config.h consoles use.
     * When it resolves to a real configured console the caller files the game
     * straight into that console's folder (extracting an archive on arrival,
     * same as a USB drop there) instead of the inbox. Empty, or one that
     * doesn't match any configured console, falls back to the inbox untouched
     * — the desktop's USB push already had this via WPD's own folder walk;
     * this is the Wi-Fi equivalent. Sanitized like recv_app. */
    char recv_folder[64];
    /* Inventory mode only: an X-Dat header marked this buffered POST as a
     * verification DAT (the companion's DAT Files tab › push). It forces the body
     * to buffer in RAM (a DAT is small XML) rather than stream to the inbox, so
     * the caller can parse its header and file it into DATS_DIR by console. Reset
     * per connection; see InvApplyDat. */
    bool recv_dat;
    char last_err[64]; /* why a ROM stream aborted, for the caller to log */
    /* Inventory mode only: system tick (ns) of the last inventory.json GET, i.e.
     * the last time a companion polled us. 0 = never. Lets the console show
     * "companion connected" vs "waiting" instead of just "listening". Survives
     * client_reset (per-connection), cleared only by (re)open's memset. */
    unsigned long long last_inv_ns;
    /* Inventory mode only: borrowed, newline-separated list of absolute folders
     * (each console's install folder plus the inbox) the pull/delete endpoints
     * are confined to. Set by the app when it (re)builds the inventory; a
     * requested path outside every root is refused. NULL leaves those endpoints
     * off. Not owned — points into an app-side string that outlives the server. */
    const char *roots;
    /* A file being streamed OUT to the PC (a game pull, GET /file). Its header is
     * sent up front, then the body goes a bounded slice per poll — like the ROM
     * upload in reverse — so a multi-GB game never buffers in RAM or freezes the
     * render thread. NULL when no pull is in flight; src_left is the bytes to go. */
    FILE *src;
    unsigned long long src_left;
    /* A streamed push (ROM/inventory raw body) is pumped off the render thread by
     * a recv thread and a writer thread, so the socket keeps draining during the
     * SD write instead of stalling the TCP window on it once per frame. The UI
     * thread only reads cbody_len for progress and finalizes (fclose, response,
     * move-into-place) once rx_done flips. rx_thread is a heap-allocated pump
     * context (void* to keep <switch.h> out of this header). */
    void *rx_thread;          /* NULL when no receive threads are running */
    volatile bool rx_running; /* threads started, not yet joined */
    volatile bool rx_done;    /* threads finished; UI thread joins + finalizes */
    volatile bool rx_cancel;  /* UI (or the writer) asks the pump to stop early */
    volatile int rx_status;   /* RX_* outcome, valid once rx_done is set */
} HttpSrv;

/* The console's LAN address as a dotted quad, e.g. "192.168.1.42".
 * False if there is no network connection (nothing to advertise). */
bool httpsrv_local_ip(char *out, size_t out_sz);

/* Bind HTTPSRV_PORT on all interfaces. False if the port is unavailable. */
bool httpsrv_open(HttpSrv *s);

/* As httpsrv_open, but binds an explicit port (e.g. HTTPSRV_INV_PORT for the
 * inventory server). httpsrv_open is this with HTTPSRV_PORT. */
bool httpsrv_open_port(HttpSrv *s, uint16_t port);

/* Recreate the listening socket on the same port, preserving all server state
 * (mode, token, dest_dir, roots, ...). Use after the network interface bounced
 * — e.g. the console slept and woke — so the server re-attaches to the new
 * interface instead of listening on a dead one. Any in-flight connection is
 * dropped (it died with the old interface). False if the rebind failed (port
 * momentarily unavailable, Wi-Fi not up yet); the server is left closed
 * (listen_fd == -1) so the caller can retry on a later poll. */
bool httpsrv_rebind(HttpSrv *s);

/* Service the connection a slice at a time and return immediately: this is
 * called once per frame from the UI thread, so it never waits for a client —
 * a large upload simply spans many polls (see httpsrv_receiving).
 *   0  nothing finished this poll — keep polling
 *   1  a file was received; s->body / s->body_len hold it
 *   2  the browser fetched the post-upload page, so it is parked somewhere a
 *      reload is harmless and the caller can stop serving
 *   3  the current config was handed to the browser (an export)
 *   4  a ROM stream aborted mid-transfer (card full, or the peer dropped);
 *      s->last_err holds a short reason and the server is left listening for a
 *      retry unless the caller stops it
 *  -1  the server is not open
 * Once this returns 1 the caller owns s->body until httpsrv_close. */
int httpsrv_poll(HttpSrv *s);

/* True while a POST body is arriving; *now / *total (either may be NULL)
 * report the bytes so far and the Content-Length, for a progress line. */
bool httpsrv_receiving(const HttpSrv *s, size_t *now, size_t *total);

/* Abort the connection currently in flight (a receive or a pull) but keep the
 * server listening: the in-progress ".part" is removed and the client dropped,
 * so a receive can be cancelled from the console without tearing the whole
 * server down. No-op when nothing is in flight. */
void httpsrv_abort(HttpSrv *s);

/* Close the socket and free any received body. Safe on an unopened server. */
void httpsrv_close(HttpSrv *s);

#ifdef __cplusplus
}
#endif

#endif /* HTTPSRV_H */
