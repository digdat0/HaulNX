#ifndef HASHX_H
#define HASHX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC32 (IEEE/zlib) + SHA-1 of a file, computed together in a single streaming
 * pass so DAT verification reads each file off the SD card only once. CRC32 is
 * the cheap first-pass match against a No-Intro/Redump DAT; SHA-1 confirms it. */
typedef struct {
    uint32_t crc;        /* CRC32/IEEE, matching the DAT's <rom crc="..."> */
    char sha1_hex[41];   /* lowercase hex SHA-1 (40 chars + NUL) */
} HashSet;

/* Progress callback: `done` bytes hashed of `total` (file size, or 0 if
 * unknown). Called once per read chunk; must not block. */
typedef void (*hash_progress_cb)(void *ud, uint64_t done, uint64_t total);

/* Compute the CRC32 and SHA-1 of a file in one pass, in its No-Intro canonical
 * form: a recognised ROM header (iNES/FDS/Lynx/7800) is stripped and a
 * byteswapped/little-endian N64 image is reordered to big-endian .z64 before
 * hashing, so a headered or wrong-order dump matches the DAT (detection is
 * magic-gated, so a plain headerless ROM is hashed verbatim). Same contract as
 * md5_file otherwise: returns false on an open/read error or on cancel, and a
 * true return means the digests cover the whole (normalised) file and nothing
 * less — a partial read never surfaces as a confident (wrong) digest.
 * `progress` and `cancel` may be NULL. */
bool hash_file(const char *path, HashSet *out, volatile bool *cancel,
               hash_progress_cb progress, void *ud);

/* Archive-aware hashing, the entry point both the verify pass and the dedupe
 * pass use so their shared hash cache stays coherent. For a single-file
 * .zip/.7z/.rar it hashes the ROM *inside* the archive — so a zipped good dump
 * matches the DAT, which catalogs the uncompressed ROM, not the zip container.
 * Any other file (a plain ROM, or a multi-file/unsupported/corrupt archive) is
 * hashed as-is. Same false-on-error/cancel contract as hash_file; `progress`
 * and `cancel` may be NULL. */
bool hash_rom(const char *path, HashSet *out, volatile bool *cancel,
              hash_progress_cb progress, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* HASHX_H */
