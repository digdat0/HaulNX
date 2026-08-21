/*
 * SteamGridDB box art: fuzzy-search a game's display title, download its
 * top-ranked grid image, and cache the outcome (hit or miss) so a title
 * already resolved -- found or not -- is never re-queried by a later scan.
 * Local library only; entirely opt-in on a user-supplied API key.
 *
 * On-disk index is one tab-separated line per title:
 *   <found:0|1>\t<file>\t<title>\n
 * `file` is a "-" placeholder on a miss (sscanf's %[^\t] can't match an empty
 * field), otherwise the PNG's filename inside BOXART_DIR. Title is last (may
 * hold spaces/punctuation) so the fixed fields parse cleanly -- same
 * convention as hashcache.c.
 */
#include "boxart.h"
#include "config.h"
#include "fsutil.h"
#include "jsonutil.h"
#include "net.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char title[256];
    bool found;
    char file[64]; /* filename inside BOXART_DIR; empty when !found */
} BoxArtEntry;

static BoxArtEntry *g_e = NULL;
static int g_count = 0, g_cap = 0;
static bool g_dirty = false;
static bool g_loaded = false;

static bool ba_reserve(int need) {
    if (need <= g_cap) {
        return true;
    }
    int cap = g_cap ? g_cap * 2 : 128;
    while (cap < need) {
        cap *= 2;
    }
    BoxArtEntry *e = (BoxArtEntry *)realloc(g_e, (size_t)cap * sizeof(*e));
    if (!e) {
        return false;
    }
    g_e = e;
    g_cap = cap;
    return true;
}

/* Linear scan: the index holds one entry per distinct title in the library
 * (at most a few hundred for any real collection) and is only consulted at
 * folder-open/scan time, never per frame, so this doesn't need hashcache's
 * sorted binary search. */
static BoxArtEntry *ba_find(const char *title) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_e[i].title, title) == 0) {
            return &g_e[i];
        }
    }
    return NULL;
}

void boxart_index_load(void) {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    FILE *f = fopen(BOXART_INDEX_PATH, "rb");
    if (!f) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int found = 0;
        char file[64], title[256];
        if (sscanf(line, "%d\t%63[^\t]\t%255[^\r\n]", &found, file, title) !=
            3) {
            continue; /* skip malformed lines rather than fail the whole cache */
        }
        if (!ba_reserve(g_count + 1)) {
            continue;
        }
        BoxArtEntry *e = &g_e[g_count++];
        snprintf(e->title, sizeof(e->title), "%s", title);
        e->found = found != 0;
        snprintf(e->file, sizeof(e->file), "%s",
                (e->found && strcmp(file, "-") != 0) ? file : "");
    }
    fclose(f);
}

void boxart_index_save(void) {
    if (!g_dirty) {
        return;
    }
    fs_mkdir_p(CACHE_DIR);
    FILE *f = fopen(BOXART_INDEX_PATH, "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < g_count; i++) {
        const BoxArtEntry *e = &g_e[i];
        fprintf(f, "%d\t%s\t%s\n", e->found ? 1 : 0,
                (e->found && e->file[0]) ? e->file : "-", e->title);
    }
    fclose(f);
    g_dirty = false;
}

bool boxart_lookup(const char *title, char *path_out, size_t path_sz) {
    if (!title || !title[0]) {
        return false;
    }
    boxart_index_load();
    BoxArtEntry *e = ba_find(title);
    if (!e || !e->found) {
        return false;
    }
    if (path_out) {
        snprintf(path_out, path_sz, "%s/%s", BOXART_DIR, e->file);
    }
    return true;
}

static void ba_record(const char *title, bool found, const char *file) {
    BoxArtEntry *e = ba_find(title);
    if (!e) {
        if (!ba_reserve(g_count + 1)) {
            return;
        }
        e = &g_e[g_count++];
        snprintf(e->title, sizeof(e->title), "%s", title);
    }
    e->found = found;
    snprintf(e->file, sizeof(e->file), "%s", found && file ? file : "");
    g_dirty = true;
}

/* FNV-1a 32-bit: a stable, collision-safe-enough filename per title without
 * pulling in the file-hashing machinery in hashx.h, which hashes bytes off
 * disk, not a short in-memory string. */
static uint32_t ba_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

/* Percent-encode a query component for the SteamGridDB search path: keep
 * unreserved characters, encode everything else (spaces, punctuation, ...).
 * Mirrors archive.c's url_encode_query -- small enough that duplicating it
 * beats sharing a helper across two otherwise-unrelated files. */
static void ba_url_encode(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in;
         *p && o + 4 < out_sz; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

/* Lowercase, alnum-and-space-only, whitespace-collapsed copy of `in` -- puts
 * "Nintendo - Super Nintendo Entertainment System" and "Super Nintendo
 * Entertainment System (SNES)" on comparable footing for ba_name_score
 * below, stripping the punctuation/prefix noise that differs between our
 * console labels and however a given SteamGridDB entry happens to be named. */
static void ba_norm(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    bool prev_space = true; /* swallow leading space */
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) {
            if (o + 1 < out_sz) out[o++] = (char)c;
            prev_space = false;
        } else if (!prev_space) {
            if (o + 1 < out_sz) out[o++] = ' ';
            prev_space = true;
        }
    }
    while (o > 0 && out[o - 1] == ' ') {
        o--; /* trailing space from trailing punctuation */
    }
    out[o] = '\0';
}

/* How well a SteamGridDB result's own name matches what we actually searched
 * for -- the autocomplete endpoint is a fuzzy/typo-tolerant search, not an
 * exact lookup, and its #1 hit for a console's display name is often a
 * same-decade game that merely shares a few words rather than the console
 * itself (this was the "auto-scan console art is really bad" complaint).
 * Higher is better; 0 means "no textual relationship found", not "bad", so a
 * caller still falls back to it when nothing scores higher (better than no
 * art at all). */
static int ba_name_score(const char *query, const char *cand) {
    char nq[256], nc[256];
    ba_norm(query, nq, sizeof(nq));
    ba_norm(cand, nc, sizeof(nc));
    if (!nq[0] || !nc[0]) {
        return 0;
    }
    if (strcmp(nq, nc) == 0) {
        return 100; /* exact match once normalized */
    }
    if (strstr(nq, nc) || strstr(nc, nq)) {
        return 60; /* one is wholly contained in the other */
    }
    /* Token overlap: how many of the query's words appear as whole words in
     * the candidate, e.g. "nintendo entertainment system" vs "nintendo nes
     * entertainment system console" shares 3 of 3 query tokens. */
    int q_tokens = 0, matched = 0;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", nq);
    char *save = NULL;
    for (char *tokn = strtok_r(buf, " ", &save); tokn;
         tokn = strtok_r(NULL, " ", &save)) {
        if (strlen(tokn) < 3) {
            continue; /* skip short/filler words ("of", "the", ...) */
        }
        q_tokens++;
        char pad[260];
        snprintf(pad, sizeof(pad), " %s ", nc);
        char needle[64];
        snprintf(needle, sizeof(needle), " %s ", tokn);
        if (strstr(pad, needle)) {
            matched++;
        }
    }
    if (q_tokens > 0 && matched > 0) {
        return 10 + (40 * matched) / q_tokens; /* 10..50, scaled by overlap */
    }
    return 0;
}

/* Best-match game id for `title`, or -1 (no result, transport error, or
 * auth/rate-limit failure). Scans up to the first SGDB_SEARCH_CANDIDATES
 * autocomplete results (already ranked by SteamGridDB's own relevance) and
 * picks whichever has the highest ba_name_score against `title`, falling
 * back to the top-ranked result on a tie or when nothing scores above 0 --
 * strictly better than the old "always take rank 0" policy, never worse.
 * `*definite` reports whether a -1 is worth caching as a permanent miss: true
 * only for an actual 200 response that came back empty. A non-200 (bad/
 * expired key, rate limit, transient 5xx) or a dropped connection leaves
 * `*definite` false so the caller re-tries the title on a later scan instead
 * of a whole library getting stamped "not found" forever off one bad key --
 * see the boxart_fetch caller. `*score_out` (may be NULL) is set to the
 * winning candidate's ba_name_score on a hit -- lets a caller flag a picked
 * title/console as "low confidence" (a fuzzy token-overlap or all-zero-score
 * pick) instead of an exact/substring match, without re-deriving the score
 * itself. Left untouched on a miss (id < 0) -- callers only care when
 * `found` is true. */
#define SGDB_SEARCH_CANDIDATES 10
static int ba_search_game_id(const char *title, bool *definite,
                             int *score_out) {
    *definite = false;
    // SteamGridDB's autocomplete endpoint ranks results differently by
    // case ("gameboy" vs "GameBoy" return different candidate sets, not
    // just a re-sort) -- lowercase what we actually send so a scan's
    // results don't depend on how a console label or a ROM's derived
    // title happened to be cased. ba_name_score below already lowercases
    // both sides for comparison; this only affects the query on the wire.
    char lower[256];
    size_t li = 0;
    for (const unsigned char *p = (const unsigned char *)title;
         *p && li + 1 < sizeof(lower); p++) {
        unsigned char c = *p;
        lower[li++] = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    lower[li] = '\0';
    char enc[512];
    ba_url_encode(lower, enc, sizeof(enc));
    char url[768];
    snprintf(url, sizeof(url),
            "https://www.steamgriddb.com/api/v2/search/autocomplete/%s", enc);
    long code = 0;
    size_t len = 0;
    char *body = http_get(url, &code, &len);
    if (!body) {
        return -1;
    }
    int id = -1;
    if (code == 200) {
        *definite = true;
        int ntok = 0;
        jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
        if (tok) {
            int arr = json_obj_get(body, tok, 0, "data");
            if (arr >= 0 && tok[arr].type == JSMN_ARRAY) {
                int child = arr + 1;
                int count = tok[arr].size;
                int best_score = -1;
                for (int i = 0; i < count && i < SGDB_SEARCH_CANDIDATES; i++) {
                    if (tok[child].type == JSMN_OBJECT) {
                        int idv = json_obj_get(body, tok, child, "id");
                        int namev = json_obj_get(body, tok, child, "name");
                        if (idv >= 0) {
                            int cid = (int)json_u64(body, tok, idv);
                            int score = 0;
                            if (namev >= 0) {
                                char name[256];
                                json_copy(body, tok, namev, name,
                                         sizeof(name));
                                score = ba_name_score(title, name);
                            }
                            if (id < 0 || score > best_score) {
                                id = cid;
                                best_score = score;
                            }
                        }
                    }
                    child = json_tok_skip(tok, child);
                }
                if (id >= 0 && score_out) {
                    *score_out = best_score;
                }
            }
            free(tok);
        }
    }
    free(body);
    return id;
}

/* One HTTP GET + JSON parse of a SteamGridDB asset-list response (the
 * "grids" or "icons" endpoints share the same {"data": [...]} shape) into up
 * to `max_out` candidates. Shared by the grid and icon url/list helpers below
 * -- they differ only in which endpoint/dimension filter they query, not in
 * how the response is parsed. Sets `*definite` true on any real 200 (whether
 * or not it carried usable entries), same "only cache a confirmed answer"
 * rule as ba_search_game_id. Entries with neither a url nor a thumb are
 * skipped; SteamGridDB's grids/icons always carry at least one of the two,
 * so this is a belt-and-suspenders check, not an expected case. */
static int ba_fetch_asset_page(const char *url, BoxArtCandidate *out,
                               int max_out, bool *definite) {
    long code = 0;
    size_t len = 0;
    char *body = http_get(url, &code, &len);
    if (!body) {
        return 0;
    }
    int n = 0;
    if (code == 200) {
        *definite = true;
        int ntok = 0;
        jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
        if (tok) {
            int arr = json_obj_get(body, tok, 0, "data");
            if (arr >= 0 && tok[arr].type == JSMN_ARRAY) {
                int child = arr + 1;
                int count = tok[arr].size;
                for (int i = 0; i < count && n < max_out; i++) {
                    if (tok[child].type == JSMN_OBJECT) {
                        BoxArtCandidate *c = &out[n];
                        c->url[0] = '\0';
                        c->thumb[0] = '\0';
                        c->width = c->height = 0;
                        int u = json_obj_get(body, tok, child, "url");
                        if (u >= 0) {
                            json_copy(body, tok, u, c->url, sizeof(c->url));
                        }
                        int th = json_obj_get(body, tok, child, "thumb");
                        if (th >= 0) {
                            json_copy(body, tok, th, c->thumb, sizeof(c->thumb));
                        }
                        int wv = json_obj_get(body, tok, child, "width");
                        if (wv >= 0) {
                            c->width = (int)json_u64(body, tok, wv);
                        }
                        int hv = json_obj_get(body, tok, child, "height");
                        if (hv >= 0) {
                            c->height = (int)json_u64(body, tok, hv);
                        }
                        // SteamGridDB occasionally carries broken/placeholder
                        // entries reported as a handful of pixels (the "1x1"
                        // covers users were hitting) -- a real cover or icon
                        // is never that small, so skip anything declaring
                        // itself under this on either axis. Only applies when
                        // a size was actually reported (0 means "unknown",
                        // not "tiny" -- keep those rather than guess).
                        static const int kMinAssetPx = 16;
                        bool tiny = (c->width > 0 && c->width < kMinAssetPx) ||
                                   (c->height > 0 && c->height < kMinAssetPx);
                        if ((c->url[0] || c->thumb[0]) && !tiny) {
                            n++;
                        }
                    }
                    child = json_tok_skip(tok, child);
                }
            }
            free(tok);
        }
    }
    free(body);
    return n;
}

/* Direct URL of the best grid image for `game_id`, or empty. Tries a
 * poster-shaped 600x900 grid first (closest to a real box cover); a game
 * with only other grid sizes still resolves via the unfiltered retry.
 * `*definite` is true once either pass got a real 200 back (whether or not it
 * carried a url) -- same "only cache a confirmed answer" rule as
 * ba_search_game_id above. */
static void ba_grid_url(int game_id, char *out, size_t out_sz, bool *definite) {
    out[0] = '\0';
    *definite = false;
    BoxArtCandidate c;
    for (int pass = 0; pass < 2 && !out[0]; pass++) {
        char url[256];
        if (pass == 0) {
            snprintf(url, sizeof(url),
                    "https://www.steamgriddb.com/api/v2/grids/game/%d"
                    "?dimensions=600x900",
                    game_id);
        } else {
            snprintf(url, sizeof(url),
                    "https://www.steamgriddb.com/api/v2/grids/game/%d",
                    game_id);
        }
        if (ba_fetch_asset_page(url, &c, 1, definite) > 0) {
            snprintf(out, out_sz, "%s", c.url);
        }
    }
}

/* Every grid-image candidate for `game_id`, up to `max_out`, for the art
 * picker -- the multi-result sibling of ba_grid_url just above, which only
 * keeps the first. Same two-pass dimension strategy (600x900 first, any size
 * as a fallback only when the filtered pass came back empty). */
static int ba_grid_list(int game_id, BoxArtCandidate *out, int max_out,
                        bool *definite) {
    *definite = false;
    int n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++) {
        char url[256];
        if (pass == 0) {
            snprintf(url, sizeof(url),
                    "https://www.steamgriddb.com/api/v2/grids/game/%d"
                    "?dimensions=600x900",
                    game_id);
        } else {
            snprintf(url, sizeof(url),
                    "https://www.steamgriddb.com/api/v2/grids/game/%d",
                    game_id);
        }
        n = ba_fetch_asset_page(url, out, max_out, definite);
    }
    return n;
}

/* True for a console-art cache key ("console:<target>", see
 * ConsoleGroup::use_boxart) -- distinguishes a console entry from a real game
 * title so the icon-endpoint fallback below only ever applies to consoles. A
 * colon can't appear in a real title (always derived from an on-disk
 * filename, and FAT32/exFAT reject the character), so this can't misfire on
 * a game whose name happens to start with "console:". */
static bool ba_is_console_key(const char *title) {
    return title && !strncmp(title, "console:", 8);
}

/* Direct URL of the best *icon* (small square art) for `game_id`, or empty.
 * SteamGridDB has no dedicated platform/console endpoint -- consoles are
 * searched as ordinary "games" (see ba_search_game_id) -- but the community
 * that catalogs them for emulator frontends (ES-DE, Pegasus, etc.) mostly
 * uploads square icons/logos rather than poster-shaped grids, so a console
 * search often lands a game_id with no grid at all. Only ever tried as a
 * fallback (see ba_fetch_internal) once a poster-shaped grid has already come
 * up empty -- a real box-art grid still wins when one exists. Single pass, no
 * dimension filter: unlike a poster there's no "wrong shape" to steer away
 * from, so the top-ranked icon of any size is taken as-is. */
static void ba_icon_url(int game_id, char *out, size_t out_sz, bool *definite) {
    out[0] = '\0';
    *definite = false;
    char url[128];
    snprintf(url, sizeof(url), "https://www.steamgriddb.com/api/v2/icons/game/%d",
             game_id);
    BoxArtCandidate c;
    if (ba_fetch_asset_page(url, &c, 1, definite) > 0) {
        snprintf(out, out_sz, "%s", c.url);
    }
}

/* Every icon candidate for `game_id`, up to `max_out` -- the multi-result
 * sibling of ba_icon_url, same role as ba_grid_list vs. ba_grid_url. Used by
 * boxart_list_candidates to widen the art picker with the community's
 * platform-icon uploads alongside whatever poster grids a search turns up. */
static int ba_icon_list(int game_id, BoxArtCandidate *out, int max_out,
                        bool *definite) {
    *definite = false;
    char url[128];
    snprintf(url, sizeof(url), "https://www.steamgriddb.com/api/v2/icons/game/%d",
             game_id);
    return ba_fetch_asset_page(url, out, max_out, definite);
}

/* Shared by boxart_fetch (query == title, respects the "already resolved"
 * cache) and boxart_fetch_query (query is a user-typed override, always a
 * fresh network round trip that overwrites whatever `title` already had --
 * see boxart_fetch_query's contract in boxart.h). `*score_out` (may be NULL)
 * is left untouched -- so still -1 from the caller's own init -- whenever
 * this returns without a fresh search (the cache short-circuit above, or any
 * miss): a caller can't tell "low confidence" from "not resolved this pass"
 * apart otherwise, and conflating them would flag every already-cached title
 * as suspect on every later scan. */
static bool ba_fetch_internal(const char *key, const char *title,
                              const char *query, bool force, char *path_out,
                              size_t path_sz, int *score_out) {
    if (!key || !key[0] || !title || !title[0] || !query || !query[0]) {
        return false;
    }
    boxart_index_load();
    if (!force) {
        BoxArtEntry *existing = ba_find(title);
        if (existing) {
            if (existing->found && path_out) {
                snprintf(path_out, path_sz, "%s/%s", BOXART_DIR, existing->file);
            }
            return existing->found;
        }
    }

    net_set_steamgriddb_key(key);
    bool definite = false;
    int game_id = ba_search_game_id(query, &definite, score_out);
    if (game_id < 0) {
        /* Only stamp this title "not found" forever when SteamGridDB actually
         * said so (a real 200, no match). A bad/expired key, rate limit, or
         * dropped connection leaves it unrecorded so the next scan retries it
         * -- otherwise one bad key poisons the whole library's index in a
         * single pass. */
        if (definite) {
            ba_record(title, false, NULL);
        }
        return false;
    }

    char img_url[512];
    ba_grid_url(game_id, img_url, sizeof(img_url), &definite);
    if (!img_url[0] && ba_is_console_key(title)) {
        /* No poster-shaped grid for this console -- try the community's
         * square icon uploads instead (see ba_icon_url). Games never reach
         * this: a real title with no grid at all is left as a plain miss,
         * same as before, rather than risking a mismatched icon standing in
         * for box art. */
        bool idef = false;
        ba_icon_url(game_id, img_url, sizeof(img_url), &idef);
        definite = definite || idef;
    }
    if (!img_url[0]) {
        if (definite) {
            ba_record(title, false, NULL);
        }
        return false;
    }

    fs_mkdir_p(BOXART_DIR);
    char fname[64];
    snprintf(fname, sizeof(fname), "%08x.png", ba_hash(title));
    char dest[768];
    snprintf(dest, sizeof(dest), "%s/%s", BOXART_DIR, fname);
    long dl_code = 0;
    bool ok = http_download(img_url, dest, NULL, NULL, NULL, NULL, NULL, 0,
                            &dl_code, NULL);
    if (!ok || dl_code < 200 || dl_code >= 300) {
        remove(dest);
        /* A real non-2xx from the CDN (e.g. the grid url 404s) is a definite
         * miss; a transport-level failure (dl_code == 0, connection never
         * completed) is not -- same retry-don't-poison rule as above. */
        if (dl_code > 0) {
            ba_record(title, false, NULL);
        }
        return false;
    }

    ba_record(title, true, fname);
    if (path_out) {
        snprintf(path_out, path_sz, "%s", dest);
    }
    return true;
}

bool boxart_fetch(const char *key, const char *title, char *path_out,
                  size_t path_sz, int *score_out) {
    return ba_fetch_internal(key, title, title, false, path_out, path_sz,
                             score_out);
}

bool boxart_fetch_query(const char *key, const char *title, const char *query,
                        char *path_out, size_t path_sz, int *score_out) {
    return ba_fetch_internal(key, title, query, true, path_out, path_sz,
                             score_out);
}

bool boxart_fetch_titled(const char *key, const char *title, const char *query,
                         char *path_out, size_t path_sz, int *score_out) {
    return ba_fetch_internal(key, title, query, false, path_out, path_sz,
                             score_out);
}

int boxart_list_candidates(const char *key, const char *query,
                           BoxArtCandidate *out, int max_out) {
    if (!key || !key[0] || !query || !query[0] || !out || max_out <= 0) {
        return 0;
    }
    net_set_steamgriddb_key(key);
    bool definite = false;
    int game_id = ba_search_game_id(query, &definite, NULL);
    if (game_id < 0) {
        return 0;
    }
    int cap = max_out > BOXART_MAX_CANDIDATES ? BOXART_MAX_CANDIDATES : max_out;
    int n = ba_grid_list(game_id, out, cap, &definite);
    // Widen with the community's square-icon uploads too (see ba_icon_url's
    // comment) whenever there's still room -- purely additive, so a game
    // with plenty of grid options is unaffected either way, while a console
    // (or any title the grid catalog is thin on) gets real choices instead
    // of an empty or single-option picker.
    if (n < cap) {
        n += ba_icon_list(game_id, out + n, cap - n, &definite);
    }
    return n;
}

bool boxart_fetch_thumb(const BoxArtCandidate *c, int slot, char *path_out,
                        size_t path_sz) {
    if (!c || slot < 0 || slot >= BOXART_MAX_CANDIDATES) {
        return false;
    }
    /* Prefer the light preview; fall back to the full image for the rare
     * candidate SteamGridDB didn't give a thumb for. */
    const char *url = c->thumb[0] ? c->thumb : c->url;
    if (!url[0]) {
        return false;
    }
    fs_mkdir_p(BOXART_TMP_DIR);
    char dest[768];
    snprintf(dest, sizeof(dest), "%s/%d.png", BOXART_TMP_DIR, slot);
    long dl_code = 0;
    bool ok = http_download(url, dest, NULL, NULL, NULL, NULL, NULL, 0,
                            &dl_code, NULL);
    if (!ok || dl_code < 200 || dl_code >= 300) {
        remove(dest);
        return false;
    }
    if (path_out) {
        snprintf(path_out, path_sz, "%s", dest);
    }
    return true;
}

bool boxart_fetch_candidate(const char *key, const char *title,
                            const BoxArtCandidate *c, char *path_out,
                            size_t path_sz) {
    (void)key; /* the grid image itself is a plain CDN URL, no auth header
                * needed -- kept in the signature for symmetry with
                * boxart_fetch/boxart_fetch_query. */
    if (!title || !title[0] || !c) {
        return false;
    }
    const char *url = c->url[0] ? c->url : c->thumb;
    if (!url[0]) {
        return false;
    }
    boxart_index_load();
    fs_mkdir_p(BOXART_DIR);
    char fname[64];
    snprintf(fname, sizeof(fname), "%08x.png", ba_hash(title));
    char dest[768];
    snprintf(dest, sizeof(dest), "%s/%s", BOXART_DIR, fname);
    long dl_code = 0;
    bool ok = http_download(url, dest, NULL, NULL, NULL, NULL, NULL, 0,
                            &dl_code, NULL);
    if (!ok || dl_code < 200 || dl_code >= 300) {
        remove(dest);
        return false;
    }
    /* An explicit user pick always overwrites whatever the index already had
     * for this title, hit or miss -- same rule as boxart_fetch_query. */
    ba_record(title, true, fname);
    boxart_index_save();
    if (path_out) {
        snprintf(path_out, path_sz, "%s", dest);
    }
    return true;
}

void boxart_clear_all(void) {
    boxart_index_load();
    for (int i = 0; i < g_count; i++) {
        if (g_e[i].found && g_e[i].file[0]) {
            char path[768];
            snprintf(path, sizeof(path), "%s/%s", BOXART_DIR, g_e[i].file);
            remove(path);
        }
    }
    g_count = 0;
    g_dirty = true;
    boxart_index_save();
}

void boxart_clear_consoles(void) {
    boxart_index_load();
    int kept = 0;
    for (int i = 0; i < g_count; i++) {
        if (ba_is_console_key(g_e[i].title)) {
            if (g_e[i].found && g_e[i].file[0]) {
                char path[768];
                snprintf(path, sizeof(path), "%s/%s", BOXART_DIR, g_e[i].file);
                remove(path);
            }
            continue; /* drop this entry -- don't advance `kept` */
        }
        if (kept != i) {
            g_e[kept] = g_e[i];
        }
        kept++;
    }
    g_count = kept;
    g_dirty = true;
    boxart_index_save();
}

void boxart_clear_games(void) {
    boxart_index_load();
    int kept = 0;
    for (int i = 0; i < g_count; i++) {
        if (!ba_is_console_key(g_e[i].title)) {
            if (g_e[i].found && g_e[i].file[0]) {
                char path[768];
                snprintf(path, sizeof(path), "%s/%s", BOXART_DIR, g_e[i].file);
                remove(path);
            }
            continue; /* drop this entry -- don't advance `kept` */
        }
        if (kept != i) {
            g_e[kept] = g_e[i];
        }
        kept++;
    }
    g_count = kept;
    g_dirty = true;
    boxart_index_save();
}

bool boxart_forget(const char *title) {
    if (!title || !title[0]) {
        return false;
    }
    boxart_index_load();
    BoxArtEntry *e = ba_find(title);
    if (!e) {
        return false;
    }
    if (e->found && e->file[0]) {
        char path[768];
        snprintf(path, sizeof(path), "%s/%s", BOXART_DIR, e->file);
        remove(path);
    }
    int idx = (int)(e - g_e);
    if (idx < g_count - 1) {
        memmove(&g_e[idx], &g_e[idx + 1],
                (size_t)(g_count - idx - 1) * sizeof(*g_e));
    }
    g_count--;
    g_dirty = true;
    boxart_index_save(); /* one interactive delete, not a batch pass -- save
                          right away rather than risk it on some later call
                          that may never come. */
    return true;
}
