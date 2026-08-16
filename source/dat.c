/*
 * Logiqx-format DAT parsing for ROM verification. A DAT is the XML catalog a
 * project like No-Intro or Redump publishes: one <rom> element per known-good
 * dump, carrying its size and CRC32/MD5/SHA-1. We stream it through libexpat
 * (already linked for the app) into a CRC-sorted table and match library files
 * against it. Only the subset we need is read — size, crc, sha1 and the
 * canonical name off <rom>, plus the system name from <header>.
 */
#include "dat.h"

#include <expat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

/* ---- parse ------------------------------------------------------------- */

typedef struct {
    Dat *d;
    bool in_header;
    bool in_header_name;
    size_t sys_len;
} ParseCtx;

static const char *attr_get(const char **atts, const char *key) {
    for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], key) == 0) {
            return atts[i + 1];
        }
    }
    return NULL;
}

static bool dat_reserve(Dat *d, int need) {
    if (need <= d->cap) {
        return true;
    }
    int cap = d->cap ? d->cap * 2 : 1024;
    while (cap < need) {
        cap *= 2;
    }
    DatRom *r = (DatRom *)realloc(d->roms, (size_t)cap * sizeof(DatRom));
    if (!r) {
        return false;
    }
    d->roms = r;
    d->cap = cap;
    return true;
}

static void XMLCALL on_start(void *ud, const XML_Char *name,
                             const XML_Char **atts) {
    ParseCtx *c = (ParseCtx *)ud;
    if (strcmp(name, "rom") == 0) {
        const char *sz = attr_get(atts, "size");
        const char *crc = attr_get(atts, "crc");
        const char *nm = attr_get(atts, "name");
        /* A DAT rom without a CRC is unusable for matching — skip it. */
        if (!crc || !dat_reserve(c->d, c->d->count + 1)) {
            return;
        }
        DatRom *r = &c->d->roms[c->d->count];
        memset(r, 0, sizeof(*r));
        r->size = sz ? strtoull(sz, NULL, 10) : 0;
        r->crc = (uint32_t)strtoul(crc, NULL, 16);
        const char *sha1 = attr_get(atts, "sha1");
        if (sha1 && strlen(sha1) == 40) {
            for (int i = 0; i < 40; i++) {
                char ch = sha1[i];
                r->sha1[i] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
            }
            r->sha1[40] = '\0';
            r->have_sha1 = true;
        }
        if (nm) {
            snprintf(r->name, sizeof(r->name), "%s", nm);
        }
        c->d->count++;
        return;
    }
    if (strcmp(name, "header") == 0) {
        c->in_header = true;
    } else if (c->in_header && strcmp(name, "name") == 0) {
        c->in_header_name = true;
        c->sys_len = 0;
        c->d->system[0] = '\0';
    }
}

static void XMLCALL on_end(void *ud, const XML_Char *name) {
    ParseCtx *c = (ParseCtx *)ud;
    if (c->in_header_name && strcmp(name, "name") == 0) {
        c->in_header_name = false;
    } else if (strcmp(name, "header") == 0) {
        c->in_header = false;
    }
}

static void XMLCALL on_chars(void *ud, const XML_Char *s, int len) {
    ParseCtx *c = (ParseCtx *)ud;
    if (!c->in_header_name) {
        return;
    }
    size_t room = sizeof(c->d->system) - 1 - c->sys_len;
    if (room == 0) {
        return;
    }
    size_t take = (size_t)len < room ? (size_t)len : room;
    memcpy(c->d->system + c->sys_len, s, take);
    c->sys_len += take;
    c->d->system[c->sys_len] = '\0';
}

static int cmp_crc(const void *a, const void *b) {
    uint32_t ca = ((const DatRom *)a)->crc, cb = ((const DatRom *)b)->crc;
    return (ca > cb) - (ca < cb);
}

bool dat_load(Dat *d, const char *path) {
    memset(d, 0, sizeof(*d));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    XML_Parser p = XML_ParserCreate(NULL);
    if (!p) {
        fclose(f);
        return false;
    }
    ParseCtx ctx = {d, false, false, 0};
    XML_SetUserData(p, &ctx);
    XML_SetElementHandler(p, on_start, on_end);
    XML_SetCharacterDataHandler(p, on_chars);

    const size_t BUF = 64 * 1024;
    char *buf = (char *)malloc(BUF);
    bool ok = buf != NULL;
    while (ok) {
        size_t r = fread(buf, 1, BUF, f);
        bool final = (r < BUF) && (feof(f) || ferror(f));
        if (XML_Parse(p, buf, (int)r, final) == XML_STATUS_ERROR) {
            ok = false;
            break;
        }
        if (final) {
            break;
        }
    }
    if (ok && ferror(f)) {
        ok = false;
    }
    free(buf);
    XML_ParserFree(p);
    fclose(f);

    if (!ok || d->count == 0) {
        dat_free(d);
        return false;
    }
    qsort(d->roms, (size_t)d->count, sizeof(DatRom), cmp_crc);
    return true;
}

void dat_free(Dat *d) {
    free(d->roms);
    d->roms = NULL;
    d->count = 0;
    d->cap = 0;
    d->system[0] = '\0';
}

/* ---- lookup ------------------------------------------------------------ */

DatMatch dat_lookup(const Dat *d, uint32_t crc, uint64_t size,
                    const char *sha1_hex, const char *filename,
                    char *out_name, size_t out_sz) {
    /* Binary-search the first rom with this CRC, then walk the equal-CRC run
     * (collisions are rare but possible) checking size and SHA-1. */
    int lo = 0, hi = d->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (d->roms[mid].crc < crc) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (int i = lo; i < d->count && d->roms[i].crc == crc; i++) {
        const DatRom *r = &d->roms[i];
        if (r->size != size) {
            continue;
        }
        /* CRC + size already pin the dump; SHA-1 (when the DAT carries it) is
         * the tamper/collision check. A match on all is verified; a SHA-1
         * mismatch despite CRC+size is a corrupt or doctored file. */
        if (!r->have_sha1 || (sha1_hex && strcmp(r->sha1, sha1_hex) == 0)) {
            if (out_name && out_sz) {
                snprintf(out_name, out_sz, "%s", r->name);
            }
            return DAT_VERIFIED;
        }
        return DAT_BAD;
    }
    /* No hash match. If a dump is *named* like this file, the file is a
     * corrupt/edited copy of a known ROM rather than something unrecognized. */
    if (filename && *filename) {
        for (int i = 0; i < d->count; i++) {
            if (strcasecmp(d->roms[i].name, filename) == 0) {
                return DAT_BAD;
            }
        }
    }
    return DAT_UNKNOWN;
}

/* Find the first rom with this CRC (binary search over the CRC-sorted table). */
static int dat_first_crc(const Dat *d, uint32_t crc) {
    int lo = 0, hi = d->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (d->roms[mid].crc < crc) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

bool dat_contains(const Dat *d, uint32_t crc, uint64_t size,
                  const char *sha1_hex) {
    /* The CRC is computed over the actual ROM bytes in both the raw and the
     * inside-a-zip case, so it always keys the search; only the size differs for
     * a zipped ROM, which is why a SHA-1 match is allowed to ignore it. */
    for (int i = dat_first_crc(d, crc); i < d->count && d->roms[i].crc == crc;
         i++) {
        const DatRom *r = &d->roms[i];
        if (r->have_sha1) {
            if (sha1_hex && strcmp(r->sha1, sha1_hex) == 0) {
                return true;
            }
        } else if (r->size == size) {
            return true; /* no SHA-1 to confirm with; trust CRC + size */
        }
    }
    return false;
}
