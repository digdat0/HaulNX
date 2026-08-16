#include "httpsrv.h"

#include "config.h" /* SOURCES_PATH: the file this page uploads and exports */
#include "fsutil.h" /* fs_log_rotate / fs_mkdir_p for the lifecycle trace */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <netinet/tcp.h> /* TCP_NODELAY */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <switch.h>
#include <unistd.h>

#define HDR_MAX 8192 /* a request head larger than this is not ours */
/* Scratch size for streaming a ROM upload to disk: recv this much, write it,
 * repeat. Bounds the per-frame disk work so a big transfer doesn't stall the
 * render loop, and keeps memory flat regardless of the file's size. */
#define STREAM_BUF (64 * 1024)
/* Cap on bytes drained to disk in a single poll. The LAN outruns the SD card,
 * so without a ceiling one poll would keep recv+writing for the whole transfer
 * — the socket buffer refills as fast as it drains — and freeze the render
 * thread until it finished (or the stalled connection died). Draining a bounded
 * slice and returning keeps the UI at frame rate, advances the progress bar, and
 * feeds the idle watchdog; the next poll resumes where this one left off. With
 * the larger socket buffers (see net.c) a slice now clears the wire well within a
 * frame, so a 1M ceiling roughly doubles the per-frame throughput without the
 * poll overrunning its budget. */
#define STREAM_PER_POLL (1024 * 1024)

/* A streamed push is drained on a dedicated thread (see rx_thread_fn) so the
 * socket is emptied continuously rather than once per render frame — the
 * frame-gated drain was capped near (receive window / frame period). The thread
 * runs below the main thread's priority (0x2C) so rendering never starves,
 * matching the download workers in queue.c. */
#define RX_STACK 0x20000
#define RX_PRIO  0x30
/* Ring of buffers handed between the two pump threads. The net thread fills a
 * slot with recv while the writer thread drains a different one with fwrite, so
 * the socket keeps draining during the slow SD write instead of stalling the TCP
 * window on it — the same recv/write overlap the USB/MTP path uses
 * (responder.cpp:840). 4 x 256K = 1 MB in flight, matching the tuned socket
 * buffer (net.c), and the 256K writes hit the SD card far better than 64K dribs. */
#define RX_NBUF  4
#define RX_SLOT  (256 * 1024)
/* Receive-thread outcome, reported via HttpSrv.rx_status once rx_done is set. */
enum { RX_RUNNING = 0, RX_OK, RX_PEERCLOSED, RX_WRITEERR, RX_ERR, RX_CANCELLED };

/* Requests are read incrementally, a slice per poll, so the UI thread keeps
 * rendering during a large upload and can show its progress. Responses are
 * still written on a briefly-blocking socket (they are small pages); this
 * timeout bounds a peer that stops draining them. Sends get a tighter budget
 * than reads: every response fits the socket buffer in one go, so a send that
 * stalls at all is a peer deliberately not reading — and each stalled send
 * blocks the UI thread for its full timeout. */
#define RECV_TIMEOUT_MS 2000
#define SEND_TIMEOUT_MS 1000
/* Watchdog for a client that connects and then goes quiet mid-request. The
 * server is polled once per render frame, so a "quiet" client can also just be
 * one the render thread hasn't gotten back to: a heavy UI action (e.g. the
 * Library tab re-walking a console's file sizes) can stall polling for a few
 * seconds mid-transfer. 5s was tight enough that such a hitch dropped an
 * otherwise-healthy LAN upload; 12s rides those out while still noticing a truly
 * dead peer promptly. TCP backpressure simply pauses the sender in the meantime. */
#define CLIENT_IDLE_NS  12000000000ULL /* ~12s without a byte drops the client */
/* The idle watchdog resets on every byte, so it alone doesn't bound a peer that
 * dribbles one byte every few seconds — that peer holds the single connection
 * slot indefinitely and the real upload can never get in. A request head is a
 * few hundred bytes off the LAN, so give the whole of phase 1 a hard ceiling
 * regardless of progress. The body phase keeps only the idle watchdog: a 16 MB
 * upload legitimately takes a while. */
#define HEAD_DEADLINE_NS 10000000000ULL /* ~10s to finish sending a request head */

/* The app badge, served to the page from romfs at GET /logo.png. The console is
 * already serving the page, so it may as well serve this rather than carry a
 * base64 copy of it in the binary. */
#define LOGO_PATH "romfs:/credits_logo.png"

/* Shared chrome for both pages. Colours track the app's dark theme: the accent
 * is the same green as the spinner dots. */
#define PAGE_CSS                                                               \
    "<style>"                                                                  \
    ":root{--bg:#1b1f27;--panel:#232833;--line:#333a49;--fg:#e6e9ef;"          \
    "--dim:#9aa3b2;--accent:#92d624}"                                          \
    "*{box-sizing:border-box}"                                                 \
    "body{font:16px/1.6 system-ui,-apple-system,'Segoe UI',sans-serif;"        \
    "background:var(--bg);color:var(--fg);margin:0;display:flex;"              \
    "min-height:100vh;align-items:center;justify-content:center;padding:1.5rem}" \
    ".card{width:100%;max-width:30rem;background:var(--panel);"                \
    "border:1px solid var(--line);border-radius:.9rem;padding:1.75rem}"        \
    "header{display:flex;align-items:center;gap:.8rem;"                        \
    "border-bottom:1px solid var(--line);padding-bottom:1rem;"                 \
    "margin-bottom:1.25rem}"                                                   \
    "header img{width:46px;height:46px;border-radius:.5rem;flex:none}"         \
    "header h1{margin:0;font-size:1.25rem}"                                    \
    "header h1 span{color:var(--accent)}"                                      \
    "header p{margin:0;color:var(--dim);font-size:.85rem}"                     \
    "</style>"

/* Shared styling + drop-zone script for both upload pages. Self-contained by
 * necessity: the pages are served off the console with no internet in the
 * path, so they cannot reference anything external. */
#define UPLOAD_CSS                                                             \
    "<style>"                                                                  \
    "ol{margin:0 0 1.25rem;padding-left:1.25rem;color:var(--dim);"             \
    "font-size:.9rem}"                                                         \
    "ol b{color:var(--fg);font-weight:600}"                                    \
    "#drop{display:block;border:2px dashed var(--line);border-radius:.6rem;"   \
    "padding:1.6rem 1rem;text-align:center;color:var(--dim);cursor:pointer;"   \
    "transition:border-color .15s,color .15s}"                                 \
    "#drop:hover,#drop.over{border-color:var(--accent);color:var(--fg)}"       \
    "#drop b{display:block;color:var(--fg);margin-bottom:.15rem;"              \
    "word-break:break-all}"                                                    \
    "#drop input{display:none}"                                                \
    "button{width:100%;margin-top:1.25rem;background:var(--accent);"           \
    "color:#12161c;border:0;border-radius:.5rem;padding:.75rem;font-size:1rem;"\
    "font-weight:600;cursor:pointer}"                                          \
    "button:disabled{background:var(--line);color:var(--dim);cursor:default}"  \
    ".alt{margin-top:1.25rem;padding-top:1.25rem;"                             \
    "border-top:1px solid var(--line);text-align:center}"                      \
    ".alt p{margin:0 0 .75rem;color:var(--dim);font-size:.85rem}"              \
    ".alt a{display:inline-block;color:var(--fg);border:1px solid var(--line);"\
    "border-radius:.5rem;padding:.6rem 1rem;text-decoration:none;"             \
    "font-size:.9rem;transition:border-color .15s,color .15s}"                 \
    ".alt a:hover{border-color:var(--accent);color:var(--accent)}"             \
    "</style>"

#define UPLOAD_SCRIPT                                                          \
    "<script>"                                                                 \
    "var d=document.getElementById('drop'),i=d.querySelector('input'),"        \
    "b=document.getElementById('go'),n=d.querySelector('b');"                  \
    "function s(){if(i.files.length){n.textContent=i.files[0].name;"           \
    "b.disabled=false;}}"                                                      \
    "i.addEventListener('change',s);"                                          \
    "['dragenter','dragover'].forEach(function(e){d.addEventListener(e,"       \
    "function(v){v.preventDefault();d.classList.add('over');});});"            \
    "['dragleave','drop'].forEach(function(e){d.addEventListener(e,"           \
    "function(v){v.preventDefault();d.classList.remove('over');});});"         \
    "d.addEventListener('drop',function(v){i.files=v.dataTransfer.files;s();});"\
    "</script>"

/* The collection upload page, shown while Import collection is open. */
static const char PAGE[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX - Import collection</title>" PAGE_CSS UPLOAD_CSS
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Import collection</p></div></header>"
    "<ol>"
    "<li>Find the <b>dl_sources.json</b> you saved from the app utility.</li>"
    "<li>Drop it below, or click to browse for it.</li>"
    "<li>Send it, then confirm the import on your Switch.</li>"
    "</ol>"
    "<form method=post enctype=multipart/form-data>"
    "<label id=drop><b>Drop dl_sources.json here</b>or click to choose a file"
    "<input type=file name=f accept=\".json,application/json\" required>"
    "</label>"
    "<button id=go disabled>Send to Switch</button>"
    "</form>" UPLOAD_SCRIPT "</div>";

/* The export page, shown while Export collection is open: no upload, just a
 * link to the collection this console is running on. The export lives under
 * this page's one-time path; the page is static, so point the link there at
 * load time rather than baking the code in. */
static const char PAGE_EXPORT[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX - Export collection</title>" PAGE_CSS
    "<style>a.dl{display:block;text-align:center;background:var(--accent);"
    "color:#12161c;border-radius:.5rem;padding:.75rem;font-size:1rem;"
    "font-weight:600;text-decoration:none;margin:.5rem 0}</style>"
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Export collection</p></div></header>"
    "<p>This console is sharing the collection it is running on.</p>"
    "<a class=dl href=\"dl_sources.json\" download>Download dl_sources.json</a>"
    "<div class=alt>"
    "<p>Or pull it straight into the app utility: "
    "<b>Send to Switch &rsaquo; Export collection</b>.</p>"
    "</div>"
    "<script>var xa=document.querySelector('a.dl');"
    "if(xa)xa.setAttribute('href',"
    "location.pathname.replace(/\\/+$/,'')+'/dl_sources.json');</script>"
    "</div>";

/* The app-update page, shown while Settings' update-over-Wi-Fi screen is
 * open. Same receiver either way — this only changes the instructions. */
static const char PAGE_NRO[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX - Update app</title>" PAGE_CSS UPLOAD_CSS
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Update app</p></div></header>"
    "<ol>"
    "<li>Find the <b>HaulNX .nro</b> build to install &mdash; the same "
    "version as installed is fine. A zip/RAR/7z containing it works too, "
    "it's unpacked on the Switch before installing.</li>"
    "<li>Drop it below, or click to browse for it. (The app utility can "
    "also push it: <b>Send to Switch &rsaquo; App update</b>.)</li>"
    "<li>Send it, then confirm the install on your Switch.</li>"
    "</ol>"
    "<form method=post enctype=multipart/form-data>"
    "<label id=drop><b>Drop HaulNX.nro here</b>or click to choose a file"
    "<input type=file name=f accept=\".nro,.zip,.rar,.7z\" required>"
    "</label>"
    "<button id=go disabled>Send to Switch</button>"
    "</form>" UPLOAD_SCRIPT "</div>";

/* The DAT upload page, shown while a per-console "Receive DAT" screen is open.
 * A DAT is a small XML catalog, so it rides the same buffered multipart path as
 * the collection import; the console saves it for the console this screen was
 * opened from, so there is nothing to choose here but the file. */
static const char PAGE_DAT[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX - Send a DAT</title>" PAGE_CSS UPLOAD_CSS
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Send a DAT</p></div></header>"
    "<ol>"
    "<li>Find the <b>No-Intro/Redump .dat</b> for any console.</li>"
    "<li>Drop it below, or click to browse for it.</li>"
    "<li>Send it &mdash; the Switch reads which console it's for from the "
    "file, then asks you to confirm.</li>"
    "</ol>"
    "<form method=post enctype=multipart/form-data>"
    "<label id=drop><b>Drop a .dat file here</b>or click to choose a file"
    "<input type=file name=f accept=\".dat,.xml\" required>"
    "</label>"
    "<button id=go disabled>Send to Switch</button>"
    "</form>" UPLOAD_SCRIPT "</div>";

/* The ROM upload page, shown while a per-console "Receive from PC" screen is
 * open. Unlike the collection/nro pages it posts the file as a raw body (not a
 * multipart form) so the console can stream it straight to the card — a game
 * can be gigabytes, far too large to buffer. The filename rides in X-Filename,
 * percent-encoded; the upload is driven by XHR so it can show a progress bar
 * and, because the response is a plain 200 rather than a redirect, stay on this
 * page afterwards. The console decides which folder it lands in (the screen was
 * opened for one console), so there is nothing to choose here but the file. */
/* Multiple files are sent one after another over their own POSTs: the console's
 * receiver stays open across files, so the browser just uploads the selected
 * list sequentially, advancing to the next once each 200 lands. */
#define ROM_SCRIPT                                                             \
    "<script>"                                                                 \
    "var d=document.getElementById('drop'),i=d.querySelector('input'),"        \
    "b=document.getElementById('go'),n=d.querySelector('b'),"                   \
    "st=document.getElementById('st'),bar=document.getElementById('bar'),fs=[];"\
    "function s(){fs=i.files;if(fs.length){n.textContent=fs.length==1?"         \
    "fs[0].name:fs.length+' files';b.disabled=false;}}"                        \
    "i.addEventListener('change',s);"                                          \
    "['dragenter','dragover'].forEach(function(e){d.addEventListener(e,"        \
    "function(v){v.preventDefault();d.classList.add('over');});});"            \
    "['dragleave','drop'].forEach(function(e){d.addEventListener(e,"           \
    "function(v){v.preventDefault();d.classList.remove('over');});});"         \
    "d.addEventListener('drop',function(v){i.files=v.dataTransfer.files;s();});"\
    "b.addEventListener('click',function(){if(!fs.length)return;"               \
    "b.disabled=true;i.disabled=true;var k=0;"                                 \
    "function nx(){if(k>=fs.length){bar.style.width='100%';"                   \
    "st.textContent='All sent ('+fs.length+'). Confirm on your Switch.';"       \
    "return;}var f=fs[k],x=new XMLHttpRequest();"                              \
    "x.open('POST',location.pathname.replace(/\\/+$/,''));"                    \
    "x.setRequestHeader('X-Filename',encodeURIComponent(f.name));"             \
    "x.upload.onprogress=function(e){if(e.lengthComputable){var p="            \
    "Math.round(e.loaded*100/e.total);bar.style.width=p+'%';"                  \
    "st.textContent='Sending '+(k+1)+'/'+fs.length+' \\u2014 '+f.name+' '+p+"   \
    "'%';}};"                                                                  \
    "x.onload=function(){if(x.status>=200&&x.status<300){k++;nx();}else{"       \
    "st.textContent='Refused (HTTP '+x.status+') on '+f.name+'. Re-open the "   \
    "receive screen and use the address it shows.';b.disabled=false;"          \
    "i.disabled=false;}};"                                                     \
    "x.onerror=function(){st.textContent='Network error \\u2014 is the "        \
    "receive screen still open?';b.disabled=false;i.disabled=false;};"         \
    "x.send(f);}nx();});"                                                      \
    "</script>"

static const char PAGE_ROM[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX - Send a game</title>" PAGE_CSS UPLOAD_CSS
    "<style>#st{margin-top:1rem;color:var(--dim);font-size:.9rem;"
    "min-height:1.2em;text-align:center}"
    ".pbar{margin-top:.75rem;height:.4rem;background:var(--line);"
    "border-radius:.3rem;overflow:hidden}"
    "#bar{height:100%;width:0;background:var(--accent);transition:width .15s}"
    "</style>"
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Send a game</p></div></header>"
    "<ol>"
    "<li>Drop the game files below, or click to browse for them.</li>"
    "<li>Send them &mdash; they copy into the folder for the console you opened "
    "this screen from.</li>"
    "<li>Confirm on your Switch if one would replace a file already there.</li>"
    "</ol>"
    "<label id=drop><b>Drop game files here</b>or click to choose files"
    "<input type=file name=f multiple required>"
    "</label>"
    "<button id=go disabled>Send to Switch</button>"
    "<div class=pbar><div id=bar></div></div>"
    "<div id=st></div>" ROM_SCRIPT "</div>";

/* Served at /sent, which a successful upload is redirected to. Reaching this
 * by GET is the point: it leaves the browser on a page it can safely reload,
 * instead of on a POST result that a reload would silently re-submit. */
static const char PAGE_OK[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX - Sent</title>" PAGE_CSS
    "<style>"
    "p.m{margin:0 0 .75rem}"
    "p.n{color:var(--dim);font-size:.875rem;margin:0 0 1.25rem}"
    "p.n b{color:var(--fg);font-weight:600}"
    "a{display:block;text-align:center;background:var(--accent);color:#12161c;"
    "border-radius:.5rem;padding:.75rem;font-size:1rem;font-weight:600;"
    "text-decoration:none}"
    "</style>"
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>File sent</p></div></header>"
    "<p class=m>Confirm on your Switch to apply it.</p>"
    "<p class=n>The console stops listening once a file arrives. To send "
    "another, re-open the receive screen on your Switch, then reload this "
    "page.</p>"
    "<a href=\"/\">Reload</a></div>";

/* Served for any path that lacks the one-time code: tells a person what to do
 * without confirming anything about the code to a probing script. */
static const char PAGE_HINT[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>HaulNX</title>" PAGE_CSS
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Nothing here</p></div></header>"
    "<p>Open the <b>exact address shown on your Switch</b> &mdash; it ends "
    "with a one-time code that changes every time the receive screen opens.</p>"
    "</div>";

/* Write every byte or fail. A short write here is not cosmetic: the response
 * has already promised a Content-Length, so giving up early hands the browser a
 * truncated body (ERR_CONTENT_LENGTH_MISMATCH) rather than a clean error. */
static bool send_all(int fd, const char *p, size_t n) {
    int stalls = 0;
    while (n > 0) {
        ssize_t w = send(fd, p, n, 0);
        if (w > 0) {
            p += w;
            n -= (size_t)w;
            stalls = 0;
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        /* Send window full for a whole SO_SNDTIMEO: allow one more drain, then
         * abandon the response — each stall here has the UI thread hostage. */
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && ++stalls < 2) {
            continue;
        }
        return false;
    }
    return true;
}

/* snprintf reports the length it WOULD have written, so its return can exceed
 * the buffer. Handing that straight to send_all would stream whatever follows
 * the buffer on the stack to the client — so every response head is checked
 * before it goes out. No caller can truncate today (each field is a literal),
 * which is exactly why this needs to be enforced rather than assumed. */
static bool head_ok(int n, size_t cap) {
    return n > 0 && (size_t)n < cap;
}

/* Send a complete response. `body` may be NULL for a bodiless status.
 * Access-Control-Allow-Origin is set so the app utility, opened from disk (and
 * therefore a "null" origin), can POST here directly later on. */
static void send_resp(int fd, const char *status, const char *ctype,
                      const char *body) {
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Connection: close\r\n\r\n",
                     status, ctype, body ? strlen(body) : 0);
    if (!head_ok(n, sizeof(head))) {
        return; /* nothing sendable; the connection is closed either way */
    }
    send_all(fd, head, (size_t)n);
    if (body) {
        send_all(fd, body, strlen(body));
    }
}

/* Answer a POST with "see other": the browser drops the request body and
 * re-fetches the target with a GET, so a later reload cannot re-upload. */
static void send_redirect(int fd, const char *loc) {
    char head[192];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 303 See Other\r\n"
                     "Location: %s\r\n"
                     "Content-Length: 0\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Connection: close\r\n\r\n",
                     loc);
    if (!head_ok(n, sizeof(head))) {
        return;
    }
    send_all(fd, head, (size_t)n);
}

/* Answer a CORS preflight. The collection/nro pushes are CORS-simple and never
 * preflight, but the ROM push sets X-Filename, which does — and it comes from
 * the app utility opened off disk (a "null" origin). Allowing any header keeps
 * that request unblocked; the one-time code in the path is still what gates it,
 * not the origin. */
static void send_preflight(int fd) {
    static const char resp[] =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Max-Age: 600\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send_all(fd, resp, sizeof(resp) - 1);
}

/* Turn a client-supplied name into a safe basename inside dest_dir. It arrives
 * percent-encoded in X-Filename (so spaces and unicode survive a header field),
 * so decode first, then drop any directory part and neutralise the characters a
 * path walk or FAT would choke on. False if nothing usable is left. */
static bool sanitize_filename(const char *in, char *out, size_t out_sz) {
    char dec[512];
    size_t o = 0;
    for (const char *p = in; *p && p[0] != '\r' && p[0] != '\n' &&
                             o + 1 < sizeof(dec);
         p++) {
        if (p[0] == '%' && isxdigit((unsigned char)p[1]) &&
            isxdigit((unsigned char)p[2])) {
            char h[3] = {p[1], p[2], '\0'};
            dec[o++] = (char)strtol(h, NULL, 16);
            p += 2;
        } else {
            dec[o++] = p[0];
        }
    }
    dec[o] = '\0';
    /* basename: everything after the last slash of either kind */
    const char *base = dec;
    for (const char *q = dec; *q; q++) {
        if (*q == '/' || *q == '\\') {
            base = q + 1;
        }
    }
    if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return false;
    }
    size_t j = 0;
    for (const char *q = base; *q && j + 1 < out_sz; q++) {
        unsigned char c = (unsigned char)*q;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c < 0x20) {
            c = '_';
        }
        out[j++] = (char)c;
    }
    out[j] = '\0';
    return j > 0;
}

/* Like sanitize_filename but for the X-App-Path update target: a full device
 * path to an installed .nro. Percent-decode (keeping the slashes), then require
 * the sdmc:/switch/ prefix and reject any ".." so the write can never escape the
 * apps tree. False if the result isn't a safe .nro path there. */
static bool sanitize_app_path(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    for (const char *p = in; *p && p[0] != '\r' && p[0] != '\n' &&
                             o + 1 < out_sz;
         p++) {
        if (p[0] == '%' && isxdigit((unsigned char)p[1]) &&
            isxdigit((unsigned char)p[2])) {
            char h[3] = {p[1], p[2], '\0'};
            out[o++] = (char)strtol(h, NULL, 16);
            p += 2;
        } else {
            out[o++] = p[0];
        }
    }
    out[o] = '\0';
    if (strncmp(out, "sdmc:/switch/", 13) != 0 || strstr(out, "..")) {
        return false;
    }
    return o > 4 && strcasecmp(out + o - 4, ".nro") == 0;
}

/* Send a file from romfs/SD as a complete response. With `dl_name` set the
 * browser saves it under that name instead of rendering it, and the response is
 * marked uncacheable — an exported config must never come from a stale copy.
 * False if the file can't be read, leaving the caller to send an error. */
static bool send_file(int fd, const char *path, const char *ctype,
                      const char *dl_name) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return false;
    }
    char *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return false;
    }
    char head[320];
    int hn = snprintf(
        head, sizeof(head),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "%s%s%s"
        "Cache-Control: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        ctype, got, dl_name ? "Content-Disposition: attachment; filename=\"" : "",
        dl_name ? dl_name : "", dl_name ? "\"\r\n" : "",
        dl_name ? "no-store" : "max-age=300");
    if (!head_ok(hn, sizeof(head))) {
        free(buf);
        return false; /* caller sends an error instead of a truncated head */
    }
    send_all(fd, head, (size_t)hn);
    send_all(fd, buf, got);
    free(buf);
    return true;
}

/* Value of a header, case-insensitively, or NULL. `name` includes the colon. */
static const char *hdr_val(const char *head, const char *name) {
    size_t nl = strlen(name);
    for (const char *p = head; *p; p++) {
        if (p != head && p[-1] != '\n') {
            continue;
        }
        if (strncasecmp(p, name, nl) != 0) {
            continue;
        }
        p += nl;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        return p;
    }
    return NULL;
}

/* memmem in all but name: find `pat` inside `hay` by explicit length. The
 * body may be a binary .nro full of NUL bytes, so strstr cannot walk it. */
static char *mem_find(char *hay, size_t hlen, const char *pat, size_t plen) {
    if (plen == 0 || hlen < plen) {
        return NULL;
    }
    for (size_t i = 0; i + plen <= hlen; i++) {
        if (hay[i] == pat[0] && memcmp(hay + i, pat, plen) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

/* Cut the first file part out of a multipart/form-data body, in place.
 * Handles the single-file form we serve, not multipart in general. */
static bool multipart_slice(char *body, size_t *len, const char *ctype) {
    const char *bp = strstr(ctype, "boundary=");
    if (!bp) {
        return false;
    }
    bp += 9;
    char bnd[144];
    size_t i = 0;
    if (*bp == '"') {
        for (bp++; *bp && *bp != '"' && i + 1 < sizeof(bnd); bp++) {
            bnd[i++] = *bp;
        }
    } else {
        for (; *bp && *bp != ';' && *bp != '\r' && *bp != '\n' && *bp != ' ' &&
               i + 1 < sizeof(bnd);
             bp++) {
            bnd[i++] = *bp;
        }
    }
    bnd[i] = '\0';
    if (i == 0) {
        return false;
    }

    char pat[160];
    int pn = snprintf(pat, sizeof(pat), "--%s", bnd);
    char *start = mem_find(body, *len, pat, (size_t)pn);
    if (!start) {
        return false;
    }
    /* end of this part's own headers */
    char *data = mem_find(start, (size_t)(body + *len - start), "\r\n\r\n", 4);
    if (!data) {
        return false;
    }
    data += 4;
    pn = snprintf(pat, sizeof(pat), "\r\n--%s", bnd);
    char *end = mem_find(data, (size_t)(body + *len - data), pat, (size_t)pn);
    if (!end) {
        return false;
    }
    *len = (size_t)(end - data);
    memmove(body, data, *len);
    body[*len] = '\0'; /* the buffer holds clen+1 bytes, so this fits */
    return true;
}

/* Drop the in-flight connection and everything read so far. Never touches
 * s->body — a completed upload stays owned by the caller. */
/* Lifecycle trace into debug.log (which diag_bundle_write captures), so a
 * connection that never gets accepted -- or one reset before it becomes a
 * Queue-tab item -- still leaves a record. Two prior Wi-Fi-push fixes were
 * theories that a hardware log later disproved (see wifi-push-extract-blocking
 * memory); this exists so the next multi-file test *names* the culprit instead
 * of another guess. Each line carries the listener port (to tell the always-on
 * inventory server apart from the ROM/import ones) and a monotonic millisecond
 * clock, so the gap between one push finishing and the next connection arriving
 * is visible. Racy across threads by design -- the pump threads never call it,
 * and a torn diagnostic line is a fine price. */
static void inv_trace(const HttpSrv *s, const char *fmt, ...) {
    static unsigned tick = 0;
    if ((tick++ & 63u) == 0) {
        fs_log_rotate(LOG_PATH, LOG_ROTATE_DEBUG);
    }
    fs_mkdir_p(LOGS_DIR);
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) {
        return;
    }
    unsigned long long ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;
    fprintf(f, "httpsrv    :%u t=%llums ", s ? s->port : 0, ms);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void client_reset(HttpSrv *s) {
    if (s->client_fd >= 0) {
        inv_trace(s, "reset fd=%d err=%s", s->client_fd,
                  s->last_err[0] ? s->last_err : "(clean)");
        close(s->client_fd);
        s->client_fd = -1;
    }
    /* A still-open sink means a ROM stream was interrupted (peer dropped, or the
     * user backed out of the receive screen). Close and delete the partial file
     * so an aborted transfer never leaves a truncated ".part" behind. A completed
     * transfer has already nulled sink and kept its file for the caller to move. */
    if (s->sink) {
        fclose(s->sink);
        s->sink = NULL;
        if (s->part_path[0]) {
            remove(s->part_path);
        }
    }
    /* A pull interrupted before its last slice (peer dropped, or the server was
     * closed mid-stream): close the source we were reading from. */
    if (s->src) {
        fclose(s->src);
        s->src = NULL;
        s->src_left = 0;
    }
    free(s->head);
    s->head = NULL;
    s->head_len = 0;
    free(s->cbody);
    s->cbody = NULL;
    s->cbody_len = 0;
    s->cbody_total = 0;
    s->ctype[0] = '\0';
    s->last_data_ns = 0;
}

/* Reads are non-blocking (resumed a poll at a time); switch to a briefly
 * blocking socket before writing a response, so send_all doesn't spin on
 * EAGAIN and the timeouts below actually apply. */
static void make_blocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }
    struct timeval rtv = {RECV_TIMEOUT_MS / 1000,
                          (RECV_TIMEOUT_MS % 1000) * 1000};
    struct timeval stv = {SEND_TIMEOUT_MS / 1000,
                          (SEND_TIMEOUT_MS % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
}

/* The request line's path: after the method, up to the next space/EOL. */
static size_t req_path(const char *head, const char **out) {
    const char *sp = strchr(head, ' ');
    if (!sp) {
        *out = head;
        return 0;
    }
    sp++;
    size_t n = 0;
    while (sp[n] && sp[n] != ' ' && sp[n] != '\r' && sp[n] != '\n') {
        n++;
    }
    *out = sp;
    return n;
}

/* True if the path is exactly "/<token>" (an optional trailing slash is fine —
 * that's a human retyping the address, not a different resource). */
static bool path_is_token(const HttpSrv *s, const char *p, size_t pl) {
    size_t tl = strlen(s->token);
    if (pl == tl + 2 && p[pl - 1] == '/') {
        pl--;
    }
    return pl == tl + 1 && p[0] == '/' && strncmp(p + 1, s->token, tl) == 0;
}

/* Percent-decode a query value (%xx only; '+' is left literal, as our own
 * requests encode space as %20 and never use '+' for it). Writes at most
 * out_sz-1 bytes and NUL-terminates. */
static void pct_decode(const char *in, size_t inlen, char *out, size_t out_sz) {
    size_t o = 0;
    for (size_t i = 0; i < inlen && o + 1 < out_sz; i++) {
        if (in[i] == '%' && i + 2 < inlen && isxdigit((unsigned char)in[i + 1]) &&
            isxdigit((unsigned char)in[i + 2])) {
            char h[3] = {in[i + 1], in[i + 2], '\0'};
            out[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

/* True if the path contains a ".." segment (bounded by '/' or the ends). The
 * roots below are plain prefixes, so without this a path like
 * "<root>/../../switch/x" would pass the prefix test yet escape the root. */
static bool has_dotdot(const char *p) {
    for (const char *q = strstr(p, ".."); q; q = strstr(q + 2, "..")) {
        char before = (q == p) ? '/' : q[-1];
        char after = q[2];
        if (before == '/' && (after == '/' || after == '\0')) {
            return true;
        }
    }
    return false;
}

/* True if `path` is `root` itself or a file/dir under it. `root`'s trailing
 * slashes are ignored so a configured folder with or without one both match. */
static bool path_under_root(const char *root, size_t rl, const char *path) {
    while (rl > 0 && root[rl - 1] == '/') {
        rl--;
    }
    if (rl == 0 || strncmp(path, root, rl) != 0) {
        return false;
    }
    char after = path[rl];
    return after == '\0' || after == '/';
}

/* Gate for the file/delete endpoints: the decoded path must be an sdmc: path,
 * free of ".." segments, and inside one of the newline-separated roots the app
 * handed us (the console folders + inbox). Anything else is refused, so a LAN
 * peer with the code can still only reach the folders HaulNX manages. */
static bool path_allowed(const HttpSrv *s, const char *path) {
    if (!s->roots || !path[0] || strncmp(path, "sdmc:/", 6) != 0 ||
        has_dotdot(path)) {
        return false;
    }
    for (const char *r = s->roots; *r;) {
        const char *nl = strchr(r, '\n');
        size_t len = nl ? (size_t)(nl - r) : strlen(r);
        if (len > 0 && path_under_root(r, len, path)) {
            return true;
        }
        if (!nl) {
            break;
        }
        r = nl + 1;
    }
    return false;
}

/* Match "/<token>/<leaf>?p=<value>" and return the still-encoded <value> (with
 * its length in *vlen), or NULL. The token is part of the match, so a request
 * that reaches a real value has already cleared the code gate. */
static const char *route_pval(const HttpSrv *s, const char *p, size_t pl,
                              const char *leaf, size_t *vlen) {
    size_t tl = strlen(s->token);
    size_t pre = 1 + tl + 1; /* "/<token>/" */
    if (pl < pre || p[0] != '/' || strncmp(p + 1, s->token, tl) != 0 ||
        p[1 + tl] != '/') {
        return NULL;
    }
    const char *q = p + pre;
    size_t rem = pl - pre;
    size_t ll = strlen(leaf);
    if (rem < ll + 3 || strncmp(q, leaf, ll) != 0 ||
        strncmp(q + ll, "?p=", 3) != 0) {
        return NULL;
    }
    *vlen = rem - ll - 3;
    return q + ll + 3;
}

/* Reject a request whose Host header names anyone but this console. A browser
 * lured to a DNS-rebinding page reaches this IP with the attacker's hostname
 * still in Host: — refusing it cuts that class off wholesale. A missing Host
 * (plain HTTP/1.0 tools) is allowed: rebinding always goes through a real
 * browser, and real browsers always send it. */
static bool host_ok(const HttpSrv *s, const char *head) {
    if (!s->ip[0]) {
        return true; /* own address unknown: nothing to compare against */
    }
    const char *h = hdr_val(head, "host:");
    if (!h) {
        return true;
    }
    size_t n = 0;
    while (h[n] && h[n] != '\r' && h[n] != '\n') {
        n++;
    }
    while (n > 0 && (h[n - 1] == ' ' || h[n - 1] == '\t')) {
        n--;
    }
    char want[64];
    int wl = snprintf(want, sizeof(want), "%s:%d", s->ip, s->port);
    if ((size_t)wl == n && strncasecmp(h, want, n) == 0) {
        return true;
    }
    size_t il = strlen(s->ip);
    return il == n && strncasecmp(h, s->ip, n) == 0;
}

/* Nothing finished this poll: keep the connection if it made progress (or is
 * merely young), drop it once it has been silent for the watchdog window. */
static int client_idle(HttpSrv *s, bool got_data) {
    unsigned long long now = armTicksToNs(armGetSystemTick());
    if (got_data || s->last_data_ns == 0) {
        s->last_data_ns = now;
    } else if (now - s->last_data_ns > CLIENT_IDLE_NS) {
        /* A ROM stream that goes quiet mid-transfer is a failure the caller
         * should report, not a silent drop like an abandoned GET. */
        bool rom = s->sink != NULL;
        if (rom) {
            snprintf(s->last_err, sizeof(s->last_err), "no data for %llus",
                     CLIENT_IDLE_NS / 1000000000ULL);
        }
        client_reset(s);
        return rom ? 4 : 0;
    }
    return 0;
}

/* Head complete, method GET/OPTIONS: answer at once and be done. */
static int respond_simple(HttpSrv *s, int fd, const char *head) {
    int ret = 0;
    make_blocking(fd);
    const char *p;
    size_t pl = req_path(head, &p);
    size_t tl = strlen(s->token) + 1; /* "/<token>" */
    const char *fval = NULL; /* set by the file-pull route below */
    size_t fvlen = 0;
    if (strncmp(head, "OPTIONS ", 8) == 0) {
        send_preflight(fd);
    } else if (!host_ok(s, head)) {
        send_resp(fd, "403 Forbidden", "text/plain", "wrong host");
    } else if (pl == 9 && strncmp(p, "/logo.png", 9) == 0) {
        /* Tokenless on purpose: every page (the hint page included) shows it,
         * and it is the app's public badge — there is nothing to protect. */
        if (!send_file(fd, LOGO_PATH, "image/png", NULL)) {
            send_resp(fd, "404 Not Found", "text/plain", "no logo");
        }
    } else if (pl == 5 && strncmp(p, "/sent", 5) == 0) {
        send_resp(fd, "200 OK", "text/html; charset=utf-8", PAGE_OK);
        ret = 2; /* the upload landed safely; nothing is pending */
    } else if (path_is_token(s, p, pl)) {
        /* The one-time address from the console's screen. Which page it lands
         * on depends on the task the screen opened the server for. */
        const char *page = PAGE;
        if (s->mode == HTTPSRV_MODE_NRO) {
            page = PAGE_NRO;
        } else if (s->mode == HTTPSRV_MODE_EXPORT) {
            page = PAGE_EXPORT;
        } else if (s->mode == HTTPSRV_MODE_ROM) {
            page = PAGE_ROM;
        } else if (s->mode == HTTPSRV_MODE_DAT) {
            page = PAGE_DAT;
        } else if (s->mode == HTTPSRV_MODE_INVENTORY) {
            page = PAGE_HINT; /* read-only API: nothing to show a browser here */
        }
        send_resp(fd, "200 OK", "text/html; charset=utf-8", page);
    } else if ((s->mode == HTTPSRV_MODE_EXPORT ||
                s->mode == HTTPSRV_MODE_INVENTORY) &&
               pl == tl + 16 && p[0] == '/' &&
               strncmp(p + 1, s->token, tl - 1) == 0 &&
               strncmp(p + tl, "/dl_sources.json", 16) == 0) {
        /* Export (and the read-only inventory server), at
         * "/<token>/dl_sources.json": hand back the collection the console is
         * running on, so it can be edited and sent straight back. Token-gated
         * (it lists the user's repos). */
        bool sent = send_file(fd, SOURCES_PATH, "application/json",
                              "dl_sources.json");
        if (!sent) {
            send_resp(fd, "404 Not Found", "text/plain", "no config");
        }
        ret = sent ? 3 : 0;
    } else if (s->mode == HTTPSRV_MODE_INVENTORY && pl == tl + 15 &&
               p[0] == '/' && strncmp(p + 1, s->token, tl - 1) == 0 &&
               strncmp(p + tl, "/inventory.json", 15) == 0) {
        /* The device inventory the desktop companion reads. Regenerated
         * app-side; the server only serves the file (see WriteInventoryJson).
         * Stamp the poll so the console can show a live "connected" state. */
        s->last_inv_ns = armTicksToNs(armGetSystemTick());
        if (!send_file(fd, INVENTORY_PATH, "application/json", NULL)) {
            send_resp(fd, "404 Not Found", "text/plain", "no inventory");
        }
    } else if (s->mode == HTTPSRV_MODE_INVENTORY && pl == tl + 17 &&
               p[0] == '/' && strncmp(p + 1, s->token, tl - 1) == 0 &&
               strncmp(p + tl, "/credentials.json", 17) == 0) {
        /* The device's saved archive.org S3 keys, so the desktop companion can
         * adopt them on connect rather than re-typing them. Token-gated like the
         * rest of this server; 404 until the user has saved credentials. */
        if (!send_file(fd, CREDS_PATH, "application/json", NULL)) {
            send_resp(fd, "404 Not Found", "text/plain", "no credentials");
        }
    } else if (s->mode == HTTPSRV_MODE_INVENTORY && pl == tl + 17 &&
               p[0] == '/' && strncmp(p + 1, s->token, tl - 1) == 0 &&
               strncmp(p + tl, "/debug_bundle.txt", 17) == 0) {
        /* The desktop companion's "pull device logs" button. Regenerated fresh
         * on every request (a handful of small text logs, cheap) so a sync
         * always gets current logs instead of a stale manual export -- see
         * diag_bundle_write (config.c) and MainApplication::ExportBundle. */
        diag_bundle_write();
        if (!send_file(fd, DIAG_BUNDLE_PATH, "text/plain", NULL)) {
            send_resp(fd, "404 Not Found", "text/plain", "no logs yet");
        }
    } else if (s->mode == HTTPSRV_MODE_INVENTORY && pl == tl + 20 &&
               p[0] == '/' && strncmp(p + 1, s->token, tl - 1) == 0 &&
               strncmp(p + tl, "/update_sources.json", 20) == 0) {
        /* The shared emulator/app update manifest (id/kind/detect + each entry's
         * GitHub repo). Served so the desktop companion can show and manage the
         * same list the on-device update manager uses. Token-gated like the rest
         * of this server; 404 until the manager has seeded it. */
        if (!send_file(fd, UPDSRC_PATH, "application/json", NULL)) {
            send_resp(fd, "404 Not Found", "text/plain", "no update sources");
        }
    } else if (s->mode == HTTPSRV_MODE_INVENTORY &&
               (fval = route_pval(s, p, pl, "file", &fvlen)) != NULL) {
        /* Pull one game to the PC: "/<token>/file?p=<abs path>", confined by
         * path_allowed to the console folders + inbox. Handed back as an
         * attachment under its basename (header-sanitised) so the browser saves
         * it rather than trying to render a multi-GB file. */
        char path[1024];
        pct_decode(fval, fvlen, path, sizeof(path));
        const char *base = path;
        for (const char *q = path; *q; q++) {
            if (*q == '/') {
                base = q + 1;
            }
        }
        char dl[256];
        size_t j = 0;
        for (const char *q = base; *q && j + 1 < sizeof(dl); q++) {
            unsigned char c = (unsigned char)*q;
            dl[j++] = (c < 0x20 || c == '"') ? '_' : (char)c;
        }
        dl[j] = '\0';
        FILE *src = path_allowed(s, path) ? fopen(path, "rb") : NULL;
        long sz = -1;
        if (src) {
            fseek(src, 0, SEEK_END);
            sz = ftell(src); /* long is 64-bit here, so multi-GB games fit */
            fseek(src, 0, SEEK_SET);
        }
        if (!src || sz < 0) {
            if (src) {
                fclose(src);
            }
            send_resp(fd, "404 Not Found", "text/plain", "no file");
        } else {
            /* Send the header now, then hand the body to stream_out over the
             * following polls — the file never buffers in RAM. */
            char head[384];
            int hn = snprintf(head, sizeof(head),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/octet-stream\r\n"
                              "Content-Length: %ld\r\n"
                              "Content-Disposition: attachment; filename=\"%s\"\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Connection: close\r\n\r\n",
                              sz, dl);
            if (!head_ok(hn, sizeof(head)) || !send_all(fd, head, (size_t)hn)) {
                fclose(src);
                client_reset(s);
                return 0;
            }
            s->src = src;
            s->src_left = (unsigned long long)sz;
            return 0; /* body streams next poll; keep the connection (no reset) */
        }
    } else {
        /* No (or a wrong) one-time code: explain, without echoing anything a
         * probing script could learn from. */
        send_resp(fd, "404 Not Found", "text/html; charset=utf-8", PAGE_HINT);
    }
    client_reset(s);
    return ret;
}

/* Push the next slice of a file pull (GET /file) to the PC. Bounded to
 * STREAM_PER_POLL a poll — the same ceiling the upload path uses — so a large
 * game streams over many frames instead of freezing the render thread for the
 * whole transfer. The header already went out (see respond_simple); this only
 * moves the body. On completion or any error it closes the source and the
 * connection. Returns a benign poll code (nothing for the caller to apply). */
static int stream_out(HttpSrv *s) {
    int fd = s->client_fd;
    /* Static (not on the stack): httpsrv_poll only ever runs on the UI thread,
     * so this scratch is never re-entered, and it keeps the frame small. */
    static char buf[STREAM_BUF];
    size_t sent = 0;
    while (s->src_left > 0 && sent < STREAM_PER_POLL) {
        size_t want = s->src_left < sizeof(buf) ? (size_t)s->src_left
                                                : sizeof(buf);
        size_t got = fread(buf, 1, want, s->src);
        if (got == 0 || !send_all(fd, buf, got)) {
            /* Short read (the file shrank/vanished under us) or the peer stopped
             * draining: give up. The browser sees a truncated body against the
             * promised Content-Length and reports the failed download. */
            client_reset(s);
            return 0;
        }
        s->src_left -= got;
        sent += got;
    }
    if (s->src_left == 0) {
        client_reset(s); /* closes s->src and the connection */
        return 2;        /* delivered in full; nothing pending */
    }
    return 0; /* more slices next poll */
}

/* Shared state for the two-thread streamed-push pump. Heap-allocated and hung off
 * HttpSrv.rx_thread (a void* so <switch.h> stays out of the header); freed by
 * rx_finalize / rx_join. The net thread is the producer (recv), the writer thread
 * the consumer (fwrite); a bounded ring between them lets the socket drain during
 * the SD write. Whichever thread exits last computes rx_status and flags rx_done,
 * so the UI thread joins both only once everything has stopped. */
typedef struct {
    HttpSrv *s;
    Thread   net_thr;     /* recv producer */
    Thread   wr_thr;      /* fwrite consumer */
    Mutex    lock;
    CondVar  can_produce; /* a slot was freed by the writer */
    CondVar  can_consume; /* a slot was filled by the net thread */
    uint8_t *slot[RX_NBUF];
    size_t   len[RX_NBUF];
    int      head;        /* next slot the writer drains */
    int      tail;        /* next slot the net thread fills */
    int      count;       /* filled slots in the ring */
    size_t   received;    /* bytes recv'd (producer-owned); cbody_len tracks written */
    int      live;        /* running threads; the one that zeroes it sets rx_done */
    bool     net_done;    /* producer has stopped filling the ring */
    int      net_status;  /* RX_* from the producer */
    int      wr_status;   /* RX_* from the consumer */
} RxCtx;

/* Free the ring buffers and the context; threads must already be joined. */
static void rx_ctx_free(RxCtx *c) {
    for (int i = 0; i < RX_NBUF; i++) {
        free(c->slot[i]);
    }
    free(c);
}

/* Allocate + init the pump context (ring buffers, mutex, condvars). NULL on OOM,
 * in which case the caller falls back to the inline per-frame drain. */
static RxCtx *rx_ctx_new(HttpSrv *s) {
    RxCtx *c = calloc(1, sizeof(RxCtx));
    if (!c) {
        return NULL;
    }
    for (int i = 0; i < RX_NBUF; i++) {
        c->slot[i] = malloc(RX_SLOT);
        if (!c->slot[i]) {
            rx_ctx_free(c);
            return NULL;
        }
    }
    c->s = s;
    c->received = s->cbody_len; /* the over-read head bytes are already on disk */
    c->net_status = RX_OK;
    c->wr_status = RX_OK;
    mutexInit(&c->lock);
    condvarInit(&c->can_produce);
    condvarInit(&c->can_consume);
    return c;
}

/* Called by each pump thread as it exits: the last one out turns the two
 * per-thread outcomes into rx_status (a write error wins) and flags rx_done. */
static void rx_thread_exit(RxCtx *c) {
    HttpSrv *s = c->s;
    mutexLock(&c->lock);
    if (--c->live == 0) {
        s->rx_status = (c->wr_status != RX_OK) ? c->wr_status : c->net_status;
        __sync_synchronize(); /* status must be visible before rx_done */
        s->rx_done = true;
    }
    mutexUnlock(&c->lock);
}

/* Producer: recv the body into ring slots, filling each as full as the remaining
 * body allows before publishing it so the writer gets large contiguous chunks.
 * The only thread that reads the socket. */
static void rx_net_fn(void *arg) {
    RxCtx *c = (RxCtx *)arg;
    HttpSrv *s = c->s;
    int fd = s->client_fd;

    /* This thread owns the socket for the transfer, so it may block: switch to a
     * blocking socket with a short recv timeout. The timeout keeps the cancel
     * check responsive and doubles as a stall watchdog — a live LAN sender
     * returns data at once, so it only fires when the peer goes quiet. */
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }
    struct timeval tv = {0, 500 * 1000}; /* 500 ms */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int stalls = 0;
    const int stall_max = 12; /* ~6 s of silence ends a dead transfer */
    int status = RX_OK;
    while (c->received < s->cbody_total) {
        if (s->rx_cancel) {
            status = RX_CANCELLED;
            break;
        }
        /* Claim a free slot, waiting if the writer is behind. */
        mutexLock(&c->lock);
        while (c->count == RX_NBUF && !s->rx_cancel) {
            condvarWait(&c->can_produce, &c->lock);
        }
        int idx = c->tail;
        bool cancel = s->rx_cancel;
        mutexUnlock(&c->lock);
        if (cancel) {
            status = RX_CANCELLED;
            break;
        }

        uint8_t *buf = c->slot[idx];
        size_t filled = 0;
        bool stop = false;
        while (filled < RX_SLOT && c->received + filled < s->cbody_total) {
            if (s->rx_cancel) {
                status = RX_CANCELLED;
                stop = true;
                break;
            }
            size_t want = s->cbody_total - c->received - filled;
            if (want > RX_SLOT - filled) {
                want = RX_SLOT - filled;
            }
            ssize_t r = recv(fd, buf + filled, want, 0);
            if (r > 0) {
                filled += (size_t)r;
                stalls = 0;
                continue;
            }
            if (r == 0) {
                status = RX_PEERCLOSED;
                stop = true;
                break;
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (++stalls >= stall_max) {
                    status = RX_ERR;
                    stop = true;
                    break;
                }
                continue;
            }
            status = RX_ERR;
            stop = true;
            break;
        }

        if (filled > 0) {
            mutexLock(&c->lock);
            c->len[idx] = filled;
            c->tail = (idx + 1) % RX_NBUF;
            c->count++;
            c->received += filled;
            condvarWakeOne(&c->can_consume);
            mutexUnlock(&c->lock);
        }
        if (stop) {
            break;
        }
    }

    mutexLock(&c->lock);
    c->net_status = status;
    c->net_done = true;
    condvarWakeOne(&c->can_consume); /* wake a writer parked on an empty ring */
    mutexUnlock(&c->lock);
    rx_thread_exit(c);
}

/* Consumer: fwrite filled ring slots to the sink and advance cbody_len (the
 * on-disk progress the UI shows). The only thread that touches the sink. */
static void rx_wr_fn(void *arg) {
    RxCtx *c = (RxCtx *)arg;
    HttpSrv *s = c->s;
    int status = RX_OK;

    for (;;) {
        mutexLock(&c->lock);
        while (c->count == 0 && !c->net_done && !s->rx_cancel) {
            condvarWait(&c->can_consume, &c->lock);
        }
        if (c->count == 0) { /* drained and the producer is done, or cancelled */
            mutexUnlock(&c->lock);
            break;
        }
        int idx = c->head;
        size_t len = c->len[idx];
        mutexUnlock(&c->lock);

        if (fwrite(c->slot[idx], 1, len, s->sink) != len) {
            status = RX_WRITEERR;
            s->rx_cancel = true; /* stop the producer; the .part is discarded */
            mutexLock(&c->lock);
            condvarWakeOne(&c->can_produce);
            mutexUnlock(&c->lock);
            break;
        }
        s->cbody_len += len;

        mutexLock(&c->lock);
        c->head = (idx + 1) % RX_NBUF;
        c->count--;
        condvarWakeOne(&c->can_produce);
        mutexUnlock(&c->lock);
    }

    c->wr_status = status;
    rx_thread_exit(c);
}

/* Join and free the pump threads. Sets rx_cancel first (and wakes any parked
 * thread) so a still-running transfer stops early; safe whether or not one is
 * running. */
static void rx_join(HttpSrv *s) {
    if (!s->rx_running) {
        return;
    }
    RxCtx *c = (RxCtx *)s->rx_thread;
    s->rx_cancel = true;
    mutexLock(&c->lock);
    condvarWakeAll(&c->can_produce);
    condvarWakeAll(&c->can_consume);
    mutexUnlock(&c->lock);
    threadWaitForExit(&c->net_thr);
    threadClose(&c->net_thr);
    threadWaitForExit(&c->wr_thr);
    threadClose(&c->wr_thr);
    rx_ctx_free(c);
    s->rx_thread = NULL;
    s->rx_running = false;
    s->rx_done = false;
    s->rx_cancel = false;
}

/* UI-thread side of the streamed push: returns 0 while the pump threads are still
 * running (cbody_len advances for the progress bar), and on completion joins them
 * and turns rx_status into the httpsrv_poll code + HTTP response — the same finish
 * the old inline drain did, just after the threads rather than inline. */
static int rx_finalize(HttpSrv *s) {
    if (!s->rx_done) {
        return 0; /* still receiving */
    }
    __sync_synchronize(); /* pair with the barrier before rx_done */
    int status = s->rx_status;
    int fd = s->client_fd;
    RxCtx *c = (RxCtx *)s->rx_thread;
    threadWaitForExit(&c->net_thr);
    threadClose(&c->net_thr);
    threadWaitForExit(&c->wr_thr);
    threadClose(&c->wr_thr);
    rx_ctx_free(c);
    s->rx_thread = NULL;
    s->rx_running = false;
    s->rx_done = false;
    s->rx_cancel = false;

    if (status == RX_OK) {
        /* Streamed body complete: flush and close. A close error means buffered
         * writes never reached the card, so treat it as a failed transfer. */
        int cerr = fclose(s->sink);
        s->sink = NULL; /* so client_reset keeps the finished file */
        free(s->cbody);
        s->cbody = NULL;
        s->cbody_len = 0;
        s->cbody_total = 0;
        make_blocking(fd);
        if (cerr != 0) {
            remove(s->part_path);
            send_resp(fd, "507 Insufficient Storage", "text/plain",
                      "write failed");
            snprintf(s->last_err, sizeof(s->last_err),
                     "flush failed (card full?)");
            client_reset(s);
            return 4;
        }
        send_resp(fd, "200 OK", "text/plain", "received");
        s->body = NULL; /* streamed straight to disk; nothing in RAM */
        s->body_len = 0;
        client_reset(s);
        return 1;
    }

    /* Any failure: record a short reason and drop the connection. client_reset
     * closes the still-open sink and removes the partial ".part". */
    if (status == RX_WRITEERR) {
        make_blocking(fd);
        send_resp(fd, "507 Insufficient Storage", "text/plain", "write failed");
        snprintf(s->last_err, sizeof(s->last_err), "write failed (card full?)");
    } else if (status == RX_PEERCLOSED) {
        snprintf(s->last_err, sizeof(s->last_err), "peer closed at %zu/%zu",
                 s->cbody_len, s->cbody_total);
    } else if (status == RX_CANCELLED) {
        snprintf(s->last_err, sizeof(s->last_err), "cancelled");
    } else {
        snprintf(s->last_err, sizeof(s->last_err), "recv failed");
    }
    client_reset(s);
    return 4;
}

static int client_step(HttpSrv *s) {
    int fd = s->client_fd;
    bool got_data = false;

    /* A pull in progress owns the connection until its body is fully sent. */
    if (s->src) {
        return stream_out(s);
    }

    /* A streamed push is being pumped on its own thread (see rx_thread_fn); don't
     * re-enter the request parser. Report in-progress each frame until it finishes,
     * then join and finalize here on the UI thread. */
    if (s->rx_running) {
        return rx_finalize(s);
    }

    /* Phase 1: the request head. The body (if any) starts in whatever the
     * last recv over-read, and is carried into phase 2 below. */
    if (!s->cbody) {
        char *body_start = NULL;
        while (s->head_len < HDR_MAX) {
            ssize_t r = recv(fd, s->head + s->head_len, HDR_MAX - s->head_len,
                             0);
            if (r > 0) {
                s->head_len += (size_t)r;
                got_data = true;
                s->head[s->head_len] = '\0';
                body_start = strstr(s->head, "\r\n\r\n");
                if (body_start) {
                    break;
                }
                continue;
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break; /* nothing more this frame */
            }
            client_reset(s); /* peer closed or errored mid-head */
            return 0;
        }
        if (!body_start) {
            if (s->head_len >= HDR_MAX) {
                client_reset(s); /* head too big to be ours */
                return 0;
            }
            unsigned long long now = armTicksToNs(armGetSystemTick());
            if (now - s->conn_start_ns > HEAD_DEADLINE_NS) {
                /* Still no blank line after all that: a trickle, not a slow
                 * network. Drop it so the slot is free for a real upload. */
                client_reset(s);
                return 0;
            }
            return client_idle(s, got_data);
        }
        body_start += 4;

        if (strncmp(s->head, "GET ", 4) == 0 ||
            strncmp(s->head, "OPTIONS ", 8) == 0) {
            return respond_simple(s, fd, s->head);
        }
        if (strncmp(s->head, "POST ", 5) != 0) {
            make_blocking(fd);
            send_resp(fd, "405 Method Not Allowed", "text/plain", "no");
            client_reset(s);
            return 0;
        }
        /* Delete one game: "POST /<token>/rm?p=<abs path>" (no body), confined
         * by path_allowed to the console folders + inbox. Answered and closed
         * here — no body to read — before the upload handling below. */
        if (s->mode == HTTPSRV_MODE_INVENTORY) {
            const char *p;
            size_t pl = req_path(s->head, &p);
            size_t vlen = 0;
            const char *val = route_pval(s, p, pl, "rm", &vlen);
            if (val) {
                make_blocking(fd);
                if (!host_ok(s, s->head)) {
                    send_resp(fd, "403 Forbidden", "text/plain", "wrong host");
                } else {
                    char path[1024];
                    pct_decode(val, vlen, path, sizeof(path));
                    if (path_allowed(s, path) && remove(path) == 0) {
                        send_resp(fd, "200 OK", "text/plain", "deleted");
                    } else {
                        send_resp(fd, "403 Forbidden", "text/plain", "denied");
                    }
                }
                client_reset(s);
                return 0;
            }
            /* Rename or move one game: "POST /<token>/mv?p=<abs src>&d=<abs dest>"
             * (no body). The client percent-encodes each path, so a literal '&'
             * in a name arrives as %26 and the first "&d=" is the separator.
             * Both ends must clear path_allowed, so a move can only land inside
             * the console folders + inbox; an occupied destination is refused
             * rather than clobbered (a pure case-change rename is exempt, since
             * FAT sees the source and target as the same file). */
            size_t mlen = 0;
            const char *mval = route_pval(s, p, pl, "mv", &mlen);
            if (mval) {
                make_blocking(fd);
                const char *sep = NULL;
                for (size_t i = 0; i + 3 <= mlen; i++) {
                    if (mval[i] == '&' && mval[i + 1] == 'd' &&
                        mval[i + 2] == '=') {
                        sep = mval + i;
                        break;
                    }
                }
                if (!host_ok(s, s->head)) {
                    send_resp(fd, "403 Forbidden", "text/plain", "wrong host");
                } else if (!sep) {
                    send_resp(fd, "400 Bad Request", "text/plain", "need dest");
                } else {
                    char src[1024], dst[1024];
                    pct_decode(mval, (size_t)(sep - mval), src, sizeof(src));
                    pct_decode(sep + 3, (size_t)((mval + mlen) - (sep + 3)), dst,
                               sizeof(dst));
                    bool occupied = false;
                    if (strcasecmp(src, dst) != 0) {
                        FILE *ex = fopen(dst, "rb");
                        if (ex) {
                            fclose(ex);
                            occupied = true;
                        }
                    }
                    if (!path_allowed(s, src) || !path_allowed(s, dst)) {
                        send_resp(fd, "403 Forbidden", "text/plain", "denied");
                    } else if (occupied) {
                        send_resp(fd, "409 Conflict", "text/plain", "exists");
                    } else if (rename(src, dst) == 0) {
                        send_resp(fd, "200 OK", "text/plain", "moved");
                    } else {
                        send_resp(fd, "500 Internal Server Error", "text/plain",
                                  "failed");
                    }
                }
                client_reset(s);
                return 0;
            }
        }
        /* The inventory server serves GET read-only, but accepts one write: a
         * collection pushed from the app utility. It is buffered like an import
         * (mode isn't ROM) and the caller applies it with a backup; the token
         * gate just below is the only thing authorising it. */
        /* Uploads only land on the one-time path from the console's screen.
         * Checked before the body is read: an unauthorized POST is refused
         * for the cost of its headers, not 16 MB of its payload. */
        {
            const char *p;
            size_t pl = req_path(s->head, &p);
            if (!path_is_token(s, p, pl) || !host_ok(s, s->head)) {
                make_blocking(fd);
                send_resp(fd, "403 Forbidden", "text/plain",
                          "use the address shown on the console");
                client_reset(s);
                return 0;
            }
        }

        const char *cl = hdr_val(s->head, "content-length:");
        long clen = cl ? strtol(cl, NULL, 10) : -1;
        if (clen <= 0) {
            make_blocking(fd);
            send_resp(fd, "400 Bad Request", "text/plain", "no length");
            client_reset(s);
            return 0;
        }
        /* The always-on inventory server also accepts a game streamed straight
         * to disk (into the inbox): it arrives as a raw body with an X-Filename
         * header, whereas the buffered collection/nro push is multipart with
         * none, so that tells them apart. ROM mode always streams. */
        bool stream_to_disk = (s->mode == HTTPSRV_MODE_ROM);
        if (s->mode == HTTPSRV_MODE_INVENTORY) {
            const char *ict = hdr_val(s->head, "content-type:");
            bool mp = ict && strncasecmp(ict, "multipart/form-data", 19) == 0;
            s->recv_app[0] = '\0';      /* cleared unless this is an app-update push */
            s->recv_app_path[0] = '\0';
            s->recv_app_new = false;
            s->recv_folder[0] = '\0';   /* cleared unless this is a Library game push */
            /* A DAT push (companion › DAT Files) carries X-Dat and must buffer so
             * the caller can read its header and file it by console — never route
             * it to the streamed-to-inbox path even though it has an X-Filename. */
            s->recv_dat = (hdr_val(s->head, "x-dat:") != NULL);
            if (!s->recv_dat && hdr_val(s->head, "x-filename:") && !mp) {
                stream_to_disk = true;
            } else {
                /* A buffered push (collection or .nro) doesn't stream to a
                 * .part, so recv_name is otherwise blank and the live receive
                 * row shows "…". A multipart .nro push carries its name in
                 * X-Filename; adopt it (cleared first so a nameless collection
                 * push doesn't inherit the last build's name). part_path stays
                 * empty, so this never routes to the streamed-file handler. */
                const char *fn = hdr_val(s->head, "x-filename:");
                char nm[256];
                s->recv_name[0] = '\0';
                if (fn && sanitize_filename(fn, nm, sizeof(nm))) {
                    snprintf(s->recv_name, sizeof(s->recv_name), "%s", nm);
                }
            }
        }
        long long maxb = stream_to_disk ? (long long)HTTPSRV_MAX_ROM
                                        : (long long)HTTPSRV_MAX_BODY;
        if ((long long)clen > maxb) {
            make_blocking(fd);
            send_resp(fd, "413 Payload Too Large", "text/plain", "too big");
            client_reset(s);
            return 0;
        }
        /* The content type outlives the head buffer (multipart slicing needs
         * it once the whole body is in), so keep a copy. */
        const char *ct = hdr_val(s->head, "content-type:");
        size_t ci = 0;
        if (ct) {
            while (ct[ci] && ct[ci] != '\r' && ct[ci] != '\n' &&
                   ci + 1 < sizeof(s->ctype)) {
                s->ctype[ci] = ct[ci];
                ci++;
            }
        }
        s->ctype[ci] = '\0';

        /* Bytes of the body that already rode in with the head's last recv. */
        size_t have = s->head_len - (size_t)(body_start - s->head);
        if (have > (size_t)clen) {
            have = (size_t)clen;
        }

        if (stream_to_disk) {
            /* ROM upload: stream the raw body straight to <dest_dir>/<name>.part
             * so a multi-GB game never has to fit in RAM. The ROM page posts the
             * file as the whole body (no multipart) precisely for this; reject a
             * form so the envelope never lands in the file. */
            if (strncasecmp(s->ctype, "multipart/form-data", 19) == 0) {
                make_blocking(fd);
                send_resp(fd, "400 Bad Request", "text/plain",
                          "raw body expected");
                client_reset(s);
                return 0;
            }
            const char *fn = hdr_val(s->head, "x-filename:");
            char name[256];
            if (!s->dest_dir[0] || !fn ||
                !sanitize_filename(fn, name, sizeof(name))) {
                make_blocking(fd);
                send_resp(fd, "400 Bad Request", "text/plain", "no filename");
                client_reset(s);
                return 0;
            }
            snprintf(s->recv_name, sizeof(s->recv_name), "%s", name);
            /* An Emulators-tab update rides the same stream but carries the target
             * app in X-App-Target; capture it (sanitized) so the caller installs
             * the body as that app instead of filing it in the inbox. */
            if (s->mode == HTTPSRV_MODE_INVENTORY) {
                const char *at = hdr_val(s->head, "x-app-target:");
                char an[256];
                if (at && sanitize_filename(at, an, sizeof(an))) {
                    snprintf(s->recv_app, sizeof(s->recv_app), "%s", an);
                }
                /* An exact device path (X-App-Path) takes precedence over the
                 * name: it disambiguates a same-named .nro in another subfolder. */
                const char *ap = hdr_val(s->head, "x-app-path:");
                if (ap && !sanitize_app_path(ap, s->recv_app_path,
                                             sizeof(s->recv_app_path))) {
                    s->recv_app_path[0] = '\0'; /* invalid path: fall back to name */
                }
                /* A fresh install (Emulators-tab "Install") writes recv_app_path as
                 * a brand-new app; only meaningful with a valid path in hand. */
                if (s->recv_app_path[0]) {
                    const char *ai = hdr_val(s->head, "x-app-install:");
                    s->recv_app_new = (ai && ai[0] == '1');
                }
                /* A Library-tab game push carries the console it's filed under
                 * in X-Dest-Folder; an app-update push never sets both, but if
                 * it somehow did, recv_app above still wins in InvApplyFile. */
                const char *df = hdr_val(s->head, "x-dest-folder:");
                char dn[64];
                if (df && sanitize_filename(df, dn, sizeof(dn))) {
                    snprintf(s->recv_folder, sizeof(s->recv_folder), "%s", dn);
                }
            }
            int pn = snprintf(s->part_path, sizeof(s->part_path), "%s/%s.part",
                              s->dest_dir, name);
            if (pn <= 0 || (size_t)pn >= sizeof(s->part_path)) {
                make_blocking(fd);
                send_resp(fd, "400 Bad Request", "text/plain", "name too long");
                client_reset(s);
                return 0;
            }
            s->sink = fopen(s->part_path, "wb");
            char *scratch = s->sink ? malloc(STREAM_BUF) : NULL;
            if (!s->sink || !scratch) {
                if (s->sink) {
                    fclose(s->sink);
                    s->sink = NULL;
                    remove(s->part_path);
                }
                make_blocking(fd);
                send_resp(fd, "500 Internal Server Error", "text/plain",
                          "cannot write");
                client_reset(s);
                return 0;
            }
            /* Preallocate the .part to its final size so FAT/exFAT lays down a
             * contiguous cluster run up front instead of extending the chain on
             * every 64K write — the same win the USB/MTP receive path gets from
             * ftruncate (see mtp/responder.cpp). Best-effort; every failure path
             * removes the file whole, so a preallocated tail never survives, and
             * a complete body is exactly clen bytes so no trim is needed. */
            if (clen > 0) {
                (void)ftruncate(fileno(s->sink), (off_t)clen);
            }
            /* Write the over-read bytes before freeing the head they point into. */
            if (have > 0 && fwrite(body_start, 1, have, s->sink) != have) {
                free(scratch);
                fclose(s->sink);
                s->sink = NULL;
                remove(s->part_path);
                make_blocking(fd);
                send_resp(fd, "507 Insufficient Storage", "text/plain",
                          "write failed");
                snprintf(s->last_err, sizeof(s->last_err),
                         "write failed (card full?)");
                client_reset(s);
                return 4;
            }
            s->cbody = scratch;         /* scratch recv buffer, not accumulation */
            s->cbody_len = have;        /* bytes written to disk so far */
            s->cbody_total = (size_t)clen;
        } else {
            char *body = malloc((size_t)clen + 1);
            if (!body) {
                client_reset(s);
                return 0;
            }
            memcpy(body, body_start, have);
            s->cbody = body;
            s->cbody_len = have;
            s->cbody_total = (size_t)clen;
        }
        free(s->head);
        s->head = NULL;
        s->head_len = 0;

        /* Streamed push: hand the rest of the body to a dedicated recv thread and
         * a dedicated writer thread (see rx_net_fn / rx_wr_fn) so the socket keeps
         * draining during the SD write instead of once per frame. This block only
         * runs during head setup (cbody is NULL here), so the spawn is attempted
         * exactly once. If the threads can't start — or the whole body already
         * arrived with the head — fall through to the inline per-frame drain below,
         * which still works, just at the old rate. */
        if (s->sink && s->cbody_len < s->cbody_total) {
            s->rx_cancel = false;
            s->rx_done = false;
            s->rx_status = RX_RUNNING;
            RxCtx *rc = rx_ctx_new(s);
            if (rc) {
                s->rx_thread = rc;
                rc->live = 2;
                bool made = R_SUCCEEDED(threadCreate(&rc->wr_thr, rx_wr_fn, rc,
                                                     NULL, RX_STACK, RX_PRIO, -2)) &&
                            R_SUCCEEDED(threadCreate(&rc->net_thr, rx_net_fn, rc,
                                                     NULL, RX_STACK, RX_PRIO, -2));
                if (made) {
                    /* Start the writer first (it just parks on the empty ring),
                     * then the net thread. The net thread is the only socket
                     * reader, so if its start fails nothing has been consumed and
                     * the inline drain below can still take over. */
                    if (R_SUCCEEDED(threadStart(&rc->wr_thr))) {
                        if (R_SUCCEEDED(threadStart(&rc->net_thr))) {
                            s->rx_running = true;
                            return 0; /* pumping on both threads; finalize later */
                        }
                        /* Writer is live but the net thread never started: unblock
                         * and join the writer, then fall back to inline. */
                        mutexLock(&rc->lock);
                        rc->net_done = true;
                        s->rx_cancel = true;
                        condvarWakeOne(&rc->can_consume);
                        mutexUnlock(&rc->lock);
                        threadWaitForExit(&rc->wr_thr);
                        threadClose(&rc->wr_thr);
                        threadClose(&rc->net_thr); /* created, never started */
                    } else {
                        threadClose(&rc->wr_thr);
                        threadClose(&rc->net_thr);
                    }
                }
                rx_ctx_free(rc);
                s->rx_thread = NULL;
                s->rx_cancel = false; /* hand a clean slate to the inline drain */
            }
        }
    }

    /* Phase 2: the body, as much as has arrived. */
    if (s->sink) {
        /* Streaming to disk: recv into the scratch buffer and write it out a
         * slice per frame. cbody_len is the running count of bytes written, so
         * httpsrv_receiving still reports progress. Bounded to STREAM_PER_POLL a
         * poll so a fast sender can't monopolise the render thread — the outer
         * "not done yet" path below resumes it next frame. */
        size_t drained = 0;
        while (s->cbody_len < s->cbody_total && drained < STREAM_PER_POLL) {
            size_t want = s->cbody_total - s->cbody_len;
            if (want > STREAM_BUF) {
                want = STREAM_BUF;
            }
            ssize_t r = recv(fd, s->cbody, want, 0);
            if (r > 0) {
                if (fwrite(s->cbody, 1, (size_t)r, s->sink) != (size_t)r) {
                    /* Card full or write error: give up. client_reset closes the
                     * sink and deletes the partial file. */
                    make_blocking(fd);
                    send_resp(fd, "507 Insufficient Storage", "text/plain",
                              "write failed");
                    snprintf(s->last_err, sizeof(s->last_err),
                             "write failed (card full?)");
                    client_reset(s);
                    return 4;
                }
                s->cbody_len += (size_t)r;
                drained += (size_t)r;
                got_data = true;
                continue;
            }
            if (r == 0) {
                snprintf(s->last_err, sizeof(s->last_err),
                         "peer closed at %zu/%zu", s->cbody_len,
                         s->cbody_total);
                client_reset(s); /* peer hung up before the whole body arrived */
                return 4;
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            snprintf(s->last_err, sizeof(s->last_err), "recv errno %d", errno);
            client_reset(s); /* errored mid-body */
            return 4;
        }
    } else {
        while (s->cbody_len < s->cbody_total) {
            ssize_t r = recv(fd, s->cbody + s->cbody_len,
                             s->cbody_total - s->cbody_len, 0);
            if (r > 0) {
                s->cbody_len += (size_t)r;
                got_data = true;
                continue;
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            client_reset(s); /* peer closed or errored mid-body */
            return 0;
        }
    }
    if (s->cbody_len < s->cbody_total) {
        return client_idle(s, got_data);
    }

    if (s->sink) {
        /* Streamed ROM complete: flush and close. A close error means buffered
         * writes never reached the card, so treat it as a failed transfer. On
         * success the finished ".part" is left for the caller to confirm and
         * move into place (see httpsrv.h); recv_name/part_path already hold it. */
        int cerr = fclose(s->sink);
        s->sink = NULL; /* so client_reset keeps the finished file */
        free(s->cbody);
        s->cbody = NULL;
        s->cbody_len = 0;
        s->cbody_total = 0;
        make_blocking(fd);
        if (cerr != 0) {
            remove(s->part_path);
            send_resp(fd, "507 Insufficient Storage", "text/plain",
                      "write failed");
            snprintf(s->last_err, sizeof(s->last_err),
                     "flush failed (card full?)");
            client_reset(s);
            return 4;
        }
        send_resp(fd, "200 OK", "text/plain", "received");
        s->body = NULL; /* streamed straight to disk; nothing in RAM */
        s->body_len = 0;
        client_reset(s);
        return 1;
    }

    /* Complete. The page posts a form; a direct POST sends the file as the
     * whole body. */
    s->cbody[s->cbody_len] = '\0';
    size_t blen = s->cbody_len;
    if (strncasecmp(s->ctype, "multipart/form-data", 19) == 0 &&
        !multipart_slice(s->cbody, &blen, s->ctype)) {
        make_blocking(fd);
        send_resp(fd, "400 Bad Request", "text/plain", "bad form");
        client_reset(s);
        return 0;
    }
    make_blocking(fd);
    send_redirect(fd, "/sent");
    free(s->body); /* a previous upload we never consumed */
    s->body = s->cbody;
    s->body_len = blen;
    s->cbody = NULL; /* handed over; client_reset must not free it */
    s->cbody_len = 0;
    s->cbody_total = 0;
    client_reset(s);
    return 1;
}

bool httpsrv_local_ip(char *out, size_t out_sz) {
    u32 ip = 0;
    if (R_FAILED(nifmGetCurrentIpAddress(&ip)) || ip == 0) {
        return false;
    }
    snprintf(out, out_sz, "%u.%u.%u.%u", (unsigned)(ip & 0xff),
             (unsigned)((ip >> 8) & 0xff), (unsigned)((ip >> 16) & 0xff),
             (unsigned)((ip >> 24) & 0xff));
    return true;
}

/* Create a non-blocking listening socket bound to `port` on all interfaces, or
 * -1 on failure. Shared by open (fresh server) and rebind (same port, after the
 * network interface bounced), so both get the same buffer/REUSEADDR tuning. */
static int listen_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    /* Grow the socket buffers on the *listener* so accepted connections inherit
     * them and TCP window scaling is negotiated at the SYN. This is the real cap
     * on the push path: the PC can only send as much as fits in our receive
     * window before it must wait for the render loop to drain it (once a frame),
     * so a small window throttles a push to ~(window / frame period). The init
     * config raises the pool's ceiling (tcp_rx_buf_max_size) but this stack does
     * not auto-grow the per-socket window, so ask for it explicitly. Best-effort:
     * the stack clamps to tcp_{rx,tx}_buf_max_size and a failure just leaves the
     * old (working, slower) buffers. */
    int bufsz = 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    /* Backlog was 2, which is tight: the companion runs a background inventory
     * poll (every ~5s) against this same single-client server while a push queue
     * is draining, so a real connection can arrive with one already queued. A
     * serialized push client only ever has one push connection outstanding, but
     * the poll + a reconnect can briefly need more than two slots; a full backlog
     * on this stack drops the SYN, which reads as a reset on the far end. 16 is
     * cheap and leaves plenty of headroom. */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    /* accept() is called from the render loop and must never block it. */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

bool httpsrv_open(HttpSrv *s) {
    return httpsrv_open_port(s, HTTPSRV_PORT);
}

bool httpsrv_open_port(HttpSrv *s, uint16_t port) {
    memset(s, 0, sizeof(*s));
    s->listen_fd = -1;
    s->client_fd = -1;
    s->port = port;

    int fd = listen_socket(port);
    if (fd < 0) {
        return false;
    }
    s->listen_fd = fd;

    /* The one-time code for this session's URL, from the console's CSPRNG.
     * Plain digits: it is read off a screen and typed by hand, so every symbol
     * is one keystroke on a numeric pad with no ambiguous letters to misread.
     * Rejection sampling keeps the digits unbiased — 256 is not a multiple of
     * 10, so bytes at or above 250 (25*10, the largest usable multiple) are
     * redrawn rather than folded in and skewing the low digits. */
    for (int i = 0; i < HTTPSRV_TOKEN_LEN; i++) {
        unsigned char b;
        do { randomGet(&b, 1); } while (b >= 250);
        s->token[i] = (char)('0' + (b % 10));
    }
    s->token[HTTPSRV_TOKEN_LEN] = '\0';

    /* Our own address, for the Host-header check. Best-effort: the caller has
     * already required a connection to show the URL at all. */
    if (!httpsrv_local_ip(s->ip, sizeof(s->ip))) {
        s->ip[0] = '\0';
    }
    return true;
}

int httpsrv_poll(HttpSrv *s) {
    if (s->listen_fd < 0) {
        return -1;
    }
    if (s->client_fd < 0) {
        int fd = accept(s->listen_fd, NULL, NULL);
        if (fd < 0) {
            /* EAGAIN/EWOULDBLOCK is the normal "nobody waiting" on a non-blocking
             * listener. Anything else (the kernel reset a queued connection, ran
             * out of descriptors, ...) is exactly the kind of thing that would
             * silently drop the 2nd file of a queue -- trace it. */
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                inv_trace(s, "accept errno=%d", errno);
            }
            return 0; /* nobody waiting */
        }
        /* The listener is non-blocking so accept() can't stall the render
         * loop. Reads want the same: the request is consumed a slice per
         * poll, so make sure the accepted socket carries the flag too (this
         * BSD-derived stack inherits it, but don't rely on that). */
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
        /* We already send in large slices, so Nagle only adds latency: disable it
         * so each slice goes out immediately instead of waiting to coalesce —
         * keeps the send window full and the pull path at wire speed. */
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        s->client_fd = fd;
        s->head = malloc(HDR_MAX + 1);
        if (!s->head) {
            client_reset(s);
            return 0;
        }
        s->head_len = 0;
        s->head[0] = '\0';
        s->last_err[0] = '\0';
        s->last_data_ns = armTicksToNs(armGetSystemTick());
        s->conn_start_ns = s->last_data_ns;
        inv_trace(s, "accept fd=%d", fd);
    }
    return client_step(s);
}

bool httpsrv_receiving(const HttpSrv *s, size_t *now, size_t *total) {
    bool on = s->listen_fd >= 0 && s->client_fd >= 0 && s->cbody_total > 0;
    if (now) {
        *now = on ? s->cbody_len : 0;
    }
    if (total) {
        *total = on ? s->cbody_total : 0;
    }
    return on;
}

void httpsrv_abort(HttpSrv *s) {
    /* Stop a running pump thread before client_reset closes the socket/sink it
     * owns, then client_reset drops the client, closes/removes an in-flight
     * ".part" sink and any outgoing pull, and clears the head/body-in-progress —
     * exactly a cancel. listen_fd is left open so the caller can keep serving. */
    rx_join(s);
    client_reset(s);
}

void httpsrv_close(HttpSrv *s) {
    rx_join(s);
    client_reset(s);
    if (s->listen_fd >= 0) {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    free(s->body);
    s->body = NULL;
    s->body_len = 0;
}

bool httpsrv_rebind(HttpSrv *s) {
    /* When the console sleeps, Wi-Fi drops and the listening socket is bound to
     * an interface that no longer exists; on wake accept() on it never yields a
     * connection again, so the server looks "on" but is deaf. Recreating the
     * socket on the same port re-attaches it to the freshly-associated interface.
     * Everything else in the struct is preserved (mode, token, dest_dir, roots,
     * last_inv_ns...), so the server keeps its identity — only the dead socket is
     * swapped for a live one. Any connection that was in flight when we slept is
     * already dead, so drop it. */
    rx_join(s);
    client_reset(s);
    if (s->listen_fd >= 0) {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    int fd = listen_socket(s->port);
    if (fd < 0) {
        return false; /* leave listen_fd = -1; the caller retries next poll */
    }
    s->listen_fd = fd;
    /* A rebind closes the old listener, which resets every connection still
     * queued on it -- if one fires between two files of a push, it silently
     * kills the next one. Trace it so a mid-queue rebind shows up in the bundle. */
    inv_trace(s, "rebind ok new_listen_fd=%d", fd);
    /* Refresh the advertised address: a wake can hand us a different DHCP lease. */
    if (!httpsrv_local_ip(s->ip, sizeof(s->ip))) {
        s->ip[0] = '\0';
    }
    return true;
}
