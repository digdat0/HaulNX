#ifndef MD5_H
#define MD5_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compute the lowercase hex MD5 digest of a file. `out_hex` must hold at least
 * 33 bytes (32 hex chars + NUL). If `cancel` is non-NULL and becomes true mid-
 * hash, the function aborts and returns false. Returns false if the file can't
 * be read — including a read error partway through, which must never be allowed
 * to surface as a digest of the partial data. A true return means `out_hex` is
 * the digest of the whole file and nothing less. */
bool md5_file(const char *path, char out_hex[33], volatile bool *cancel);

#ifdef __cplusplus
}
#endif

#endif /* MD5_H */
