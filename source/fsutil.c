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
    char buf[1024];
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
    char buf[1024];
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

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[64 * 1024];
    size_t r;
    bool ok = true;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
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
    if (copy_file(src, dst)) {
        remove(src);
        return true;
    }
    return false;
}
