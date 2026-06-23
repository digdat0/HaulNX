#include "extract.h"
#include "fsutil.h"
#include "config.h"

#include <archive.h>
#include <archive_entry.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

/* Append a line to the shared debug log so extraction problems are diagnosable
 * on-device (e.g. an unsupported RAR compression method). */
static void ex_log(const char *fmt, ...) {
    fs_mkdir_p(CONFIG_DIR);
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

static bool ends_with_ci(const char *s, const char *suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcasecmp(s + ls - lf, suffix) == 0;
}

bool is_archive_name(const char *filename) {
    static const char *exts[] = {".zip",     ".7z",      ".rar",
                                 ".tar",     ".tgz",     ".tbz",
                                 ".tbz2",    ".txz",     ".tar.gz",
                                 ".tar.bz2", ".tar.xz",  NULL};
    for (int i = 0; exts[i]; i++) {
        if (ends_with_ci(filename, exts[i])) {
            return true;
        }
    }
    return false;
}

/* Sanitize an archive entry path into a safe relative SD path: drop leading
 * slashes, normalize backslashes, replace FAT-illegal chars, keep '/' as the
 * separator. Returns false if the entry should be skipped (e.g. traversal). */
static bool sanitize_rel(const char *in, char *out, size_t out_sz) {
    if (strstr(in, "..")) {
        return false; /* refuse path traversal */
    }
    while (*in == '/' || *in == '\\') {
        in++;
    }
    size_t o = 0;
    for (; *in && o + 1 < out_sz; in++) {
        char c = *in;
        if (c == '\\') {
            c = '/';
        }
        if (c != '/' &&
            (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
             c == '>' || c == '|' || (unsigned char)c < 0x20)) {
            c = '_';
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return o > 0;
}

int extract_archive(const char *src, const char *dest_dir, extract_cb cb,
                    void *userdata) {
    struct archive *a = archive_read_new();
    if (!a) {
        return -1;
    }
    archive_read_support_filter_all(a); /* gzip/bzip2/xz/lzma/zstd/lz4 */
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    archive_read_support_format_tar(a);

    if (archive_read_open_filename(a, src, 64 * 1024) != ARCHIVE_OK) {
        ex_log("extract: cannot open %s: %s", src, archive_error_string(a));
        archive_read_free(a);
        return -1; /* not a (supported) archive */
    }

    int count = 0;
    struct archive_entry *entry;
    for (;;) {
        int rc = archive_read_next_header(a, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        /* ARCHIVE_WARN is recoverable (libarchive often warns but the data is
         * still readable) - keep going. Only FAILED/FATAL stop us. */
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            ex_log("extract: header rc=%d in %s: %s", rc, src,
                   archive_error_string(a));
            break;
        }
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            continue;
        }
        const char *name = archive_entry_pathname(entry);
        if (!name) {
            continue;
        }
        char rel[1024];
        if (!sanitize_rel(name, rel, sizeof(rel))) {
            continue;
        }
        char out[1280];
        snprintf(out, sizeof(out), "%s/%s", dest_dir, rel);
        fs_ensure_parent(out);

        FILE *f = fopen(out, "wb");
        if (!f) {
            ex_log("extract: cannot write %s", out);
            continue;
        }
        const void *buff;
        size_t size;
        la_int64_t offset;
        bool write_ok = true;
        for (;;) {
            int r = archive_read_data_block(a, &buff, &size, &offset);
            if (r == ARCHIVE_EOF) {
                break;
            }
            if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
                ex_log("extract: '%s' data rc=%d: %s", rel, r,
                       archive_error_string(a));
                write_ok = false;
                break;
            }
            if (size > 0) {
                /* Honor the block's offset so sparse entries land correctly
                 * (a no-op for normal contiguous archives). */
                fseeko(f, (off_t)offset, SEEK_SET);
                if (fwrite(buff, 1, size, f) != size) {
                    write_ok = false;
                    break;
                }
            }
        }
        fclose(f);
        if (!write_ok) {
            remove(out); /* drop a partially written / undecodable file */
            continue;
        }
        count++;
        if (cb && !cb(userdata, rel, count)) {
            break; /* user cancelled */
        }
    }

    ex_log("extract: %s -> %d file(s) into %s", src, count, dest_dir);
    archive_read_free(a);
    return count;
}
