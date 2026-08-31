#include "updman.h"
#include "config.h"
#include "jsonutil.h"
#include "fsutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_field(const char *js, const jsmntok_t *tok, int obj,
                       const char *key, char *out, size_t sz) {
    int i = json_obj_get(js, tok, obj, key);
    if (i >= 0) {
        json_copy(js, tok, i, out, sz);
    } else {
        out[0] = '\0';
    }
}

static int parse_sources(const char *js, size_t len, UpdSource *out, int max) {
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(js, len, &ntok);
    if (!tok) {
        return 0;
    }
    int arr = json_obj_get(js, tok, 0, "sources");
    if (arr < 0 || tok[arr].type != JSMN_ARRAY) {
        free(tok);
        return 0;
    }
    int n = tok[arr].size, child = arr + 1, count = 0;
    for (int i = 0; i < n && count < max; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            UpdSource *s = &out[count];
            memset(s, 0, sizeof(*s));
            copy_field(js, tok, child, "id", s->id, sizeof(s->id));
            copy_field(js, tok, child, "name", s->name, sizeof(s->name));
            copy_field(js, tok, child, "detect", s->detect, sizeof(s->detect));
            copy_field(js, tok, child, "repo", s->repo, sizeof(s->repo));
            copy_field(js, tok, child, "asset", s->asset, sizeof(s->asset));
            char kind[16];
            copy_field(js, tok, child, "kind", kind, sizeof(kind));
            s->kind = (strcasecmp(kind, "app") == 0) ? UPD_KIND_APP : UPD_KIND_EMU;
            if (s->id[0] && s->name[0]) {
                count++;
            }
        }
        child = json_tok_skip(tok, child);
    }
    free(tok);
    return count;
}

int updman_parse_buf(const char *buf, size_t len, UpdSource *out, int max) {
    if (!buf || len == 0 || !out || max <= 0) {
        return 0;
    }
    return parse_sources(buf, len, out, max);
}

/* Merge the bundled romfs manifest into an already-loaded on-disk one: a
 * bundled id absent locally is appended, and a local row whose repo is still
 * blank gets repo/asset filled from the bundled row. A local row with its own
 * repo already set is never touched -- shipping corrected/expanded defaults
 * in a later HaulNX release must not clobber a user's own configuration.
 * Returns true if anything changed (caller should persist). */
static bool reconcile_bundled(UpdSource *out, int *count, int max) {
    size_t blen = 0;
    char *bjs = json_read_file("romfs:/update_sources.json", &blen);
    if (!bjs) {
        return false;
    }
    UpdSource *bundled = (UpdSource *)malloc(sizeof(UpdSource) * UPD_MAX);
    if (!bundled) {
        free(bjs);
        return false;
    }
    int bn = parse_sources(bjs, blen, bundled, UPD_MAX);
    free(bjs);

    bool changed = false;
    for (int i = 0; i < bn; i++) {
        int found = -1;
        for (int j = 0; j < *count; j++) {
            if (strcasecmp(out[j].id, bundled[i].id) == 0) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            if (*count >= max) {
                continue;
            }
            out[*count] = bundled[i];
            (*count)++;
            changed = true;
        } else if (!out[found].repo[0] && bundled[i].repo[0]) {
            snprintf(out[found].repo, sizeof(out[found].repo), "%s", bundled[i].repo);
            snprintf(out[found].asset, sizeof(out[found].asset), "%s", bundled[i].asset);
            changed = true;
        }
    }
    free(bundled);
    return changed;
}

int updman_load(UpdSource *out, int max) {
    if (!out || max <= 0) {
        return 0;
    }
    size_t len = 0;
    char *js = json_read_file(UPDSRC_PATH, &len);
    if (!js) {
        /* First run: copy the bundled default to the card so the desktop has a
         * file to read/edit, then parse that same content. */
        js = json_read_file("romfs:/update_sources.json", &len);
        if (!js) {
            return 0;
        }
        fs_ensure_parent(UPDSRC_PATH);
        FILE *f = fopen(UPDSRC_PATH, "wb");
        if (f) {
            fwrite(js, 1, len, f);
            fclose(f);
        }
        int n = parse_sources(js, len, out, max);
        free(js);
        return n;
    }
    int n = parse_sources(js, len, out, max);
    free(js);
    /* Returning user: pick up any ids/repos added or filled in since their
     * on-disk copy was seeded, without disturbing their own edits. */
    if (reconcile_bundled(out, &n, max)) {
        updman_save(out, n);
    }
    return n;
}

bool updman_save(const UpdSource *arr, int count) {
    fs_ensure_parent(UPDSRC_PATH);
    FILE *f = fopen(UPDSRC_PATH, "wb");
    if (!f) {
        return false;
    }
    fputs("{\n  \"sources\": [\n", f);
    for (int i = 0; i < count; i++) {
        const UpdSource *s = &arr[i];
        fputs("    {\"id\": ", f);
        json_write_escaped(f, s->id);
        fputs(", \"name\": ", f);
        json_write_escaped(f, s->name);
        fprintf(f, ", \"kind\": \"%s\"",
                s->kind == UPD_KIND_APP ? "app" : "emulator");
        fputs(", \"detect\": ", f);
        json_write_escaped(f, s->detect);
        fputs(", \"repo\": ", f);
        json_write_escaped(f, s->repo);
        fputs(", \"asset\": ", f);
        json_write_escaped(f, s->asset);
        fputs(i + 1 < count ? "},\n" : "}\n", f);
    }
    fputs("  ]\n}\n", f);
    fclose(f);
    return true;
}
