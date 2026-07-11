#include <MainApplication.hpp>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

extern "C" {
#include "config.h"
#include "fsutil.h"
}

// SDL_ttf streams glyphs from its font file for the app's whole lifetime.
// Loading the fallback font straight from romfs therefore keeps our own NRO
// open — and the self-updater can't replace a locked file. Stage the font on
// the SD card and load it from there; romfs is only the copy source.
static const char *stage_viet_font(void) {
    static const char *sd = "sdmc:/switch/ticodlplus/viet-fallback.ttf";
    static const char *rom = "romfs:/fonts/viet-fallback.ttf";
    FILE *in = fopen(rom, "rb");
    if (!in) {
        return sd; // romfs copy missing: hope for a previously staged one
    }
    fseek(in, 0, SEEK_END);
    long rsz = ftell(in);
    fseek(in, 0, SEEK_SET);
    struct stat st;
    if (stat(sd, &st) == 0 && (long)st.st_size == rsz) {
        fclose(in);
        return sd; // already staged and current
    }
    fs_mkdir_p(CONFIG_DIR);
    FILE *out = fopen(sd, "wb");
    if (!out) {
        fclose(in);
        return rom; // can't stage: romfs fallback (self-update stays locked)
    }
    static char buf[8192];
    size_t r;
    bool ok = true;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) {
            ok = false;
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        ok = false;
    }
    if (!ok) {
        remove(sd);
        return rom;
    }
    return sd;
}

// Finish a self-update staged by the previous run. The updater can't replace
// the running NRO (the loader keeps it open for the app's whole lifetime), so
// it leaves the new build at "<self>.new" and we swap the files here — before
// romfs is mounted or any file of ours is opened. Returns true when the swap
// landed and the caller should exit so hbloader chainloads the new build.
static bool apply_staged_update(const char *self) {
    if (!self || !*self) {
        return false;
    }
    char stage[1100], prev[1100];
    snprintf(stage, sizeof(stage), "%s.new", self);
    snprintf(prev, sizeof(prev), "%s.previous", self);
    // Sanity: a real NRO has "NRO0" at offset 0x10 — never swap in junk.
    FILE *f = fopen(stage, "rb");
    if (!f) {
        return false;
    }
    char h[0x14];
    bool valid = fread(h, 1, sizeof(h), f) == sizeof(h) &&
                 memcmp(h + 0x10, "NRO0", 4) == 0;
    fclose(f);
    FILE *log = fopen(LOG_PATH, "a");
    bool swapped = false;
    if (!valid) {
        remove(stage);
        if (log) {
            fprintf(log, "upd: staged file invalid, dropped\n");
        }
    } else {
        remove(prev); // clear a stale backup so the rename can land
        if (rename(self, prev) != 0) {
            // Leave the stage in place: it gets another try next launch.
            if (log) {
                fprintf(log, "upd: swap self->prev failed (errno=%d)\n",
                        errno);
            }
        } else if (rename(stage, self) != 0) {
            int e = errno;
            rename(prev, self); // put the original back
            if (log) {
                fprintf(log, "upd: swap stage->self failed (errno=%d)\n", e);
            }
        } else {
            swapped = true;
            if (log) {
                fprintf(log, "upd: staged update applied -> '%s'\n", self);
            }
        }
    }
    if (log) {
        fclose(log);
    }
    return swapped;
}

int main(int argc, char **argv) {
    // hbloader passes the launched .nro path as argv[0]; the self-updater uses
    // it so it overwrites the file you actually ran (wherever it lives).
    const char *self = (argc > 0 && argv[0]) ? argv[0] : "";
    MainApplication::SetLaunchPath(self);

    if (apply_staged_update(self) && envHasNextLoad()) {
        // The old image is already in memory; chainload so this very launch
        // boots the freshly swapped-in build instead of needing another one.
        char qargv[1104];
        snprintf(qargv, sizeof(qargv), "\"%s\"", self);
        envSetNextLoad(self, qargv);
        return 0;
    }

    // Mount romfs before the renderer initializes: the bundled fallback font
    // below is loaded from romfs during renderer init (OnLoad's romfsInit
    // then no-ops against the existing mount).
    romfsInit();

    auto opts = pu::ui::render::RendererInitOptions(
        SDL_INIT_EVERYTHING, pu::ui::render::RendererHardwareFlags);
    opts.UseImage(pu::ui::render::ImgAllFlags);

    // Use the console's shared fonts (covers Latin + CJK etc. — the i18n win).
    opts.SetPlServiceType(PlServiceType_User);
    // Vietnamese fallback: the Switch shared fonts lack Latin Extended
    // Additional (ế, ệ, ợ...). This subset contains ONLY those glyphs, so the
    // system font still renders everything it can (font paths are consulted
    // before shared fonts, hence the aggressive subsetting). Served from the
    // SD card so the font handle doesn't pin our own NRO (see stage_viet_font).
    opts.AddDefaultFontPath(stage_viet_font());
    opts.AddDefaultAllSharedFonts();
    // Tiny size for the queue cards' chip/filename (defaults start at 27).
    opts.AddExtraDefaultFontSize(21);

    opts.SetInputPlayerCount(1);
    opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
    opts.AddInputNpadIdType(HidNpadIdType_Handheld);
    opts.AddInputNpadIdType(HidNpadIdType_No1);

    auto renderer = pu::ui::render::Renderer::New(opts);
    auto main = MainApplication::New(renderer);

    const auto rc = main->Load();
    if (R_FAILED(rc)) {
        diagAbortWithResult(rc);
    }
    main->Show();
    main->Shutdown();
    return 0;
}
