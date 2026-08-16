/*
 * Glue between HaulNX's extraction pipeline (extract.h) and the ported
 * unarr RAR3 decoder in this directory. This file is HaulNX's own code, not
 * ported from unarr -- it only calls unarr's public API (unarr.h). See
 * rar3.h for when this is used and why.
 */
#include "rar3.h"
#include "unarr.h"

#include "fsutil.h"
#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Unsampled, unlike extract.c's ex_log: this path only runs (a) once per
 * archive libarchive already failed on, and (b) at most a few times within
 * that archive, so the per-block volume ex_log guards against doesn't apply
 * here. */
static void r3_log(const char *fmt, ...) {
    fs_mkdir_p(LOGS_DIR);
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

#define R3_CHUNK (256 * 1024)

int rar3_extract(const char *src, const char *dest_dir, extract_cb cb,
                 void *userdata, int *out_overwrites) {
    if (out_overwrites) {
        *out_overwrites = 0;
    }

    ar_stream *stream = ar_open_file(src);
    if (!stream) {
        return -1;
    }
    ar_archive *ar = ar_open_rar_archive(stream);
    if (!ar) {
        /* Not a RAR archive this build can even open at all (RAR5, an
         * encrypted archive, an ancient/SFX variant) -- distinct from the
         * per-entry filter case this fallback exists for, but the caller's
         * contract is the same either way: -1, fall back to raw-save. */
        ar_close(stream);
        return -1;
    }

    char *buf = malloc(R3_CHUNK);
    if (!buf) {
        ar_close_archive(ar);
        ar_close(stream);
        return -1;
    }

    int count = 0;
    int overwrites = 0;
    bool ok = true;

    while (ar_parse_entry(ar)) {
        const char *name = ar_entry_get_name(ar);
        if (!name) {
            r3_log("rar3: entry with no name, aborting");
            ok = false;
            break;
        }

        char rel[1024];
        if (!sanitize_rel(name, rel, sizeof(rel))) {
            continue; /* traversal or empty name: skip this entry, not fatal */
        }

        char out[2048];
        int on = snprintf(out, sizeof(out), "%s/%s", dest_dir, rel);
        if (on < 0 || (size_t)on >= sizeof(out)) {
            r3_log("rar3: path too long, skipped '%s'", rel);
            continue;
        }

        if (!fs_ensure_parent(out)) {
            r3_log("rar3: couldn't create parent dir for '%s'", out);
            ok = false;
            break;
        }

        bool existed = fs_exists(out);
        FILE *f = fopen(out, "wb");
        if (!f) {
            r3_log("rar3: cannot write %s", out);
            ok = false;
            break;
        }
        if (existed) {
            overwrites++;
        }

        size_t remaining = ar_entry_get_size(ar);
        bool entry_ok = true;
        while (remaining > 0) {
            size_t want = remaining < (size_t)R3_CHUNK ? remaining : (size_t)R3_CHUNK;
            if (!ar_entry_uncompress(ar, buf, want)) {
                r3_log("rar3: decode failed for '%s'", rel);
                entry_ok = false;
                break;
            }
            if (fwrite(buf, 1, want, f) != want) {
                r3_log("rar3: write failed for '%s'", out);
                entry_ok = false;
                break;
            }
            remaining -= want;

            if (cb && !cb(userdata, rel, count, (uint64_t)ar_tell(stream))) {
                entry_ok = false; /* cancelled */
                break;
            }
        }
        fclose(f);

        if (!entry_ok) {
            /* fopen(..., "wb") above already created (or truncated) `out`, so a
             * decode/write failure mid-entry leaves a partial file on disk --
             * 0 bytes if it failed on the very first chunk. extract_archive()
             * removes its own half-written entry on failure (extract.c); do the
             * same here so the caller's "extraction incomplete, raw archive
             * kept" fallback doesn't also leave a bogus placeholder file next
             * to it for the user to trip over. */
            remove(out);
            ok = false;
            break;
        }
        count++;
    }

    /* ar_parse_entry() returning false without reaching a clean EOF means a
     * real parse/decode error on some entry -- most commonly, here, one
     * using a filter program this build doesn't recognize (see
     * filter-rar.c). Files already extracted before that point are left in
     * place, same as extract_archive()'s own documented contract: the
     * caller treats any non-positive return as "extraction incomplete,"
     * regardless of partial output already on disk. */
    if (ok && !ar_at_eof(ar)) {
        ok = false;
    }

    free(buf);
    ar_close_archive(ar);
    ar_close(stream);

    if (out_overwrites) {
        *out_overwrites = overwrites;
    }
    return ok ? count : -1;
}
