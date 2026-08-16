#ifndef UPDMAN_H
#define UPDMAN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One row of the shared update manifest (update_sources.json). The desktop
 * companion edits the same file, so this mirrors its entry shape. */
#define UPD_MAX      64
#define UPD_KIND_EMU 0
#define UPD_KIND_APP 1

typedef struct {
    char id[48];      /* stable slug, e.g. "retroarch" */
    char name[64];    /* display name */
    uint8_t kind;     /* UPD_KIND_EMU / UPD_KIND_APP -> which Tools section */
    char detect[128]; /* comma-separated lowercase filename substrings */
    char repo[80];    /* "owner/name" GitHub repo; empty = no update source set */
    char asset[48];   /* lowercase .nro asset hint when a release ships several */
} UpdSource;

/* Load the manifest into out[] (up to `max` rows). On first run, when the
 * on-disk file is absent, the bundled romfs default is copied to UPDSRC_PATH so
 * the desktop has a file to read/edit, then parsed. Returns the row count. */
int updman_load(UpdSource *out, int max);

/* Persist arr[0..count) back to UPDSRC_PATH (the shared source of truth).
 * Returns false on write error. */
bool updman_save(const UpdSource *arr, int count);

/* Parse a manifest out of a memory buffer (a PC push over the inventory link).
 * Returns the number of rows filled, 0 if the buffer isn't a sources manifest
 * (so the caller can tell it apart from a pushed collection). */
int updman_parse_buf(const char *buf, size_t len, UpdSource *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* UPDMAN_H */
