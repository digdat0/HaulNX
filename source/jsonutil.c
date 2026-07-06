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

static int hex4(const char *s) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

size_t json_unescape(const char *src, size_t len, char *out, size_t out_sz) {
    size_t o = 0;
    if (out_sz == 0) {
        return 0;
    }
    for (size_t i = 0; i < len && o + 1 < out_sz;) {
        char c = src[i];
        if (c != '\\' || i + 1 >= len) {
            out[o++] = c;
            i++;
            continue;
        }
        char e = src[i + 1];
        i += 2;
        switch (e) {
        case 'n': out[o++] = '\n'; break;
        case 't': out[o++] = '\t'; break;
        case 'r': out[o++] = '\r'; break;
        case 'b': out[o++] = '\b'; break;
        case 'f': out[o++] = '\f'; break;
        case 'u': {
            if (i + 4 > len) {
                i = len;
                break;
            }
            int cp = hex4(src + i);
            if (cp < 0) {
                break; /* malformed: drop the escape, resume after \u */
            }
            i += 4;
            /* Combine a UTF-16 surrogate pair into one code point. */
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= len &&
                src[i] == '\\' && src[i + 1] == 'u') {
                int lo = hex4(src + i + 2);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
            }
            if (cp < 0x80) {
                out[o++] = (char)cp;
            } else if (cp < 0x800) {
                if (o + 2 >= out_sz) { i = len; break; }
                out[o++] = (char)(0xC0 | (cp >> 6));
                out[o++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                if (o + 3 >= out_sz) { i = len; break; }
                out[o++] = (char)(0xE0 | (cp >> 12));
                out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[o++] = (char)(0x80 | (cp & 0x3F));
            } else {
                if (o + 4 >= out_sz) { i = len; break; }
                out[o++] = (char)(0xF0 | (cp >> 18));
                out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[o++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: out[o++] = e; break; /* includes \" \\ \/ */
        }
    }
    out[o] = '\0';
    return o;
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
    if (t[idx].type == JSMN_STRING) {
        json_unescape(js + t[idx].start, (size_t)len, out, out_sz);
        return;
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
