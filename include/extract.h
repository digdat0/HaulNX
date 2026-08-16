#ifndef EXTRACT_H
#define EXTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True if the filename looks like a supported archive (by extension):
 * .zip .7z .rar .tar .tgz .tbz .tbz2 .txz .tar.gz .tar.bz2 .tar.xz */
bool is_archive_name(const char *filename);

/* Sanitize an archive entry path into a safe relative SD path: drop leading
 * slashes, normalize backslashes, replace FAT-illegal chars, keep '/' as the
 * separator, and refuse ".." path-traversal segments. Returns false if the
 * entry should be skipped entirely. Shared with source/rar3/ so the fallback
 * RAR3 decoder (see rar3.h) applies the exact same path safety libarchive
 * extraction does, rather than a second hand-rolled copy. */
bool sanitize_rel(const char *in, char *out, size_t out_sz);

/* Progress callback during extraction: called per data block and per finished
 * entry. `done` = entries completed so far; `bytes_read` = raw bytes consumed
 * from the archive file so far (compare against the archive's file size for a
 * percentage — meaningful even mid-way through one huge entry). Return false
 * to cancel. */
typedef bool (*extract_cb)(void *userdata, const char *entry_name, int done,
                           uint64_t bytes_read);

/* Extract every regular file in `src` into `dest_dir`, preserving the archive's
 * internal directory structure. Returns the number of files extracted, or -1 if
 * `src` could not be opened as a supported archive OR a file could not be
 * written to the SD card (extraction incomplete — do not treat as success).
 * If `out_overwrites` is non-NULL it receives how many extracted files
 * replaced an existing file. */
int extract_archive(const char *src, const char *dest_dir, extract_cb cb,
                    void *userdata, int *out_overwrites);

/* Runtime extraction knobs for on-device A/B benchmarking. The app sets these
 * from Prefs at startup and whenever a toggle changes; extraction snapshots the
 * current values at the start of each archive. The defaults reproduce the
 * shipped behavior, so an app that never calls the setter is unaffected. */
typedef struct {
    bool bench;     /* append a per-archive throughput line to the bench log */
    bool prealloc;  /* grow each output file to its final size up front */
    int  chunk_mb;  /* write-coalescing chunk size in MB: 1, 2, or 4 */
} ExtractTunables;

void extract_set_tunables(const ExtractTunables *t);

#ifdef __cplusplus
}
#endif

#endif /* EXTRACT_H */
