#ifndef SDUSAGE_H
#define SDUSAGE_H

#include <stdbool.h>

#include "config.h" /* DATA_DIR */

/* Where the report is written for serving: GET sd_usage.json and the USB root
 * object of the same name. */
#define SDUSAGE_JSON_PATH DATA_DIR "/sd_usage.json"

#ifdef __cplusplus
extern "C" {
#endif

/* SD card usage report for the desktop companion's Storage page: a background
 * walk that totals each scanned folder and its subfolders and keeps the
 * largest files. GET /<token>/sd_usage_scan[?p=<folders>] (or a root push of
 * sd_usage_request.txt over USB) starts one -- a no-op while one runs; GET
 * /<token>/sd_usage.json (or the USB root object) serves progress and the last
 * result. */

/* Start a scan on a background thread unless one is already running. `dirs`
 * lists the folders to walk, relative to sdmc:/ ("roms,switch" -- commas or
 * newlines between them); NULL or empty walks the whole card. */
void sdusage_request(const char *dirs);

/* Write the current state (running/progress plus the last finished result,
 * if any) to `path` as JSON. False if the file couldn't be written. */
bool sdusage_write_json(const char *path);

/* Cancel a running scan and join its thread. Safe to call when none ran. */
void sdusage_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
