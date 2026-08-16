#include "romm_client.h"
#include "net.h"
#include "jsonutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- credential setup -------------------------------------------------- */

/* Decide how to authenticate: an API token (Bearer) takes priority over
 * username+password (Basic), matching romm_creds_configured/the Settings UI's
 * "token is the advanced alternative" framing. Fills exactly one of
 * (*user,*pass) or bearer_hdr; the other is left empty/NULL. Returns false if
 * neither is set (caller should not attempt the request unauthenticated --
 * RomM has no public-item concept the way archive.org does). */
static bool romm_auth_pick(const RommCredentials *c, const char **user,
                           const char **pass, char *bearer_hdr,
                           size_t bearer_sz) {
    bearer_hdr[0] = '\0';
    *user = NULL;
    *pass = NULL;
    if (c->api_token[0]) {
        snprintf(bearer_hdr, bearer_sz, "authorization: Bearer %s",
                 c->api_token);
        return true;
    }
    if (c->username[0] && c->password[0]) {
        *user = c->username;
        *pass = c->password;
        return true;
    }
    return false;
}

/* The bare host[:port] authority of server_url, for http_get_authed's
 * auth_host (the exact host the credential may be sent to). server_url is
 * always stored without a trailing slash (see romm_creds_load/save), so this
 * is just the part between "://" and the next '/' or end of string. */
void romm_server_authority(const char *server_url, char *out, size_t out_sz) {
    out[0] = '\0';
    const char *p = strstr(server_url, "://");
    if (!p) {
        return;
    }
    p += 3;
    size_t n = 0;
    while (p[n] && p[n] != '/' && n + 1 < out_sz) {
        n++;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

/* ---- JSON helpers -------------------------------------------------------- */

/* Like json_copy, but leaves out empty for anything that isn't a JSON string
 * -- notably a JSON null, which json_copy would otherwise copy in as the
 * literal 4 characters "null" (it only special-cases JSMN_STRING). Several
 * RomM fields (rom name/slug/summary, md5_hash, ...) are typed `str | None`. */
static void copy_str_field(const char *js, jsmntok_t *tok, int obj,
                           const char *key, char *out, size_t out_sz) {
    int idx = json_obj_get(js, tok, obj, key);
    if (idx >= 0 && tok[idx].type == JSMN_STRING) {
        json_copy(js, tok, idx, out, out_sz);
    } else {
        out[0] = '\0';
    }
}

static int int_field(const char *js, jsmntok_t *tok, int obj, const char *key) {
    return (int)json_u64(js, tok, json_obj_get(js, tok, obj, key));
}

/* Base64-encode src (RFC 4648, standard alphabet, '=' padded) into out
 * (NUL-terminated). out_sz must be at least 4*ceil(len/3) + 1; a too-small
 * buffer truncates rather than overflowing. */
static void base64_encode(const unsigned char *src, size_t len, char *out,
                          size_t out_sz) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0, i = 0;
    for (; i + 3 <= len && o + 4 < out_sz; i += 3) {
        uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) |
                    src[i + 2];
        out[o++] = tbl[(n >> 18) & 0x3F];
        out[o++] = tbl[(n >> 12) & 0x3F];
        out[o++] = tbl[(n >> 6) & 0x3F];
        out[o++] = tbl[n & 0x3F];
    }
    size_t rem = len - i;
    if (rem == 1 && o + 4 < out_sz) {
        uint32_t n = (uint32_t)src[i] << 16;
        out[o++] = tbl[(n >> 18) & 0x3F];
        out[o++] = tbl[(n >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2 && o + 4 < out_sz) {
        uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
        out[o++] = tbl[(n >> 18) & 0x3F];
        out[o++] = tbl[(n >> 12) & 0x3F];
        out[o++] = tbl[(n >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
}

void romm_creds_queue_auth_header(const RommCredentials *c, char *out,
                                  size_t out_sz) {
    if (c->api_token[0]) {
        snprintf(out, out_sz, "authorization: Bearer %s", c->api_token);
        return;
    }
    if (c->username[0] && c->password[0]) {
        char userpass[300];
        snprintf(userpass, sizeof(userpass), "%s:%s", c->username,
                c->password);
        char b64[420];
        base64_encode((const unsigned char *)userpass, strlen(userpass), b64,
                      sizeof(b64));
        snprintf(out, out_sz, "authorization: Basic %s", b64);
        return;
    }
    out[0] = '\0';
}

/* ---- transport ------------------------------------------------------- */

/* One GET against the RomM API: builds the URL (server_url + path), attaches
 * whichever credential is configured (scoped to the RomM host only --
 * romm_server_authority/http_get_authed keep it from ever reaching
 * archive.org), and reports a short, plain-English reason on failure. */
static char *romm_get(const RommCredentials *c, const char *path,
                      long *http_code, char *err, size_t err_sz,
                      size_t *out_len) {
    if (err && err_sz) {
        err[0] = '\0';
    }
    long code = 0;
    if (!http_code) {
        http_code = &code;
    }
    *http_code = 0;

    if (!romm_creds_configured(c)) {
        snprintf(err, err_sz, "RomM not configured");
        return NULL;
    }

    char url[600];
    snprintf(url, sizeof(url), "%s%s", c->server_url, path);
    char authority[256];
    romm_server_authority(c->server_url, authority, sizeof(authority));

    const char *user = NULL, *pass = NULL;
    char bearer[160];
    if (!romm_auth_pick(c, &user, &pass, bearer, sizeof(bearer))) {
        snprintf(err, err_sz, "no credentials configured");
        return NULL;
    }

    char *body = http_get_authed(url, authority, user, pass, bearer,
                                 !c->ignore_cert_verify, http_code, out_len);
    if (!body) {
        snprintf(err, err_sz, *http_code == 0 ? "connection failed"
                                               : "request failed");
        return NULL;
    }
    if (*http_code == 401 || *http_code == 403) {
        snprintf(err, err_sz, "authentication failed (HTTP %ld)", *http_code);
        free(body);
        return NULL;
    }
    if (*http_code != 200) {
        snprintf(err, err_sz, "server returned HTTP %ld", *http_code);
        free(body);
        return NULL;
    }
    return body;
}

/* ---- platforms --------------------------------------------------------- */

static bool parse_platforms(const char *body, size_t len,
                            RommPlatformList *out) {
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (!tok || tok[0].type != JSMN_ARRAY) {
        free(tok);
        return false;
    }

    int count = tok[0].size;
    RommPlatform *arr =
        (RommPlatform *)calloc(count > 0 ? (size_t)count : 1, sizeof(RommPlatform));
    if (!arr) {
        free(tok);
        return false;
    }

    int child = 1;
    int added = 0;
    for (int i = 0; i < count; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            RommPlatform *p = &arr[added];
            p->id = int_field(body, tok, child, "id");
            copy_str_field(body, tok, child, "slug", p->slug, sizeof(p->slug));
            copy_str_field(body, tok, child, "fs_slug", p->fs_slug,
                           sizeof(p->fs_slug));
            copy_str_field(body, tok, child, "name", p->name, sizeof(p->name));
            p->rom_count = int_field(body, tok, child, "rom_count");
            if (p->slug[0] || p->fs_slug[0]) {
                added++;
            }
        }
        child = json_tok_skip(tok, child);
    }
    out->platforms = arr;
    out->count = added;
    free(tok);
    return true;
}

bool romm_fetch_platforms(const RommCredentials *c, RommPlatformList *out,
                          long *http_code, char *err, size_t err_sz) {
    memset(out, 0, sizeof(*out));
    size_t len = 0;
    char *body = romm_get(c, "/api/platforms", http_code, err, err_sz, &len);
    if (!body) {
        return false;
    }
    bool ok = parse_platforms(body, len, out);
    if (!ok && err) {
        snprintf(err, err_sz, "unexpected response from server");
    }
    free(body);
    return ok;
}

void romm_free_platforms(RommPlatformList *list) {
    if (list && list->platforms) {
        free(list->platforms);
        list->platforms = NULL;
        list->count = 0;
    }
}

/* ---- roms --------------------------------------------------------------- */

static bool parse_roms(const char *body, size_t len, RommRomList *out) {
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (!tok) {
        return false;
    }
    /* GET /api/roms is paginated (fastapi-pagination's LimitOffsetPage): the
     * top-level response is an object with an "items" array, unlike
     * /api/platforms' bare array. A bare array is still accepted here too,
     * in case some instance/version ever returns one directly. */
    int items = 0;
    if (tok[0].type == JSMN_OBJECT) {
        items = json_obj_get(body, tok, 0, "items");
    }
    if (items < 0 || tok[items].type != JSMN_ARRAY) {
        free(tok);
        return false;
    }

    int count = tok[items].size;
    RommRom *arr =
        (RommRom *)calloc(count > 0 ? (size_t)count : 1, sizeof(RommRom));
    if (!arr) {
        free(tok);
        return false;
    }

    int child = items + 1;
    int added = 0;
    for (int i = 0; i < count; i++) {
        if (tok[child].type == JSMN_OBJECT) {
            RommRom *r = &arr[added];
            r->id = int_field(body, tok, child, "id");
            r->platform_id = int_field(body, tok, child, "platform_id");
            copy_str_field(body, tok, child, "fs_name", r->fs_name,
                           sizeof(r->fs_name));
            copy_str_field(body, tok, child, "name", r->name, sizeof(r->name));
            r->size = json_u64_size(body, tok,
                                    json_obj_get(body, tok, child, "fs_size_bytes"));
            copy_str_field(body, tok, child, "md5_hash", r->md5, sizeof(r->md5));
            copy_str_field(body, tok, child, "path_cover_small",
                          r->path_cover_small, sizeof(r->path_cover_small));
            if (r->fs_name[0]) {
                added++;
            }
        }
        child = json_tok_skip(tok, child);
    }
    out->roms = arr;
    out->count = added;
    free(tok);
    return true;
}

bool romm_fetch_roms(const RommCredentials *c, int platform_id,
                     RommRomList *out, long *http_code, char *err,
                     size_t err_sz) {
    memset(out, 0, sizeof(*out));
    char path[160];
    /* platform_ids (plural) is the real query param -- GET /api/roms takes a
     * repeatable list, matching one value here. limit=10000 (the server's
     * max) avoids the default page size of 50; the with_* flags turn off
     * pagination extras (char index, filter values, rom id index, total
     * count) this client has no use for and that only inflate the response. */
    snprintf(path, sizeof(path),
            "/api/roms?platform_ids=%d&limit=10000"
            "&with_char_index=false&with_filter_values=false"
            "&with_rom_id_index=false&with_total=false",
            platform_id);
    size_t len = 0;
    char *body = romm_get(c, path, http_code, err, err_sz, &len);
    if (!body) {
        return false;
    }
    bool ok = parse_roms(body, len, out);
    if (!ok && err) {
        snprintf(err, err_sz, "unexpected response from server");
    }
    free(body);
    return ok;
}

void romm_free_roms(RommRomList *list) {
    if (list && list->roms) {
        free(list->roms);
        list->roms = NULL;
        list->count = 0;
    }
}

/* ---- connection test / content URL -------------------------------------- */

bool romm_test_connection(const RommCredentials *c, long *http_code,
                          char *err, size_t err_sz) {
    RommPlatformList list = {0};
    bool ok = romm_fetch_platforms(c, &list, http_code, err, err_sz);
    romm_free_platforms(&list);
    return ok;
}

/* Percent-encode a single filename component: keep unreserved chars, encode
 * everything else (spaces, parentheses, '/', etc -- fs_name is one filename,
 * never a path, so a stray '/' is encoded rather than treated as a separator). */
static void url_encode_component(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in;
         *p && o + 4 < out_sz; p++) {
        unsigned char ch = *p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
            ch == '~') {
            out[o++] = (char)ch;
        } else {
            out[o++] = '%';
            out[o++] = hex[ch >> 4];
            out[o++] = hex[ch & 0xF];
        }
    }
    out[o] = '\0';
}

void romm_content_url(const RommCredentials *c, const RommRom *rom, char *out,
                      size_t out_sz) {
    char enc[1536];
    url_encode_component(rom->fs_name, enc, sizeof(enc));
    snprintf(out, out_sz, "%s/api/roms/%d/content/%s", c->server_url, rom->id,
            enc);
}

void romm_cover_url(const RommCredentials *c, const RommRom *rom, char *out,
                    size_t out_sz) {
    if (!rom->path_cover_small[0]) {
        out[0] = '\0';
        return;
    }
    /* path_cover_small carries a "?ts=<updated_at>" cache-busting suffix
     * whose value is RomM's server building it from Python's raw
     * str(datetime) rather than isoformat() -- e.g. "2026-01-01
     * 00:54:22.107990+00:00", with an unencoded space before the time. Handed
     * to curl as-is, that space silently breaks the request (no cover ever
     * loads, nothing else visibly wrong -- see the grid's fallback-icon
     * behavior). Drop the query entirely: harmless, since this client caches
     * every cover to disk forever once fetched (RommCoversStart never
     * re-fetches a path that already exists), so cache-busting serves no
     * purpose here anyway. */
    char path[600];
    snprintf(path, sizeof(path), "%s", rom->path_cover_small);
    char *q = strchr(path, '?');
    if (q) {
        *q = '\0';
    }
    /* path_cover_small already starts with '/' and is itself a static-asset
     * path (not under /api) -- server_url + path is the whole URL, same
     * concatenation romm_content_url does for /api paths. */
    snprintf(out, out_sz, "%s%s", c->server_url, path);
}

/* ---- platform -> HaulNX console mapping --------------------------------- */

/* RomM slug (fs_slug or slug, as returned by /api/platforms) -> HaulNX
 * console target (one of romfs:/dl_sources.json's "consoles" list). RomM's
 * slugs mostly follow IGDB's; several predate HaulNX's own naming and don't
 * match verbatim (e.g. RomM's "ngc"/"segacd"/"sms" vs HaulNX's
 * "gc"/"sega-cd"/"master-system"), so this is a deliberate table rather than
 * a pass-through. Cross-checked against rommapp/romm's own canonical slug
 * enum (backend/handler/metadata/base_handler.py: UniversalPlatformSlug) --
 * every entry on the left is a real current slug, and every HaulNX target
 * that has one is reachable by it. "fbneo", "naomi" and "atomiswave" have no
 * entry because RomM has no matching platform for them (fbneo is an
 * emulator core, not an IGDB platform; Naomi/Atomiswave aren't in RomM's
 * platform list at all) -- romm_map_platform_console returns NULL for
 * those, same as for any RomM platform this table doesn't recognise. */
typedef struct {
    const char *romm_slug;
    const char *haulnx_target;
} RommConsoleMapEntry;

static const RommConsoleMapEntry ROMM_CONSOLE_MAP[] = {
    {"nes", "nes"},
    {"famicom", "nes"},
    {"fds", "fds"},
    {"snes", "snes"},
    {"sfam", "snes"},
    {"n64", "n64"},
    {"gb", "gb"},
    {"gbc", "gbc"},
    {"gba", "gba"},
    {"nds", "nds"},
    {"3ds", "3ds"},
    {"ngc", "gc"},
    {"wii", "wii"},
    {"wiiu", "wiiu"},
    {"virtualboy", "virtual-boy"},
    {"pokemon-mini", "pokemon-mini"},
    {"g-and-w", "game-and-watch"},
    {"sg1000", "sg-1000"},
    {"sms", "master-system"},
    {"gamegear", "game-gear"},
    {"genesis", "genesis"},
    {"segacd", "sega-cd"},
    {"sega32", "sega-32x"},
    {"saturn", "saturn"},
    {"dc", "dc"},
    {"psx", "psx"},
    {"ps2", "ps2"},
    {"psp", "psp"},
    {"tg16", "pc-engine"},
    {"turbografx-cd", "pc-engine-cd"},
    {"supergrafx", "supergrafx"},
    {"pc-fx", "pc-fx"},
    {"neogeoaes", "neo-geo"},
    {"neogeomvs", "neo-geo"},
    {"neo-geo-cd", "neo-geo-cd"},
    {"neo-geo-pocket", "neo-geo-pocket"},
    {"neo-geo-pocket-color", "neo-geo-pocket-color"},
    {"atari2600", "atari-2600"},
    {"atari5200", "atari-5200"},
    {"atari7800", "atari-7800"},
    {"lynx", "atari-lynx"},
    {"jaguar", "atari-jaguar"},
    {"wonderswan", "wonderswan"},
    {"wonderswan-color", "wonderswan-color"},
    {"colecovision", "colecovision"},
    {"intellivision", "intellivision"},
    {"odyssey-2", "odyssey2"},
    {"vectrex", "vectrex"},
    {"fairchild-channel-f", "channel-f"},
    {"3do", "3do"},
    {"philips-cd-i", "cd-i"},
    {"supervision", "supervision"},
    {"arcade", "arcade"},
    /* No RomM platform exists for these -- deliberately absent, not missed:
     * "fbneo" is an emulator core, not an IGDB/RomM platform; Naomi and
     * Atomiswave aren't in RomM's platform list at all (verified against
     * UniversalPlatformSlug, rommapp/romm's canonical slug enum). A HaulNX
     * console in this situation just never matches romm_map_platform_console
     * -- see its doc comment. */
};

static const char *map_slug(const char *slug) {
    if (!slug || !slug[0]) {
        return NULL;
    }
    size_t n = sizeof(ROMM_CONSOLE_MAP) / sizeof(ROMM_CONSOLE_MAP[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcasecmp(slug, ROMM_CONSOLE_MAP[i].romm_slug) == 0) {
            return ROMM_CONSOLE_MAP[i].haulnx_target;
        }
    }
    return NULL;
}

const char *romm_map_platform_console(const RommPlatform *p) {
    const char *t = map_slug(p->fs_slug);
    if (t) {
        return t;
    }
    return map_slug(p->slug);
}
