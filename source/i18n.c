#include "i18n.h"
#include "jsonutil.h"
#include "jsmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- built-in English strings (generated from lang/en.json) -------------- */

#include "i18n_strings.inc"

/* ---- runtime override table -------------------------------------------- */

static char *g_override[S__COUNT];

const char *tr(int id) {
    if (id < 0 || id >= S__COUNT) return "";
    if (g_override[id]) return g_override[id];
    return g_en[id] ? g_en[id] : "";
}

/* ---- JSON language file loader ----------------------------------------- */

/* Collect a string's printf conversions, in order, as one letter each ('*'
 * widths count as their own entry — they consume an argument too). Many of
 * these strings are fed straight to snprintf as the FORMAT, and lang files
 * can come off the SD card: a translation with a stray or reordered %spec
 * must never reach varargs. False if the signature is malformed or too long. */
static bool fmt_signature(const char *s, char *out, size_t out_sz) {
    size_t o = 0;
    for (const char *p = s; *p; p++) {
        if (*p != '%') {
            continue;
        }
        p++;
        if (*p == '%') {
            continue; /* literal percent */
        }
        while (*p && strchr("-+ #0123456789.*", *p)) {
            if (*p == '*') {
                if (o + 1 >= out_sz) {
                    return false;
                }
                out[o++] = '*';
            }
            p++;
        }
        while (*p && strchr("hljztL", *p)) {
            p++;
        }
        if (!*p || o + 1 >= out_sz) {
            return false; /* trailing lone '%', or too many conversions */
        }
        out[o++] = *p;
    }
    out[o] = '\0';
    return true;
}

/* A translation is only usable if it consumes exactly the arguments the
 * built-in English string does, in the same order. */
static bool fmt_matches(const char *ref, const char *loc) {
    char rs[24], ls[24];
    return fmt_signature(ref ? ref : "", rs, sizeof(rs)) &&
           fmt_signature(loc, ls, sizeof(ls)) && strcmp(rs, ls) == 0;
}

static void i18n_free_overrides(void) {
    for (int i = 0; i < S__COUNT; i++) {
        free(g_override[i]);
        g_override[i] = NULL;
    }
}

void i18n_load(const char *path) {
    i18n_free_overrides();
    if (!path) return;

    FILE *f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 256 * 1024) { fclose(f); return; }

    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return; }
    /* Terminate and measure at what was actually read. Discarding the count and
     * handing jsmn `len` bytes meant a short read — a truncated file, a flaky
     * card — left the tail of the buffer as uninitialized heap, and any string
     * token jsmn found in there became a translated UI string: arbitrary bytes,
     * possibly not even UTF-8, rendered on screen. Reading less now just means
     * the keys past the cut fall back to English, which is what a missing key
     * already does. */
    size_t got = fread(buf, 1, (size_t)len, f);
    buf[got] = '\0';
    len = (long)got;
    fclose(f);
    if (len <= 0) { free(buf); return; }

    /* Skip UTF-8 BOM if present (common on Windows-edited files). */
    char *js = buf;
    size_t jslen = (size_t)len;
    if (jslen >= 3 && (unsigned char)js[0] == 0xEF &&
        (unsigned char)js[1] == 0xBB && (unsigned char)js[2] == 0xBF) {
        js += 3;
        jslen -= 3;
    }

    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(js, jslen, &ntok);
    if (!tok || tok[0].type != JSMN_OBJECT) {
        free(tok);
        free(buf);
        return;
    }

    for (int i = 0; i < S__COUNT; i++) {
        if (!g_key_names[i]) continue;
        int idx = json_obj_get(js, tok, 0, g_key_names[i]);
        if (idx < 0 || tok[idx].type != JSMN_STRING) continue;
        int slen = tok[idx].end - tok[idx].start;
        g_override[i] = (char *)malloc(slen + 1);
        if (g_override[i]) {
            /* Decode \n, \", \uXXXX etc.; decoded text never exceeds the raw. */
            json_unescape(js + tok[idx].start, (size_t)slen, g_override[i],
                          (size_t)slen + 1);
            /* A translation whose %-conversions don't mirror the English
             * string would corrupt or crash the snprintf it's used in: drop
             * just that string and let English show through. */
            if (!fmt_matches(g_en[i], g_override[i])) {
                free(g_override[i]);
                g_override[i] = NULL;
            }
        }
    }

    free(tok);
    free(buf);
}
