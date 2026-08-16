/*
 * Tier 3 of the game-identification ladder (see idgame.h): when the cheap
 * extension/header tiers can't place a file, hash it and look that hash up
 * across every per-console DAT. A hash the user's DATs agree belongs to exactly
 * one system is a confident answer; a hash found in none, or in more than one,
 * is left for the user. Kept in its own translation unit so idgame.c stays a
 * pure, dependency-free header sniffer — this half needs the DAT parser and the
 * file hasher.
 *
 * The DATs are parsed once into a DatCache and reused across a whole batch of
 * files: a tidy pass deep-identifies many files, and re-parsing every DAT (a
 * streamed multi-MB XML) once per file would be O(files x DATs) parses.
 */
#include "idgame.h"

#include "dat.h"
#include "hashcache.h"
#include "hashx.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <sys/stat.h>

/* One loaded DAT plus the console target its filename stem names. */
typedef struct {
    char target[64];
    Dat  dat;
} DatCacheEnt;

struct DatCache {
    DatCacheEnt *ents;
    int          n;
};

/* Does `name` end in ".dat" (case-insensitive)? */
static bool is_dat_name(const char *name, size_t *stem_len) {
    size_t n = strlen(name);
    if (n < 4 || strcasecmp(name + n - 4, ".dat") != 0) {
        return false;
    }
    *stem_len = n - 4; /* the console target is the filename without ".dat" */
    return true;
}

DatCache *idgame_dat_cache_new(const char *dats_dir) {
    DatCache *dc = (DatCache *)calloc(1, sizeof(*dc));
    if (!dc) {
        return NULL;
    }
    DIR *d = opendir(dats_dir);
    if (!d) {
        return dc; /* no DATs installed: an empty cache resolves nothing */
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        size_t stem = 0;
        if (!is_dat_name(e->d_name, &stem) || stem == 0) {
            continue;
        }
        char full[1024];
        int n = snprintf(full, sizeof(full), "%s/%s", dats_dir, e->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) {
            continue;
        }
        Dat dat;
        if (!dat_load(&dat, full)) {
            continue;
        }
        DatCacheEnt *grown =
            (DatCacheEnt *)realloc(dc->ents, (dc->n + 1) * sizeof(*grown));
        if (!grown) {
            dat_free(&dat);
            continue;
        }
        dc->ents = grown;
        DatCacheEnt *ent = &dc->ents[dc->n++];
        ent->dat = dat;
        size_t t = stem < sizeof(ent->target) ? stem : sizeof(ent->target) - 1;
        memcpy(ent->target, e->d_name, t);
        ent->target[t] = '\0';
    }
    closedir(d);
    return dc;
}

void idgame_dat_cache_free(DatCache *dc) {
    if (!dc) {
        return;
    }
    for (int i = 0; i < dc->n; i++) {
        dat_free(&dc->ents[i].dat);
    }
    free(dc->ents);
    free(dc);
}

IdResult idgame_deep_identify(const DatCache *dc, const char *path,
                              HashCache *cache, volatile bool *cancel) {
    IdResult r;
    r.target[0] = '\0';
    r.conf = IDC_NONE;
    r.ambiguous = true;
    if (!dc || dc->n == 0) {
        return r; /* no DATs installed: nothing to deep-identify against */
    }

    struct stat st;
    uint64_t size = 0, mtime = 0;
    if (stat(path, &st) == 0) {
        size = (uint64_t)st.st_size;
        mtime = (uint64_t)st.st_mtime;
    }

    /* One read of the file (or its inner ROM, for a single-file zip). Whole-file
     * hashing is the expensive part of a scan, so reuse the shared hash cache
     * when the caller supplies one: the tidy dedup pass stores hash_rom's digest
     * under the same path+size+mtime key, so an unchanged file is hashed at most
     * once per scan (misfile + dedup no longer double-hash it) and not at all on
     * a repeat scan. cache may be NULL (falls back to hashing every call). */
    HashSet hs;
    bool cacheable = (cache && size);
    if (!(cacheable && hashcache_get(cache, path, size, mtime, &hs))) {
        if (!hash_rom(path, &hs, cancel, NULL, NULL)) {
            return r; /* unreadable, or cancelled */
        }
        if (cacheable) hashcache_put(cache, path, size, mtime, &hs);
    }

    int matches = 0;
    const char *match_target = NULL;
    for (int i = 0; i < dc->n && !(cancel && *cancel); i++) {
        if (!dat_contains(&dc->ents[i].dat, hs.crc, size, hs.sha1_hex)) {
            continue;
        }
        if (++matches > 1) {
            /* The same hash in two consoles' DATs — can't pick one; ask. */
            match_target = NULL;
            break;
        }
        match_target = dc->ents[i].target;
    }

    if (matches == 1 && match_target) {
        snprintf(r.target, sizeof(r.target), "%s", match_target);
        r.conf = IDC_CONTENT;
        r.ambiguous = false;
    }
    return r;
}
