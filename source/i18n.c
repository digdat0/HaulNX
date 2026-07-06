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
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

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
        }
    }

    free(tok);
    free(buf);
}
