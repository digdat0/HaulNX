#include "jsonutil.h"

#include <stdlib.h>
#include <string.h>

jsmntok_t *json_parse_alloc(const char *js, size_t len, int *ntok) {
    jsmn_parser p;
    jsmn_init(&p);
    int needed = jsmn_parse(&p, js, len, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }
    jsmntok_t *tok = (jsmntok_t *)malloc(sizeof(jsmntok_t) * needed);
    if (!tok) {
        return NULL;
    }
    jsmn_init(&p);
    int n = jsmn_parse(&p, js, len, tok, needed);
    if (n < 1) {
        free(tok);
        return NULL;
    }
    if (ntok) {
        *ntok = n;
    }
    return tok;
}

bool json_tok_eq(const char *js, const jsmntok_t *t, const char *s) {
    int len = t->end - t->start;
    return t->type == JSMN_STRING && (int)strlen(s) == len &&
           strncmp(js + t->start, s, len) == 0;
}

static int tok_skip_d(const jsmntok_t *t, int i, int depth) {
    /* Guard against pathologically nested JSON overflowing the stack. Real
     * metadata/config is only a few levels deep. */
    if (depth > 96) {
        return i + 1;
    }
    int n = t[i].size;
    int j = i + 1;
    if (t[i].type == JSMN_OBJECT) {
        for (int k = 0; k < n; k++) {
            j++;                            /* key */
            j = tok_skip_d(t, j, depth + 1); /* value */
        }
    } else if (t[i].type == JSMN_ARRAY) {
        for (int k = 0; k < n; k++) {
            j = tok_skip_d(t, j, depth + 1);
        }
    }
    return j;
}

int json_tok_skip(const jsmntok_t *t, int i) { return tok_skip_d(t, i, 0); }

int json_obj_get(const char *js, const jsmntok_t *t, int obj, const char *key) {
    if (t[obj].type != JSMN_OBJECT) {
        return -1;
    }
    int n = t[obj].size;
    int j = obj + 1;
    for (int k = 0; k < n; k++) {
        int key_idx = j;
        int val_idx = j + 1;
        if (json_tok_eq(js, &t[key_idx], key)) {
            return val_idx;
        }
        j = json_tok_skip(t, val_idx);
    }
    return -1;
}

void json_copy(const char *js, const jsmntok_t *t, int idx, char *out,
               size_t out_sz) {
    out[0] = '\0';
    if (idx < 0 || out_sz == 0) {
        return;
    }
    int len = t[idx].end - t[idx].start;
    if (len < 0) {
        len = 0;
    }
    if ((size_t)len >= out_sz) {
        len = (int)out_sz - 1;
    }
    memcpy(out, js + t[idx].start, len);
    out[len] = '\0';
}

uint64_t json_u64(const char *js, const jsmntok_t *t, int idx) {
    if (idx < 0) {
        return 0;
    }
    char buf[32];
    json_copy(js, t, idx, buf, sizeof(buf));
    return strtoull(buf, NULL, 10);
}

bool json_bool(const char *js, const jsmntok_t *t, int idx) {
    if (idx < 0) {
        return false;
    }
    return t[idx].type == JSMN_PRIMITIVE && js[t[idx].start] == 't';
}

char *json_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) {
        *out_len = rd;
    }
    return buf;
}

void json_write_escaped(FILE *fp, const char *s) {
    fputc('"', fp);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  fputs("\\\"", fp); break;
        case '\\': fputs("\\\\", fp); break;
        case '\n': fputs("\\n", fp);  break;
        case '\r': fputs("\\r", fp);  break;
        case '\t': fputs("\\t", fp);  break;
        default:
            if (c < 0x20) {
                fprintf(fp, "\\u%04x", c);
            } else {
                fputc(c, fp);
            }
        }
    }
    fputc('"', fp);
}
