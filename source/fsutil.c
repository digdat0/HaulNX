#include "fsutil.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

bool fs_rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return !fs_exists(path); /* already gone */
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
                    continue;
                }
                char child[1024];
                snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
                fs_rm_rf(child);
            }
            closedir(d);
        }
        return rmdir(path) == 0;
    }
    return remove(path) == 0;
}

bool fs_log_rotate(const char *path, uint64_t max_bytes) {
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < 0 ||
        (uint64_t)st.st_size <= max_bytes) {
        return false;
    }
    char prev[512];
    int n = snprintf(prev, sizeof(prev), "%s.1", path);
    if (n < 0 || (size_t)n >= sizeof(prev)) {
        return false;
    }
    /* FAT rename won't overwrite an existing target, so clear it first. If the
     * rename still fails, drop the log outright — capping growth matters more
     * than keeping a generation we can't move. */
    remove(prev);
    if (rename(path, prev) != 0) {
        remove(path);
    }
    return true;
}

uint64_t fs_free_bytes(const char *path) {
    struct statvfs st;
    if (statvfs(path, &st) != 0) {
        return UINT64_MAX; /* unknown: don't block downloads */
    }
    uint64_t bs = st.f_frsize ? st.f_frsize : st.f_bsize;
    return bs * (uint64_t)st.f_bavail;
}

uint64_t fs_total_bytes(const char *path) {
    struct statvfs st;
    if (statvfs(path, &st) != 0) {
        return UINT64_MAX;
    }
    uint64_t bs = st.f_frsize ? st.f_frsize : st.f_bsize;
    return bs * (uint64_t)st.f_blocks;
}

bool fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool fs_mkdir_p(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
    /* Matches EX_PATH_MAX: an archive entry can nest deeply enough that a 1KB
     * buffer refused the directory, which then read as a write failure. */
    char buf[2048];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, path, n + 1);

    /* Walk components, skipping the drive prefix "sdmc:". */
    char *p = buf;
    char *colon = strchr(buf, ':');
    if (colon) {
        p = colon + 1;
    }
    if (*p == '/') {
        p++;
    }

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (buf[0] && !fs_exists(buf)) {
                mkdir(buf, 0777);
            }
            *p = '/';
        }
    }
    if (!fs_exists(buf)) {
        mkdir(buf, 0777);
    }
    return fs_exists(buf);
}

bool fs_ensure_parent(const char *file_path) {
    /* Sized with fs_mkdir_p: callers pass paths built from a library root plus
     * a sanitized remote name, which comfortably outgrew 1KB. */
    char buf[2048];
    size_t n = strlen(file_path);
    if (n >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, file_path, n + 1);
    char *slash = strrchr(buf, '/');
    if (!slash) {
        return true; /* no directory component */
    }
    *slash = '\0';
    return fs_mkdir_p(buf);
}

#define COPY_CHUNK (64 * 1024)

bool fs_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    /* Heap, not stack and not static. 64KB is too much to put on a thread stack,
     * and the `static` an earlier copy of this loop used made it non-reentrant —
     * two concurrent copies would have interleaved through one buffer, which
     * matters because one caller is copying the app's own .nro over itself. */
    char *buf = (char *)malloc(COPY_CHUNK);
    bool ok = buf != NULL;
    if (ok) {
        size_t r;
        while ((r = fread(buf, 1, COPY_CHUNK, in)) > 0) {
            if (fwrite(buf, 1, r, out) != r) {
                ok = false;
                break;
            }
        }
        if (ferror(in)) {
            ok = false; /* short read: dst would be a silently truncated copy */
        }
    }
    free(buf);
    fclose(in);
    /* stdio defers writes, so a full card usually surfaces here rather than at
     * the fwrite above. */
    if (fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        remove(dst);
    }
    return ok;
}

bool fs_move(const char *src, const char *dst) {
    fs_ensure_parent(dst);
    if (fs_exists(dst)) {
        remove(dst);
    }
    if (rename(src, dst) == 0) {
        return true;
    }
    /* Cross-device or rename failure: copy then unlink. */
    if (fs_copy_file(src, dst)) {
        remove(src);
        return true;
    }
    return false;
}
