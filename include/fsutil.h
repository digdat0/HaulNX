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

/* Copy src over dst, removing a partial dst if anything goes wrong. The caller
 * ensures dst's parent exists (fs_move does this before calling). Reentrant: the
 * transfer buffer is heap, so two threads may copy at once. */
bool fs_copy_file(const char *src, const char *dst);

/* true if path exists. */
bool fs_exists(const char *path);

/* Recursively delete a file or directory (rm -rf). Returns true if the path is
 * gone afterwards. */
bool fs_rm_rf(const char *path);

/* Keep an append-only log from growing without bound. Once `path` is larger
 * than max_bytes it is moved aside as "<path>.1" (replacing any previous one)
 * and the live file starts empty, so at most two generations are ever on the
 * card. Returns true if a rotation happened. Costs one stat, so a per-request
 * logger should sample rather than call it on every line. */
bool fs_log_rotate(const char *path, uint64_t max_bytes);

/* Size ceilings for the app's logs, applied by their writers. Diagnostics churn
 * fast and are disposable; the download history is something the user reads (and
 * re-downloads from), so it gets a lot more room before the oldest is dropped. */
#define LOG_ROTATE_DEBUG   (1024ull * 1024)      /* debug.log     */
#define LOG_ROTATE_XFER    (256ull * 1024)       /* transfers.log */
#define LOG_ROTATE_HISTORY (4ull * 1024 * 1024)  /* downloads.log / .jsonl */

#ifdef __cplusplus
}
#endif

#endif /* FSUTIL_H */
