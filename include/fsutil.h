#ifndef FSUTIL_H
#define FSUTIL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Free space in bytes on the filesystem containing `path` (e.g. "sdmc:/").
 * Returns UINT64_MAX if it can't be determined (so callers don't false-block). */
uint64_t fs_free_bytes(const char *path);

/* Total size in bytes of the filesystem containing `path`.
 * Returns UINT64_MAX if it can't be determined. */
uint64_t fs_total_bytes(const char *path);

/* mkdir -p for an sdmc path (the final component is treated as a directory). */
bool fs_mkdir_p(const char *path);

/* Ensure the parent directory of a file path exists. */
bool fs_ensure_parent(const char *file_path);

/* Move/rename a file, falling back to copy+unlink across mount points. */
bool fs_move(const char *src, const char *dst);

/* true if path exists. */
bool fs_exists(const char *path);

/* Recursively delete a file or directory (rm -rf). Returns true if the path is
 * gone afterwards. */
bool fs_rm_rf(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FSUTIL_H */
