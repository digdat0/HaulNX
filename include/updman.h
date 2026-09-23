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
#define UPD_MAX      300
#define UPD_KIND_EMU 0
#define UPD_KIND_APP 1

typedef struct {
    char id[48];      /* stable slug, e.g. "retroarch" */
    char name[64];    /* display name */
    uint8_t kind;     /* UPD_KIND_EMU / UPD_KIND_APP -> which Tools section */
    char detect[128]; /* comma-separated lowercase filename substrings */
    char repo[80];    /* "owner/name" GitHub repo; empty = no update source set */
    char asset[48];   /* lowercase .nro asset hint when a release ships several */
    /* The GitHub tag we ourselves last successfully installed/updated this app
     * to (set by UmiTick, source/MainApplication.cpp). Some releases (e.g.
     * 2ship2harkinian, Shipwright) ship a NACP whose DisplayVersion is a
     * placeholder with no digits at all -- nro_file_version then can't read a
     * real version off the installed file, ever, no matter what's actually
     * installed. This is the durable fallback ground truth for that case:
     * "we personally verified and swapped in this exact tag", checked when a
     * fresh disk rescan can't read a version, instead of always assuming the
     * app is behind. Empty = never recorded (older manifest, or never
     * installed through HaulNX). */
    char installed_tag[32];
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
