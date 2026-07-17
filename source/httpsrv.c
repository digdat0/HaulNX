#include "httpsrv.h"

#include "config.h" /* SOURCES_PATH: the file this page uploads and exports */

#include <arpa/inet.h>
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

/* Once a client is accepted we read it with a blocking socket, which parks the
 * UI for the duration. That is deliberate: the peer is a browser on the same
 * LAN sending a few KB, so it takes a frame or two. The timeout bounds the
 * worst case (a client that connects and then dies) to a brief stall. */
#define RECV_TIMEOUT_MS 2000

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

/* The upload page. Self-contained by necessity: it is served off the console
 * with no internet in the path, so it cannot reference anything external. */
static const char PAGE[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>ticodl+ - Import collection</title>" PAGE_CSS
    "<style>"
    "ol{margin:0 0 1.25rem;padding-left:1.25rem;color:var(--dim);"
    "font-size:.9rem}"
    "ol b{color:var(--fg);font-weight:600}"
    "#drop{display:block;border:2px dashed var(--line);border-radius:.6rem;"
    "padding:1.6rem 1rem;text-align:center;color:var(--dim);cursor:pointer;"
    "transition:border-color .15s,color .15s}"
    "#drop:hover,#drop.over{border-color:var(--accent);color:var(--fg)}"
    "#drop b{display:block;color:var(--fg);margin-bottom:.15rem;"
    "word-break:break-all}"
    "#drop input{display:none}"
    "button{width:100%;margin-top:1.25rem;background:var(--accent);"
    "color:#12161c;border:0;border-radius:.5rem;padding:.75rem;font-size:1rem;"
    "font-weight:600;cursor:pointer}"
    "button:disabled{background:var(--line);color:var(--dim);cursor:default}"
    ".alt{margin-top:1.25rem;padding-top:1.25rem;"
    "border-top:1px solid var(--line);text-align:center}"
    ".alt p{margin:0 0 .75rem;color:var(--dim);font-size:.85rem}"
    ".alt a{display:inline-block;color:var(--fg);border:1px solid var(--line);"
    "border-radius:.5rem;padding:.6rem 1rem;text-decoration:none;"
    "font-size:.9rem;transition:border-color .15s,color .15s}"
    ".alt a:hover{border-color:var(--accent);color:var(--accent)}"
    "</style>"
    "<div class=card>"
    "<header><img src=\"/logo.png\" alt=\"\">"
    "<div><h1>ticodl<span>+</span></h1><p>Import collection</p></div></header>"
    "<ol>"
    "<li>Find the <b>dl_sources.json</b> you saved from the repo editor.</li>"
    "<li>Drop it below, or click to browse for it.</li>"
    "<li>Send it, then confirm the import on your Switch.</li>"
    "</ol>"
    "<form method=post enctype=multipart/form-data>"
    "<label id=drop><b>Drop dl_sources.json here</b>or click to choose a file"
    "<input type=file name=f accept=\".json,application/json\" required></label>"
    "<button id=go disabled>Send to Switch</button>"
    "</form>"
    "<div class=alt>"
    "<p>Want to start from what this console is already using?</p>"
    "<a href=\"/dl_sources.json\" download>Download current dl_sources.json</a>"
    "</div>"
    "<script>"
    "var d=document.getElementById('drop'),i=d.querySelector('input'),"
    "b=document.getElementById('go'),n=d.querySelector('b');"
    "function s(){if(i.files.length){n.textContent=i.files[0].name;"
    "b.disabled=false;}}"
    "i.addEventListener('change',s);"
    "['dragenter','dragover'].forEach(function(e){d.addEventListener(e,"
    "function(v){v.preventDefault();d.classList.add('over');});});"
    "['dragleave','drop'].forEach(function(e){d.addEventListener(e,"
    "function(v){v.preventDefault();d.classList.remove('over');});});"
    "d.addEventListener('drop',function(v){i.files=v.dataTransfer.files;s();});"
    "</script></div>";

/* Served at /sent, which a successful upload is redirected to. Reaching this
 * by GET is the point: it leaves the browser on a page it can safely reload,
 * instead of on a POST result that a reload would silently re-submit. */
static const char PAGE_OK[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>ticodl+ - Sent</title>" PAGE_CSS
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
    "<div><h1>ticodl<span>+</span></h1><p>File sent</p></div></header>"
    "<p class=m>Confirm the import on your Switch to apply it.</p>"
    "<p class=n>The console stops listening once a file arrives. To send "
    "another, re-open <b>Manage data &rsaquo; Import collection</b> on your "
    "Switch, then reload this page.</p>"
    "<a href=\"/\">Reload</a></div>";

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
        /* Send window full for a whole SO_SNDTIMEO: let the peer drain a couple
         * more times before abandoning a half-written response. */
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && ++stalls < 3) {
            continue;
        }
        return false;
    }
    return true;
}

/* Send a complete response. `body` may be NULL for a bodiless status.
 * Access-Control-Allow-Origin is set so the repo editor, opened from disk (and
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
    send_all(fd, head, (size_t)n);
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
    snprintf(pat, sizeof(pat), "--%s", bnd);
    char *start = strstr(body, pat);
    if (!start) {
        return false;
    }
    char *data = strstr(start, "\r\n\r\n"); /* end of this part's own headers */
    if (!data) {
        return false;
    }
    data += 4;
    snprintf(pat, sizeof(pat), "\r\n--%s", bnd);
    char *end = strstr(data, pat);
    if (!end) {
        return false;
    }
    *end = '\0';
    *len = (size_t)(end - data);
    memmove(body, data, *len + 1);
    return true;
}

/* Serve one accepted connection. Returns 1 if it delivered a file. */
static int handle_client(HttpSrv *s, int fd) {
    char *head = malloc(HDR_MAX + 1);
    if (!head) {
        return 0;
    }
    /* Read until the end of the request head. The body (if any) starts in
     * whatever the last recv over-read, and is picked up below. */
    size_t n = 0;
    char *body_start = NULL;
    while (n < HDR_MAX) {
        ssize_t r = recv(fd, head + n, HDR_MAX - n, 0);
        if (r <= 0) {
            break;
        }
        n += (size_t)r;
        head[n] = '\0';
        body_start = strstr(head, "\r\n\r\n");
        if (body_start) {
            break;
        }
    }
    if (!body_start) {
        free(head);
        return 0;
    }
    body_start += 4;

    if (strncmp(head, "GET ", 4) == 0) {
        if (strncmp(head + 4, "/logo.png", 9) == 0) {
            if (!send_file(fd, LOGO_PATH, "image/png", NULL)) {
                send_resp(fd, "404 Not Found", "text/plain", "no logo");
            }
            free(head);
            return 0;
        }
        /* Export: hand back the collection file the console is running on, so
         * it can be edited and sent straight back. */
        if (strncmp(head + 4, "/dl_sources.json", 16) == 0) {
            bool sent = send_file(fd, SOURCES_PATH, "application/json",
                                  "dl_sources.json");
            if (!sent) {
                send_resp(fd, "404 Not Found", "text/plain", "no config");
            }
            free(head);
            return sent ? 3 : 0;
        }
        if (strncmp(head + 4, "/sent", 5) == 0) {
            send_resp(fd, "200 OK", "text/html; charset=utf-8", PAGE_OK);
            free(head);
            return 2; /* the upload landed safely; nothing is pending */
        }
        /* Any other path is the upload form: there is one thing to do here. */
        send_resp(fd, "200 OK", "text/html; charset=utf-8", PAGE);
        free(head);
        return 0;
    }
    if (strncmp(head, "OPTIONS ", 8) == 0) {
        send_resp(fd, "204 No Content", "text/plain", NULL);
        free(head);
        return 0;
    }
    if (strncmp(head, "POST ", 5) != 0) {
        send_resp(fd, "405 Method Not Allowed", "text/plain", "no");
        free(head);
        return 0;
    }

    const char *cl = hdr_val(head, "content-length:");
    long clen = cl ? strtol(cl, NULL, 10) : -1;
    if (clen <= 0) {
        send_resp(fd, "400 Bad Request", "text/plain", "no length");
        free(head);
        return 0;
    }
    if (clen > HTTPSRV_MAX_BODY) {
        send_resp(fd, "413 Payload Too Large", "text/plain", "too big");
        free(head);
        return 0;
    }

    char *body = malloc((size_t)clen + 1);
    if (!body) {
        free(head);
        return 0;
    }
    size_t have = n - (size_t)(body_start - head);
    if (have > (size_t)clen) {
        have = (size_t)clen;
    }
    memcpy(body, body_start, have);
    while (have < (size_t)clen) {
        ssize_t r = recv(fd, body + have, (size_t)clen - have, 0);
        if (r <= 0) {
            break;
        }
        have += (size_t)r;
    }
    body[have] = '\0';
    size_t blen = have;
    if (have < (size_t)clen) {
        send_resp(fd, "400 Bad Request", "text/plain", "short body");
        free(body);
        free(head);
        return 0;
    }

    /* The page posts a form; a direct POST (the repo editor, later) sends the
     * JSON as the whole body. */
    const char *ctype = hdr_val(head, "content-type:");
    if (ctype && strncasecmp(ctype, "multipart/form-data", 19) == 0 &&
        !multipart_slice(body, &blen, ctype)) {
        send_resp(fd, "400 Bad Request", "text/plain", "bad form");
        free(body);
        free(head);
        return 0;
    }
    free(head);

    send_redirect(fd, "/sent");
    free(s->body); /* a previous upload we never consumed */
    s->body = body;
    s->body_len = blen;
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
    return true;
}

int httpsrv_poll(HttpSrv *s) {
    if (s->listen_fd < 0) {
        return -1;
    }
    int fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) {
        return 0; /* nobody waiting */
    }
    /* The listener is non-blocking so accept() can't stall the render loop, and
     * this stack (BSD-derived, unlike Linux) hands that flag down to the
     * accepted socket. Clear it: a non-blocking send() fails with EAGAIN as soon
     * as the window fills, which truncates anything bigger than one bufferful,
     * and it would silently ignore the timeouts set below. */
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }
    struct timeval tv = {RECV_TIMEOUT_MS / 1000, (RECV_TIMEOUT_MS % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int r = handle_client(s, fd);
    close(fd);
    return r;
}

void httpsrv_close(HttpSrv *s) {
    if (s->listen_fd >= 0) {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    free(s->body);
    s->body = NULL;
    s->body_len = 0;
}
