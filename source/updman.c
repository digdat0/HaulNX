#include "updman.h"
#include "config.h"
#include "jsonutil.h"
#include "fsutil.h"

#include <ctype.h>
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
            copy_field(js, tok, child, "installed_tag", s->installed_tag,
                      sizeof(s->installed_tag));
            char kind[16];
            copy_field(js, tok, child, "kind", kind, sizeof(kind));
            s->kind = (strcasecmp(kind, "app") == 0) ? UPD_KIND_APP : UPD_KIND_EMU;
#ifdef HAULNX_LITE
            /* The bundled manifest's self-entry ("detect":"haulnx") points at
             * digdat0/HaulNX, but that substring also matches a Lite install's
             * own "HaulNX-Lite.nro" -- left alone, a Lite build would list
             * itself in the Emulators tab offering an update from the FULL
             * repo. Repoint it to the Lite repo here rather than in the JSON
             * (which the full build shares and can't #ifdef). */
            if (strcasecmp(s->id, "haulnx") == 0) {
                snprintf(s->name, sizeof(s->name), "HaulNX Lite");
                snprintf(s->repo, sizeof(s->repo), "digdat0/HaulNX-lite");
            }
#endif
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

/* True if `haystack_tokens` (a comma-separated detect list, already lowercase
 * per the "comma-separated lowercase filename substrings" contract on
 * UpdSource::detect) contains `needle` (one already-lowercased token) as a
 * substring of any of its own tokens -- lowercased again anyway rather than
 * trust every caller/user-edited manifest to honor that contract. Mirrors
 * detect_match()'s own per-token substring rule (source/MainApplication.cpp)
 * against a filename, but here against another entry's whole detect string. */
static bool detect_str_contains_token(const char *haystack_tokens,
                                      const char *needle) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", haystack_tokens);
    for (char *p = buf; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    return strstr(buf, needle) != NULL;
}

/* True if any comma-separated token in `a` is a substring of `b` (or, by the
 * loop in the caller trying both directions, vice versa) -- i.e. the two
 * entries would both claim the exact same installed .nro via detect_match's
 * substring-per-token rule. Used only to catch a local row that's really a
 * duplicate of a bundled one shipped under a different id later -- typically
 * a user's own "Add manually" registration made before HaulNX's catalog
 * caught up to that emulator by its proper id (e.g. a hand-added "ARMSX2-NX"
 * row predating the bundled "armsx2nx" one: same installed file, two ids
 * that don't match by strcasecmp alone -- see reconcile_bundled below). */
static bool detect_overlaps(const char *a, const char *b) {
    if (!a[0] || !b[0]) {
        return false;
    }
    char abuf[128];
    snprintf(abuf, sizeof(abuf), "%s", a);
    char *save = NULL;
    for (char *tok = strtok_r(abuf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        char t[128];
        int j = 0;
        for (char *c = tok; *c && j < (int)sizeof(t) - 1; c++) {
            if (!isspace((unsigned char)*c)) {
                t[j++] = (char)tolower((unsigned char)*c);
            }
        }
        t[j] = '\0';
        if (t[0] && detect_str_contains_token(b, t)) {
            return true;
        }
    }
    return false;
}

/* Merge the bundled romfs manifest into an already-loaded on-disk one: a
 * bundled id absent locally is appended, and a local row whose repo is still
 * blank gets repo/asset filled from the bundled row. A local row with its own
 * repo already set is never touched -- shipping corrected/expanded defaults
 * in a later HaulNX release must not clobber a user's own configuration.
 *
 * A bundled row that doesn't match any local id by name, but whose detect
 * string overlaps a local row's (see detect_overlaps above), is the same
 * "duplicate registration" case under a different id -- adopted in place
 * (id/name/detect/asset replaced with the bundled row's) rather than
 * appended, so the app stops showing one installed file as two entries. The
 * local row's own repo is kept if it had already set one, same as the
 * plain repo-fill case below.
 *
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
        bool same_id = false;
        for (int j = 0; j < *count; j++) {
            if (strcasecmp(out[j].id, bundled[i].id) == 0) {
                found = j;
                same_id = true;
                break;
            }
        }
        if (found < 0) {
            for (int j = 0; j < *count; j++) {
                if (detect_overlaps(out[j].detect, bundled[i].detect)) {
                    found = j;
                    break;
                }
            }
        }
        if (found < 0) {
            if (*count >= max) {
                continue;
            }
            out[*count] = bundled[i];
            (*count)++;
            changed = true;
        } else if (!same_id) {
            char kept_repo[80];
            snprintf(kept_repo, sizeof(kept_repo), "%s", out[found].repo);
            out[found] = bundled[i];
            if (kept_repo[0]) {
                snprintf(out[found].repo, sizeof(out[found].repo), "%s", kept_repo);
            }
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
        if (s->installed_tag[0]) {
            fputs(", \"installed_tag\": ", f);
            json_write_escaped(f, s->installed_tag);
        }
        fputs(i + 1 < count ? "},\n" : "}\n", f);
    }
    fputs("  ]\n}\n", f);
    fclose(f);
    return true;
}
