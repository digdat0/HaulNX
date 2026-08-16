/*
 * Single-pass CRC32 (IEEE/zlib) + SHA-1 file hashing for DAT verification.
 * The CRC32 is the standard reflected polynomial (0xEDB88320) that zlib, PNG
 * and the No-Intro/Redump DATs all use; SHA-1 is the standard digest. Both are
 * folded over the same read chunks so a file is read exactly once.
 *
 * Both digests come from libnx, which implements them on the ARMv8 crypto and
 * CRC32 extensions the Tegra X1's Cortex-A57 provides (the Makefile already
 * builds with -march=armv8-a+crc+crypto). That matters: a scalar C SHA-1 runs
 * at roughly SD-card speed, so hashing and reading cost about the same and a
 * multi-GB dump takes minutes. On the crypto unit SHA-1 is an order of
 * magnitude faster, which drops the CPU out of the critical path and leaves
 * the SD read as the only real cost. libnx's digest byte order is the standard
 * big-endian one, so cached digests from older builds stay valid.
 */
#include "hashx.h"

#include <switch.h>

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <sys/stat.h>
#include <unistd.h>

/* ---- ROM normalisation (No-Intro canonical form) ----------------------- */

/* No-Intro/Redump hashes are of the *canonical* dump, not whatever wrapper a
 * ROM happens to arrive in. Two classes of file don't match the DAT as-is and
 * are folded to their canonical bytes here, streaming, before hashing:
 *
 *   - Headered dumps: iNES (NES), fwNES (FDS), Atari Lynx (.lnx) and Atari 7800
 *     (.a78) carry a fixed-size header the DAT strips. Detection is by the
 *     header's own magic, so a *headerless* dump (no magic) is left untouched.
 *   - N64 byte order: No-Intro catalogs big-endian .z64; a byteswapped .v64
 *     (16-bit) or little-endian .n64 (32-bit) is reordered to .z64 first.
 *
 * RomHash buffers the first 16 bytes to sniff the format, then applies a skip
 * (header) and/or a byte-group reversal (byte order) as bytes stream through,
 * carrying a partial group across chunk boundaries so arbitrary read/block
 * sizes work. */
typedef struct {
    uint32_t crc;
    Sha1Context sc;
    int  skip;              /* header bytes still to drop from the front */
    int  swap;              /* byte-group reversal width: 0 (none), 2 or 4 */
    unsigned char carry[4]; /* partial swap group straddling a feed */
    int  ncarry;
    unsigned char sniff[16];
    int  nsniff;
    bool detected;
} RomHash;

static void romhash_init(RomHash *h) {
    h->crc = 0;
    sha1ContextCreate(&h->sc);
    h->skip = 0;
    h->swap = 0;
    h->ncarry = 0;
    h->nsniff = 0;
    h->detected = false;
}

/* Both helpers chain across calls: crc32CalculateWithSeed seeds from the
 * running value exactly as the old table loop did, and the SHA-1 context
 * buffers any partial 64-byte block, so feeds of arbitrary size are fine. */
static void rom_fold(RomHash *h, const unsigned char *p, size_t n) {
    h->crc = crc32CalculateWithSeed(h->crc, p, n);
    sha1ContextUpdate(&h->sc, p, n);
}

/* Decide skip/swap from the sniffed leading bytes (magic-gated: no match means
 * no transform, so plain ROMs and containers pass through unchanged). */
static void rom_detect(RomHash *h) {
    h->detected = true;
    const unsigned char *s = h->sniff;
    int n = h->nsniff;
    if (n >= 4 && s[0] == 'N' && s[1] == 'E' && s[2] == 'S' && s[3] == 0x1A) {
        h->skip = 16; /* iNES */
    } else if (n >= 4 && s[0] == 'F' && s[1] == 'D' && s[2] == 'S' && s[3] == 0x1A) {
        h->skip = 16; /* fwNES (FDS) */
    } else if (n >= 4 && s[0] == 'L' && s[1] == 'Y' && s[2] == 'N' && s[3] == 'X') {
        h->skip = 64; /* Atari Lynx (.lnx) */
    } else if (n >= 10 && memcmp(s + 1, "ATARI7800", 9) == 0) {
        h->skip = 128; /* Atari 7800 (.a78) */
    } else if (n >= 4 && s[0] == 0x37 && s[1] == 0x80 && s[2] == 0x40 && s[3] == 0x12) {
        h->swap = 2; /* N64 .v64 byteswapped -> .z64 */
    } else if (n >= 4 && s[0] == 0x40 && s[1] == 0x12 && s[2] == 0x37 && s[3] == 0x80) {
        h->swap = 4; /* N64 .n64 little-endian -> .z64 */
    }
    /* 0x80 37 12 40 is native .z64 and everything else is left as-is. */
}

/* Apply skip + swap to a run of bytes and fold the result. */
static void rom_feed_x(RomHash *h, const unsigned char *p, size_t n) {
    if (h->skip) {
        size_t d = (size_t)h->skip < n ? (size_t)h->skip : n;
        h->skip -= (int)d;
        p += d;
        n -= d;
    }
    if (n == 0) {
        return;
    }
    if (h->swap < 2) {
        rom_fold(h, p, n);
        return;
    }
    int w = h->swap;
    /* finish a group left straddling from the previous feed */
    while (h->ncarry && n) {
        h->carry[h->ncarry++] = *p++;
        n--;
        if (h->ncarry == w) {
            unsigned char g[4];
            for (int i = 0; i < w; i++) {
                g[i] = h->carry[w - 1 - i];
            }
            rom_fold(h, g, (size_t)w);
            h->ncarry = 0;
        }
    }
    /* reverse full groups in a bounded scratch (a multiple of both widths) so
     * the caller's buffer is never mutated and the fold stays batched */
    unsigned char scratch[4096];
    size_t whole = n - (n % (size_t)w);
    size_t off = 0;
    while (off < whole) {
        size_t chunk = whole - off;
        if (chunk > sizeof(scratch)) {
            chunk = sizeof(scratch);
        }
        for (size_t i = 0; i < chunk; i += (size_t)w) {
            for (int b = 0; b < w; b++) {
                scratch[i + b] = p[off + i + (w - 1 - b)];
            }
        }
        rom_fold(h, scratch, chunk);
        off += chunk;
    }
    for (size_t i = whole; i < n; i++) {
        h->carry[h->ncarry++] = p[i]; /* trailing partial group -> next feed */
    }
}

static void romhash_feed(RomHash *h, const unsigned char *p, size_t n) {
    if (!h->detected) {
        while (n && h->nsniff < (int)sizeof(h->sniff)) {
            h->sniff[h->nsniff++] = *p++;
            n--;
        }
        if (h->nsniff < (int)sizeof(h->sniff)) {
            return; /* need more before we can decide */
        }
        rom_detect(h);
        rom_feed_x(h, h->sniff, (size_t)h->nsniff);
    }
    if (n) {
        rom_feed_x(h, p, n);
    }
}

static void romhash_final(RomHash *h, HashSet *out) {
    if (!h->detected) { /* file shorter than the sniff window */
        rom_detect(h);
        rom_feed_x(h, h->sniff, (size_t)h->nsniff);
    }
    if (h->ncarry) { /* truncated final group: no order to fix, fold as-is */
        rom_fold(h, h->carry, (size_t)h->ncarry);
        h->ncarry = 0;
    }
    unsigned char digest[SHA1_HASH_SIZE];
    sha1ContextGetHash(&h->sc, digest);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        out->sha1_hex[i * 2] = hex[digest[i] >> 4];
        out->sha1_hex[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out->sha1_hex[40] = '\0';
    out->crc = h->crc;
}

/* ---- public: hash a file ----------------------------------------------- */

bool hash_file(const char *path, HashSet *out, volatile bool *cancel,
               hash_progress_cb progress, void *ud) {
    /* Raw fd rather than stdio: newlib would stage every read through its own
     * FILE buffer, which on a multi-GB dump is a wasted copy of the whole file
     * and a lock per call, for no benefit when we already read in big blocks. */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    uint64_t total = 0;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        total = (uint64_t)st.st_size;
    }

    /* 1MB page-aligned reads. SD access has a high per-call cost on Switch, and
     * an unaligned buffer makes the FS layer bounce the data through a staging
     * copy, so alignment is worth more here than the buffer size alone. Falls
     * back to smaller blocks if the heap is tight (applet mode). */
    size_t BUF = 1024 * 1024;
    unsigned char *buf = NULL;
    while (!(buf = (unsigned char *)memalign(0x1000, BUF)) && BUF > 64 * 1024) {
        BUF /= 2;
    }
    if (!buf) {
        close(fd);
        return false;
    }

    RomHash rh;
    romhash_init(&rh);

    bool aborted = false, read_err = false;
    uint64_t done = 0;
    for (;;) {
        ssize_t r = read(fd, buf, BUF);
        if (r == 0) {
            break; /* eof */
        }
        if (r < 0) {
            /* A transient SD failure must be reported as "couldn't verify",
             * never as a confident digest of the partial data. */
            read_err = true;
            break;
        }
        romhash_feed(&rh, buf, (size_t)r);
        done += (uint64_t)r;
        if (progress) {
            progress(ud, done, total);
        }
        if (cancel && *cancel) {
            aborted = true;
            break;
        }
    }
    free(buf);
    close(fd);
    if (aborted || read_err) {
        return false;
    }

    romhash_final(&rh, out);
    return true;
}

/* ---- hash the single member of an archive ------------------------------ */

/* Returns 1 if the archive held exactly one file (digests in *out), 0 if it is
 * not a single-file archive we can peek into (unreadable, empty, or >1 member)
 * so the caller falls back to the container, or -1 on a read error / cancel. */
static int hash_archive_member(const char *path, HashSet *out,
                               volatile bool *cancel, hash_progress_cb progress,
                               void *ud) {
    struct archive *a = archive_read_new();
    if (!a) {
        return -1;
    }
    /* The same reader set extraction uses, so anything we can install we can
     * verify: filters cover the compression, formats cover the container. */
    archive_read_support_filter_all(a);
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    archive_read_support_format_tar(a);
    /* 1MB reads: SD access has high per-call overhead on Switch, matching the
     * extractor's buffer. */
    if (archive_read_open_filename(a, path, 1024 * 1024) != ARCHIVE_OK) {
        archive_read_free(a);
        return 0; /* not a readable archive: hash the container instead */
    }

    RomHash rh;
    romhash_init(&rh);
    int n_regular = 0;
    bool err = false, cancelled = false;

    struct archive_entry *entry;
    for (;;) {
        int rc = archive_read_next_header(a, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            err = true;
            break;
        }
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            continue;
        }
        if (++n_regular > 1) {
            /* More than one member: the DAT catalogs single dumps, so an inner
             * hash would be ambiguous. Fall back to the container. */
            archive_read_free(a);
            return 0;
        }
        /* Fold the first (and required-only) member's decompressed bytes. */
        const void *buff;
        size_t size;
        la_int64_t offset;
        for (;;) {
            int r = archive_read_data_block(a, &buff, &size, &offset);
            if (r == ARCHIVE_EOF) {
                break;
            }
            if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
                err = true;
                break;
            }
            if (size > 0) {
                romhash_feed(&rh, (const unsigned char *)buff, size);
            }
            if (progress) {
                /* Compressed bytes read, so this shares the plain-file size
                 * denominator the verify progress bar was primed with. */
                progress(ud, (uint64_t)archive_filter_bytes(a, -1), 0);
            }
            if (cancel && *cancel) {
                cancelled = true;
                break;
            }
        }
        if (err || cancelled) {
            break;
        }
    }

    archive_read_free(a);
    if (err || cancelled) {
        return -1;
    }
    if (n_regular != 1) {
        return 0; /* empty archive: nothing to hash, use the container */
    }
    romhash_final(&rh, out);
    return 1;
}

/* ---- public: archive-aware hashing ------------------------------------- */

/* Extension gate so a raw ROM isn't needlessly probed as an archive (which
 * would cost a read of its first block before the format bid fails). */
static bool name_is_rom_archive(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".zip") == 0 || strcasecmp(dot, ".7z") == 0 ||
           strcasecmp(dot, ".rar") == 0;
}

bool hash_rom(const char *path, HashSet *out, volatile bool *cancel,
              hash_progress_cb progress, void *ud) {
    if (name_is_rom_archive(path)) {
        int ar = hash_archive_member(path, out, cancel, progress, ud);
        if (ar == 1) {
            return true; /* used the inner ROM */
        }
        if (ar < 0 && cancel && *cancel) {
            return false; /* cancelled mid-archive: don't retry as a container */
        }
        /* ar == 0 (not a single-file archive) or a decompress error: fall back
         * to hashing the file's own bytes (which classifies as UNKNOWN). */
    }
    return hash_file(path, out, cancel, progress, ud);
}
