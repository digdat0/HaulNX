#include "iarchive.h"
#include "net.h"
#include "jsonutil.h"
#include "fsutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- identifier extraction ------------------------------------------ */

static void strip_trailing_slashes(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '/' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

bool ia_extract_id(const char *input, char *out, size_t out_sz) {
    if (!input || !out || out_sz == 0) {
        return false;
    }
    while (*input == ' ' || *input == '\t') {
        input++;
    }

    char work[1024];
    snprintf(work, sizeof(work), "%s", input);
    strip_trailing_slashes(work);

    char *q = strpbrk(work, "?#");
    if (q) {
        *q = '\0';
    }

    const char *markers[] = {"/details/", "/download/", "/metadata/",
                             "/serve/"};
    const char *id = NULL;
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        char *m = strstr(work, markers[i]);
        if (m) {
            id = m + strlen(markers[i]);
            break;
        }
    }
    if (!id) {
        if (strstr(work, "://")) {
            return false;
        }
        id = work;
    }

    char idbuf[256];
    snprintf(idbuf, sizeof(idbuf), "%s", id);
    char *slash = strchr(idbuf, '/');
    if (slash) {
        *slash = '\0';
    }
    if (idbuf[0] == '\0') {
        return false;
    }
    snprintf(out, out_sz, "%s", idbuf);
    return true;
}

/* ---- metadata fetch + parse ----------------------------------------- */

/* Parse a metadata JSON document into *item (identifier must be set already). */
static bool parse_metadata(const char *body, size_t len, ArchiveItem *item) {
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (!tok || tok[0].type != JSMN_OBJECT) {
        free(tok);
        return false;
    }

    json_copy(body, tok, json_obj_get(body, tok, 0, "server"), item->server,
              sizeof(item->server));
    json_copy(body, tok, json_obj_get(body, tok, 0, "dir"), item->dir,
              sizeof(item->dir));

    int files_idx = json_obj_get(body, tok, 0, "files");
    if (files_idx < 0 || tok[files_idx].type != JSMN_ARRAY) {
        free(tok);
        return false;
    }

    int count = tok[files_idx].size;
    item->files =
        (ArchiveFile *)calloc(count > 0 ? count : 1, sizeof(ArchiveFile));
    if (!item->files) {
        free(tok);
        return false;
    }

    int child = files_idx + 1;
    int added = 0;
    for (int i = 0; i < count; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            ArchiveFile *f = &item->files[added];
            json_copy(body, tok, json_obj_get(body, tok, child, "name"),
                      f->name, sizeof(f->name));
            json_copy(body, tok, json_obj_get(body, tok, child, "format"),
                      f->format, sizeof(f->format));
            f->size = json_u64(body, tok, json_obj_get(body, tok, child, "size"));
            json_copy(body, tok, json_obj_get(body, tok, child, "md5"), f->md5,
                      sizeof(f->md5));
            if (f->name[0]) {
                added++;
            }
        }
        child = json_tok_skip(tok, child);
    }
    item->file_count = added;
    if (added == 0) {
        /* Parsed but no usable files: free the allocation so it doesn't leak
         * (callers don't ia_free on a failed fetch). */
        free(item->files);
        item->files = NULL;
    }

    free(tok);
    return added > 0;
}

/* Build a safe cache file path for an identifier. */
static void cache_path_for(const char *cache_dir, const char *id, char *out,
                           size_t out_sz) {
    char safe[256];
    size_t o = 0;
    for (const char *p = id; *p && o + 1 < sizeof(safe); p++) {
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            safe[o++] = c;
        } else {
            safe[o++] = '_';
        }
    }
    safe[o] = '\0';
    snprintf(out, out_sz, "%s/%s.json", cache_dir, safe);
}

bool ia_fetch(const char *identifier, ArchiveItem *item, bool use_cache,
              const char *cache_dir) {
    if (!identifier || !item) {
        return false;
    }
    memset(item, 0, sizeof(*item));
    snprintf(item->identifier, sizeof(item->identifier), "%s", identifier);

    char cpath[1024] = {0};
    if (cache_dir) {
        cache_path_for(cache_dir, identifier, cpath, sizeof(cpath));
    }

    char *body = NULL;
    size_t len = 0;

    if (use_cache && cache_dir) {
        body = json_read_file(cpath, &len);
        if (body) {
            if (parse_metadata(body, len, item)) {
                free(body);
                return true;
            }
            /* Corrupt/stale cache (bad write, error page): refetch instead of
             * failing forever until the user manually clears the cache. */
            free(body);
        }
    }

    char url[512];
    snprintf(url, sizeof(url), "https://archive.org/metadata/%s", identifier);
    long code = 0;
    body = http_get(url, &code, &len);
    if (!body) {
        return false;
    }
    if (code != 200 || len < 2) {
        free(body);
        return false;
    }

    bool ok = parse_metadata(body, len, item);
    /* Only cache metadata that actually parsed, so a bad response (item
     * removed, error JSON) can't poison the cache. */
    if (ok && cache_dir) {
        fs_mkdir_p(cache_dir);
        FILE *f = fopen(cpath, "wb");
        if (f) {
            fwrite(body, 1, len, f);
            fclose(f);
        }
    }
    free(body);
    return ok;
}

/* Percent-encode a file path: keep unreserved chars and '/' (path separator),
 * encode everything else (spaces, ()[]!, etc). archive.org returns 400 for
 * unencoded paths. */
static void url_encode_path(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in;
         *p && o + 4 < out_sz; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

void ia_file_url(const ArchiveItem *item, const ArchiveFile *file,
                 char *out, size_t out_sz) {
    char enc[1024];
    url_encode_path(file->name, enc, sizeof(enc));
    if (item->download_base[0]) {
        snprintf(out, out_sz, "%s/%s", item->download_base, enc);
    } else {
        snprintf(out, out_sz, "https://archive.org/download/%s/%s",
                 item->identifier, enc);
    }
}

void ia_free(ArchiveItem *item) {
    if (item && item->files) {
        free(item->files);
        item->files = NULL;
        item->file_count = 0;
    }
}
