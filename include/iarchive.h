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
} ArchiveFile;

typedef struct {
    char identifier[256];
    char server[256];        /* preferred download host from metadata */
    char dir[512];           /* item directory path on that host */
    char download_base[512]; /* optional override, e.g. from a configured source */
    ArchiveFile *files;
    int file_count;
} ArchiveItem;

/* One hit from an archive.org catalogue search (advancedsearch API). */
typedef struct {
    char identifier[256]; /* the item id — becomes a repo's id when added */
    char title[256];      /* human title, "" if the item has none */
    char mediatype[32];   /* "software", "collection", ... */
    uint64_t downloads;   /* lifetime download count, for ranking/among-hits */
} ArchiveSearchItem;

/*
 * Search archive.org for items matching `query`. Writes up to `max` hits into
 * the caller-provided `out` array and returns the number written, or -1 on a
 * transport/parse error. Results come back sorted by download count (most
 * popular first). conn == NULL uses the shared handle; pass a caller-owned
 * connection (net_conn_new) to run off the shared one.
 */
int ia_search(void *conn, const char *query, ArchiveSearchItem *out, int max);

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

/* Build the canonical download URL for a file in the item. */
void ia_file_url(const ArchiveItem *item, const ArchiveFile *file,
                 char *out, size_t out_sz);

/*
 * If item->download_base points into a subfolder of this same archive.org
 * item (e.g. ".../download/<identifier>/Neo-Geo%20CD/Games") this decodes and
 * writes that subfolder path (percent-decoded, no leading/trailing slash)
 * into out and returns true. Returns false (out left empty) for a bare item
 * root, a custom mirror host, or an item-relative path that doesn't actually
 * start under item->identifier. Used to scope the browsable file list to a
 * repo's configured subfolder, matching what ia_file_url() downloads.
 */
bool ia_item_subfolder(const ArchiveItem *item, char *out, size_t out_sz);

void ia_free(ArchiveItem *item);

#ifdef __cplusplus
}
#endif

#endif /* IARCHIVE_H */
