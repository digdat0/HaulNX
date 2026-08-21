#ifndef VFYSTATUS_H
#define VFYSTATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "dat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One file's last-known DAT verification result, keyed by its full path.
 * size+mtime are the freshness stamp, exactly like the hash cache: a hit is
 * only trusted when both still match on disk, so a file replaced since it was
 * last verified reports no badge rather than a stale one. */
typedef struct {
    char path[768];
    uint64_t size;
    uint64_t mtime;
    int status; /* DatMatch */
} VfyStatusEntry;

/* Persistent map of file path -> last verify result. This is what lets the
 * Installed/Library browser show a lasting verified/bad badge on a file after
 * the user leaves the Verify results screen — without it, a verify pass's
 * findings evaporate the moment VerifyJob is freed. Loaded from and saved to a
 * flat TSV file, same shape and lifecycle as HashCache (see hashcache.h): the
 * caller owns one instance per use (load, query/update as needed, save, free)
 * rather than this module holding its own state, so a screen that renders many
 * rows loads once and looks up many times instead of hitting the filesystem
 * per row. */
typedef struct VfyStatusCache {
    VfyStatusEntry *e;
    int count, cap;
    int sorted; /* entries [0,sorted) are path-sorted; see hashcache.c's hc_find
                   for why the unsorted tail exists and is scanned too */
    bool dirty;
} VfyStatusCache;

/* Load the TSV cache at `path` into *c. A missing or malformed file yields an
 * empty cache rather than an error — this is an optimization/UI nicety, never
 * a correctness dependency. Safe on a zeroed VfyStatusCache. */
void vfystatus_load(VfyStatusCache *c, const char *path);

/* On a fresh hit (an entry for `path` whose size and mtime both match), copy
 * its status into *out and return true; otherwise return false. */
bool vfystatus_get(VfyStatusCache *c, const char *path, uint64_t size,
                   uint64_t mtime, DatMatch *out);

/* Record (or replace) the status for path/size/mtime and mark the cache dirty. */
void vfystatus_put(VfyStatusCache *c, const char *path, uint64_t size,
                   uint64_t mtime, DatMatch status);

/* Write the cache back to `path`, but only if it changed since load. */
void vfystatus_save(VfyStatusCache *c, const char *path);

/* Same shape and rationale as hashcache_prune (hashcache.h): drop every entry
 * whose file no longer exists on disk. Marks the cache dirty when anything
 * was removed. Returns the number of entries dropped. */
int vfystatus_prune(VfyStatusCache *c);

void vfystatus_free(VfyStatusCache *c);

#ifdef __cplusplus
}
#endif

#endif /* VFYSTATUS_H */
