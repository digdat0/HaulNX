/*
 * Persistent verify-status cache: remembers each library file's last DAT
 * verification result (verified/bad/unknown) keyed by its path plus a
 * (size,mtime) freshness stamp, so the Installed/Library browser can show a
 * lasting badge on a file long after the VerifyJob that produced it is gone.
 * Same on-disk shape and lifecycle contract as hashcache.c — see that file's
 * header for the rationale this mirrors. A missing or corrupt cache file just
 * means no badges show up, never a wrong one.
 *
 * On-disk form is one tab-separated line per entry:
 *   <mtime>\t<size>\t<status>\t<path>\n
 * The path is last (and may contain spaces) so the fixed fields parse cleanly.
 */
#include "vfystatus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool vc_reserve(VfyStatusCache *c, int need) {
    if (need <= c->cap) {
        return true;
    }
    int cap = c->cap ? c->cap * 2 : 256;
    while (cap < need) {
        cap *= 2;
    }
    VfyStatusEntry *e = (VfyStatusEntry *)realloc(c->e, (size_t)cap * sizeof(*e));
    if (!e) {
        return false;
    }
    c->e = e;
    c->cap = cap;
    return true;
}

static int vc_cmp(const void *a, const void *b) {
    return strcmp(((const VfyStatusEntry *)a)->path,
                  ((const VfyStatusEntry *)b)->path);
}

/* Same binary-search-then-tail-scan shape as hashcache.c's hc_find — see there
 * for why the unsorted tail matters (a verify pass that re-checks a path it
 * already wrote this run must find its own fresh entry, not miss and append a
 * duplicate row). */
static VfyStatusEntry *vc_find(VfyStatusCache *c, const char *path) {
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

void vfystatus_load(VfyStatusCache *c, const char *path) {
    memset(c, 0, sizeof(*c));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long mtime, size;
        int status;
        char p[768];
        if (sscanf(line, "%llu\t%llu\t%d\t%767[^\r\n]", &mtime, &size, &status,
                   p) != 4) {
            continue; /* skip malformed lines rather than fail the whole cache */
        }
        if (status < 0 || status > 2 || !vc_reserve(c, c->count + 1)) {
            continue;
        }
        VfyStatusEntry *e = &c->e[c->count];
        snprintf(e->path, sizeof(e->path), "%s", p);
        e->size = (uint64_t)size;
        e->mtime = (uint64_t)mtime;
        e->status = status;
        c->count++;
    }
    fclose(f);
    if (c->count > 1) {
        qsort(c->e, (size_t)c->count, sizeof(*c->e), vc_cmp);
    }
    c->sorted = c->count;
}

bool vfystatus_get(VfyStatusCache *c, const char *path, uint64_t size,
                   uint64_t mtime, DatMatch *out) {
    const VfyStatusEntry *e = vc_find(c, path);
    if (!e) {
        return false;
    }
    if (e->size != size || e->mtime != mtime) {
        return false; /* stale: file changed since it was last verified */
    }
    *out = (DatMatch)e->status;
    return true;
}

void vfystatus_put(VfyStatusCache *c, const char *path, uint64_t size,
                   uint64_t mtime, DatMatch status) {
    VfyStatusEntry *prev = vc_find(c, path);
    if (prev) {
        prev->size = size;
        prev->mtime = mtime;
        prev->status = (int)status;
        c->dirty = true;
        return;
    }
    if (!vc_reserve(c, c->count + 1)) {
        return;
    }
    VfyStatusEntry *e = &c->e[c->count++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->size = size;
    e->mtime = mtime;
    e->status = (int)status;
    c->dirty = true;
}

void vfystatus_save(VfyStatusCache *c, const char *path) {
    if (!c->dirty) {
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < c->count; i++) {
        const VfyStatusEntry *e = &c->e[i];
        fprintf(f, "%llu\t%llu\t%d\t%s\n", (unsigned long long)e->mtime,
                (unsigned long long)e->size, e->status, e->path);
    }
    fclose(f);
    c->dirty = false;
}

void vfystatus_free(VfyStatusCache *c) {
    free(c->e);
    c->e = NULL;
    c->count = c->cap = c->sorted = 0;
    c->dirty = false;
}
