#ifndef ROMM_CLIENT_H
#define ROMM_CLIENT_H

/* Client for a self-hosted RomM instance (https://github.com/rommapp/romm),
 * a second download source alongside archive.org. Mirrors iarchive.h's shape
 * (fetch -> dynamic array + count, free) but talks to /api/platforms and
 * /api/roms instead of archive.org's /metadata/. */

#include "config.h" /* RommCredentials */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id;
    char slug[128];    /* RomM's platform slug, e.g. "snes" */
    char fs_slug[128]; /* filesystem-facing slug RomM stores the library under;
                         * usually equal to slug, sometimes not */
    char name[128];    /* display name, e.g. "Super Nintendo Entertainment System" */
    int rom_count;
} RommPlatform;

typedef struct {
    RommPlatform *platforms;
    int count;
} RommPlatformList;

typedef struct {
    int id;
    int platform_id;
    char fs_name[512]; /* filename RomM stores/serves this rom under; used both
                         * as the display name fallback and in the content URL */
    char name[256];    /* display name; "" if RomM has none (fall back to fs_name) */
    uint64_t size;      /* fs_size_bytes, 0 if unknown */
    char md5[33];       /* md5_hash hex, "" if unknown */
    /* Root-relative path to the small cover thumbnail (RomM's own
     * `path_cover_small`, e.g. "/assets/romm/resources/roms/1/1/cover/
     * small.png?ts=..."), already query-stringed for cache-busting on
     * re-scrape. "" if the rom has no cover. Served by RomM's own
     * frontend/nginx as a static file (not under /api), so
     * server_url+path_cover_small is the complete URL -- see
     * romm_cover_url. */
    char path_cover_small[600];
} RommRom;

typedef struct {
    RommRom *roms;
    int count;
} RommRomList;

/*
 * Fetch GET /api/platforms. On success (even zero platforms) returns true and
 * fills *out (free with romm_free_platforms). On failure returns false; if
 * http_code/err are non-NULL they report what happened (http_code 0 means a
 * transport-level failure -- network/DNS/TLS -- not an HTTP response; err is a
 * short, plain-English, non-localized reason, in the same spirit as
 * QueueItem.fail_reason).
 */
bool romm_fetch_platforms(const RommCredentials *c, RommPlatformList *out,
                          long *http_code, char *err, size_t err_sz);
void romm_free_platforms(RommPlatformList *list);

/*
 * Fetch GET /api/roms?platform_id=<id>. Same success/failure contract as
 * romm_fetch_platforms.
 */
bool romm_fetch_roms(const RommCredentials *c, int platform_id,
                     RommRomList *out, long *http_code, char *err,
                     size_t err_sz);
void romm_free_roms(RommRomList *list);

/*
 * Exactly romm_fetch_platforms, discarding the result -- for the Settings
 * "Testar conexão" action, which only cares whether the credentials work.
 */
bool romm_test_connection(const RommCredentials *c, long *http_code, char *err,
                          size_t err_sz);

/* Build the download URL for a rom: <server_url>/api/roms/<id>/content/<fs_name>
 * (fs_name percent-encoded). */
void romm_content_url(const RommCredentials *c, const RommRom *rom, char *out,
                      size_t out_sz);

/* Build the small cover-art thumbnail URL for a rom (server_url +
 * path_cover_small). out is set to "" if the rom has no cover. */
void romm_cover_url(const RommCredentials *c, const RommRom *rom, char *out,
                    size_t out_sz);

/* The bare host[:port] authority of a RomM server_url (e.g. "192.168.1.10:8080"
 * from "http://192.168.1.10:8080") -- the auth_host queue_add_ex and
 * http_get_authed/http_download_authed expect. server_url is always stored
 * without a trailing slash (see romm_creds_load/save). */
void romm_server_authority(const char *server_url, char *out, size_t out_sz);

/* Build a raw "authorization: ..." header line for the download queue
 * (queue_add_ex's auth param): "Bearer <token>" when an API token is set
 * (priority, matching romm_creds_configured), else HTTP Basic
 * ("Basic <base64 of user:pass>") -- queue.c/net.c only know how to send a
 * literal header, unlike http_get_authed's CURLOPT_USERPWD, so Basic is
 * built by hand here. Empty string if neither credential is set. Recommend
 * out_sz >= 400 (worst case: two 128-byte fields, base64'd). */
void romm_creds_queue_auth_header(const RommCredentials *c, char *out,
                                  size_t out_sz);

/*
 * Map a RomM platform to a HaulNX console (target) — one of the strings
 * romfs:/dl_sources.json ships in its "consoles" list, e.g. "snes", "n64",
 * "sega-32x". Tries platform->fs_slug then platform->slug against a static
 * table (RomM's slugs mostly follow IGDB's, which HaulNX's own targets
 * predate and don't always match verbatim -- e.g. RomM's "ngc"/"segacd" vs
 * HaulNX's "gc"/"sega-cd"). Returns NULL if the platform has no known HaulNX
 * console; callers must treat that as "unsupported" rather than guessing a
 * folder -- callers should log/report the platform as unsupported rather
 * than guessing one.
 */
const char *romm_map_platform_console(const RommPlatform *p);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_CLIENT_H */
