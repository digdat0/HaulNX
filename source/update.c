#include "update.h"
#include "net.h"
#include "jsonutil.h"
#include "extract.h"

#include <switch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Case-insensitive substring test (strcasestr isn't in this libc). True when
 * `hay` contains `needle`; an empty needle matches. */
static bool ci_contains(const char *hay, const char *needle) {
    if (!needle || !needle[0]) {
        return true;
    }
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        if (strncasecmp(hay, needle, nl) == 0) {
            return true;
        }
    }
    return false;
}

static void parse_ver(const char *s, int *a, int *b, int *c) {
    while (*s && !(*s >= '0' && *s <= '9')) {
        s++; /* skip leading 'v' or other prefix */
    }
    *a = *b = *c = 0;
    sscanf(s, "%d.%d.%d", a, b, c);
}

int version_cmp(const char *a, const char *b) {
    int a1, b1, c1, a2, b2, c2;
    parse_ver(a, &a1, &b1, &c1);
    parse_ver(b, &a2, &b2, &c2);
    if (a1 != a2) {
        return a1 < a2 ? -1 : 1;
    }
    if (b1 != b2) {
        return b1 < b2 ? -1 : 1;
    }
    if (c1 != c2) {
        return c1 < c2 ? -1 : 1;
    }
    return 0;
}

/* Find an installable asset's download URL on one release object (token index
 * `rel`). An asset is installable if it's a `.nro` OR an archive we can unzip to
 * get one (many homebrew ship the .nro inside a .zip). A direct .nro always wins
 * over an archive. When `hint` is empty, the first seen of each kind is used.
 * When `hint` is non-empty, ONLY an asset whose name contains it is eligible --
 * a release is allowed to come back empty rather than matching on an unrelated
 * asset. This matters for a repo like Cpasjuste/pemu, which ships several
 * distinct emulators (pfbneo.nro, pgba.nro, pgen.nro, pnes.nro, psnes.nro) as
 * assets of the *same* releases: falling back to "any .nro in this release"
 * would let e.g. the pGBA entry pick up pNES's file/tag just because that's
 * the only .nro a given release happened to ship, silently misreporting every
 * sibling emulator as updated. Writes the URL into out (empty if none) and,
 * when non-NULL, the chosen asset's file name into name_out (its extension
 * tells the caller whether it must be unzipped). */
static void asset_nro_url(const char *body, const jsmntok_t *tok, int rel,
                          const char *hint, char *out, size_t out_sz,
                          char *name_out, size_t name_sz) {
    out[0] = '\0';
    if (name_out) {
        name_out[0] = '\0';
    }
    int ai = json_obj_get(body, tok, rel, "assets");
    if (ai < 0 || tok[ai].type != JSMN_ARRAY) {
        return;
    }
    bool have_hint = hint && hint[0];
    /* Best direct .nro and best archive, tracked separately so a plain .nro is
     * preferred when a release offers both. Each keeps the first match unless a
     * hinted one turns up. */
    char nro_url[1024] = "", nro_name[256] = "";
    char arc_url[1024] = "", arc_name[256] = "";
    bool nro_hinted = false, arc_hinted = false;
    int cnt = tok[ai].size;
    int child = ai + 1;
    for (int i = 0; i < cnt; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            char name[256];
            json_copy(body, tok, json_obj_get(body, tok, child, "name"), name,
                      sizeof(name));
            size_t ln = strlen(name);
            bool is_nro = (ln > 4 && strcasecmp(name + ln - 4, ".nro") == 0);
            bool is_arc = !is_nro && is_archive_name(name);
            if (is_nro || is_arc) {
                char url[1024];
                json_copy(body, tok,
                          json_obj_get(body, tok, child, "browser_download_url"),
                          url, sizeof(url));
                bool h = have_hint && ci_contains(name, hint);
                if (!have_hint || h) {
                    char *u = is_nro ? nro_url : arc_url;
                    char *nm = is_nro ? nro_name : arc_name;
                    bool *hf = is_nro ? &nro_hinted : &arc_hinted;
                    if (!u[0] || (h && !*hf)) {
                        snprintf(u, 1024, "%s", url);
                        snprintf(nm, 256, "%s", name);
                        *hf = h;
                    }
                }
            }
        }
        child = json_tok_skip(tok, child);
    }
    const char *purl = nro_url[0] ? nro_url : arc_url;
    const char *pnm = nro_url[0] ? nro_name : arc_name;
    if (purl[0]) {
        snprintf(out, out_sz, "%s", purl);
        if (name_out) {
            snprintf(name_out, name_sz, "%s", pnm);
        }
    }
}

bool update_fetch_latest(const char *repo, char *tag, size_t tag_sz, char *url,
                         size_t url_sz, volatile int *attempt) {
    return update_fetch_latest_asset(repo, NULL, tag, tag_sz, url, url_sz, NULL,
                                     0, attempt, NULL);
}

bool update_fetch_latest_asset(const char *repo, const char *asset_hint,
                               char *tag, size_t tag_sz, char *url,
                               size_t url_sz, char *asset, size_t asset_sz,
                               volatile int *attempt, long *last_code) {
    tag[0] = '\0';
    url[0] = '\0';
    if (asset) {
        asset[0] = '\0';
    }
    if (last_code) {
        *last_code = 0;
    }

    /* Use the releases *list*, not /releases/latest: the latter has been seen
     * returning intermittent 504s, and it relies on GitHub's "latest" flag
     * (which bulk-migrating releases can leave pointing at an older tag). We
     * fetch the list and pick the highest version ourselves, with retries for
     * transient transport/5xx errors. */
    char api[256];
    snprintf(api, sizeof(api),
             "https://api.github.com/repos/%s/releases?per_page=100", repo);

    char *body = NULL;
    long code = 0;
    size_t len = 0;
    for (int a = 0; a < 3; a++) {
        if (attempt) {
            *attempt = a + 1;
        }
        body = http_get(api, &code, &len);
        if (body && code == 200 && len >= 2) {
            break;
        }
        free(body);
        body = NULL;
        svcSleepThread(700000000ULL); /* ~0.7s before retrying */
    }
    /* Report the last HTTP status so the caller can tell a rate limit (403/429,
     * body present) from an offline device (code stays 0, no response). */
    if (last_code) {
        *last_code = code;
    }
    if (!body) {
        return false;
    }

    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (!tok || tok[0].type != JSMN_ARRAY) {
        free(tok);
        free(body);
        return false;
    }

    /* Prefer a stable release, but some repos (e.g. a Switch port that hasn't
     * cut a non-prerelease tag yet) never publish anything else -- fall back to
     * the best prerelease when no stable candidate with a matching asset was
     * found. Drafts are never eligible either way. */
    char best_tag[64] = "";
    char best_url[1024] = "";
    char best_name[256] = "";
    char pre_tag[64] = "";
    char pre_url[1024] = "";
    char pre_name[256] = "";
    int nrel = tok[0].size;
    int rel = 1;
    for (int r = 0; r < nrel; r++) {
        if (tok[rel].type != JSMN_OBJECT) {
            rel = json_tok_skip(tok, rel);
            continue;
        }
        bool draft = json_bool(body, tok, json_obj_get(body, tok, rel, "draft"));
        bool prerelease =
            json_bool(body, tok, json_obj_get(body, tok, rel, "prerelease"));
        char rtag[64] = "";
        char rurl[1024] = "";
        char rname[256] = "";
        if (!draft) {
            json_copy(body, tok, json_obj_get(body, tok, rel, "tag_name"), rtag,
                      sizeof(rtag));
            asset_nro_url(body, tok, rel, asset_hint, rurl, sizeof(rurl), rname,
                          sizeof(rname));
        }
        if (!draft && rtag[0] && rurl[0]) {
            if (!prerelease &&
                (!best_tag[0] || version_cmp(rtag, best_tag) > 0)) {
                snprintf(best_tag, sizeof(best_tag), "%s", rtag);
                snprintf(best_url, sizeof(best_url), "%s", rurl);
                snprintf(best_name, sizeof(best_name), "%s", rname);
            } else if (prerelease &&
                       (!pre_tag[0] || version_cmp(rtag, pre_tag) > 0)) {
                snprintf(pre_tag, sizeof(pre_tag), "%s", rtag);
                snprintf(pre_url, sizeof(pre_url), "%s", rurl);
                snprintf(pre_name, sizeof(pre_name), "%s", rname);
            }
        }
        rel = json_tok_skip(tok, rel);
    }
    if (!best_tag[0] && pre_tag[0]) {
        snprintf(best_tag, sizeof(best_tag), "%s", pre_tag);
        snprintf(best_url, sizeof(best_url), "%s", pre_url);
        snprintf(best_name, sizeof(best_name), "%s", pre_name);
    }

    free(tok);
    free(body);

    if (!best_tag[0] || !best_url[0]) {
        return false;
    }
    snprintf(tag, tag_sz, "%s", best_tag);
    snprintf(url, url_sz, "%s", best_url);
    if (asset && asset_sz) {
        snprintf(asset, asset_sz, "%s", best_name);
    }
    return true;
}
