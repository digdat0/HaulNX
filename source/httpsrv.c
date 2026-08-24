#include "httpsrv.h"

#include "config.h" /* SOURCES_PATH: the file this page uploads and exports */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
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
 * feeds the idle watchdog; the next poll resumes where this one left off. */
#define STREAM_PER_POLL (512 * 1024)

/* Requests are read incrementally, a slice per poll, so the UI thread keeps
 * rendering during a large upload and can show its progress. Responses are
 * still written on a briefly-blocking socket (they are small pages); this
 * timeout bounds a peer that stops draining them. Sends get a tighter budget
 * than reads: every response fits the socket buffer in one go, so a send that
 * stalls at all is a peer deliberately not reading — and each stalled send
 * blocks the UI thread for its full timeout. */
#define RECV_TIMEOUT_MS 2000
#define SEND_TIMEOUT_MS 1000
/* Watchdog for a client that connects and then goes quiet mid-request. */
#define CLIENT_IDLE_NS  5000000000ULL /* ~5s without a byte drops the client */
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
    "<title>HaulNX-Romm-App - Update app</title>" PAGE_CSS UPLOAD_CSS
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>Haul<span>NX</span></h1><p>Update app</p></div></header>"
    "<ol>"
    "<li>Find the <b>HaulNX-Romm-App .nro</b> build to install &mdash; the "
    "same version as installed is fine.</li>"
    "<li>Drop it below, or click to browse for it. (The app utility can "
    "also push it: <b>Send to Switch &rsaquo; App update</b>.)</li>"
    "<li>Send it, then confirm the install on your Switch.</li>"
    "</ol>"
    "<form method=post enctype=multipart/form-data>"
    "<label id=drop><b>Drop HaulNX-Romm-App.nro here</b>or click to choose a file"
    "<input type=file name=f accept=\".nro\" required>"
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
#define ROM_SCRIPT                                                             \
    "<script>"                                                                 \
    "var d=document.getElementById('drop'),i=d.querySelector('input'),"        \
    "b=document.getElementById('go'),n=d.querySelector('b'),"                   \
    "st=document.getElementById('st'),bar=document.getElementById('bar'),f=0;" \
    "function s(){if(i.files.length){f=i.files[0];n.textContent=f.name;"        \
    "b.disabled=false;}}"                                                       \
    "i.addEventListener('change',s);"                                          \
    "['dragenter','dragover'].forEach(function(e){d.addEventListener(e,"        \
    "function(v){v.preventDefault();d.classList.add('over');});});"            \
    "['dragleave','drop'].forEach(function(e){d.addEventListener(e,"           \
    "function(v){v.preventDefault();d.classList.remove('over');});});"         \
    "d.addEventListener('drop',function(v){i.files=v.dataTransfer.files;s();});"\
    "b.addEventListener('click',function(){if(!f)return;b.disabled=true;"       \
    "i.disabled=true;var x=new XMLHttpRequest();"                              \
    "x.open('POST',location.pathname.replace(/\\/+$/,''));"                    \
    "x.setRequestHeader('X-Filename',encodeURIComponent(f.name));"             \
    "x.upload.onprogress=function(e){if(e.lengthComputable){var p="            \
    "Math.round(e.loaded*100/e.total);bar.style.width=p+'%';"                  \
    "st.textContent='Sending '+p+'%';}};"                                      \
    "x.onload=function(){if(x.status>=200&&x.status<300){bar.style.width="     \
    "'100%';st.textContent='Sent. Confirm on your Switch.';}else{"             \
    "st.textContent='Refused (HTTP '+x.status+'). Re-open the receive screen "  \
    "and use the address it shows.';b.disabled=false;i.disabled=false;}};"     \
    "x.onerror=function(){st.textContent='Network error \\u2014 is the "        \
    "receive screen still open?';b.disabled=false;i.disabled=false;};"         \
    "x.send(f);});"                                                            \
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
    "<li>Drop the game file below, or click to browse for it.</li>"
    "<li>Send it &mdash; it copies into the folder for the console you opened "
    "this screen from.</li>"
    "<li>Confirm on your Switch if it would replace a file already there.</li>"
    "</ol>"
    "<label id=drop><b>Drop a game file here</b>or click to choose a file"
    "<input type=file name=f required>"
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
static void client_reset(HttpSrv *s) {
    if (s->client_fd >= 0) {
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
    int wl = snprintf(want, sizeof(want), "%s:%d", s->ip, HTTPSRV_PORT);
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
        }
        send_resp(fd, "200 OK", "text/html; charset=utf-8", page);
    } else if (s->mode == HTTPSRV_MODE_EXPORT && pl == tl + 16 && p[0] == '/' &&
               strncmp(p + 1, s->token, tl - 1) == 0 &&
               strncmp(p + tl, "/dl_sources.json", 16) == 0) {
        /* Export, at "/<token>/dl_sources.json": hand back the collection the
         * console is running on, so it can be edited and sent straight back.
         * Token-gated (it lists the user's repos) and only while the Export
         * screen is open — import and update have no business exporting. */
        bool sent = send_file(fd, SOURCES_PATH, "application/json",
                              "dl_sources.json");
        if (!sent) {
            send_resp(fd, "404 Not Found", "text/plain", "no config");
        }
        ret = sent ? 3 : 0;
    } else {
        /* No (or a wrong) one-time code: explain, without echoing anything a
         * probing script could learn from. */
        send_resp(fd, "404 Not Found", "text/html; charset=utf-8", PAGE_HINT);
    }
    client_reset(s);
    return ret;
}

/* Advance the in-flight connection by whatever has arrived. Called once per
 * poll; returns the httpsrv_poll codes. */
static int client_step(HttpSrv *s) {
    int fd = s->client_fd;
    bool got_data = false;

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
        long long maxb = (s->mode == HTTPSRV_MODE_ROM)
                             ? (long long)HTTPSRV_MAX_ROM
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

        if (s->mode == HTTPSRV_MODE_ROM) {
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

bool httpsrv_open(HttpSrv *s) {
    memset(s, 0, sizeof(*s));
    s->listen_fd = -1;
    s->client_fd = -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTPSRV_PORT);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 2) != 0) {
        close(fd);
        return false;
    }
    /* accept() is called from the render loop and must never block it. */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
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

void httpsrv_close(HttpSrv *s) {
    client_reset(s);
    if (s->listen_fd >= 0) {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    free(s->body);
    s->body = NULL;
    s->body_len = 0;
}
