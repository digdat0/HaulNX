#include "update.h"
#include "net.h"
#include "jsonutil.h"

#include <switch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

/* Find the .nro asset's download URL on one release object (token index `rel`).
 * Writes into out (empty if none). */
static void asset_nro_url(const char *body, const jsmntok_t *tok, int rel,
                          char *out, size_t out_sz) {
    out[0] = '\0';
    int ai = json_obj_get(body, tok, rel, "assets");
    if (ai < 0 || tok[ai].type != JSMN_ARRAY) {
        return;
    }
    int cnt = tok[ai].size;
    int child = ai + 1;
    for (int i = 0; i < cnt; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            char name[256];
            json_copy(body, tok, json_obj_get(body, tok, child, "name"), name,
                      sizeof(name));
            size_t ln = strlen(name);
            if (ln > 4 && strcasecmp(name + ln - 4, ".nro") == 0) {
                json_copy(body, tok,
                          json_obj_get(body, tok, child, "browser_download_url"),
                          out, out_sz);
                return;
            }
        }
        child = json_tok_skip(tok, child);
    }
}

bool update_fetch_latest(const char *repo, char *tag, size_t tag_sz, char *url,
                         size_t url_sz, volatile int *attempt) {
    tag[0] = '\0';
    url[0] = '\0';

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

    char best_tag[64] = "";
    char best_url[1024] = "";
    int nrel = tok[0].size;
    int rel = 1;
    for (int r = 0; r < nrel; r++) {
        if (tok[rel].type != JSMN_OBJECT) {
            rel = json_tok_skip(tok, rel);
            continue;
        }
        bool skip =
            json_bool(body, tok, json_obj_get(body, tok, rel, "draft")) ||
            json_bool(body, tok, json_obj_get(body, tok, rel, "prerelease"));
        char rtag[64] = "";
        char rurl[1024] = "";
        if (!skip) {
            json_copy(body, tok, json_obj_get(body, tok, rel, "tag_name"), rtag,
                      sizeof(rtag));
            asset_nro_url(body, tok, rel, rurl, sizeof(rurl));
        }
        if (!skip && rtag[0] && rurl[0] &&
            (!best_tag[0] || version_cmp(rtag, best_tag) > 0)) {
            snprintf(best_tag, sizeof(best_tag), "%s", rtag);
            snprintf(best_url, sizeof(best_url), "%s", rurl);
        }
        rel = json_tok_skip(tok, rel);
    }

    free(tok);
    free(body);

    if (!best_tag[0] || !best_url[0]) {
        return false;
    }
    snprintf(tag, tag_sz, "%s", best_tag);
    snprintf(url, url_sz, "%s", best_url);
    return true;
}
