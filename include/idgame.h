#ifndef IDGAME_H
#define IDGAME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Guess which console a game file belongs to, so a file that arrived over USB
 * or the LAN receiver can be filed into the right library folder without the
 * user picking a console by hand.
 *
 * This is the cheap-to-expensive front of the identification ladder:
 *   Tier 1  extension        free            unambiguous extensions (.nes, .sfc)
 *   Tier 2  header magic      one ~96 KB read shared extensions (.iso, .bin, ...)
 * Tier 3 (hash the whole file and look it up across the loaded DATs) is far more
 * expensive on multi-GB images and needs DAT state, so it lives in the caller
 * and is only run on what this leaves undetermined.
 *
 * The emitted target is a HaulNX console folder id ("nes", "snes", "gc", "psx",
 * ...); the caller resolves it to a configured console with config_find_console
 * and, when that fails or the result is ambiguous, asks the user. */

typedef enum {
    IDC_NONE = 0,    /* nothing matched — target is "" */
    IDC_EXT,         /* matched by extension alone */
    IDC_CONTENT,     /* matched by reading the file's header */
} IdConfidence;

typedef struct {
    char target[64];     /* console folder id, or "" when undetermined */
    IdConfidence conf;
    bool ambiguous;      /* the extension/content fits more than one console, or a
                            disc/container we can't pin down — the caller should
                            deep-identify (Tier 3) or ask, even if target is set */
} IdResult;

/* Identify the file at `path`. Never fails: an unreadable or unknown file comes
 * back {"", IDC_NONE, true}. Reads at most the first ~96 KB of the file. */
IdResult idgame_identify(const char *path);

/* True when `res` is a confident, actionable single-console answer — safe to
 * file without asking. (conf != NONE, not ambiguous, and a target is set.) */
bool idgame_is_confident(const IdResult *res);

/* Parsed DATs held in memory so a batch of deep-identify calls parses each DAT
 * once rather than re-streaming every DAT's XML for every file. Opaque: build
 * with idgame_dat_cache_new (reads every DATS_DIR/<target>.dat once), pass to
 * idgame_deep_identify, release with idgame_dat_cache_free. new() never returns
 * NULL for a missing/empty dir — an empty cache simply resolves nothing. */
typedef struct DatCache DatCache;
DatCache *idgame_dat_cache_new(const char *dats_dir);
void idgame_dat_cache_free(DatCache *dc);

/* Tier 3: the deep-identify fallback for a file that idgame_identify left
 * unresolved. Hashes the file (the inner ROM, for a single-file zip) and looks
 * that hash up across the cached DATs; when exactly one console's DAT contains
 * it, returns a confident IdResult for that console. None or more than one match
 * returns undetermined. Expensive — reads the whole file — so run it only on
 * what idgame_identify couldn't place. Pass a shared HashCache (`cache`, may be
 * NULL) to reuse/populate the same digests the tidy dedup pass caches, so an
 * unchanged file is hashed at most once per scan and skipped on repeat scans.
 * `cancel` may be NULL. Lives apart from idgame_identify because it needs DAT +
 * hashing state the cheap tiers don't. */
struct HashCache; /* fwd: full type in hashcache.h; opaque to header-only users */
IdResult idgame_deep_identify(const DatCache *dc, const char *path,
                              struct HashCache *cache, volatile bool *cancel);

#ifdef __cplusplus
}
#endif

#endif /* IDGAME_H */
