#ifndef BOXART_H
#define BOXART_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Local-library cover art via SteamGridDB: fuzzy-search a game's display
 * title, download its top-ranked grid image, and cache the outcome (hit or
 * miss) on disk so a title already resolved -- found or not -- is never
 * re-queried by a later scan. Entirely opt-in: nothing here makes a network
 * call without a user-supplied API key from Settings -> Account.
 */

/* Load the persisted title -> art (or miss) index into memory. Safe to call
 * more than once (a no-op after the first successful call); boxart_lookup
 * and boxart_fetch call it themselves, so most callers never need to. */
void boxart_index_load(void);

/* Write the in-memory index back to BOXART_INDEX_PATH if it changed since
 * load. Call this once after a batch of boxart_fetch calls (a scan), not
 * after each one -- an SD card write per title would make a large library
 * scan needlessly slow. */
void boxart_index_save(void);

/* Local-only lookup: true if `title` has a cached hit, with path_out filled
 * with the PNG's full path. False for a cached miss or an unresolved title.
 * No network I/O -- cheap enough to call while building a UI list. */
bool boxart_lookup(const char *title, char *path_out, size_t path_sz);

/* Resolve art for `title` if it isn't already cached (hit or miss), via a
 * SteamGridDB fuzzy name search followed by a grid-image lookup, and
 * download the result into BOXART_DIR. `key` is the user's SteamGridDB API
 * key; NULL/empty returns false without touching the index (not recorded as
 * a miss -- there was no key to search with). A title already cached (either
 * way) returns immediately without a network call. Returns true and fills
 * path_out on a hit. `score_out` (may be NULL) is filled with the winning
 * candidate's name-match score (0-100, see ba_name_score in boxart.c) only
 * when this call actually performed a fresh search and found something --
 * left untouched on a cache hit/miss or a network miss, so a caller can tell
 * "just resolved, here's how confident the match is" apart from "already
 * had an answer, no fresh score available" (initialize it to -1 yourself and
 * treat -1 as "no confidence data"). Blocking network I/O -- call off the UI
 * thread. */
bool boxart_fetch(const char *key, const char *title, char *path_out,
                  size_t path_sz, int *score_out);

/* Delete a title's cached cover (if any) -- both the PNG under BOXART_DIR and
 * its index entry, hit or miss -- and persist the index immediately (unlike
 * boxart_fetch, this is one interactive delete, not a batch pass, so it isn't
 * worth risking on a save the caller might forget). After this, the title is
 * unresolved again: a future scan will re-query it rather than reusing the
 * old answer. Returns false if `title` had no index entry to remove. Callers
 * that also hold a runtime texture for this title (the UI's icon cache) must
 * drop it themselves -- this only touches disk. */
bool boxart_forget(const char *title);

/* Resolve art for `title` using an explicit, user-typed search string instead
 * of the title itself -- the automatic title-based search (boxart_fetch)
 * either missed or matched the wrong game, so the user is overriding it by
 * hand. Unlike boxart_fetch this always makes a fresh SteamGridDB query and
 * overwrites whatever the index already has for `title` (hit or miss, right
 * or wrong) with the new result -- an explicit interactive search always
 * wins over a stale scan answer, so there is no "already cached" short
 * circuit here. Same key/blocking/opt-in contract as boxart_fetch otherwise;
 * a real "no match" response is recorded as a miss same as there. Same
 * `score_out` contract as boxart_fetch too -- always a fresh search here, so
 * it's filled on every hit. */
bool boxart_fetch_query(const char *key, const char *title, const char *query,
                        char *path_out, size_t path_sz, int *score_out);

/* Resolve art for `title` if it isn't already cached (hit or miss), same
 * "already cached short-circuits, no network call" contract as boxart_fetch
 * -- but searching `query` instead of `title` itself. For callers whose
 * cache key isn't a valid search string on its own, e.g. a console-art scan
 * keying its entries "console:<target>" while still needing to search the
 * console's real display name. Unlike boxart_fetch_query this never forces a
 * re-query, so a bulk pass can skip entries already resolved on an earlier
 * one. Same key/blocking/opt-in contract as boxart_fetch otherwise, `score_out`
 * included (untouched on the "already cached" short circuit, same as
 * boxart_fetch). */
bool boxart_fetch_titled(const char *key, const char *title, const char *query,
                         char *path_out, size_t path_sz, int *score_out);

/* One SteamGridDB grid-image option for the art picker: `thumb` is a light
 * preview worth downloading up front to show the user, `url` is the full-res
 * image, only fetched once they actually pick this one. Either may be empty
 * if SteamGridDB didn't provide it for this entry, but not both. */
typedef struct {
    char url[512];
    char thumb[512];
    int width, height; // 0 if SteamGridDB didn't report a size for this grid
} BoxArtCandidate;

/* Cap on how many candidates boxart_list_candidates will ever fill -- plenty
 * for a picker grid, and keeps the caller's array on the stack. */
#define BOXART_MAX_CANDIDATES 24

/* List up to `max_out` grid-image candidates for `query`'s best game match
 * (same single search->game resolution as boxart_fetch/boxart_fetch_query --
 * this only widens the second step, game->image, from "take the top one" to
 * "show the options". Pass the library title for the normal case, or a
 * user-typed override the same way boxart_fetch_query's `query` works).
 * Purely a listing: no on-disk index write, no image download. Returns the
 * count filled (0 on no game match / no images / error). `key` is the user's
 * SteamGridDB API key; NULL/empty returns 0. Blocking network I/O -- call
 * off the UI thread. */
int boxart_list_candidates(const char *key, const char *query,
                           BoxArtCandidate *out, int max_out);

/* Download candidate `c`'s thumb into BOXART_TMP_DIR under `slot` (0-based,
 * < BOXART_MAX_CANDIDATES) for the picker to preview -- overwrites whatever
 * was at that slot before, and falls back to `c->url` if `c->thumb` is
 * empty. Not part of the resolved-title index; see BOXART_TMP_DIR. Returns
 * true and fills path_out on success. Blocking network I/O -- call off the
 * UI thread. */
bool boxart_fetch_thumb(const BoxArtCandidate *c, int slot, char *path_out,
                        size_t path_sz);

/* Download candidate `c`'s full image (as chosen from a boxart_list_candidates
 * picker) and record it as `title`'s resolved cover -- same "always
 * overwrites" contract as boxart_fetch_query, since this is an explicit user
 * pick overriding whatever the index already had. Returns true and fills
 * path_out on success. Blocking network I/O -- call off the UI thread. */
bool boxart_fetch_candidate(const char *key, const char *title,
                            const BoxArtCandidate *c, char *path_out,
                            size_t path_sz);

/* Register `data` (`len` bytes, PNG or JPEG -- the loader sniffs content, not
 * the extension, same as every other cached cover) as `target`'s console-art
 * image: writes it into BOXART_DIR under the "console:<target>" cache key,
 * force-overwriting whatever was cached for that console before (hit or miss),
 * same "always wins" contract boxart_fetch_query has for a game. For the
 * desktop companion's Console Art push -- the image already arrived as bytes
 * in memory (an HTTP POST body), not as a file on disk to copy. Does NOT flip
 * ConsoleGroup::use_boxart on -- same split boxart_fetch/ConsoleArtMenu keep,
 * so the caller decides that and persists config, and must also drop the
 * runtime icon-texture cache (this only touches disk, see boxart_forget's
 * note). Returns true and fills path_out on success. */
bool boxart_set_console_art(const char *target, const void *data, size_t len,
                            char *path_out, size_t path_sz);

/* Delete every cached cover -- every PNG under BOXART_DIR this index knows
 * about -- and the index itself, hits and misses alike, in one pass. This is
 * the deliberate nuke-everything action behind Data Files > Art Cache's
 * "Clear all"; the caller is expected to confirm before calling it, the same
 * way it would before calling boxart_forget in a loop. Runtime UI textures
 * aren't this file's concern -- see boxart_forget's note above. */
void boxart_clear_all(void);

/* Delete every cached console-art entry ("console:<target>" keys only, see
 * ba_is_console_key) -- both their PNGs under BOXART_DIR and their index
 * entries, hits and misses alike, leaving every other title (games)
 * untouched. This is the bulk "Reset All to Default" behind Tools > Scan
 * Console Art: after this, every console falls back to its built-in icon
 * until re-scanned, and a future scan re-queries all of them rather than
 * reusing any old answer. Caller is expected to confirm first, same as
 * boxart_clear_all, and to also clear ConsoleGroup::use_boxart / the
 * runtime icon-texture cache for every console -- this only touches disk. */
void boxart_clear_consoles(void);

/* Same as boxart_clear_consoles, mirrored: deletes every cached GAME cover
 * (any key that ISN'T "console:<target>") and leaves console art untouched.
 * This is the bulk "Reset All to Default" behind Tools > Scan Box Art
 * (games). Caller must confirm first, same as boxart_clear_consoles. */
void boxart_clear_games(void);

#ifdef __cplusplus
}
#endif

#endif /* BOXART_H */
