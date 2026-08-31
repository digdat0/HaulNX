#include "idgame.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- Tier 1: extension -> console ------------------------------------------
 * Only extensions that belong to exactly one console live here. Extensions
 * shared across systems (.iso, .bin, .chd, disc/container formats) are listed
 * in kAmbiguousExts below and go to the header sniffer instead. */
typedef struct {
    const char *ext;    /* lowercase, no leading dot */
    const char *target; /* HaulNX console folder id */
} ExtMap;

static const ExtMap kExtMap[] = {
    {"nes", "nes"},   {"unf", "nes"},   {"unif", "nes"},
    {"fds", "fds"},
    {"sfc", "snes"},  {"smc", "snes"},  {"swc", "snes"}, {"fig", "snes"},
    {"n64", "n64"},   {"z64", "n64"},   {"v64", "n64"},
    {"gb", "gb"},
    {"gbc", "gbc"},
    {"gba", "gba"},   {"srl", "gba"},
    {"nds", "nds"},   {"dsi", "nds"},   {"ids", "nds"},
    {"3ds", "3ds"},   {"cci", "3ds"},   {"cxi", "3ds"},  {"cia", "3ds"},
    {"gcm", "gc"},    {"gcz", "gc"},
    {"wbfs", "wii"},  {"wad", "wii"},   {"wia", "wii"},
    {"wud", "wiiu"},  {"wux", "wiiu"},  {"wua", "wiiu"}, {"rpx", "wiiu"},
    {"vb", "virtual-boy"}, {"vboy", "virtual-boy"},
    {"min", "pokemon-mini"},
    {"sg", "sg-1000"},
    {"sms", "master-system"},
    {"gg", "game-gear"},
    {"md", "genesis"}, {"gen", "genesis"}, {"smd", "genesis"},
    {"32x", "sega-32x"},
    {"gdi", "dc"},    {"cdi", "dc"},
    {"cso", "psp"},   {"dax", "psp"},
    {"pce", "pc-engine"},
    {"sgx", "supergrafx"},
    {"ngp", "neo-geo-pocket"},
    {"ngc", "neo-geo-pocket-color"}, {"ngpc", "neo-geo-pocket-color"},
    {"a26", "atari-2600"},
    {"a52", "atari-5200"},
    {"a78", "atari-7800"},
    {"lnx", "atari-lynx"},
    {"jag", "atari-jaguar"}, {"j64", "atari-jaguar"},
    {"ws", "wonderswan"},
    {"wsc", "wonderswan-color"},
    {"col", "colecovision"},
    {"int", "intellivision"},
    {"vec", "vectrex"},
    {"chf", "channel-f"},
    {"sv", "supervision"},
    {"vpk", "vita"},
    {"adf", "amiga"},
    {"tzx", "zx-spectrum"}, {"z80", "zx-spectrum"},
    {"ch8", "chip8"},
    {"p8", "pico8"},
    {"swf", "flash"},
};

/* Shared extensions that need a look inside to place: disc images, raw dumps
 * and containers. Anything here skips Tier 1 and goes to sniff_content(). */
static const char *kAmbiguousExts[] = {
    "iso", "bin", "img", "cue", "chd", "rvz", "zip", "7z", "pbp",
};

/* Lowercase the extension after the last dot into out (no leading dot). */
static void file_ext(const char *path, char *out, size_t out_sz) {
    out[0] = '\0';
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1]) {
        return;
    }
    size_t n = 0;
    for (const char *p = dot + 1; *p && n + 1 < out_sz; ++p) {
        out[n++] = (char)tolower((unsigned char)*p);
    }
    out[n] = '\0';
}

static bool ext_is_ambiguous(const char *ext) {
    for (size_t i = 0; i < sizeof(kAmbiguousExts) / sizeof(kAmbiguousExts[0]); ++i) {
        if (strcmp(ext, kAmbiguousExts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static const char *ext_target(const char *ext) {
    for (size_t i = 0; i < sizeof(kExtMap) / sizeof(kExtMap[0]); ++i) {
        if (strcmp(ext, kExtMap[i].ext) == 0) {
            return kExtMap[i].target;
        }
    }
    return NULL;
}

/* Bounded substring search over a byte buffer (buf may contain NULs). */
static bool mem_has(const uint8_t *buf, size_t len, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0 || nl > len) {
        return false;
    }
    for (size_t i = 0; i + nl <= len; ++i) {
        if (memcmp(buf + i, needle, nl) == 0) {
            return true;
        }
    }
    return false;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* --- Tier 2: header magic --------------------------------------------------
 * `buf` holds the first `len` bytes of the file (up to SNIFF_BYTES). Returns a
 * target and sets *amb when the format is a disc/container we recognise but
 * can't resolve to one console from the header alone. */
static const char *sniff_content(const uint8_t *buf, size_t len, bool *amb) {
    *amb = false;

    /* Fixed-offset console magics. */
    if (len >= 4 && buf[0] == 'N' && buf[1] == 'E' && buf[2] == 'S' &&
        buf[3] == 0x1A) {
        return "nes"; /* iNES / NES 2.0 */
    }
    if (len >= 0x20 && be32(buf + 0x1C) == 0xC2339F3Du) {
        return "gc"; /* GameCube disc */
    }
    if (len >= 0x1C && be32(buf + 0x18) == 0x5D1C9EA3u) {
        return "wii"; /* Wii disc */
    }
    if (len >= 0x104 && buf[0x100] == 'S' && buf[0x101] == 'E' &&
        buf[0x102] == 'G' && buf[0x103] == 'A') {
        return "genesis"; /* "SEGA" console name at 0x100 */
    }

    /* Sega disc headers sit at the start of the data track — offset 0 for a
     * 2048-byte/sector rip, 0x10 for a 2352-byte raw rip. */
    for (size_t off = 0; off <= 0x10; off += 0x10) {
        if (len < off + 16) {
            break;
        }
        const uint8_t *p = buf + off;
        if (memcmp(p, "SEGADISCSYSTEM", 14) == 0) {
            return "sega-cd";
        }
        if (memcmp(p, "SEGA SEGASATURN", 15) == 0) {
            return "saturn";
        }
        if (memcmp(p, "SEGA SEGAKATANA", 15) == 0) {
            return "dc";
        }
    }

    /* ISO9660 volume descriptor: "CD001" at 0x8001 for a 2048-byte/sector image.
     * The PlayStation family and PSP identify themselves in the descriptor's
     * system-identifier field (0x8008) and in strings a little further in. */
    if (len >= 0x8006 && memcmp(buf + 0x8001, "CD001", 5) == 0) {
        size_t win_off = 0x8000;
        size_t win_len = len - win_off; /* whatever of the descriptor we captured */
        const uint8_t *w = buf + win_off;
        if (mem_has(w, win_len, "PSP GAME") || mem_has(w, win_len, "UMD_DATA")) {
            return "psp";
        }
        if (mem_has(w, win_len, "PLAYSTATION")) {
            /* PS2 boot key is BOOT2=; PS1 uses BOOT=. Fall back to PS1. */
            return mem_has(w, win_len, "BOOT2") ? "ps2" : "psx";
        }
        /* Some other ISO9660 disc (3DO, CD-i, PC-FX, ...) — recognised as a
         * disc but not pinned to a console. */
        *amb = true;
        return NULL;
    }

    /* Zip container: read the first local file header's name and identify by
     * the member's extension. Signature "PK\x03\x04", name length at 0x1A (LE),
     * name at 0x1E. */
    if (len >= 0x1E && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x03 &&
        buf[3] == 0x04) {
        uint16_t nlen = (uint16_t)(buf[0x1A] | (buf[0x1B] << 8));
        if (nlen > 0 && (size_t)0x1E + nlen <= len) {
            char member[260];
            size_t m = nlen < sizeof(member) - 1 ? nlen : sizeof(member) - 1;
            memcpy(member, buf + 0x1E, m);
            member[m] = '\0';
            char mext[16];
            file_ext(member, mext, sizeof(mext));
            if (mext[0] && !ext_is_ambiguous(mext)) {
                const char *t = ext_target(mext);
                if (t) {
                    return t;
                }
            }
        }
        *amb = true; /* a zip of something we couldn't place from the first entry */
        return NULL;
    }

    /* CHD: recognised (magic "MComprHD") but the source system lives in metadata
     * we don't parse here — leave it for Tier 3 / the user. */
    if (len >= 8 && memcmp(buf, "MComprHD", 8) == 0) {
        *amb = true;
        return NULL;
    }

    *amb = true;
    return NULL;
}

#define SNIFF_BYTES (96 * 1024) /* covers offset-0 headers + the 0x8000 VD + slack */

IdResult idgame_identify(const char *path) {
    IdResult r;
    r.target[0] = '\0';
    r.conf = IDC_NONE;
    r.ambiguous = true;

    char ext[16];
    file_ext(path, ext, sizeof(ext));

    /* Tier 1: an unambiguous extension is enough. */
    if (ext[0] && !ext_is_ambiguous(ext)) {
        const char *t = ext_target(ext);
        if (t) {
            snprintf(r.target, sizeof(r.target), "%s", t);
            r.conf = IDC_EXT;
            r.ambiguous = false;
            return r;
        }
    }

    /* Tier 2: read the head of the file and look inside. */
    FILE *f = fopen(path, "rb");
    if (f) {
        static uint8_t buf[SNIFF_BYTES]; /* not reentrant; the sorter is single-threaded */
        size_t len = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (len > 0) {
            bool amb = false;
            const char *t = sniff_content(buf, len, &amb);
            if (t) {
                snprintf(r.target, sizeof(r.target), "%s", t);
                r.conf = IDC_CONTENT;
                r.ambiguous = false;
                return r;
            }
            (void)amb; /* nothing placed: undetermined, which is ambiguous */
        }
    }

    return r; /* undetermined */
}

bool idgame_is_confident(const IdResult *res) {
    return res && res->conf != IDC_NONE && !res->ambiguous && res->target[0];
}
