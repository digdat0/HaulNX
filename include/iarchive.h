#ifndef IARCHIVE_H
#define IARCHIVE_H

/* Internet Archive (archive.org) item metadata + download URLs.
 * Named "iarchive" to avoid colliding with libarchive's <archive.h>. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[512];   /* file name within the item */
    char format[128]; /* archive.org "format" field, e.g. "ZIP" */
    uint64_t size;    /* bytes, 0 if unknown */
    char md5[33];     /* expected MD5 hex from metadata, "" if unknown */
    /* Full download URL, bypassing ia_file_url's base+name construction.
     * Empty for every archive.org file (ia_fetch never sets this) -- exists
     * so a non-archive.org provider whose files don't share one item-level
     * base (e.g. RomM, where each rom's URL carries its own numeric id) can
     * still populate an ArchiveItem/ArchiveFile list and reuse the Files
     * screen built around them. Sized for a server URL (~256) plus a
     * percent-encoded filename (up to name's 512 chars, 3x worst case). */
    char url_override[2048];
    /* RomM cover-art thumbnail URL (romm_cover_url), or "" if this file has
     * no cover / isn't from RomM. Never set by ia_fetch; only
     * romm_roms_to_archive_item populates it, mirroring url_override. Kept
     * here (rather than re-derived from the since-freed RommRomList) so it
     * survives as long as the file list itself does. */
    char cover_url[600];
} ArchiveFile;

typedef struct {
    char identifier[256];
    char server[256];        /* preferred download host from metadata */
    char dir[512];           /* item directory path on that host */
    char download_base[512]; /* optional override, e.g. from a configured source */
    ArchiveFile *files;
    int file_count;
} ArchiveItem;

/*
 * Accepts a full archive.org URL (details/download/metadata form) or a bare
 * item id, and writes the identifier into out. Returns false if nothing
 * usable could be extracted.
 */
bool ia_extract_id(const char *input, char *out, size_t out_sz);

/*
 * Fetch + parse https://archive.org/metadata/<id>. Fills *item on success.
 * If use_cache is true and cache_dir/<id>.json exists, it is loaded instead of
 * hitting the network. Otherwise the metadata is downloaded and written to that
 * cache file. Pass cache_dir = NULL to disable caching entirely.
 */
bool ia_fetch(const char *identifier, ArchiveItem *item, bool use_cache,
              const char *cache_dir);

/* Like ia_fetch, but performs the network fetch on a caller-owned connection
 * (net_conn_new) instead of the shared handle, so multiple can run in parallel.
 * Cache read/write is identical. */
bool ia_fetch_on(void *conn, const char *identifier, ArchiveItem *item,
                 bool use_cache, const char *cache_dir);

/* Build the canonical download URL for a file in the item. If
 * file->url_override is set, it is copied out as-is and item is ignored --
 * see ArchiveFile.url_override. */
void ia_file_url(const ArchiveItem *item, const ArchiveFile *file,
                 char *out, size_t out_sz);

void ia_free(ArchiveItem *item);

#ifdef __cplusplus
}
#endif

#endif /* IARCHIVE_H */
