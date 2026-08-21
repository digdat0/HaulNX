#ifndef DAT_H
#define DAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One known-good ROM described by a No-Intro/Redump DAT <rom> entry. */
typedef struct {
    uint64_t size;
    uint32_t crc;
    bool have_sha1;
    char sha1[41];   /* lowercase hex; valid only when have_sha1 */
    char name[192];  /* canonical file name, e.g. "Super Mario World (USA).sfc" */
} DatRom;

/* A parsed DAT: the set of known-good dumps for one system, sorted by CRC for
 * binary-search matching. */
typedef struct {
    DatRom *roms;
    int count;
    int cap;
    char system[128]; /* <header><name>, the system this DAT describes */
    /* Indices into roms, sorted by roms[i].name (case-insensitive) once at
     * load time. Lets dat_lookup's "named like a known dump" fallback
     * binary-search instead of scanning every entry -- see dat.c. */
    int *by_name;
} Dat;

/* Outcome of classifying one library file against a loaded DAT. */
typedef enum {
    DAT_VERIFIED, /* hash matches a known-good dump */
    DAT_BAD,      /* named like a known dump but the data differs (corrupt) */
    DAT_UNKNOWN   /* nothing in the DAT matches (hack, translation, or absent) */
} DatMatch;

/* Parse a Logiqx-format .dat (XML) at `path`. Returns false if it can't be read
 * or holds no <rom> entries. On success *d owns heap memory — free with
 * dat_free. Streams the file through expat, so a large DAT is not fully
 * buffered. Safe to call on an already-freed/zeroed Dat. */
bool dat_load(Dat *d, const char *path);
void dat_free(Dat *d);

/* Classify a library file by its hashes (and name, for corrupt-file detection).
 * `filename` is the on-disk base name. When the result is DAT_VERIFIED and
 * out_name != NULL, the canonical DAT name is copied into out_name/out_sz. */
DatMatch dat_lookup(const Dat *d, uint32_t crc, uint64_t size,
                    const char *sha1_hex, const char *filename,
                    char *out_name, size_t out_sz);

/* Tier-3 deep identify: does this DAT catalog a dump with the given hash? A
 * SHA-1 match (when the DAT carries one) is decisive on its own; otherwise the
 * CRC32 must match together with `size`. Being able to skip the size check on a
 * SHA-1 match is what lets this resolve a ROM stored inside a zip, whose on-disk
 * size is the container's rather than the dump's. `sha1_hex` may be NULL. */
bool dat_contains(const Dat *d, uint32_t crc, uint64_t size,
                  const char *sha1_hex);

#ifdef __cplusplus
}
#endif

#endif /* DAT_H */
