/*
 * DAT verification pass: hash every file in a console folder (recursing into
 * subfolders) and classify each against a loaded DAT (verified / bad /
 * unknown). Runs on a worker thread; the UI polls the progress counters and can
 * set `cancel`. A persistent hash cache (when cache_path is set) lets a second
 * pass over an unchanged library skip re-reading files off the SD card.
 */
#include "verify.h"

#include "hashcache.h"
#include "hashx.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool vitem_reserve(VerifyJob *j, int need) {
    if (need <= j->cap) {
        return true;
    }
    int cap = j->cap ? j->cap * 2 : 128;
    while (cap < need) {
        cap *= 2;
    }
    VerifyItem *it = (VerifyItem *)realloc(j->items, (size_t)cap * sizeof(VerifyItem));
    if (!it) {
        return false;
    }
    j->items = it;
    j->cap = cap;
    return true;
}

/* Per-chunk hash progress: advance the job's byte counter so the UI bar moves
 * smoothly through a large file, not just when the file finishes. */
static void on_hash_progress(void *ud, uint64_t done, uint64_t total) {
    (void)total;
    VerifyJob *j = (VerifyJob *)ud;
    j->bytes_done = j->bytes_base + done;
}

/* Pass 1, recursive: enumerate every regular file under `dir`, recording its
 * path relative to the scan root (`rel`, "" at the top) so names stay unique and
 * point emulators at the right subfolder. Totals bytes up front so the progress
 * bar has a denominator before hashing starts. */
static void scan_dir(VerifyJob *j, const char *dir, const char *rel) {
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *de;
    char path[1024], childrel[768];
    while ((de = readdir(d)) != NULL && !j->cancel) {
        if (de->d_name[0] == '.') {
            continue; /* "." / ".." and dotfiles */
        }
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            int rn = rel[0] ? snprintf(childrel, sizeof(childrel), "%s/%s", rel,
                                       de->d_name)
                            : snprintf(childrel, sizeof(childrel), "%s",
                                       de->d_name);
            if (rn > 0 && (size_t)rn < sizeof(childrel)) {
                scan_dir(j, path, childrel); /* skip a path too deep to represent */
            }
            continue;
        }
        if (!S_ISREG(st.st_mode) || !vitem_reserve(j, j->count + 1)) {
            continue;
        }
        VerifyItem *it = &j->items[j->count];
        memset(it, 0, sizeof(*it));
        int nn = rel[0]
                     ? snprintf(it->name, sizeof(it->name), "%s/%s", rel,
                                de->d_name)
                     : snprintf(it->name, sizeof(it->name), "%s", de->d_name);
        if (nn <= 0 || (size_t)nn >= sizeof(it->name)) {
            continue; /* name too long to store; skip rather than truncate */
        }
        it->size = (uint64_t)st.st_size;
        it->mtime = (uint64_t)st.st_mtime;
        it->status = DAT_UNKNOWN;
        j->bytes_total += it->size;
        j->count++;
    }
    closedir(d);
}

void verify_run(VerifyJob *j) {
    j->items = NULL;
    j->count = j->cap = 0;
    j->files_total = j->files_done = 0;
    j->bytes_total = j->bytes_done = j->bytes_base = 0;
    j->n_ok = j->n_bad = j->n_unknown = j->n_err = 0;
    j->completed = false;

    scan_dir(j, j->folder, "");
    j->files_total = j->count;

    /* Load the persistent hash cache so unchanged files skip re-hashing. */
    HashCache cache;
    bool use_cache = j->cache_path[0] != '\0';
    if (use_cache) {
        hashcache_load(&cache, j->cache_path);
    }

    /* Pass 2: hash (or reuse a cached digest for) and classify each file. */
    char path[1024];
    for (int i = 0; i < j->count && !j->cancel; i++) {
        VerifyItem *it = &j->items[i];
        snprintf(path, sizeof(path), "%s/%s", j->folder, it->name);
        HashSet hs;
        bool hashed = use_cache && !j->force_rehash &&
                      hashcache_get(&cache, path, it->size, it->mtime, &hs);
        if (!hashed) {
            /* hash_rom peeks inside a single-file .zip/.7z/.rar so a zipped good
             * dump matches the DAT's uncompressed entry; a plain file (or a
             * multi-file/unsupported archive) is hashed as-is. */
            if (!hash_rom(path, &hs, &j->cancel, on_hash_progress, j)) {
                /* Read error, or cancelled mid-file. A cancel is not an error. */
                if (!j->cancel) {
                    it->status = DAT_UNKNOWN;
                    j->n_err++;
                }
                j->bytes_base += it->size;
                j->bytes_done = j->bytes_base;
                j->files_done = i + 1;
                continue;
            }
            if (use_cache) {
                hashcache_put(&cache, path, it->size, it->mtime, &hs);
            }
        }
        /* dat_lookup's name match is on the base name, not the relative path. */
        const char *base = strrchr(it->name, '/');
        base = base ? base + 1 : it->name;
        it->status = dat_lookup(j->dat, hs.crc, it->size, hs.sha1_hex, base,
                                it->canonical, sizeof(it->canonical));
        if (it->status == DAT_VERIFIED) {
            j->n_ok++;
        } else if (it->status == DAT_BAD) {
            j->n_bad++;
        } else {
            j->n_unknown++;
        }
        j->bytes_base += it->size;
        j->bytes_done = j->bytes_base;
        j->files_done = i + 1;
    }

    if (use_cache) {
        /* Persist even on cancel: files hashed before the cancel are still valid
         * to reuse next time. */
        hashcache_save(&cache, j->cache_path);
        hashcache_free(&cache);
    }
    j->completed = !j->cancel;
}

void verify_free(VerifyJob *j) {
    free(j->items);
    j->items = NULL;
    j->count = j->cap = 0;
}
