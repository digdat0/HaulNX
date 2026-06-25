#ifndef EXTRACT_H
#define EXTRACT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True if the filename looks like a supported archive (by extension):
 * .zip .7z .rar .tar .tgz .tbz .tbz2 .txz .tar.gz .tar.bz2 .tar.xz */
bool is_archive_name(const char *filename);

/* Per-entry callback during extraction. Return false to cancel. */
typedef bool (*extract_cb)(void *userdata, const char *entry_name, int done);

/* Extract every regular file in `src` into `dest_dir`, preserving the archive's
 * internal directory structure. Returns the number of files extracted, or -1 if
 * `src` could not be opened as a supported archive. If `out_overwrites` is
 * non-NULL it receives how many extracted files replaced an existing file. */
int extract_archive(const char *src, const char *dest_dir, extract_cb cb,
                    void *userdata, int *out_overwrites);

#ifdef __cplusplus
}
#endif

#endif /* EXTRACT_H */
