/*
 * Persistent file-hash cache for DAT verification. Hashing a file means reading
 * every byte of it off the SD card; for a large library that is the whole cost
 * of a verify pass. This cache remembers each file's CRC32/SHA-1 keyed by its
 * path plus a (size,mtime) freshness stamp, so a second pass over an unchanged
 * library re-reads nothing. It is a pure optimization: a missing or corrupt
 * cache file just means a slower pass, never a wrong result — the digests are
 * always re-classified against the current DAT, never cached classifications.
 *
 * On-disk form is one tab-separated line per entry:
 *   <mtime>\t<size>\t<crc8hex>\t<sha1hex>\t<path>\n
 * The path is last (and may contain spaces) so the fixed fields parse cleanly.
 */
#include "hashcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool hc_reserve(HashCache *c, int need) {
    if (need <= c->cap) {
        return true;
    }
    int cap = c->cap ? c->cap * 2 : 256;
    while (cap < need) {
        cap *= 2;
    }
    HashCacheEntry *e = (HashCacheEntry *)realloc(c->e, (size_t)cap * sizeof(*e));
    if (!e) {
        return false;
    }
    c->e = e;
    c->cap = cap;
    return true;
}

static int hc_cmp(const void *a, const void *b) {
    return strcmp(((const HashCacheEntry *)a)->path,
                  ((const HashCacheEntry *)b)->path);
}

/* Locate `path`, or NULL. Binary-searches the sorted region, then linearly
 * scans the tail of entries appended during this pass. The tail scan is what
 * makes the cache coherent for a caller that reads back a digest it stored
 * earlier in the same run — the tidy scan does exactly that, hashing a file in
 * the misfile phase and looking it up again in the dedup phase. Without it that
 * lookup misses (re-hashing the file) and the follow-up put appends a second
 * row for the same path, so the saved TSV grows a duplicate every scan. The
 * tail is bounded by the files touched in one pass and each scan step costs a
 * strcmp against a whole-file hash, so the linear part is never the cost. */
static HashCacheEntry *hc_find(HashCache *c, const char *path) {
    int lo = 0, hi = c->sorted;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int r = strcmp(c->e[mid].path, path);
        if (r < 0) {
            lo = mid + 1;
        } else if (r > 0) {
            hi = mid;
        } else {
            return &c->e[mid];
        }
    }
    for (int i = c->sorted; i < c->count; i++) {
        if (strcmp(c->e[i].path, path) == 0) {
            return &c->e[i];
        }
    }
    return NULL;
}

void hashcache_load(HashCache *c, const char *path) {
    memset(c, 0, sizeof(*c));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long mtime, size;
        unsigned int crc;
        char sha1[64], p[768];
        /* Fixed fields, then the rest of the line (path may hold spaces) as %[. */
        if (sscanf(line, "%llu\t%llu\t%x\t%63[0-9a-fA-F]\t%767[^\r\n]", &mtime,
                   &size, &crc, sha1, p) != 5) {
            continue; /* skip malformed lines rather than fail the whole cache */
        }
        if (strlen(sha1) != 40 || !hc_reserve(c, c->count + 1)) {
            continue;
        }
        HashCacheEntry *e = &c->e[c->count];
        snprintf(e->path, sizeof(e->path), "%s", p);
        e->size = (uint64_t)size;
        e->mtime = (uint64_t)mtime;
        e->crc = (uint32_t)crc;
        memcpy(e->sha1_hex, sha1, 41);
        c->count++;
    }
    fclose(f);
    /* Sort so hashcache_get can binary-search; misses appended during a pass go
     * to the unsorted tail and aren't searched again this run. */
    if (c->count > 1) {
        qsort(c->e, (size_t)c->count, sizeof(*c->e), hc_cmp);
    }
    c->sorted = c->count;
}

bool hashcache_get(HashCache *c, const char *path, uint64_t size,
                   uint64_t mtime, HashSet *out) {
    const HashCacheEntry *e = hc_find(c, path);
    if (!e) {
        return false;
    }
    if (e->size != size || e->mtime != mtime) {
        return false; /* stale: file changed since it was hashed */
    }
    out->crc = e->crc;
    memcpy(out->sha1_hex, e->sha1_hex, 41);
    return true;
}

void hashcache_put(HashCache *c, const char *path, uint64_t size,
                   uint64_t mtime, const HashSet *hs) {
    /* Overwrite an existing entry for this path in place rather than appending a
     * second one: a put only happens on a miss (path absent, stale size/mtime,
     * or a forced re-hash), so an already-present path means a stale entry that
     * should be replaced, not duplicated. hc_find covers the pass-local tail as
     * well as the loaded region, so a path put twice in one run (a forced
     * re-hash) updates its row instead of writing a duplicate to the TSV. */
    HashCacheEntry *prev = hc_find(c, path);
    if (prev) {
        prev->size = size;
        prev->mtime = mtime;
        prev->crc = hs->crc;
        memcpy(prev->sha1_hex, hs->sha1_hex, 41);
        c->dirty = true;
        return;
    }
    if (!hc_reserve(c, c->count + 1)) {
        return;
    }
    HashCacheEntry *e = &c->e[c->count++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->size = size;
    e->mtime = mtime;
    e->crc = hs->crc;
    memcpy(e->sha1_hex, hs->sha1_hex, 41);
    c->dirty = true;
}

void hashcache_save(HashCache *c, const char *path) {
    if (!c->dirty) {
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < c->count; i++) {
        const HashCacheEntry *e = &c->e[i];
        fprintf(f, "%llu\t%llu\t%08x\t%s\t%s\n", (unsigned long long)e->mtime,
                (unsigned long long)e->size, e->crc, e->sha1_hex, e->path);
    }
    fclose(f);
    c->dirty = false;
}

int hashcache_prune(HashCache *c) {
    /* Compact in place, preserving relative order within both the sorted
     * region and the unsorted tail (a stable filter of a sorted run is still
     * sorted), so hc_find's binary-search-then-tail-scan split stays valid
     * without a re-sort. new_sorted counts survivors seen while still inside
     * the original [0,sorted) region. */
    int w = 0, new_sorted = 0;
    for (int i = 0; i < c->count; i++) {
        struct stat st;
        if (stat(c->e[i].path, &st) != 0) {
            continue; /* file's gone: drop this row */
        }
        if (w != i) {
            c->e[w] = c->e[i];
        }
        w++;
        if (i < c->sorted) {
            new_sorted = w;
        }
    }
    int removed = c->count - w;
    if (removed > 0) {
        c->count = w;
        c->sorted = new_sorted;
        c->dirty = true;
    }
    return removed;
}

void hashcache_free(HashCache *c) {
    free(c->e);
    c->e = NULL;
    c->count = c->cap = c->sorted = 0;
    c->dirty = false;
}
