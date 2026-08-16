#ifndef HASHCACHE_H
#define HASHCACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "hashx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One cached digest for a library file, keyed by its full path. size+mtime are
 * the freshness stamp: a hit is only trusted when both still match on disk. */
typedef struct {
    char path[768];    /* full "sdmc:/..." path */
    uint64_t size;
    uint64_t mtime;
    uint32_t crc;
    char sha1_hex[41];
} HashCacheEntry;

/* Persistent map of file path -> CRC/SHA-1, backing the verify pass so an
 * unchanged file is never re-hashed. Loaded from and saved to a flat TSV file.
 * Owns heap memory — free with hashcache_free. */
typedef struct HashCache {
    HashCacheEntry *e;
    int count, cap;
    int sorted;  /* entries [0,sorted) are path-sorted (binary-searchable); the
                    tail is freshly appended misses, scanned linearly so a
                    digest stored earlier in the same pass is still found */
    bool dirty;
} HashCache;

/* Load the TSV cache at `path` into *c. A missing or malformed file yields an
 * empty cache rather than an error — the cache is an optimization, never a
 * correctness dependency. Safe on a zeroed HashCache. */
void hashcache_load(HashCache *c, const char *path);

/* On a fresh hit (an entry for `path` whose size and mtime both match), copy its
 * digests into *out and return true; otherwise return false. */
bool hashcache_get(HashCache *c, const char *path, uint64_t size,
                   uint64_t mtime, HashSet *out);

/* Record (or replace) the digests for path/size/mtime and mark the cache dirty. */
void hashcache_put(HashCache *c, const char *path, uint64_t size,
                   uint64_t mtime, const HashSet *hs);

/* Write the cache back to `path`, but only if it changed since load. */
void hashcache_save(HashCache *c, const char *path);

void hashcache_free(HashCache *c);

#ifdef __cplusplus
}
#endif

#endif /* HASHCACHE_H */
