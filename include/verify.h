#ifndef VERIFY_H
#define VERIFY_H

#include <stdbool.h>
#include <stdint.h>

#include "dat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One scanned library file and how it matched the DAT. */
typedef struct {
    char name[256];      /* path relative to the scan folder (may include subdirs) */
    char canonical[192]; /* DAT name when VERIFIED, else "" */
    DatMatch status;
    uint64_t size;
    uint64_t mtime;      /* on-disk mtime; the hash-cache freshness key with size */
} VerifyItem;

/* A verify pass over one console folder. The caller fills `folder` and `dat`,
 * spawns verify_run on a worker thread, and reads the progress fields each
 * frame to drive the UI; `cancel` aborts between/within files. The join that
 * reaps the worker publishes the final results and tallies. */
typedef struct {
    /* inputs */
    char folder[512];
    char cache_path[512]; /* persistent hash cache file; "" disables caching */
    bool force_rehash;    /* ignore cached digests and re-read every file (the
                             cache is still refreshed), to catch silent corruption
                             that leaves size+mtime unchanged */
    const Dat *dat;

    /* live progress (worker writes, UI reads) */
    volatile bool cancel;
    volatile int files_total;
    volatile int files_done;
    volatile uint64_t bytes_total;
    volatile uint64_t bytes_done;

    /* results (owned; free with verify_free) */
    VerifyItem *items;
    int count;
    int cap;
    int n_ok, n_bad, n_unknown, n_err;
    bool completed; /* finished without cancel */

    /* internal: bytes hashed by already-completed files */
    uint64_t bytes_base;
} VerifyJob;

/* Scan folder recursively, hash each file (using the persistent cache when
 * cache_path is set), and classify against dat. Blocking — run on a worker
 * thread. Item names are paths relative to `folder`. */
void verify_run(VerifyJob *j);
void verify_free(VerifyJob *j);

#ifdef __cplusplus
}
#endif

#endif /* VERIFY_H */
