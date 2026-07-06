#include <MainApplication.hpp>
#include <pu/ui/extras/extras_Toast.hpp>
#include "version.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "config.h"
#include "i18n.h"
#include "iarchive.h"
#include "net.h"
#include "queue.h"
#include "extract.h"
#include "fsutil.h"
#include "update.h"
#include "jsonutil.h"
#include "jsmn.h"
#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
}

// ---- backend state --------------------------------------------------------
static SourcesConfig g_cfg;
static Credentials g_creds;
static Prefs g_prefs;
static TicoState g_tico;
static ArchiveItem g_item;
static bool g_have_item = false;

static std::vector<int> g_files; // filtered indices into g_item.files
static std::vector<char> g_marks;
static std::string g_filter;
static char g_files_id[256], g_files_base[512], g_files_target[64];
static bool g_files_manual = false;

#define FILES_SUBTITLE tr(S_SUB_FILES)

struct DirEnt {
    std::string name;
    bool is_dir;
    uint64_t size;
};
static std::vector<DirEnt> g_inst;
static std::vector<DirEnt> g_dlfiles; // files in the downloads temp folder
static std::vector<std::string> g_picker; // sorted supported consoles for the picker
static std::vector<int> g_home_map; // grouped Browse: visible row -> console index
static std::string g_launch_path;   // argv[0] from main(), for self-update

// ---- theme ----------------------------------------------------------------
struct AppTheme {
    pu::ui::Color bg;           // layout background
    pu::ui::Color header_bg;    // header rectangle
    pu::ui::Color tab_bar_bg;   // tab strip
    pu::ui::Color footer_bg;    // footer rectangle
    pu::ui::Color title_clr;    // "TicoDL+" title text
    pu::ui::Color status_clr;   // status text top-right
    pu::ui::Color tab_clr;      // inactive tab text
    pu::ui::Color tab_active;   // active tab text
    pu::ui::Color tab_under;    // tab underline
    pu::ui::Color footer_clr;   // footer hint text
    pu::ui::Color rom_info_clr; // ROM folder text
    pu::ui::Color row_text;     // default row text
    pu::ui::Color dialog_bg;
    pu::ui::Color dialog_title;
    pu::ui::Color dialog_body;
    pu::ui::Color dialog_opt;
    pu::ui::Color dialog_over;
    // TableList colors
    pu::ui::Color tl_row_bg;
    pu::ui::Color tl_row_alt;
    pu::ui::Color tl_focus;
    pu::ui::Color tl_scroll;
    pu::ui::Color tl_mark;
};

static const AppTheme g_theme_dark = {
    {12,12,14,255},       {28,54,104,255},     {58,104,178,255},
    {22,42,80,255},       {255,255,255,255},   {210,222,245,255},
    {196,212,240,255},    {255,255,255,255},   {150,205,255,255},
    {206,216,238,255},    {150,160,185,255},   {232,234,240,255},
    {28,54,104,255},      {255,255,255,255},   {210,222,245,255},
    {255,255,255,255},    {40,75,140,255},
    {22,23,27,255},       {28,30,36,255},      {28,122,116,255},
    {80,86,100,255},      {60,80,120,255},
};

static const AppTheme g_theme_light = {
    {235,237,242,255},    {45,90,170,255},     {180,200,230,255},
    {200,210,230,255},    {255,255,255,255},   {220,230,250,255},
    {70,85,120,255},      {20,20,30,255},      {50,120,200,255},
    {50,60,80,255},       {90,100,120,255},    {0,0,0,255},
    {210,218,235,255},    {30,30,40,255},      {50,60,80,255},
    {30,30,40,255},       {170,190,220,255},
    {225,228,234,255},    {215,218,224,255},   {60,160,150,255},
    {160,168,185,255},    {170,190,220,255},
};

static const AppTheme *g_theme = &g_theme_dark;

static bool is_light_theme() { return strcmp(g_prefs.theme, "light") == 0; }

static void select_theme() {
    g_theme = is_light_theme() ? &g_theme_light : &g_theme_dark;
}

// ---- helpers --------------------------------------------------------------
static std::string human_size(uint64_t bytes) {
    const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        i++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", v, u[i]);
    return std::string(buf);
}

// Color-code a row by file size magnitude (KB / MB / GB), restoring the size
// color cues the text UI had. (Plutonium colors a whole row, not just the size
// token, so the whole row takes the tier color.)
static pu::ui::Color size_color(uint64_t b) {
    bool light = is_light_theme();
    if (b >= (1ull << 30)) {
        return light ? pu::ui::Color(180, 110, 30, 255)
                     : pu::ui::Color(245, 175, 95, 255);
    }
    if (b >= (1ull << 20)) {
        return light ? pu::ui::Color(30, 140, 60, 255)
                     : pu::ui::Color(130, 225, 150, 255);
    }
    return light ? pu::ui::Color(40, 120, 200, 255)
                 : pu::ui::Color(150, 205, 255, 255);
}

static pu::ui::Color count_color() {
    return is_light_theme() ? pu::ui::Color(50, 120, 135, 255)
                            : pu::ui::Color(150, 200, 210, 255);
}

// Full display name for a known console folder, or NULL if unknown (custom).
static const char *console_full_name(const char *abbr) {
    static const struct {
        const char *key;
        const char *name;
    } map[] = {
        {"nes", "Nintendo Entertainment System"},
        {"snes", "Super Nintendo Entertainment System"},
        {"n64", "Nintendo 64"},
        {"gb", "Game Boy"},
        {"gbc", "Game Boy Color"},
        {"gba", "Game Boy Advance"},
        {"gc", "Nintendo GameCube"},
        {"wii", "Nintendo Wii"},
        {"genesis", "Sega Genesis"},
        {"master-system", "Sega Master System"},
        {"game-gear", "Sega Game Gear"},
        {"sega-cd", "Sega CD"},
        {"saturn", "Sega Saturn"},
        {"dc", "Sega Dreamcast"},
        {"atomiswave", "Sammy Atomiswave"},
        {"naomi", "Sega NAOMI"},
        {"psx", "Sony PlayStation"},
        {"psp", "Sony PlayStation Portable"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(abbr, map[i].key) == 0) {
            return map[i].name;
        }
    }
    return NULL;
}

// Browse display label for a console folder, e.g.
// "Nintendo Entertainment System (NES)", or just the folder for custom ones.
static void console_label(const char *abbr, char *out, size_t out_sz) {
    const char *full = console_full_name(abbr);
    if (!full) {
        snprintf(out, out_sz, "%s", abbr);
        return;
    }
    char up[64];
    size_t j = 0;
    for (; abbr[j] && j < sizeof(up) - 1; j++) {
        char c = abbr[j];
        up[j] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[j] = '\0';
    snprintf(out, out_sz, "%s (%s)", full, up);
}

static void style_dialog(pu::ui::Dialog::Ref &d) {
    d->SetDialogColor(g_theme->dialog_bg);
    d->SetTitleColor(g_theme->dialog_title);
    d->SetContentColor(g_theme->dialog_body);
    d->SetOptionColor(g_theme->dialog_opt);
    d->SetOverColor(g_theme->dialog_over);
}

// Compact "time remaining" string from a seconds count.
static std::string human_eta(uint64_t secs) {
    char buf[24];
    if (secs >= 3600) {
        snprintf(buf, sizeof(buf), "%lluh%llum", (unsigned long long)(secs / 3600),
                 (unsigned long long)((secs % 3600) / 60));
    } else if (secs >= 60) {
        snprintf(buf, sizeof(buf), "%llum%llus", (unsigned long long)(secs / 60),
                 (unsigned long long)(secs % 60));
    } else {
        snprintf(buf, sizeof(buf), "%llus", (unsigned long long)secs);
    }
    return std::string(buf);
}

static bool ci_contains(const char *hay, const char *needle) {
    if (!needle[0]) {
        return true;
    }
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nl) == 0) {
            return true;
        }
    }
    return false;
}

// libnx swkbd; require non-empty.
static bool prompt(const char *guide, const char *initial, char *out,
                   size_t out_sz) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    if (guide) {
        swkbdConfigSetGuideText(&kbd, guide);
    }
    if (initial) {
        swkbdConfigSetInitialText(&kbd, initial);
    }
    Result rc = swkbdShow(&kbd, out, out_sz);
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc) && out[0] != '\0';
}

// swkbd that accepts an empty result (used for the filter, blank = clear).
static bool prompt_raw(const char *guide, const char *initial, char *out,
                       size_t out_sz) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    if (guide) {
        swkbdConfigSetGuideText(&kbd, guide);
    }
    if (initial) {
        swkbdConfigSetInitialText(&kbd, initial);
    }
    Result rc = swkbdShow(&kbd, out, out_sz);
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc);
}

static const char *qstatus(QStatus s) {
    switch (s) {
    case Q_QUEUED:      return "wait";
    case Q_PAUSED:      return "pause";
    case Q_DOWNLOADING: return "dl";
    case Q_VERIFYING:      return "vrfy";
    case Q_AWAIT_EXTRACT:  return "wait-unz";
    case Q_EXTRACTING:     return "unzip";
    case Q_DONE:           return "done";
    case Q_SAVED:       return "saved";
    case Q_FAILED:      return "FAIL";
    case Q_CANCELLED:   return "cxl";
    default:            return "?";
    }
}

static pu::ui::Color qstatus_color(QStatus s) {
    bool light = is_light_theme();
    switch (s) {
    case Q_DOWNLOADING: return light ? pu::ui::Color(20, 80, 180, 255)
                                     : pu::ui::Color(245, 246, 250, 255);
    case Q_PAUSED:      return light ? pu::ui::Color(40, 120, 200, 255)
                                     : pu::ui::Color(150, 205, 255, 255);
    case Q_VERIFYING:
    case Q_AWAIT_EXTRACT:
    case Q_EXTRACTING:  return pu::ui::Color(210, 185, 120, 255);
    case Q_DONE:        return pu::ui::Color(130, 225, 150, 255);
    case Q_SAVED:       return pu::ui::Color(190, 205, 130, 255);
    case Q_FAILED:      return pu::ui::Color(240, 110, 110, 255);
    case Q_CANCELLED:   return pu::ui::Color(150, 150, 162, 255);
    case Q_QUEUED:
    default:            return light ? pu::ui::Color(80, 90, 110, 255)
                                     : pu::ui::Color(205, 212, 225, 255);
    }
}

// Flat-mode rows skip the repos of hidden consoles, so indexing matches the
// primary page (which also hides them).
static bool flat_ref(int flat, int *ci, int *ri) {
    int k = 0;
    for (int c = 0; c < g_cfg.console_count; c++) {
        if (!g_cfg.consoles[c].shown) {
            continue;
        }
        for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
            if (k == flat) {
                *ci = c;
                *ri = r;
                return true;
            }
            k++;
        }
    }
    return false;
}
static int flat_count() {
    int k = 0;
    for (int c = 0; c < g_cfg.console_count; c++) {
        if (g_cfg.consoles[c].shown) {
            k += g_cfg.consoles[c].repo_count;
        }
    }
    return k;
}

static bool file_installed(const char *target, const char *fname) {
    char p[1200];
    snprintf(p, sizeof(p), "%s/%s/%s", roms_root(&g_tico), target, fname);
    if (fs_exists(p)) {
        return true;
    }
    if (!is_archive_name(fname)) {
        return false;
    }
    char base[512];
    snprintf(base, sizeof(base), "%s", fname);
    char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        *dot = '\0';
    }
    size_t bl = strlen(base);
    if (bl == 0) {
        return false;
    }
    char dir[1200];
    snprintf(dir, sizeof(dir), "%s/%s", roms_root(&g_tico), target);
    DIR *d = opendir(dir);
    bool found = false;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strncasecmp(e->d_name, base, bl) == 0) {
                found = true;
                break;
            }
        }
        closedir(d);
    }
    return found;
}

enum { SORT_DEFAULT, SORT_NAME_AZ, SORT_NAME_ZA, SORT_SIZE_DESC, SORT_SIZE_ASC, SORT__COUNT };
static int g_sort_mode = SORT_DEFAULT;

static void rebuild_files(MainLayout *lay, const char *target) {
    lay->ClearMenu();
    g_files.clear();
    g_marks.clear();
    if (!g_have_item) {
        lay->AddRow(tr(S_META_FAILED));
        return;
    }
    for (int i = 0; i < g_item.file_count; i++) {
        if (g_filter.empty() ||
            ci_contains(g_item.files[i].name, g_filter.c_str())) {
            g_files.push_back(i);
        }
    }
    if (g_sort_mode != SORT_DEFAULT && !g_files.empty()) {
        std::sort(g_files.begin(), g_files.end(), [](int a, int b) {
            const ArchiveFile *fa = &g_item.files[a];
            const ArchiveFile *fb = &g_item.files[b];
            switch (g_sort_mode) {
            case SORT_NAME_AZ:   return strcasecmp(fa->name, fb->name) < 0;
            case SORT_NAME_ZA:   return strcasecmp(fa->name, fb->name) > 0;
            case SORT_SIZE_DESC: return fa->size > fb->size;
            case SORT_SIZE_ASC:  return fa->size < fb->size;
            default:             return false;
            }
        });
    }
    for (int k = 0; k < (int)g_files.size(); k++) {
        ArchiveFile *f = &g_item.files[g_files[k]];
        bool inst = file_installed(target, f->name);
        g_marks.push_back(inst ? 1 : 0);
        char name[540];
        snprintf(name, sizeof(name), "%s%s", inst ? "* " : "", f->name);
        lay->AddRow2(name, human_size(f->size),
                     g_theme->row_text, size_color(f->size));
    }
    if (g_files.empty()) {
        lay->AddRow(tr(S_NO_FILES_MATCH));
    }
}

// Background metadata load: ia_fetch runs on its own thread so the file
// list shows an animated "Loading metadata..." indicator instead of freezing.
void MainApplication::MetaThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    bool ok = false;
    if (g_files_id[0]) {
        ok = ia_fetch(g_files_id, &g_item, g_prefs.use_cache && !self->meta_force,
                      CACHE_DIR);
    }
    self->meta_ok = ok;
    self->meta_done = true;
}

void MainApplication::StartMetaLoad(const std::string &id,
                                    const std::string &base,
                                    const std::string &target, bool force,
                                    const std::string &done_subtitle) {
    snprintf(g_files_id, sizeof(g_files_id), "%s", id.c_str());
    snprintf(g_files_base, sizeof(g_files_base), "%s", base.c_str());
    snprintf(g_files_target, sizeof(g_files_target), "%s", target.c_str());
    if (g_have_item) {
        ia_free(&g_item);
        g_have_item = false;
    }
    this->meta_force = force;
    this->meta_done_subtitle = done_subtitle;
    this->meta_done = false;
    this->meta_ok = false;
    this->meta_running = true;

    this->layout->SetSubtitle(tr(S_LOADING_META));
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_LOADING_META));

    Result rc = threadCreate(&this->meta_thread, &MainApplication::MetaThread,
                             this, NULL, 0x40000, 0x2C, -2);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&this->meta_thread))) {
        return;
    }
    // Couldn't spawn: fall back to a synchronous fetch so the list still loads.
    this->meta_running = false;
    if (g_files_id[0] &&
        ia_fetch(g_files_id, &g_item, g_prefs.use_cache && !force, CACHE_DIR)) {
        if (g_files_base[0]) {
            snprintf(g_item.download_base, sizeof(g_item.download_base), "%s",
                     g_files_base);
        }
        g_have_item = true;
    }
    g_filter.clear();
    rebuild_files(this->layout.get(), g_files_target);
    this->layout->SetSubtitle(done_subtitle);
}

void MainApplication::MetaTick() {
    if (!this->meta_done) {
        // Animate the dots (~3/sec) so it's clearly working, not stuck.
        int dots = (int)((armTicksToNs(armGetSystemTick()) / 350000000ULL) % 4);
        this->layout->ClearMenu();
        this->layout->AddRow(std::string(tr(S_LOADING_META)) +
                             std::string(dots, '.'));
        return;
    }
    threadWaitForExit(&this->meta_thread);
    threadClose(&this->meta_thread);
    this->meta_running = false;
    if (this->meta_ok) {
        if (g_files_base[0]) {
            snprintf(g_item.download_base, sizeof(g_item.download_base), "%s",
                     g_files_base);
        }
        g_have_item = true;
    }
    g_filter.clear();
    rebuild_files(this->layout.get(), g_files_target);
    this->layout->SetSubtitle(this->meta_done_subtitle);
    // Returning to the same repo we last viewed? Restore the scroll position.
    if (g_files_id[0] && this->files_sel_id == g_files_id) {
        this->layout->SetSel(this->files_sel);
    }
}

// Delete stale .part files left by the old temp-file naming (plain
// "<file>.part"). Current temp files are "<target>_<file>.part", so anything
// without a known console-target prefix can never be matched or resumed again.
static void cleanup_stale_parts() {
    DIR *d = opendir(DL_TMP_DIR);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t ln = strlen(e->d_name);
        if (ln < 6 || strcasecmp(e->d_name + ln - 5, ".part") != 0) {
            continue;
        }
        char full[1200];
        snprintf(full, sizeof(full), "%s/%s", DL_TMP_DIR, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) {
            continue;
        }
        bool known = false;
        for (int c = 0; c < g_cfg.console_count && !known; c++) {
            const char *t = g_cfg.consoles[c].target;
            size_t tl = strlen(t);
            known = tl > 0 && ln > tl + 5 &&
                    strncasecmp(e->d_name, t, tl) == 0 && e->d_name[tl] == '_';
        }
        for (int s = 0; s < g_cfg.supported_count && !known; s++) {
            const char *t = g_cfg.supported[s];
            size_t tl = strlen(t);
            known = tl > 0 && ln > tl + 5 &&
                    strncasecmp(e->d_name, t, tl) == 0 && e->d_name[tl] == '_';
        }
        if (!known) {
            remove(full);
        }
    }
    closedir(d);
}

// Sanity-check a downloaded update: a real NRO has "NRO0" at offset 0x10.
// Refuses to install junk (e.g. an error page saved as a file).
static bool looks_like_nro(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    unsigned char h[0x14];
    size_t r = fread(h, 1, sizeof(h), f);
    fclose(f);
    return r == sizeof(h) && memcmp(h + 0x10, "NRO0", 4) == 0;
}

// Copy src over dst in place (truncate-write) — used to replace our own .nro.
static bool install_over(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    static char buf[65536];
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
    return ok;
}

void MainApplication::SetLaunchPath(const std::string &p) { g_launch_path = p; }

// Resolve which .nro the self-update should overwrite. Prefer the actual launch
// path (argv[0]); otherwise probe the documented install locations; finally
// fall back to the default. This handles both sdmc:/switch/TicoDLplus/...nro
// and sdmc:/switch/TicoDLplus.nro.
static std::string resolve_self_path() {
    if (g_launch_path.size() >= 4 &&
        strcasecmp(g_launch_path.c_str() + g_launch_path.size() - 4, ".nro") ==
            0 &&
        fs_exists(g_launch_path.c_str())) {
        return g_launch_path;
    }
    const char *candidates[] = {"sdmc:/switch/TicoDLplus/TicoDLplus.nro",
                                "sdmc:/switch/TicoDLplus.nro"};
    for (const char *c : candidates) {
        if (fs_exists(c)) {
            return std::string(c);
        }
    }
    return std::string(DEFAULT_SELF_PATH);
}

// Count immediate entries (files/folders) inside a directory.
static int count_dir_entries(const std::string &path) {
    DIR *d = opendir(path.c_str());
    if (!d) {
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        n++;
    }
    closedir(d);
    return n;
}

static std::vector<DirEnt> list_dir(const std::string &path) {
    std::vector<DirEnt> v;
    DIR *d = opendir(path.c_str());
    if (!d) {
        return v;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        DirEnt de;
        de.name = e->d_name;
        de.is_dir = false;
        de.size = 0;
        std::string full = path + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            de.is_dir = S_ISDIR(st.st_mode);
            de.size = (uint64_t)st.st_size;
        }
        v.push_back(de);
    }
    closedir(d);
    std::sort(v.begin(), v.end(), [](const DirEnt &a, const DirEnt &b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return v;
}

// ---- MainLayout -----------------------------------------------------------
MainLayout::MainLayout() : Layout::Layout() {
    this->SetBackgroundColor(g_theme->bg);
    const s32 sw = (s32)pu::ui::render::ScreenWidth;
    const s32 sh = (s32)pu::ui::render::ScreenHeight;

    this->header = pu::ui::elm::Rectangle::New(0, 0, sw, 150, g_theme->header_bg);
    this->Add(this->header);

    this->title = pu::ui::elm::TextBlock::New(45, 24, "TicoDL+");
    this->title->SetColor(g_theme->title_clr);
    this->Add(this->title);

    this->status = pu::ui::elm::TextBlock::New(sw - 400, 30, "");
    this->status->SetColor(g_theme->status_clr);
    this->Add(this->status);

    this->net_icon = pu::ui::elm::TextBlock::New(sw - 440, 30, "●");
    this->net_icon->SetColor(pu::ui::Color(100, 100, 100, 255));
    this->Add(this->net_icon);

    this->bat_info = pu::ui::elm::TextBlock::New(sw - 100, 30, "");
    this->bat_info->SetColor(g_theme->status_clr);
    this->Add(this->bat_info);

    const s32 strip_y = 80;
    const s32 strip_h = 70;
    this->tab_bar = pu::ui::elm::Rectangle::New(
        0, strip_y, sw, strip_h, g_theme->tab_bar_bg);
    this->Add(this->tab_bar);

    const char *labels[] = {tr(S_TAB_BROWSE), tr(S_TAB_INSTALLED), tr(S_TAB_QUEUE), tr(S_TAB_SETTINGS)};
    const s32 tab_y = strip_y + 16;
    const s32 seg = sw / 4;
    for (int i = 0; i < 4; i++) {
        auto tb = pu::ui::elm::TextBlock::New(0, tab_y, labels[i]);
        tb->SetColor(g_theme->tab_clr);
        tb->SetX(seg * i + (seg - tb->GetWidth()) / 2);
        this->Add(tb);
        this->tabs.push_back(tb);
    }
    this->tab_underline = pu::ui::elm::Rectangle::New(
        0, strip_y + strip_h - 5, 120, 5, g_theme->tab_under);
    this->Add(this->tab_underline);

    const s32 footer_h = 64;
    const s32 list_y = 158;
    const s32 row_h = 84;
    const s32 avail = sh - list_y - footer_h;
    const s32 rows_visible = avail / row_h;
    this->list = TableList::New(0, list_y, sw, row_h, rows_visible);
    this->Add(this->list);

    this->rom_info = pu::ui::elm::TextBlock::New(45, sh - footer_h - 38, "");
    this->rom_info->SetColor(g_theme->rom_info_clr);
    this->Add(this->rom_info);

    this->footer = pu::ui::elm::Rectangle::New(0, sh - footer_h, sw, footer_h,
                                               g_theme->footer_bg);
    this->Add(this->footer);
    for (int i = 0; i < 8; i++) {
        auto seg = pu::ui::elm::TextBlock::New(0, sh - footer_h + 14, "");
        seg->SetColor(g_theme->footer_clr);
        this->Add(seg);
        this->footer_segs.push_back(seg);
    }

    this->SetActiveTab(0);
}

void MainLayout::ApplyTheme() {
    this->SetBackgroundColor(g_theme->bg);
    this->header->SetColor(g_theme->header_bg);
    this->tab_bar->SetColor(g_theme->tab_bar_bg);
    this->footer->SetColor(g_theme->footer_bg);
    this->title->SetColor(g_theme->title_clr);
    this->status->SetColor(g_theme->status_clr);
    this->bat_info->SetColor(g_theme->status_clr);
    this->rom_info->SetColor(g_theme->rom_info_clr);
    this->tab_underline->SetColor(g_theme->tab_under);
    for (auto &s : this->footer_segs)
        s->SetColor(g_theme->footer_clr);
    for (auto &t : this->tabs)
        t->SetColor(g_theme->tab_clr);
    this->list->SetThemeColors(g_theme->tl_row_bg, g_theme->tl_row_alt,
                               g_theme->tl_focus, g_theme->tl_scroll,
                               g_theme->tl_mark);
}

void MainLayout::SetActiveTab(int idx) {
    if (idx < 0 || idx >= (int)this->tabs.size()) {
        return;
    }
    for (int i = 0; i < (int)this->tabs.size(); i++) {
        this->tabs[i]->SetColor(i == idx
                                    ? g_theme->tab_active
                                    : g_theme->tab_clr);
    }
    this->tab_underline->SetX(this->tabs[idx]->GetX());
    this->tab_underline->SetWidth(this->tabs[idx]->GetWidth());
}

void MainLayout::RefreshTabs() {
    const char *labels[] = {tr(S_TAB_BROWSE), tr(S_TAB_INSTALLED), tr(S_TAB_QUEUE), tr(S_TAB_SETTINGS)};
    const s32 seg = 1920 / 4;
    for (int i = 0; i < 4 && i < (int)this->tabs.size(); i++) {
        this->tabs[i]->SetText(labels[i]);
        this->tabs[i]->SetX(seg * i + (seg - this->tabs[i]->GetWidth()) / 2);
    }
}

void MainLayout::SetTitle(const std::string &t) {
    // Keep the app name visible alongside the per-screen breadcrumb.
    this->title->SetText(t.empty() ? std::string("TicoDL+")
                                   : std::string("TicoDL+     ") + t);
}
void MainLayout::SetRomInfo(const std::string &t) { this->rom_info->SetText(t); }
static void layout_status_bar(pu::ui::elm::TextBlock::Ref &storage,
                              pu::ui::elm::TextBlock::Ref &net,
                              pu::ui::elm::TextBlock::Ref &bat) {
    // Right-aligned, left-to-right: network | storage | battery.
    const s32 margin = 30, gap = 12;
    s32 sw = (s32)pu::ui::render::ScreenWidth;
    s32 bw = bat->GetWidth();
    bat->SetX(sw - margin - bw);
    s32 stw = storage->GetWidth();
    storage->SetX(bat->GetX() - gap - stw);
    s32 nw = net->GetWidth();
    net->SetX(storage->GetX() - gap - nw);
}

void MainLayout::SetStatus(const std::string &t) {
    this->status->SetText(t);
    layout_status_bar(this->status, this->net_icon, this->bat_info);
}
void MainLayout::SetNetColor(pu::ui::Color c) {
    this->net_icon->SetColor(c);
}
void MainLayout::SetNetIcon(const std::string &text, pu::ui::Color c) {
    this->net_icon->SetText(text);
    this->net_icon->SetColor(c);
    layout_status_bar(this->status, this->net_icon, this->bat_info);
}
void MainLayout::SetBatInfo(const std::string &t) {
    this->bat_info->SetText(t);
    layout_status_bar(this->status, this->net_icon, this->bat_info);
}
void MainLayout::SetSubtitle(const std::string &t) {
    // Split the hint on runs of 2+ spaces into segments, then center each
    // segment within an equal share of the row so they spread evenly.
    std::vector<std::string> segs;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && t[i] == ' ') {
            i++;
        }
        if (i >= t.size()) {
            break;
        }
        size_t end = t.size();
        for (size_t j = i; j + 1 < t.size(); j++) {
            if (t[j] == ' ' && t[j + 1] == ' ') {
                end = j;
                break;
            }
        }
        segs.push_back(t.substr(i, end - i));
        i = end;
    }

    const s32 sw = (s32)pu::ui::render::ScreenWidth;
    const s32 margin = 30;
    int n = (int)segs.size();
    for (int k = 0; k < (int)this->footer_segs.size(); k++) {
        if (k < n) {
            this->footer_segs[k]->SetText(segs[k]);
            s32 cell = (sw - 2 * margin) / (n > 0 ? n : 1);
            s32 center = margin + cell * k + cell / 2;
            this->footer_segs[k]->SetX(center -
                                       this->footer_segs[k]->GetWidth() / 2);
        } else {
            this->footer_segs[k]->SetText("");
        }
    }
}
void MainLayout::ClearMenu() { this->list->Clear(); this->rom_info->SetText(""); }
void MainLayout::AddRow(const std::string &name) {
    this->AddRow(name, g_theme->row_text);
}
void MainLayout::AddRow(const std::string &name, pu::ui::Color clr) {
    this->list->AddRow(name, clr);
}
void MainLayout::AddRow2(const std::string &left, const std::string &right,
                         pu::ui::Color lclr, pu::ui::Color rclr, float progress) {
    this->list->AddRow2(left, right, lclr, rclr, progress);
}
s32 MainLayout::Sel() { return this->list->GetSelected(); }
void MainLayout::SetSel(s32 i) { this->list->SetSelected(i); }
s32 MainLayout::RowCount() { return this->list->Count(); }
void MainLayout::MoveBy(s32 delta) { this->list->MoveBy(delta); }
void MainLayout::Step(s32 delta) { this->list->Step(delta); }
void MainLayout::MoveUp() { this->MoveBy(-1); }
void MainLayout::MoveDown() { this->MoveBy(1); }
void MainLayout::PageUp() { this->MoveBy(-this->list->RowsVisible()); }
void MainLayout::PageDown() { this->MoveBy(this->list->RowsVisible()); }
void MainLayout::ToggleMark(s32 i) { this->list->ToggleMark(i); }
int MainLayout::MarkedCount() { return this->list->MarkedCount(); }
const std::set<s32> &MainLayout::Marked() { return this->list->Marked(); }
void MainLayout::ClearMarks() { this->list->ClearMarks(); }

// ---- app: feedback --------------------------------------------------------
void MainApplication::Toast(const std::string &msg) {
    auto tb = pu::ui::elm::TextBlock::New(0, 0, msg);
    tb->SetColor(pu::ui::Color(255, 255, 255, 255));
    auto t = pu::ui::extras::Toast::New(tb, pu::ui::Color(46, 120, 78, 240));
    this->StartOverlayWithTimeout(t, 1200);
}

void MainApplication::ToastErr(const std::string &msg) {
    auto tb = pu::ui::elm::TextBlock::New(0, 0, msg);
    tb->SetColor(pu::ui::Color(255, 255, 255, 255));
    auto t = pu::ui::extras::Toast::New(tb, pu::ui::Color(160, 52, 52, 240));
    this->StartOverlayWithTimeout(t, 1500);
}

bool MainApplication::Confirm(const std::string &title, const std::string &msg) {
    // "Cancel" first so it's the default-highlighted (safe) option; B cancels.
    int r = this->CreateShowDialog(title, msg, {tr(S_CANCEL), tr(S_YES)}, false, {}, style_dialog);
    return r == 1;
}

void MainApplication::RefreshStatus() {
    uint64_t fb = fs_free_bytes("sdmc:/");
    uint64_t tb = fs_total_bytes("sdmc:/");
    u32 bat = 0;
    psmGetBatteryChargePercentage(&bat);
    PsmChargerType charger = PsmChargerType_Unconnected;
    psmGetChargerType(&charger);
    std::string sf = (fb == UINT64_MAX) ? std::string("?") : human_size(fb);
    std::string st = (tb == UINT64_MAX) ? std::string("?") : human_size(tb);
    if (sf.size() > 3 && st.size() > 3) {
        std::string fu = sf.substr(sf.rfind(' '));
        std::string tu = st.substr(st.rfind(' '));
        if (fu == tu) sf = sf.substr(0, sf.rfind(' '));
    }
    const char *plug = (charger != PsmChargerType_Unconnected) ? "+" : "";
    char s[80];
    snprintf(s, sizeof(s), "%s/%s", sf.c_str(), st.c_str());
    this->layout->SetStatus(s);
    char bs[32];
    snprintf(bs, sizeof(bs), "%u%%%s", (unsigned)bat, plug);
    this->layout->SetBatInfo(bs);

    NifmInternetConnectionType ntype = (NifmInternetConnectionType)0;
    u32 wstr = 0;
    NifmInternetConnectionStatus nst = (NifmInternetConnectionStatus)0;
    bool net = R_SUCCEEDED(nifmGetInternetConnectionStatus(&ntype, &wstr, &nst)) &&
               nst == NifmInternetConnectionStatus_Connected;
    if (net) {
        const char *bars[] = {"▂__", "▂▄_", "▂▄▆", "▂▄█"};
        // Wired (LAN adapter) reports wireless strength 0; show full bars.
        int lvl = (ntype == NifmInternetConnectionType_Ethernet) ? 3
                  : (wstr > 3)                                   ? 3
                                                                 : (int)wstr;
        this->layout->SetNetIcon(bars[lvl], pu::ui::Color(80, 210, 120, 255));
    } else {
        this->layout->SetNetIcon("───", pu::ui::Color(200, 60, 60, 255));
    }
}

static bool console_has_pin(const ConsoleGroup *g) {
    for (int i = 0; i < g->repo_count; i++)
        if (g->repos[i].pinned) return true;
    return false;
}

// ---- screens --------------------------------------------------------------
void MainApplication::GotoHome() {
    this->screen = Screen::Home;
    this->layout->ClearMenu();
    if (g_prefs.group_consoles) {
        this->layout->SetTitle(tr(S_TITLE_CONSOLES));
        this->layout->SetSubtitle(tr(S_SUB_HOME_GROUPED));
        // Build the shown consoles, sorted A-Z by their displayed label (the
        // full name), since the stored order is by folder key. g_home_map maps
        // each visible row back to its real console index (for open / delete).
        struct HomeRow {
            std::string label;
            int idx;
            bool pinned;
        };
        std::vector<HomeRow> rows;
        for (int i = 0; i < g_cfg.console_count; i++) {
            if (!g_cfg.consoles[i].shown) {
                continue;
            }
            char label[160];
            console_label(g_cfg.consoles[i].console, label, sizeof(label));
            rows.push_back({label, i, console_has_pin(&g_cfg.consoles[i])});
        }
        std::sort(rows.begin(), rows.end(),
                  [](const HomeRow &a, const HomeRow &b) {
                      if (a.pinned != b.pinned) return a.pinned > b.pinned;
                      return strcasecmp(a.label.c_str(), b.label.c_str()) < 0;
                  });
        g_home_map.clear();
        for (const auto &row : rows) {
            int rc = g_cfg.consoles[row.idx].repo_count;
            char rdir[1200];
            snprintf(rdir, sizeof(rdir), "%s/%s",
                     roms_root(&g_tico), g_cfg.consoles[row.idx].target);
            int fc = count_dir_entries(rdir);
            char cnt[96];
            char rc_str[32], fc_str[32];
            snprintf(rc_str, sizeof(rc_str), tr(S_N_REPOS), rc);
            if (fc > 0) {
                snprintf(fc_str, sizeof(fc_str), tr(S_N_APPS), fc);
                snprintf(cnt, sizeof(cnt), "%s · %s", rc_str, fc_str);
            } else {
                snprintf(cnt, sizeof(cnt), "%s", rc_str);
            }
            std::string display = row.pinned
                ? std::string("★ ") + row.label : row.label;
            this->layout->AddRow2(display, cnt,
                                  g_theme->row_text,
                                  count_color());
            g_home_map.push_back(row.idx);
        }
        if (g_home_map.empty()) {
            this->layout->AddRow(tr(S_NO_COLLECTIONS));
        }
    } else {
        this->layout->SetTitle(tr(S_TITLE_REPOS));
        this->layout->SetSubtitle(tr(S_SUB_HOME_FLAT));
        struct FlatRow { std::string label; bool pinned; };
        std::vector<FlatRow> flat_rows;
        for (int c = 0; c < g_cfg.console_count; c++) {
            if (!g_cfg.consoles[c].shown) continue;
            for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
                Repo *rp = &g_cfg.consoles[c].repos[r];
                char row[180];
                snprintf(row, sizeof(row), "%s[%s] %s - %s",
                         rp->pinned ? "★ " : "",
                         rp->enabled ? tr(S_ON) : tr(S_OFF),
                         g_cfg.consoles[c].target, rp->label);
                flat_rows.push_back({row, rp->pinned});
            }
        }
        for (const auto &fr : flat_rows)
            this->layout->AddRow(fr.label);
        if (flat_count() == 0) {
            this->layout->AddRow(tr(S_NO_REPOS));
        }
    }
    this->layout->SetSel(this->home_sel); // restore place
}

void MainApplication::GotoRepos(int ci) {
    this->screen = Screen::Repos;
    this->sel_ci = ci;
    ConsoleGroup *g = &g_cfg.consoles[ci];
    char ctitle[192];
    snprintf(ctitle, sizeof(ctitle), tr(S_CONSOLE_PREFIX), g->console);
    this->layout->SetTitle(ctitle);
    this->layout->SetSubtitle(tr(S_SUB_REPOS));
    this->layout->ClearMenu();
    for (int i = 0; i < g->repo_count; i++) {
        char row[180];
        snprintf(row, sizeof(row), "%s[%s] %s",
                 g->repos[i].pinned ? "★ " : "",
                 g->repos[i].enabled ? tr(S_ON) : tr(S_OFF),
                 g->repos[i].label);
        this->layout->AddRow(row);
    }
    if (g->repo_count == 0) {
        this->layout->AddRow(tr(S_NO_REPOS));
    }
    this->layout->SetSel(ci == this->repos_sel_ci ? this->repos_sel : 0);
}

void MainApplication::GotoFiles(int ci, int ri) {
    g_sort_mode = SORT_DEFAULT;
    g_files_manual = false;
    this->sel_ci = ci;
    this->sel_ri = ri;
    ConsoleGroup *g = &g_cfg.consoles[ci];
    Repo *rp = &g->repos[ri];
    this->layout->SetTitle(std::string(g->console) + " > " + rp->label);
    this->screen = Screen::Files;
    this->StartMetaLoad(rp->id, rp->download_base, g->target, false,
                        FILES_SUBTITLE);
}

// ---- tabs -----------------------------------------------------------------
MainApplication::Tab MainApplication::CurrentTab() {
    switch (this->screen) {
    case Screen::Installed: return Tab::Installed;
    case Screen::Queue:     return Tab::Queue;
    case Screen::Settings:
    case Screen::Log:
    case Screen::Manage:
    case Screen::Creds:
    case Screen::Advanced:
    case Screen::Downloads:
    case Screen::Language:
    case Screen::Cache:
    case Screen::ManageData: return Tab::Settings;
    default:                return Tab::Browse; // Home/Repos/Files/RepoEdit/Picker/Search
    }
}

void MainApplication::SyncTab() {
    this->layout->SetActiveTab((int)this->CurrentTab());
}

void MainApplication::GotoTab(Tab t) {
    switch (t) {
    case Tab::Browse:    this->GotoHome(); break;
    case Tab::Installed: this->GotoInstalled(roms_root(&g_tico)); break;
    case Tab::Queue:     this->GotoQueue(); break;
    case Tab::Settings:  this->GotoSettings(); break;
    }
}

void MainApplication::SwitchTab(int dir) {
    const int n = 4;
    int nx = (((int)this->CurrentTab() + dir) % n + n) % n;
    this->GotoTab((Tab)nx);
}

void MainApplication::GotoQueue() {
    this->screen = Screen::Queue;
    this->layout->SetTitle(tr(S_TITLE_QUEUE));
    this->layout->SetSubtitle(tr(S_SUB_QUEUE));
    this->layout->ClearMenu();
}

struct LangEntry { const char *code; const char *label; };
static const LangEntry g_langs[] = {
    {"en", "English"},
    {"es", "Español"},
    {"fr", "Français"},
    {"de", "Deutsch"},
    {"it", "Italiano"},
    {"pt", "Português"},
    {"nl", "Nederlands"},
    {"sv", "Svenska"},
    {"da", "Dansk"},
    {"nb", "Norsk"},
    {"fi", "Suomi"},
    {"pl", "Polski"},
    {"cs", "Čeština"},
    {"hu", "Magyar"},
    {"ro", "Română"},
    {"tr", "Türkçe"},
    {"el", "Ελληνικά"},
    {"ru", "Русский"},
    {"uk", "Українська"},
    {"ja", "日本語"},
    {"ko", "한국어"},
    {"zh", "中文"},
    {"zht", "中文(繁體)"},
    {"vi", "Tiếng Việt"},
    {"id", "Bahasa Indonesia"},
};
static const int g_lang_count = sizeof(g_langs) / sizeof(g_langs[0]);

static const char *lang_display_name(const char *code) {
    for (int i = 0; i < g_lang_count; i++) {
        if (strcmp(g_langs[i].code, code) == 0) return g_langs[i].label;
    }
    return code;
}

void MainApplication::GotoSettings() {
    this->screen = Screen::Settings;
    this->layout->SetTitle(std::string(tr(S_TITLE_SETTINGS)) + "   (v" + APP_VERSION_STR + ")");
    this->layout->SetSubtitle(tr(S_SUB_SETTINGS));
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_CHECK_UPDATES));          // 0
    this->layout->AddRow(tr(S_VIEW_LOG));               // 1
    this->layout->AddRow(tr(S_MANAGE_CONSOLES));        // 2
    this->layout->AddRow(tr(S_MANAGE_DATA));            // 3
    {
        char lb[64];
        const char *cur = g_prefs.lang[0] ? g_prefs.lang : "en";
        snprintf(lb, sizeof(lb), tr(S_LANGUAGE), lang_display_name(cur));
        this->layout->AddRow(lb);                       // 4
    }
    {
        char tb[64];
        snprintf(tb, sizeof(tb), tr(S_THEME),
                 is_light_theme() ? tr(S_THEME_LIGHT) : tr(S_THEME_DARK));
        this->layout->AddRow(tb);                       // 5
    }
    this->layout->AddRow(tr(S_ADVANCED));               // 6
    this->layout->AddRow(tr(S_CREDITS));                // 7
    char ri[600];
    snprintf(ri, sizeof(ri), tr(S_ROM_FOLDER), roms_root(&g_tico));
    this->layout->SetRomInfo(ri);
}

void MainApplication::GotoAdvanced() {
    this->screen = Screen::Advanced;
    this->layout->SetTitle(tr(S_TITLE_ADVANCED));
    this->layout->SetSubtitle(tr(S_SUB_ADVANCED));
    this->layout->ClearMenu();
    char r[96];
    snprintf(r, sizeof(r), tr(S_STAY_AWAKE),
             g_prefs.prevent_sleep ? tr(S_ON) : tr(S_OFF));
    this->layout->AddRow(r);                      // 0
    snprintf(r, sizeof(r), tr(S_GROUP_CONSOLES),
             g_prefs.group_consoles ? tr(S_ON) : tr(S_OFF));
    this->layout->AddRow(r);                      // 1
    snprintf(r, sizeof(r), tr(S_ARCHIVE_CREDS),
             g_creds.access_key[0] ? tr(S_SET) : tr(S_UNSET));
    this->layout->AddRow(r);                      // 2
    snprintf(r, sizeof(r), tr(S_META_CACHE), g_prefs.use_cache ? tr(S_ON) : tr(S_OFF));
    this->layout->AddRow(r);                      // 3
    snprintf(r, sizeof(r), tr(S_MAX_DOWNLOADS), g_prefs.max_downloads);
    this->layout->AddRow(r);                      // 4
    snprintf(r, sizeof(r), tr(S_NET_CHECK_STARTUP),
             g_prefs.net_check ? tr(S_ON) : tr(S_OFF));
    this->layout->AddRow(r);                      // 5
}

void MainApplication::GotoDownloads() {
    this->screen = Screen::Downloads;
    this->layout->SetTitle(tr(S_TITLE_DOWNLOADS));
    this->layout->SetSubtitle(tr(S_SUB_DOWNLOADS));
    this->layout->ClearMenu();
    g_dlfiles = list_dir(DL_TMP_DIR);
    // Remove directories — only show files
    g_dlfiles.erase(
        std::remove_if(g_dlfiles.begin(), g_dlfiles.end(),
                       [](const DirEnt &e) { return e.is_dir; }),
        g_dlfiles.end());
    uint64_t total = 0;
    for (auto &e : g_dlfiles) {
        this->layout->AddRow2(e.name, human_size(e.size),
                              g_theme->row_text,
                              size_color(e.size));
        total += e.size;
    }
    if (g_dlfiles.empty()) {
        this->layout->AddRow(tr(S_EMPTY));
    } else {
        char info[128];
        snprintf(info, sizeof(info), tr(S_DL_N_TOTAL),
                 (int)g_dlfiles.size(), human_size(total).c_str());
        this->layout->SetRomInfo(info);
    }
}

static std::vector<DirEnt> g_cache_files;

void MainApplication::GotoCache() {
    this->screen = Screen::Cache;
    this->layout->SetTitle(tr(S_TITLE_CACHE));
    this->layout->SetSubtitle(tr(S_SUB_CACHE));
    this->layout->ClearMenu();
    g_cache_files = list_dir(CACHE_DIR);
    g_cache_files.erase(
        std::remove_if(g_cache_files.begin(), g_cache_files.end(),
                       [](const DirEnt &e) { return e.is_dir; }),
        g_cache_files.end());
    uint64_t total = 0;
    for (auto &e : g_cache_files) {
        std::string label = e.name;
        if (label.size() > 5 &&
            strcasecmp(label.c_str() + label.size() - 5, ".json") == 0)
            label = label.substr(0, label.size() - 5);
        // Prefix the console short code of the repo this cache belongs to.
        for (int c = 0; c < g_cfg.console_count; c++) {
            bool hit = false;
            for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
                if (strcasecmp(g_cfg.consoles[c].repos[r].id,
                               label.c_str()) == 0) {
                    label = std::string("[") + g_cfg.consoles[c].target +
                            "] " + label;
                    hit = true;
                    break;
                }
            }
            if (hit) break;
        }
        this->layout->AddRow2(label, human_size(e.size),
                              g_theme->row_text, size_color(e.size));
        total += e.size;
    }
    if (g_cache_files.empty()) {
        this->layout->AddRow(tr(S_CACHE_EMPTY));
    } else {
        char info[128];
        snprintf(info, sizeof(info), tr(S_N_CACHED),
                 (int)g_cache_files.size(), human_size(total).c_str());
        this->layout->SetRomInfo(info);
    }
}

void MainApplication::GotoManageData() {
    this->screen = Screen::ManageData;
    this->layout->SetTitle(tr(S_TITLE_MANAGE_DATA));
    this->layout->SetSubtitle(tr(S_SUB_MANAGE_DATA));
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_MANAGE_DOWNLOADS)); // 0
    this->layout->AddRow(tr(S_MANAGE_CACHE));     // 1
}

struct SearchHit {
    std::string name;
    std::string url;
    std::string target;
    std::string md5;
    uint64_t size;
    bool is_archive;
};
static std::vector<SearchHit> g_search_results;
static std::string g_search_query;

void MainApplication::GotoSearch(const std::string &query) {
    this->screen = Screen::Search;
    g_search_query = query;
    g_search_results.clear();
    this->layout->SetTitle(tr(S_TITLE_SEARCH));
    this->layout->SetSubtitle(tr(S_SUB_SEARCH));
    this->layout->ClearMenu();

    // Map repo id -> target console folder for download context.
    struct RepoRef { std::string id; std::string target; std::string base; };
    std::vector<RepoRef> repos;
    for (int c = 0; c < g_cfg.console_count; c++) {
        for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
            Repo *rp = &g_cfg.consoles[c].repos[r];
            if (!rp->enabled || !rp->id[0]) continue;
            repos.push_back({rp->id, g_cfg.consoles[c].target,
                             rp->download_base});
        }
    }

    // Scan all cached metadata files.
    DIR *d = opendir(CACHE_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".json") != 0) continue;

            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", CACHE_DIR, e->d_name);
            size_t len = 0;
            char *body = json_read_file(path, &len);
            if (!body) continue;

            ArchiveItem item;
            memset(&item, 0, sizeof(item));
            // Extract identifier from filename (strip .json).
            char id_buf[256];
            snprintf(id_buf, sizeof(id_buf), "%.*s",
                     (int)(dot - e->d_name), e->d_name);
            snprintf(item.identifier, sizeof(item.identifier), "%s", id_buf);

            int ntok = 0;
            jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
            if (!tok || tok[0].type != JSMN_OBJECT) {
                free(tok); free(body); continue;
            }
            json_copy(body, tok, json_obj_get(body, tok, 0, "server"),
                      item.server, sizeof(item.server));
            json_copy(body, tok, json_obj_get(body, tok, 0, "dir"),
                      item.dir, sizeof(item.dir));

            // Find which configured repo this cache belongs to.
            std::string target, base;
            for (const auto &rr : repos) {
                // Cached filename uses sanitized id; match loosely.
                if (strcasecmp(rr.id.c_str(), id_buf) == 0) {
                    target = rr.target;
                    base = rr.base;
                    break;
                }
            }
            // Orphan cache (repo deleted/disabled): no target console, so a
            // download would land in the roms root. Skip it.
            if (target.empty()) {
                free(tok);
                free(body);
                continue;
            }
            if (base.empty() && item.identifier[0])
                snprintf(item.download_base, sizeof(item.download_base),
                         "https://archive.org/download/%s", item.identifier);
            else
                snprintf(item.download_base, sizeof(item.download_base),
                         "%s", base.c_str());

            int fi = json_obj_get(body, tok, 0, "files");
            if (fi >= 0 && tok[fi].type == JSMN_ARRAY) {
                int n = tok[fi].size, ch = fi + 1;
                for (int i = 0; i < n && g_search_results.size() < 200; i++) {
                    if (tok[ch].type == JSMN_OBJECT) {
                        char fname[512];
                        json_copy(body, tok,
                                  json_obj_get(body, tok, ch, "name"),
                                  fname, sizeof(fname));
                        if (fname[0] && ci_contains(fname, query.c_str())) {
                            SearchHit h;
                            h.name = fname;
                            h.target = target;
                            h.size = json_u64(body, tok,
                                json_obj_get(body, tok, ch, "size"));
                            char md5[33] = "";
                            json_copy(body, tok,
                                json_obj_get(body, tok, ch, "md5"),
                                md5, sizeof(md5));
                            h.md5 = md5;
                            h.is_archive = is_archive_name(fname);

                            // Build download URL.
                            ArchiveFile af;
                            memset(&af, 0, sizeof(af));
                            snprintf(af.name, sizeof(af.name), "%s", fname);
                            char url[1024];
                            ia_file_url(&item, &af, url, sizeof(url));
                            h.url = url;
                            g_search_results.push_back(h);
                        }
                    }
                    ch = json_tok_skip(tok, ch);
                }
            }
            free(tok);
            free(body);
        }
        closedir(d);
    }

    // Sort results alphabetically.
    std::sort(g_search_results.begin(), g_search_results.end(),
              [](const SearchHit &a, const SearchHit &b) {
                  return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
              });

    for (const auto &h : g_search_results) {
        std::string label = "[" + h.target + "] " + h.name;
        this->layout->AddRow2(label, human_size(h.size),
                              g_theme->row_text, size_color(h.size));
    }
    if (g_search_results.empty()) {
        this->layout->AddRow(tr(S_SEARCH_NO_RESULTS));
    } else {
        char info[64];
        snprintf(info, sizeof(info), tr(S_SEARCH_N_RESULTS),
                 (int)g_search_results.size());
        this->layout->SetRomInfo(info);
    }
}

void MainApplication::GotoLanguage() {
    this->screen = Screen::Language;
    this->layout->SetTitle(tr(S_TITLE_LANGUAGE));
    this->layout->SetSubtitle(tr(S_SUB_LANGUAGE));
    this->layout->ClearMenu();
    const char *cur = g_prefs.lang[0] ? g_prefs.lang : "en";
    for (int i = 0; i < g_lang_count; i++) {
        bool active = (strcmp(g_langs[i].code, cur) == 0);
        this->layout->AddRow2(
            g_langs[i].label, active ? "◀" : "",
            g_theme->row_text,
            pu::ui::Color(100, 210, 255, 255));
    }
}

void MainApplication::GotoManage() {
    this->screen = Screen::Manage;
    this->layout->SetTitle(tr(S_TITLE_MANAGE));
    this->layout->SetSubtitle(tr(S_SUB_MANAGE));
    this->layout->ClearMenu();
    for (int i = 0; i < g_cfg.console_count; i++) {
        bool sh = g_cfg.consoles[i].shown;
        this->layout->AddRow2(
            g_cfg.consoles[i].console, sh ? tr(S_SHOWN) : tr(S_HIDDEN),
            g_theme->row_text,
            sh ? pu::ui::Color(130, 225, 150, 255)   // green = shown
               : pu::ui::Color(150, 150, 162, 255)); // gray = hidden
    }
    if (g_cfg.console_count == 0) {
        this->layout->AddRow(tr(S_NO_CONSOLES));
    }
}

void MainApplication::GotoCreds() {
    this->screen = Screen::Creds;
    this->layout->SetTitle(tr(S_TITLE_CREDS));
    this->layout->SetSubtitle(tr(S_SUB_CREDS));
    this->layout->ClearMenu();
    char r[200];
    snprintf(r, sizeof(r), "%s: %.50s", tr(S_ACCESS_KEY),
             g_creds.access_key[0] ? g_creds.access_key : tr(S_UNSET));
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "%s: %s", tr(S_SECRET_KEY),
             g_creds.secret[0] ? tr(S_SET) : tr(S_UNSET));
    this->layout->AddRow(r);
    this->layout->AddRow(tr(S_CLEAR_CREDS));
}

void MainApplication::GotoInstalled(const std::string &path) {
    this->screen = Screen::Installed;
    this->inst_path = path;
    g_inst = list_dir(path);
    bool is_root = (path == roms_root(&g_tico));
    std::sort(g_inst.begin(), g_inst.end(), [is_root](const DirEnt &a, const DirEnt &b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        if (is_root && a.is_dir && b.is_dir) {
            bool pa = prefs_dir_pinned(&g_prefs, a.name.c_str());
            bool pb = prefs_dir_pinned(&g_prefs, b.name.c_str());
            if (pa != pb) return pa > pb;
            const char *fa = console_full_name(a.name.c_str());
            const char *fb = console_full_name(b.name.c_str());
            const char *sa = fa ? fa : a.name.c_str();
            const char *sb = fb ? fb : b.name.c_str();
            return strcasecmp(sa, sb) < 0;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    std::string shown = path;
    if (shown.rfind(roms_root(&g_tico), 0) == 0) {
        shown = "roms" + shown.substr(strlen(roms_root(&g_tico)));
    }
    this->layout->SetTitle(std::string(tr(S_TITLE_INSTALLED)) + ": " + shown);
    this->layout->SetSubtitle(tr(S_SUB_INSTALLED));
    this->layout->ClearMenu();
    for (int i = 0; i < (int)g_inst.size(); i++) {
        DirEnt &e = g_inst[i];
        if (e.is_dir) {
            int n = count_dir_entries(path + "/" + e.name);
            char cnt[32];
            snprintf(cnt, sizeof(cnt), tr(S_N_APPS), n);
            std::string label;
            if (path == roms_root(&g_tico) &&
                prefs_dir_pinned(&g_prefs, e.name.c_str()))
                label = "★ ";
            label += tr(S_DIR_PREFIX);
            const char *full = (path == roms_root(&g_tico))
                                   ? console_full_name(e.name.c_str())
                                   : nullptr;
            if (full) {
                label += full;
                label += " (";
                label += e.name;
                label += ")";
            } else {
                label += e.name;
            }
            this->layout->AddRow2(label, cnt,
                                  g_theme->row_text,
                                  count_color());
        } else {
            // File: right column is the size, tinted by magnitude.
            this->layout->AddRow2(e.name, human_size(e.size),
                                  g_theme->row_text,
                                  size_color(e.size));
        }
    }
    if (g_inst.empty()) {
        this->layout->AddRow(tr(S_EMPTY));
    }
}

void MainApplication::GotoRepoEdit(int ci, int ri) {
    this->screen = Screen::RepoEdit;
    this->sel_ci = ci;
    this->sel_ri = ri;
    Repo *rp = &g_cfg.consoles[ci].repos[ri];
    this->layout->SetTitle(std::string(tr(S_TITLE_EDIT_REPO)) + ": " + g_cfg.consoles[ci].console);
    this->layout->SetSubtitle(tr(S_SUB_EDIT_REPO));
    this->layout->ClearMenu();
    char r[600];
    snprintf(r, sizeof(r), tr(S_LABEL_NAME),
             rp->label[0] ? rp->label : tr(S_UNSET));
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), tr(S_LABEL_ARCHIVE_ID),
             rp->id[0] ? rp->id : tr(S_UNSET));
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), tr(S_LABEL_DOWNLOAD_URL),
             rp->download_base[0] ? rp->download_base : tr(S_AUTO));
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), tr(S_LABEL_ENABLED),
             rp->enabled ? tr(S_ON) : tr(S_OFF));
    this->layout->AddRow(r);
    this->layout->AddRow(tr(S_DELETE_REPO));
}

void MainApplication::GotoPicker(Pending what) {
    this->screen = Screen::Picker;
    this->pending = what;
    this->layout->SetTitle(tr(S_TITLE_SELECT_CONSOLE));
    this->layout->SetSubtitle(tr(S_SUB_SELECT_CONSOLE));
    this->layout->ClearMenu();

    // Build a sorted (A-Z) copy of the supported list so the picker is ordered;
    // the input handler reads back from g_picker by index.
    g_picker.clear();
    for (int i = 0; i < g_cfg.supported_count; i++) {
        g_picker.push_back(g_cfg.supported[i]);
    }
    std::sort(g_picker.begin(), g_picker.end(),
              [](const std::string &a, const std::string &b) {
                  const char *fa = console_full_name(a.c_str());
                  const char *fb = console_full_name(b.c_str());
                  return strcasecmp(fa ? fa : a.c_str(), fb ? fb : b.c_str()) < 0;
              });

    for (const auto &name : g_picker) {
        ConsoleGroup *g = config_find_console(&g_cfg, name.c_str());
        int rc = g ? g->repo_count : 0;
        char cnt[32];
        snprintf(cnt, sizeof(cnt), tr(S_N_REPOS), rc);
        char label[160];
        console_label(name.c_str(), label, sizeof(label));
        this->layout->AddRow2(label, cnt, g_theme->row_text,
                              count_color());
    }
    if (g_picker.empty()) {
        this->layout->AddRow(tr(S_NO_CONSOLES));
    }
}

struct LogEntry {
    std::string display;
    std::string url;
    std::string name;
    std::string target;
    std::string md5;
    uint64_t size;
    bool is_archive;
    bool can_retry;
};
static std::vector<LogEntry> g_log_entries;

static void parse_log_json(std::vector<LogEntry> &out) {
    out.clear();
    std::ifstream jf(DLLOG_JSON);
    if (!jf.is_open()) return;
    std::string line;
    while (std::getline(jf, line)) {
        if (line.empty() || line[0] != '{') continue;
        LogEntry e;
        e.size = 0;
        e.is_archive = false;
        e.can_retry = false;

        size_t len = line.size();
        const char *js = line.c_str();
        int ntok = 0;
        jsmntok_t *tok = json_parse_alloc(js, len, &ntok);
        if (!tok || tok[0].type != JSMN_OBJECT) { free(tok); continue; }

        char buf[1024];
        json_copy(js, tok, json_obj_get(js, tok, 0, "ts"), buf, sizeof(buf));
        e.display = buf;
        e.display += "  ";
        json_copy(js, tok, json_obj_get(js, tok, 0, "st"), buf, sizeof(buf));
        std::string st = buf;
        e.display += st;
        e.display += "  [";
        json_copy(js, tok, json_obj_get(js, tok, 0, "target"), buf, sizeof(buf));
        e.target = buf;
        e.display += buf;
        e.display += "]  ";
        json_copy(js, tok, json_obj_get(js, tok, 0, "name"), buf, sizeof(buf));
        e.name = buf;
        e.display += buf;
        json_copy(js, tok, json_obj_get(js, tok, 0, "url"), buf, sizeof(buf));
        e.url = buf;
        json_copy(js, tok, json_obj_get(js, tok, 0, "md5"), buf, sizeof(buf));
        e.md5 = buf;
        int si = json_obj_get(js, tok, 0, "size");
        if (si >= 0) e.size = json_u64(js, tok, si);
        int ai = json_obj_get(js, tok, 0, "arc");
        if (ai >= 0) e.is_archive = json_bool(js, tok, ai);
        e.can_retry = !e.url.empty();

        free(tok);
        out.push_back(e);
    }
}

void MainApplication::GotoLog() {
    if (this->screen != Screen::Log) {
        this->log_origin = this->screen;
    }
    this->screen = Screen::Log;
    this->layout->SetTitle(tr(S_TITLE_LOG));
    this->layout->SetSubtitle(tr(S_SUB_LOG));
    this->layout->ClearMenu();

    parse_log_json(g_log_entries);
    if (g_log_entries.empty()) {
        // Fall back to the text log for entries written before the JSON log existed.
        std::ifstream f(DLLOG_PATH);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(f, line))
            if (!line.empty()) lines.push_back(line);
        for (int i = (int)lines.size() - 1; i >= 0; i--) {
            LogEntry e;
            e.display = lines[i]; e.size = 0; e.is_archive = false;
            e.can_retry = false;
            g_log_entries.push_back(e);
        }
    } else {
        std::reverse(g_log_entries.begin(), g_log_entries.end());
    }
    for (const auto &e : g_log_entries) {
        this->layout->AddRow(e.display);
    }
    if (g_log_entries.empty()) {
        this->layout->AddRow(tr(S_NO_LOG));
    }
}

// ---- input ----------------------------------------------------------------
void MainApplication::HandleInput(u64 down, u64 held) {
    // One-shot startup dialogs, deferred from OnLoad so they render over a
    // live frame instead of a black screen.
    if (this->startup_checks) {
        this->startup_checks = false;
        if (!g_tico.installed) {
            char tmsg[512];
            snprintf(tmsg, sizeof(tmsg), tr(S_TICO_NOT_FOUND_MSG),
                     roms_root(&g_tico));
            // No cancel option: "Exit" must be a real option (a cancel option
            // makes CreateShowDialog return -1, not its index). B (= -1)
            // dismisses the warning and continues.
            int opt = this->CreateShowDialog(
                tr(S_TICO_NOT_FOUND), tmsg,
                {tr(S_CONTINUE), tr(S_EXIT)}, false, {}, style_dialog);
            if (opt == 1) {
                this->Close();
                return;
            }
        }
        if (g_prefs.net_check) {
            for (;;) {
                NifmInternetConnectionType ntype = (NifmInternetConnectionType)0;
                u32 wstr = 0;
                NifmInternetConnectionStatus nst = (NifmInternetConnectionStatus)0;
                bool net = R_SUCCEEDED(nifmGetInternetConnectionStatus(
                               &ntype, &wstr, &nst)) &&
                           nst == NifmInternetConnectionStatus_Connected;
                if (net) break;
                // "Exit" must be a real option, not the cancel option — a
                // cancel option returns -1 from CreateShowDialog, so "opt ==
                // 2" would never match and Exit silently fell through to
                // Continue. B (= -1) dismisses and continues.
                int opt = this->CreateShowDialog(
                    tr(S_NO_NETWORK), tr(S_NO_NETWORK_MSG),
                    {tr(S_RETRY), tr(S_CONTINUE), tr(S_EXIT)}, false, {},
                    style_dialog);
                if (opt == 0) continue;      // Retry
                if (opt == 2) { this->Close(); return; } // Exit
                break;                       // Continue (1) or B
            }
        }
        return;
    }
    // A self-update download owns the UI while it runs: drive its progress /
    // finish and swallow all other input until it completes.
    if (this->upd_running) {
        if (down & HidNpadButton_B) {
            this->upd_cancel = true;
        }
        this->UpdTick();
        return;
    }

    // An update *check* is fetching release info on a background thread: show
    // the attempt counter and swallow input until it finishes. B dismisses it —
    // the fetch can't be aborted mid-request, so it finishes silently in the
    // background and the result is discarded.
    if (this->chk_running) {
        if (!this->chk_discard) {
            if (down & HidNpadButton_B) {
                this->chk_discard = true;
                this->GotoSettings(); // restore the normal screen + hints
                return; // consume the B press — don't leak it to the screen
            }
            this->ChkTick();
            return;
        }
        // Dismissed: reap the thread when it finishes; input flows normally.
        if (this->chk_done) {
            threadWaitForExit(&this->chk_thread);
            threadClose(&this->chk_thread);
            this->chk_running = false;
        }
    }

    // A repo's metadata is loading on a background thread: animate the indicator
    // and swallow input until it's ready.
    if (this->meta_running) {
        (void)down;
        (void)held;
        this->MetaTick();
        return;
    }

    // Keep the tab bar highlight in sync with whatever screen we're on.
    this->SyncTab();

    // Remember the current selection per browseable list so backing out and
    // returning keeps your place.
    switch (this->screen) {
    case Screen::Home:
        this->home_sel = this->layout->Sel();
        break;
    case Screen::Repos:
        this->repos_sel = this->layout->Sel();
        this->repos_sel_ci = this->sel_ci;
        break;
    case Screen::Files:
        this->files_sel = this->layout->Sel();
        this->files_sel_id = g_files_id;
        break;
    default:
        break;
    }

    // Completion toasts: notice downloads reaching a terminal state on any
    // screen, so you know they finished even from another tab. Only do the
    // (locking) snapshot while the queue is active, plus a couple of frames
    // after it drains so the final done/failed transition is still caught.
    {
        static QStatus last[QUEUE_MAX];
        static bool init = false;
        static int idle = 1000;
        static QueueView cqv[QUEUE_MAX];
        idle = queue_active_count() > 0 ? 0 : (idle < 1000 ? idle + 1 : idle);
        if (idle <= 2) {
            int n = queue_snapshot(cqv, QUEUE_MAX);
            QStatus cur[QUEUE_MAX];
            for (int s = 0; s < QUEUE_MAX; s++) {
                cur[s] = Q_FREE;
            }
            for (int k = 0; k < n; k++) {
                cur[cqv[k].slot] = cqv[k].item.status;
            }
            if (init) {
                for (int k = 0; k < n; k++) {
                    int slot = cqv[k].slot;
                    QStatus s = cqv[k].item.status;
                    QStatus prev = last[slot];
                    bool now_term =
                        (s == Q_DONE || s == Q_SAVED || s == Q_FAILED);
                    bool was_term = (prev == Q_DONE || prev == Q_SAVED ||
                                     prev == Q_FAILED || prev == Q_FREE);
                    if (now_term && !was_term) {
                        char nm[48];
                        snprintf(nm, sizeof(nm), "%.44s", cqv[k].item.name);
                        if (s == Q_FAILED) {
                            this->ToastErr(std::string(tr(S_TOAST_FAILED)) + nm);
                        } else {
                            this->Toast(std::string(s == Q_SAVED ? tr(S_TOAST_SAVED)
                                                                 : tr(S_TOAST_DONE)) +
                                        nm);
                        }
                    }
                }
            }
            memcpy(last, cur, sizeof(last));
            init = true;
        }
    }

    // Live-refresh the queue list while it's open.
    if (this->screen == Screen::Queue) {
        static QueueView qv[QUEUE_MAX];
        s32 keep = this->layout->Sel();
        int n = queue_snapshot(qv, QUEUE_MAX);
        this->layout->ClearMenu();
        for (int i = 0; i < n; i++) {
            const QueueItem *it = &qv[i].item;
            char info[80] = "";
            float prog = -1.0f; // no bar unless actively downloading
            if (it->status == Q_DOWNLOADING && it->total) {
                prog = (float)it->now / (float)it->total;
                // The bar shows percent; the text gives size, speed and ETA.
                if (it->speed) {
                    uint64_t eta = (it->total > it->now)
                                       ? (it->total - it->now) / it->speed
                                       : 0;
                    snprintf(info, sizeof(info), "%s @ %s/s  ~%s",
                             human_size(it->total).c_str(),
                             human_size(it->speed).c_str(),
                             human_eta(eta).c_str());
                } else {
                    snprintf(info, sizeof(info), "%s",
                             human_size(it->total).c_str());
                }
            } else if (it->status == Q_PAUSED && it->total) {
                // Where the download stopped / will resume from.
                snprintf(info, sizeof(info), "%s / %s",
                         human_size(it->now).c_str(),
                         human_size(it->total).c_str());
            } else if (it->status == Q_FAILED && it->fail_reason[0]) {
                snprintf(info, sizeof(info), "%s", it->fail_reason);
            } else if (it->status == Q_DONE || it->status == Q_SAVED) {
                // Result tag: (repl) = replaced an existing file (with a count
                // for multi-file archives), (new) = a brand-new file. The cell
                // colour reinforces it (orange/green, set below).
                char sz[24];
                if (it->total) {
                    snprintf(sz, sizeof(sz), "%s  ",
                             human_size(it->total).c_str());
                } else {
                    sz[0] = '\0';
                }
                if (it->overwrote > 1) {
                    snprintf(info, sizeof(info), "%s(repl %d)", sz,
                             it->overwrote);
                } else if (it->overwrote == 1) {
                    snprintf(info, sizeof(info), "%s(repl)", sz);
                } else {
                    snprintf(info, sizeof(info), "%s(new)", sz);
                }
            } else if (it->total) {
                snprintf(info, sizeof(info), "%s", human_size(it->total).c_str());
            }
            char left[560];
            snprintf(left, sizeof(left), "%-6s [%s] %s", qstatus(it->status),
                     it->target, it->name);
            pu::ui::Color c = qstatus_color(it->status);
            pu::ui::Color rc = c;
            // Colour the result column by outcome: orange = replaced, green = new.
            if (it->status == Q_DONE || it->status == Q_SAVED) {
                rc = it->overwrote > 0 ? pu::ui::Color(245, 170, 90, 255)
                                       : pu::ui::Color(130, 225, 150, 255);
            }
            this->layout->AddRow2(left, info, c, rc, prog);
        }
        if (n == 0) {
            this->layout->AddRow(tr(S_QUEUE_EMPTY));
        }
        this->layout->SetSel(keep);
    }

    // SD/battery/network refresh at most ~every 10s (psm/statvfs aren't free,
    // and the input callback runs every frame — uncapped fps would spam them).
    // 10s keeps the network indicator honest while downloads wait offline.
    {
        static u64 last = 0;
        u64 now = armGetSystemTick();
        if (last == 0 || armTicksToNs(now - last) >= 10000000000ULL) {
            this->RefreshStatus();
            last = now;
        }
    }

    // Keep the console awake while downloads are active (main-thread only).
    {
        static bool cur = false;
        bool want = g_prefs.prevent_sleep && (queue_active_count() > 0);
        if (want != cur) {
            appletSetMediaPlaybackState(want);
            cur = want;
        }
    }

    // List selection: single press moves once; holding auto-repeats after a
    // short delay. Wraps around the ends (top<->bottom). Both the D-pad and the
    // left analog stick navigate. (TableList is passive, so the app owns it.)
    const u64 NAV_UP = HidNpadButton_Up | HidNpadButton_StickLUp;
    const u64 NAV_DOWN = HidNpadButton_Down | HidNpadButton_StickLDown;
    if (down & NAV_DOWN) {
        this->layout->Step(1);
    }
    if (down & NAV_UP) {
        this->layout->Step(-1);
    }
    {
        static int hold = 0;
        int dir = (held & NAV_DOWN)  ? 1
                  : (held & NAV_UP)  ? -1
                                     : 0;
        if (dir == 0) {
            hold = 0;
        } else {
            hold++;
            if (hold > 22 && ((hold - 22) % 3) == 0) {
                this->layout->Step(dir);
            }
        }
    }
    // ZL/ZR page lists, except in the queue where they reorder the selected
    // item (handled in the Queue case).
    if ((down & HidNpadButton_ZL) && this->screen != Screen::Queue) {
        this->layout->PageUp();
    }
    if ((down & HidNpadButton_ZR) && this->screen != Screen::Queue) {
        this->layout->PageDown();
    }
    if (down & HidNpadButton_Plus) {
        int active = queue_active_count();
        if (active > 0) {
            char msg[80];
            snprintf(msg, sizeof(msg), tr(S_EXIT_CONFIRM), active);
            if (!this->Confirm(tr(S_EXIT), msg)) {
                return;
            }
        }
        this->Close();
        return;
    }

    // L/R cycle the top tabs (Browse | Installed | Queue | Settings). Inside a
    // file list, the previous/next repo of a console is on D-pad Left/Right.
    if (down & HidNpadButton_L) {
        this->SwitchTab(-1);
        return;
    }
    if (down & HidNpadButton_R) {
        this->SwitchTab(+1);
        return;
    }

    switch (this->screen) {
    case Screen::Home: {
        if (g_prefs.group_consoles) {
            s32 sel = this->layout->Sel();
            bool valid = sel >= 0 && sel < (s32)g_home_map.size();
            if ((down & HidNpadButton_A) && valid) {
                this->GotoRepos(g_home_map[sel]);
            } else if ((down & HidNpadButton_Right) && valid) {
                // Pin/unpin — D-pad Right, same as on every other screen.
                int ci = g_home_map[sel];
                ConsoleGroup *g = &g_cfg.consoles[ci];
                bool was = console_has_pin(g);
                for (int r = 0; r < g->repo_count; r++)
                    g->repos[r].pinned = !was;
                config_save(&g_cfg);
                this->GotoHome();
                this->layout->SetSel(0);
            } else if (down & HidNpadButton_Minus) {
                char q[256] = {0};
                if (prompt_raw(tr(S_SEARCH_PROMPT), nullptr, q, sizeof(q)) &&
                    q[0]) {
                    this->GotoSearch(q);
                    return;
                }
            } else if (down & HidNpadButton_Y) {
                this->GotoPicker(Pending::AddRepo);
            }
        } else {
            int ci, ri;
            if ((down & HidNpadButton_A) &&
                flat_ref(this->layout->Sel(), &ci, &ri)) {
                this->GotoFiles(ci, ri);
            } else if ((down & HidNpadButton_X) &&
                       flat_ref(this->layout->Sel(), &ci, &ri)) {
                this->GotoRepoEdit(ci, ri);
            } else if (down & HidNpadButton_Y) {
                this->GotoPicker(Pending::AddRepo);
            } else if ((down & HidNpadButton_Minus) &&
                       flat_ref(this->layout->Sel(), &ci, &ri)) {
                if (this->Confirm(tr(S_DELETE_REPO), tr(S_DELETE_REPO_CONFIRM))) {
                    config_remove_repo(&g_cfg.consoles[ci], ri);
                    config_save(&g_cfg);
                    this->Toast(tr(S_DELETED));
                    this->GotoHome();
                }
            } else if ((down & HidNpadButton_Right) &&
                       flat_ref(this->layout->Sel(), &ci, &ri)) {
                // Pin/unpin — D-pad Right, same as on every other screen.
                // Pinned repos partition to the top within their console.
                ConsoleGroup *g = &g_cfg.consoles[ci];
                g->repos[ri].pinned = !g->repos[ri].pinned;
                Repo tmp[MAX_REPOS];
                int n = 0, newpos = 0;
                for (int i = 0; i < g->repo_count; i++)
                    if (g->repos[i].pinned) {
                        if (i == ri) newpos = n;
                        tmp[n++] = g->repos[i];
                    }
                for (int i = 0; i < g->repo_count; i++)
                    if (!g->repos[i].pinned) {
                        if (i == ri) newpos = n;
                        tmp[n++] = g->repos[i];
                    }
                memcpy(g->repos, tmp, sizeof(Repo) * g->repo_count);
                config_save(&g_cfg);
                this->GotoHome();
                // Follow the toggled repo to its new flat position.
                int base = 0;
                for (int c = 0; c < ci; c++)
                    if (g_cfg.consoles[c].shown)
                        base += g_cfg.consoles[c].repo_count;
                this->layout->SetSel(base + newpos);
            }
        }
        break;
    }

    case Screen::Repos: {
        ConsoleGroup *g = &g_cfg.consoles[this->sel_ci];
        if (down & HidNpadButton_B) {
            this->GotoHome();
        } else if ((down & HidNpadButton_A) && g->repo_count > 0) {
            this->GotoFiles(this->sel_ci, this->layout->Sel());
        } else if ((down & HidNpadButton_X) && g->repo_count > 0) {
            this->GotoRepoEdit(this->sel_ci, this->layout->Sel());
        } else if (down & HidNpadButton_Y) {
            char nm[64] = {0}, id[256] = {0};
            if (prompt(tr(S_LABEL_NAME), nullptr, nm, sizeof(nm)) &&
                prompt(tr(S_LABEL_ARCHIVE_ID), nullptr, id, sizeof(id))) {
                if (config_add_repo(g, nm, id)) {
                    config_save(&g_cfg);
                    this->Toast(tr(S_ADDED));
                }
            }
            this->GotoRepos(this->sel_ci);
        } else if ((down & HidNpadButton_Minus) && g->repo_count > 0) {
            if (this->Confirm(tr(S_DELETE_REPO), tr(S_DELETE_REPO_CONFIRM))) {
                config_remove_repo(g, this->layout->Sel());
                config_save(&g_cfg);
                this->Toast(tr(S_DELETED));
                this->GotoRepos(this->sel_ci);
            }
        } else if ((down & HidNpadButton_Right) && g->repo_count > 0) {
            s32 sel = this->layout->Sel();
            if (sel >= 0 && sel < g->repo_count) {
                g->repos[sel].pinned = !g->repos[sel].pinned;
                // Stable-partition pinned repos to the top; track where the
                // toggled repo lands so the selection can follow it.
                Repo tmp[MAX_REPOS];
                int n = 0, newpos = 0;
                for (int i = 0; i < g->repo_count; i++)
                    if (g->repos[i].pinned) {
                        if (i == sel) newpos = n;
                        tmp[n++] = g->repos[i];
                    }
                for (int i = 0; i < g->repo_count; i++)
                    if (!g->repos[i].pinned) {
                        if (i == sel) newpos = n;
                        tmp[n++] = g->repos[i];
                    }
                memcpy(g->repos, tmp, sizeof(Repo) * g->repo_count);
                config_save(&g_cfg);
                this->GotoRepos(this->sel_ci);
                this->layout->SetSel(newpos);
            }
        }
        break;
    }

    case Screen::Files: {
        if (down & HidNpadButton_B) {
            if (g_files_manual || !g_prefs.group_consoles) {
                this->GotoHome();
            } else {
                this->GotoRepos(this->sel_ci);
            }
        } else if (down & HidNpadButton_A) {
            if (g_have_item) {
                s32 i = this->layout->Sel();
                if (i >= 0 && i < (s32)g_files.size()) {
                    ArchiveFile *f = &g_item.files[g_files[i]];
                    char url[1024];
                    ia_file_url(&g_item, f, url, sizeof(url));
                    char auth[320];
                    creds_auth_header(&g_creds, auth, sizeof(auth));
                    bool ok = queue_add(url, f->name, g_files_target, auth,
                                        f->size, is_archive_name(f->name),
                                        f->md5);
                    if (ok) {
                        this->Toast(std::string(tr(S_QUEUED)) + ": " + f->name);
                    } else {
                        this->ToastErr(tr(S_QUEUE_FULL));
                    }
                }
            }
        } else if (down & HidNpadButton_Minus) {
            // Download all: queue every file matching the current filter.
            if (g_have_item && !g_files.empty()) {
                uint64_t total_sz = 0;
                for (int k = 0; k < (int)g_files.size(); k++) {
                    total_sz += g_item.files[g_files[k]].size;
                }
                uint64_t avail = fs_free_bytes("sdmc:/");
                char msg[300];
                snprintf(msg, sizeof(msg), tr(S_QUEUE_ALL_CONFIRM),
                         (int)g_files.size(),
                         human_size(total_sz).c_str(),
                         avail != UINT64_MAX
                             ? human_size(avail).c_str() : "?");
                if (avail != UINT64_MAX && total_sz > avail) {
                    size_t ml = strlen(msg);
                    snprintf(msg + ml, sizeof(msg) - ml, "\n\n%s",
                             tr(S_FREE_SPACE_WARN));
                }
                if (this->Confirm(tr(S_DOWNLOAD_ALL), msg)) {
                    char auth[320];
                    creds_auth_header(&g_creds, auth, sizeof(auth));
                    int ok = 0;
                    bool full = false;
                    for (int k = 0; k < (int)g_files.size(); k++) {
                        ArchiveFile *f = &g_item.files[g_files[k]];
                        char url[1024];
                        ia_file_url(&g_item, f, url, sizeof(url));
                        if (queue_add(url, f->name, g_files_target, auth,
                                      f->size, is_archive_name(f->name),
                                      f->md5)) {
                            ok++;
                        } else {
                            full = true;
                            break;
                        }
                    }
                    char t[80];
                    if (full) {
                        snprintf(t, sizeof(t), tr(S_QUEUED_N_FULL), ok);
                        this->ToastErr(t);
                    } else {
                        snprintf(t, sizeof(t), tr(S_QUEUED_N), ok);
                        this->Toast(t);
                    }
                }
            }
        } else if (down & HidNpadButton_Y) {
            char fb[64] = {0};
            if (prompt_raw(tr(S_FILTER_GUIDE), g_filter.c_str(), fb,
                           sizeof(fb))) {
                g_filter = fb;
                rebuild_files(this->layout.get(), g_files_target);
            }
        } else if (down & HidNpadButton_X) {
            g_sort_mode = (g_sort_mode + 1) % SORT__COUNT;
            static const int sort_keys[] = {
                S_SORT_DEFAULT, S_SORT_NAME_AZ, S_SORT_NAME_ZA,
                S_SORT_SIZE_DESC, S_SORT_SIZE_ASC
            };
            this->Toast(tr(sort_keys[g_sort_mode]));
            s32 keep = this->layout->Sel();
            rebuild_files(this->layout.get(), g_files_target);
            if (keep >= 0 && keep < this->layout->RowCount())
                this->layout->SetSel(keep);
        } else if ((down & (HidNpadButton_Left | HidNpadButton_Right)) &&
                   !g_files_manual) {
            // Switch to the previous/next repo of the same console (L/R now
            // cycle the top tabs, so repo switching lives on D-pad Left/Right).
            ConsoleGroup *g = &g_cfg.consoles[this->sel_ci];
            if (g->repo_count > 1) {
                int dir = (down & HidNpadButton_Right) ? 1 : -1;
                int nr = (this->sel_ri + dir + g->repo_count) % g->repo_count;
                this->GotoFiles(this->sel_ci, nr);
            }
        }
        break;
    }

    case Screen::Queue: {
        if (down & HidNpadButton_B) {
            this->GotoHome();
        } else if (down & HidNpadButton_Minus) {
            this->GotoLog();
            return;
        } else if (down & HidNpadButton_Y) {
            queue_clear_finished();
            this->Toast(tr(S_CLEARED));
        } else {
            static QueueView qv[QUEUE_MAX];
            int n = queue_snapshot(qv, QUEUE_MAX);
            s32 i = this->layout->Sel();
            if (i >= 0 && i < n) {
                if (down & HidNpadButton_A) {
                    QStatus s = qv[i].item.status;
                    bool cancellable =
                        (s == Q_QUEUED || s == Q_PAUSED || s == Q_DOWNLOADING ||
                         s == Q_VERIFYING || s == Q_AWAIT_EXTRACT ||
                         s == Q_EXTRACTING);
                    if (cancellable &&
                        this->Confirm(tr(S_CANCEL),
                                      std::string(qv[i].item.name) + "?")) {
                        queue_cancel(qv[i].slot);
                        this->Toast(tr(S_CANCELLED));
                    }
                } else if (down & HidNpadButton_X) {
                    queue_retry(qv[i].slot);
                    this->Toast(tr(S_RETRYING));
                } else if (down & HidNpadButton_ZL) {
                    if (queue_move(qv[i].slot, -1)) {
                        this->layout->SetSel(i - 1); // follow the moved item
                    }
                } else if (down & HidNpadButton_ZR) {
                    if (queue_move(qv[i].slot, 1)) {
                        this->layout->SetSel(i + 1);
                    }
                }
            }
        }
        break;
    }

    case Screen::Settings: {
        if (down & HidNpadButton_B) {
            this->GotoHome();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            switch (i) {
            case 0: // Check for updates (on a background thread: the release
                    // fetch retries transient errors and would freeze the UI)
                this->ChkStart();
                return;
            case 1: // View download log
                this->GotoLog();
                return;
            case 2: // Manage consoles
                this->GotoManage();
                return;
            case 3: // Manage data (downloads folder + metadata cache)
                this->GotoManageData();
                return;
            case 4: // Language
                this->GotoLanguage();
                return;
            case 5: // Theme toggle
            {
                if (is_light_theme())
                    strcpy(g_prefs.theme, "dark");
                else
                    strcpy(g_prefs.theme, "light");
                select_theme();
                this->layout->ApplyTheme();
                prefs_save(&g_prefs);
                this->SyncTab();
                break;
            }
            case 6: // Advanced
                this->GotoAdvanced();
                return;
            case 7: // Credits
                this->CreateShowDialog(
                    tr(S_CREDITS),
                    std::string("TicoDL+ v") + APP_VERSION_STR + " by digdat0\n\n"
                    "Plutonium UI library provided by XorTroll\n\n"
                    "TICO emulator - https://ticoverse.com/",
                    {tr(S_OK)}, true, {}, style_dialog);
                break;
            default:
                break;
            }
            if (this->screen == Screen::Settings) {
                this->GotoSettings();
            }
        }
        break;
    }

    case Screen::Advanced: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            switch (i) {
            case 0:
                g_prefs.prevent_sleep = !g_prefs.prevent_sleep;
                prefs_save(&g_prefs);
                break;
            case 1:
                g_prefs.group_consoles = !g_prefs.group_consoles;
                prefs_save(&g_prefs);
                break;
            case 2:
                this->GotoCreds();
                return;
            case 3:
                g_prefs.use_cache = !g_prefs.use_cache;
                prefs_save(&g_prefs);
                break;
            case 4:
                g_prefs.max_downloads = (g_prefs.max_downloads % 5) + 1;
                queue_set_max_dl(g_prefs.max_downloads);
                prefs_save(&g_prefs);
                break;
            case 5:
                g_prefs.net_check = !g_prefs.net_check;
                prefs_save(&g_prefs);
                break;
            default:
                break;
            }
            if (this->screen == Screen::Advanced) {
                s32 sel = this->layout->Sel();
                this->GotoAdvanced();
                this->layout->SetSel(sel);
            }
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            s32 i = this->layout->Sel();
            if (i == 4) {
                if (down & HidNpadButton_Right) {
                    g_prefs.max_downloads = (g_prefs.max_downloads % 5) + 1;
                } else {
                    g_prefs.max_downloads = (g_prefs.max_downloads <= 1) ? 5 : g_prefs.max_downloads - 1;
                }
                queue_set_max_dl(g_prefs.max_downloads);
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoAdvanced();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::Language: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < g_lang_count) {
                const char *code = g_langs[i].code;
                if (strcmp(code, "en") == 0) {
                    g_prefs.lang[0] = '\0';
                    i18n_load(NULL);
                } else {
                    snprintf(g_prefs.lang, sizeof(g_prefs.lang), "%s", code);
                    char path[256];
                    snprintf(path, sizeof(path), LANG_DIR "/%s.json", code);
                    if (!fs_exists(path))
                        snprintf(path, sizeof(path), "romfs:/lang/%s.json", code);
                    i18n_load(path);
                }
                prefs_save(&g_prefs);
                this->layout->RefreshTabs();
                this->SyncTab();
                this->GotoLanguage();
                this->layout->SetSel(i);
            }
        }
        break;
    }

    case Screen::ManageData: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->GotoDownloads(); return;
            case 1: this->GotoCache(); return;
            default: break;
            }
        }
        break;
    }

    case Screen::Downloads: {
        if (down & HidNpadButton_B) {
            this->GotoManageData();
        } else if (down & HidNpadButton_Y) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_dlfiles.size()) {
                this->layout->ToggleMark(i);
            }
        } else if (down & HidNpadButton_A) {
            // File info (A = open/inspect everywhere; never destructive).
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_dlfiles.size()) {
                this->CreateShowDialog(
                    tr(S_FILE),
                    g_dlfiles[i].name + "\n" + human_size(g_dlfiles[i].size),
                    {tr(S_OK)}, true, {}, style_dialog);
            }
        } else if (down & HidNpadButton_X) {
            // Delete all (X = bulk destructive action, same as Log/Cache)
            if (g_dlfiles.empty()) break;
            int qc = queue_active_count();
            char dmsg[256];
            snprintf(dmsg, sizeof(dmsg), tr(S_DELETE_ALL_CONFIRM), (int)g_dlfiles.size());
            std::string msg = dmsg;
            if (qc > 0) {
                char wmsg[128];
                snprintf(wmsg, sizeof(wmsg), tr(S_DL_ACTIVE_WARN), qc);
                msg += wmsg;
            }
            if (this->Confirm(tr(S_DELETE_ALL), msg)) {
                if (qc > 0) {
                    for (auto &e : g_dlfiles)
                        queue_cancel_by_part(e.name.c_str(), true);
                }
                for (auto &e : g_dlfiles) {
                    std::string fp = std::string(DL_TMP_DIR) + "/" + e.name;
                    remove(fp.c_str());
                }
                this->Toast(tr(S_DL_CLEARED));
                this->GotoDownloads();
            }
        } else if (down & HidNpadButton_Minus) {
            int mc = this->layout->MarkedCount();
            if (mc > 0) {
                // Multi-select delete
                bool has_queued = false;
                auto marks = this->layout->Marked();
                for (auto it = marks.begin(); it != marks.end(); ++it) {
                    s32 idx = *it;
                    if (idx >= 0 && idx < (s32)g_dlfiles.size() &&
                        queue_cancel_by_part(g_dlfiles[idx].name.c_str(), false) > 0) {
                        has_queued = true;
                    }
                }
                char msg[128];
                snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), mc);
                std::string full = msg;
                if (has_queued)
                    full += tr(S_DL_QUEUE_WARN);
                if (this->Confirm(tr(S_DELETE), full)) {
                    for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
                        s32 idx = *it;
                        if (idx >= 0 && idx < (s32)g_dlfiles.size()) {
                            queue_cancel_by_part(g_dlfiles[idx].name.c_str(), true);
                            std::string fp = std::string(DL_TMP_DIR) + "/" + g_dlfiles[idx].name;
                            remove(fp.c_str());
                        }
                    }
                    char t[32];
                    snprintf(t, sizeof(t), tr(S_DELETED_N), mc);
                    this->Toast(t);
                    this->GotoDownloads();
                }
            } else {
                // Single delete
                s32 i = this->layout->Sel();
                if (i >= 0 && i < (s32)g_dlfiles.size()) {
                    std::string msg = std::string(tr(S_DELETE)) + " '" + g_dlfiles[i].name + "'?";
                    bool in_queue = queue_cancel_by_part(g_dlfiles[i].name.c_str(), false) > 0;
                    if (in_queue)
                        msg += tr(S_DL_QUEUE_WARN);
                    if (this->Confirm(tr(S_DELETE), msg)) {
                        if (in_queue)
                            queue_cancel_by_part(g_dlfiles[i].name.c_str(), true);
                        std::string fp = std::string(DL_TMP_DIR) + "/" + g_dlfiles[i].name;
                        remove(fp.c_str());
                        this->Toast(tr(S_DELETED));
                        s32 keep = i;
                        this->GotoDownloads();
                        if (keep >= (s32)g_dlfiles.size()) keep = (s32)g_dlfiles.size() - 1;
                        if (keep >= 0) this->layout->SetSel(keep);
                    }
                }
            }
        }
        break;
    }

    case Screen::Cache: {
        if (down & HidNpadButton_B) {
            this->GotoManageData();
        } else if (down & HidNpadButton_A) {
            // File info (A = open/inspect everywhere; never destructive).
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_cache_files.size()) {
                this->CreateShowDialog(
                    tr(S_FILE),
                    g_cache_files[i].name + "\n" +
                        human_size(g_cache_files[i].size),
                    {tr(S_OK)}, true, {}, style_dialog);
            }
        } else if (down & HidNpadButton_X) {
            // Clear all (X = bulk destructive action, same as Log/Downloads)
            if (g_cache_files.empty()) break;
            char dmsg[256];
            snprintf(dmsg, sizeof(dmsg), tr(S_CLEAR_CACHE_CONFIRM),
                     (int)g_cache_files.size());
            if (this->Confirm(tr(S_CLEAR_CACHE), dmsg)) {
                for (auto &e : g_cache_files) {
                    std::string fp = std::string(CACHE_DIR) + "/" + e.name;
                    remove(fp.c_str());
                }
                this->Toast(tr(S_CACHE_CLEARED));
                this->GotoCache();
            }
        } else if (down & HidNpadButton_Minus) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_cache_files.size()) {
                if (this->Confirm(tr(S_DELETE),
                                  std::string(tr(S_DELETE)) + " '" +
                                      g_cache_files[i].name + "'?")) {
                    std::string fp =
                        std::string(CACHE_DIR) + "/" + g_cache_files[i].name;
                    remove(fp.c_str());
                    this->Toast(tr(S_DELETED));
                    s32 keep = i;
                    this->GotoCache();
                    if (keep >= (s32)g_cache_files.size())
                        keep = (s32)g_cache_files.size() - 1;
                    if (keep >= 0) this->layout->SetSel(keep);
                }
            }
        }
        break;
    }

    case Screen::Installed: {
        if (down & HidNpadButton_B) {
            if (this->inst_path == roms_root(&g_tico)) {
                this->GotoHome();
            } else {
                auto p = this->inst_path.find_last_of('/');
                this->GotoInstalled(p == std::string::npos
                                        ? std::string(roms_root(&g_tico))
                                        : this->inst_path.substr(0, p));
            }
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                if (g_inst[i].is_dir) {
                    this->GotoInstalled(this->inst_path + "/" + g_inst[i].name);
                } else {
                    this->CreateShowDialog(
                        tr(S_FILE),
                        g_inst[i].name + "\n" + human_size(g_inst[i].size),
                        {tr(S_OK)}, true, {}, style_dialog);
                }
            }
        } else if (down & HidNpadButton_Y) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                this->layout->ToggleMark(i);
            }
        } else if (down & HidNpadButton_Right) {
            // Pin/unpin a top-level console folder to the top of the list.
            s32 i = this->layout->Sel();
            if (this->inst_path == roms_root(&g_tico) && i >= 0 &&
                i < (s32)g_inst.size() && g_inst[i].is_dir) {
                prefs_dir_pin_toggle(&g_prefs, g_inst[i].name.c_str());
                prefs_save(&g_prefs);
                std::string nm = g_inst[i].name;
                this->GotoInstalled(this->inst_path);
                for (s32 k = 0; k < (s32)g_inst.size(); k++) {
                    if (g_inst[k].name == nm) {
                        this->layout->SetSel(k);
                        break;
                    }
                }
            }
        } else if (down & HidNpadButton_X) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                char nm[256] = {0};
                if (prompt(tr(S_RENAME_PROMPT), g_inst[i].name.c_str(), nm, sizeof(nm))) {
                    // A rename must stay a rename: no separators or "..",
                    // which would move the file to another directory.
                    if (strchr(nm, '/') || strchr(nm, '\\') ||
                        strstr(nm, "..")) {
                        this->ToastErr(tr(S_RENAME_FAILED));
                        break;
                    }
                    std::string from = this->inst_path + "/" + g_inst[i].name;
                    std::string to = this->inst_path + "/" + nm;
                    // Never silently overwrite another file (rename() would).
                    // Case-only renames are fine — FAT is case-insensitive.
                    if (fs_exists(to.c_str()) &&
                        strcasecmp(nm, g_inst[i].name.c_str()) != 0) {
                        this->ToastErr(tr(S_RENAME_FAILED));
                        break;
                    }
                    if (rename(from.c_str(), to.c_str()) == 0) {
                        this->Toast(tr(S_RENAMED));
                    } else {
                        this->ToastErr(tr(S_RENAME_FAILED));
                    }
                    this->GotoInstalled(this->inst_path);
                }
            }
        } else if (down & HidNpadButton_Minus) {
            int mc = this->layout->MarkedCount();
            if (mc > 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), mc);
                if (this->Confirm(tr(S_DELETE), msg)) {
                    // Delete in reverse order so indices stay valid
                    auto marks = this->layout->Marked();
                    for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
                        s32 idx = *it;
                        if (idx >= 0 && idx < (s32)g_inst.size()) {
                            fs_rm_rf((this->inst_path + "/" + g_inst[idx].name).c_str());
                        }
                    }
                    char t[32];
                    snprintf(t, sizeof(t), tr(S_DELETED_N), mc);
                    this->Toast(t);
                    this->GotoInstalled(this->inst_path);
                }
            } else {
                s32 i = this->layout->Sel();
                if (i >= 0 && i < (s32)g_inst.size()) {
                    if (this->Confirm(tr(S_DELETE), std::string(tr(S_DELETE)) +
                                                    " '" + g_inst[i].name + "'?")) {
                        fs_rm_rf((this->inst_path + "/" + g_inst[i].name).c_str());
                        this->Toast(tr(S_DELETED));
                        s32 keep = i;
                        this->GotoInstalled(this->inst_path);
                        if (keep >= (s32)g_inst.size()) keep = (s32)g_inst.size() - 1;
                        if (keep >= 0) this->layout->SetSel(keep);
                    }
                }
            }
        }
        break;
    }

    case Screen::RepoEdit: {
        if (down & HidNpadButton_B) {
            if (g_prefs.group_consoles) {
                this->GotoRepos(this->sel_ci);
            } else {
                this->GotoHome();
            }
        } else if (down & HidNpadButton_A) {
            Repo *rp = &g_cfg.consoles[this->sel_ci].repos[this->sel_ri];
            s32 i = this->layout->Sel();
            char v[600] = {0};
            switch (i) {
            case 0:
                if (prompt(tr(S_LABEL_NAME), rp->label, v, sizeof(v))) {
                    snprintf(rp->label, sizeof(rp->label), "%s", v);
                    config_save(&g_cfg);
                }
                break;
            case 1:
                if (prompt(tr(S_LABEL_ARCHIVE_ID), rp->id, v, sizeof(v))) {
                    snprintf(rp->id, sizeof(rp->id), "%s", v);
                    rp->download_base[0] = '\0';
                    repo_set_url_default(rp);
                    config_save(&g_cfg);
                }
                break;
            case 2:
                if (prompt(tr(S_LABEL_DOWNLOAD_URL), rp->download_base, v, sizeof(v))) {
                    snprintf(rp->download_base, sizeof(rp->download_base), "%s",
                             v);
                    config_save(&g_cfg);
                }
                break;
            case 3:
                rp->enabled = !rp->enabled;
                config_save(&g_cfg);
                break;
            case 4:
                if (this->Confirm(tr(S_DELETE_REPO), tr(S_DELETE_REPO_CONFIRM))) {
                    config_remove_repo(&g_cfg.consoles[this->sel_ci],
                                       this->sel_ri);
                    config_save(&g_cfg);
                    this->Toast(tr(S_DELETED));
                    if (g_prefs.group_consoles) {
                        this->GotoRepos(this->sel_ci);
                    } else {
                        this->GotoHome();
                    }
                    return;
                }
                break;
            default:
                break;
            }
            this->GotoRepoEdit(this->sel_ci, this->sel_ri);
        }
        break;
    }

    case Screen::Picker: {
        if (down & HidNpadButton_B) {
            this->GotoHome();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_picker.size()) {
                const char *cname = g_picker[i].c_str();
                if (this->pending == Pending::AddRepo) {
                    char nm[64] = {0}, id[256] = {0};
                    if (prompt(tr(S_LABEL_NAME), nullptr, nm, sizeof(nm)) &&
                        prompt(tr(S_LABEL_ARCHIVE_ID), nullptr, id, sizeof(id))) {
                        ConsoleGroup *g = config_add_console(&g_cfg, cname);
                        if (g && config_add_repo(g, nm, id)) {
                            config_sort(&g_cfg);
                            config_save(&g_cfg);
                            this->Toast(tr(S_ADDED));
                        }
                    }
                    this->GotoHome();
                } else if (this->pending == Pending::Manual) {
                    g_files_manual = true;
                    char base[700];
                    snprintf(base, sizeof(base),
                             "https://archive.org/download/%s",
                             this->pending_id.c_str());
                    this->layout->SetTitle(std::string("URL > ") + cname);
                    this->screen = Screen::Files;
                    this->StartMetaLoad(
                        this->pending_id, base, cname, false,
                        FILES_SUBTITLE);
                }
            }
        }
        break;
    }

    case Screen::Log: {
        if (down & HidNpadButton_B) {
            if (this->log_origin == Screen::Queue) {
                this->GotoQueue();
            } else {
                this->GotoSettings();
            }
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_log_entries.size() &&
                g_log_entries[i].can_retry) {
                const LogEntry &e = g_log_entries[i];
                char auth[320];
                creds_auth_header(&g_creds, auth, sizeof(auth));
                bool ok = queue_add(e.url.c_str(), e.name.c_str(),
                                    e.target.c_str(), auth, e.size,
                                    e.is_archive, e.md5.c_str());
                if (ok)
                    this->Toast(std::string(tr(S_QUEUED)) + ": " + e.name);
                else
                    this->ToastErr(tr(S_QUEUE_FULL));
            }
        } else if (down & HidNpadButton_X) {
            if (this->Confirm(tr(S_CLEAR_LOG),
                              tr(S_CLEAR_LOG_CONFIRM))) {
                remove(DLLOG_PATH);
                remove(DLLOG_JSON);
                this->Toast(tr(S_LOG_CLEARED));
                this->GotoLog();
            }
        }
        break;
    }

    case Screen::Manage: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < g_cfg.console_count) {
                g_cfg.consoles[i].shown = !g_cfg.consoles[i].shown;
                config_save(&g_cfg);
                this->GotoManage(); // refresh the shown/hidden labels
                this->layout->SetSel(i);
            }
        }
        break;
    }

    case Screen::Creds: {
        if (down & HidNpadButton_B) {
            this->GotoAdvanced();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            char v[1024] = {0};
            if (i == 0) {
                if (prompt(tr(S_ACCESS_KEY), g_creds.access_key, v, sizeof(v))) {
                    snprintf(g_creds.access_key, sizeof(g_creds.access_key), "%s",
                             v);
                    creds_save(&g_creds);
                    this->Toast(tr(S_SAVED));
                }
            } else if (i == 1) {
                // Pre-filled with the current secret so it's easy to edit.
                if (prompt(tr(S_SECRET_KEY), g_creds.secret, v, sizeof(v))) {
                    snprintf(g_creds.secret, sizeof(g_creds.secret), "%s", v);
                    creds_save(&g_creds);
                    this->Toast(tr(S_SAVED));
                }
            } else if (i == 2) {
                if (this->Confirm(tr(S_CLEAR_CREDS),
                                  tr(S_CLEAR_CREDS_CONFIRM))) {
                    g_creds.access_key[0] = '\0';
                    g_creds.secret[0] = '\0';
                    creds_save(&g_creds);
                    this->Toast(tr(S_CLEARED));
                }
            }
            s32 keep = this->layout->Sel();
            this->GotoCreds();
            this->layout->SetSel(keep);
        }
        break;
    }

    case Screen::Search: {
        if (down & HidNpadButton_B) {
            this->GotoHome();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_search_results.size()) {
                const SearchHit &h = g_search_results[i];
                char auth[320];
                creds_auth_header(&g_creds, auth, sizeof(auth));
                bool ok = queue_add(h.url.c_str(), h.name.c_str(),
                                    h.target.c_str(), auth, h.size,
                                    h.is_archive, h.md5.c_str());
                if (ok)
                    this->Toast(std::string(tr(S_QUEUED)) + ": " + h.name);
                else
                    this->ToastErr(tr(S_QUEUE_FULL));
            }
        } else if (down & HidNpadButton_Y) {
            char q[256] = {0};
            if (prompt_raw(tr(S_SEARCH_PROMPT), g_search_query.c_str(),
                           q, sizeof(q)) && q[0]) {
                this->GotoSearch(q);
            }
        }
        break;
    }

    default:
        break;
    }
}

void MainApplication::OnLoad() {
    romfsInit();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    net_init();
    tico_init(&g_tico);
    config_load(&g_cfg);
    config_sort(&g_cfg);
    creds_load(&g_creds);
    prefs_load(&g_prefs);
    select_theme();
    if (g_prefs.lang[0] && strcmp(g_prefs.lang, "en") != 0) {
        char lpath[256];
        snprintf(lpath, sizeof(lpath), LANG_DIR "/%s.json", g_prefs.lang);
        if (!fs_exists(lpath))
            snprintf(lpath, sizeof(lpath), "romfs:/lang/%s.json", g_prefs.lang);
        i18n_load(lpath);
    }
    queue_init(roms_root(&g_tico), g_prefs.max_downloads);
    cleanup_stale_parts(); // drop unresumable old-format .part leftovers

    this->screen = Screen::Home;
    this->sel_ci = 0;
    this->sel_ri = 0;
    this->pending = Pending::None;
    this->inst_path = roms_root(&g_tico);
    this->log_origin = Screen::Settings;

    this->layout = MainLayout::New();
    this->layout->ApplyTheme();
    this->LoadLayout(this->layout);

    // Startup dialogs (TICO missing / no network) must NOT run here: OnLoad
    // executes before Show() starts the render loop, so a dialog would wait
    // for input on a screen that is still black. Defer them to the first
    // frame of the input callback instead.
    this->startup_checks = true;

    this->GotoHome();
    this->RefreshStatus();

    this->SetOnInput([&](const u64 down, const u64 up, const u64 held,
                         const pu::ui::TouchPoint touch) {
        (void)up;
        (void)touch;
        this->HandleInput(down, held);
    });
}

// Orderly teardown after the UI loop ends. The background queue thread MUST be
// joined (queue_exit) before the process exits, or libnx faults on a still-
// running thread — that was the "an error occurred" crash on +. Tear services
// down in reverse init order; queue_exit before net_exit since the worker uses
// sockets/curl.
void MainApplication::Shutdown() {
    if (this->upd_running) {
        threadWaitForExit(&this->upd_thread);
        threadClose(&this->upd_thread);
        this->upd_running = false;
    }
    if (this->chk_running) {
        threadWaitForExit(&this->chk_thread);
        threadClose(&this->chk_thread);
        this->chk_running = false;
    }
    if (this->meta_running) {
        threadWaitForExit(&this->meta_thread);
        threadClose(&this->meta_thread);
        this->meta_running = false;
    }
    queue_exit();
    appletSetMediaPlaybackState(false);
    net_exit();
    nifmExit();
    psmExit();
    romfsExit();
}

// ---- app: update check (release-info fetch on a background thread) --------
void MainApplication::ChkThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->chk_ok = update_fetch_latest(UPDATE_REPO, self->chk_tag,
                                       sizeof(self->chk_tag), self->chk_url,
                                       sizeof(self->chk_url),
                                       &self->chk_attempt);
    self->chk_done = true;
}

void MainApplication::ChkStart() {
    if (this->chk_running) {
        // A dismissed check is still finishing: re-attach to it (progress UI
        // comes back) instead of spawning a second thread over the first.
        this->chk_discard = false;
        return;
    }
    this->chk_attempt = 1;
    this->chk_done = false;
    this->chk_ok = false;
    this->chk_discard = false;
    this->chk_tag[0] = '\0';
    this->chk_url[0] = '\0';

    Result rc = threadCreate(&this->chk_thread, &MainApplication::ChkThread,
                             this, NULL, 0x40000, 0x2C, -2);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&this->chk_thread))) {
        this->chk_running = true;
        return;
    }
    // Couldn't spawn: fetch synchronously so the check still works.
    this->chk_ok = update_fetch_latest(UPDATE_REPO, this->chk_tag,
                                       sizeof(this->chk_tag), this->chk_url,
                                       sizeof(this->chk_url), NULL);
    this->ChkFinish();
}

void MainApplication::ChkTick() {
    if (!this->chk_done) {
        // Live status with the attempt number, e.g. "Check for updates... (2/3)".
        char s[160];
        snprintf(s, sizeof(s), "%s...  (%d/3)  B %s", tr(S_CHECK_UPDATES),
                 (int)this->chk_attempt, tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    threadWaitForExit(&this->chk_thread);
    threadClose(&this->chk_thread);
    this->chk_running = false;
    this->ChkFinish();
}

void MainApplication::ChkFinish() {
    if (!this->chk_ok) {
        this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_FETCH_FAIL),
                               {tr(S_OK)}, true, {}, style_dialog);
        this->GotoSettings();
        return;
    }
    if (version_cmp(APP_VERSION_STR, this->chk_tag) >= 0) {
        char umsg[128];
        snprintf(umsg, sizeof(umsg), tr(S_UPDATE_UP_TO_DATE), APP_VERSION_STR);
        this->CreateShowDialog(tr(S_TITLE_UPDATE), umsg, {tr(S_OK)}, true, {},
                               style_dialog);
        this->GotoSettings();
        return;
    }
    {
        char umsg[128];
        snprintf(umsg, sizeof(umsg), tr(S_UPDATE_CONFIRM), this->chk_tag);
        if (!this->Confirm(tr(S_TITLE_UPDATE), umsg)) {
            this->GotoSettings();
            return;
        }
    }
    char dl[1024];
    snprintf(dl, sizeof(dl), "%s/downloads/update.nro", CONFIG_DIR);
    fs_ensure_parent(dl);
    this->UpdStart(this->chk_url, dl, this->chk_tag);
}

// ---- app: in-app self-update download -------------------------------------
int MainApplication::UpdProgress(void *ud, u64 now, u64 total) {
    auto self = static_cast<MainApplication *>(ud);
    self->upd_now = now;
    self->upd_total = total;
    return self->upd_cancel ? 1 : 0;
}

void MainApplication::UpdThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    long code = 0;
    bool ok = http_download(self->upd_url.c_str(), self->upd_dl.c_str(), NULL,
                            &MainApplication::UpdProgress, self, 0, &code);
    self->upd_ok = ok;
    self->upd_done = true;
}

void MainApplication::UpdStart(const std::string &url, const std::string &dl,
                               const std::string &tag) {
    this->upd_url = url;
    this->upd_dl = dl;
    this->upd_tag = tag;
    this->upd_now = 0;
    this->upd_total = 0;
    this->upd_done = false;
    this->upd_ok = false;
    this->upd_cancel = false;
    this->upd_running = true;

    this->layout->SetTitle(tr(S_UPDATING));
    this->layout->SetSubtitle(std::string("Downloading ") + tag + "...  (B to cancel)");
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_UPDATE_DL_CANCEL));

    Result rc = threadCreate(&this->upd_thread, &MainApplication::UpdThread, this,
                             NULL, 0x40000, 0x2C, -2);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&this->upd_thread))) {
        return;
    }
    this->upd_running = false;
    this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_START_FAIL), {tr(S_OK)},
                           true, {}, style_dialog);
    this->GotoSettings();
}

void MainApplication::UpdTick() {
    if (!this->upd_done) {
        // Still downloading: show live progress in the subtitle.
        u64 now = this->upd_now, total = this->upd_total;
        int pct = total ? (int)((now * 100) / total) : 0;
        char s[160];
        snprintf(s, sizeof(s), tr(S_UPDATE_DOWNLOADING),
                 this->upd_tag.c_str(), pct, human_size(now).c_str(),
                 total ? human_size(total).c_str() : "?");
        this->layout->SetSubtitle(s);
        return;
    }

    // Download finished: join the worker and install on the main thread.
    threadWaitForExit(&this->upd_thread);
    threadClose(&this->upd_thread);
    this->upd_running = false;

    if (this->upd_cancel) {
        remove(this->upd_dl.c_str());
        this->GotoSettings();
        return;
    }

    bool ok = this->upd_ok;
    std::string tag = this->upd_tag, dl = this->upd_dl;
    if (ok && !looks_like_nro(dl.c_str())) {
        // Downloaded file isn't an NRO: treat as a failed download.
        ok = false;
    }
    if (ok) {
        romfsExit();
        std::string selfp = resolve_self_path();
        const char *self = selfp.c_str();
        char prev[1100], stage[1100];
        snprintf(prev, sizeof(prev), "%s.previous", self);
        snprintf(stage, sizeof(stage), "%s.new", self);
        install_over(self, prev); // best-effort backup
        // Stage the new build next to the old one, then swap with a rename —
        // a crash/power loss mid-copy can't leave a half-written app file.
        bool inst = install_over(dl.c_str(), stage);
        if (inst) {
            remove(self);
            inst = (rename(stage, self) == 0);
            if (!inst) {
                install_over(prev, self); // try to restore the backup
            }
        }
        remove(stage);
        romfsInit();
        if (inst) {
            remove(dl.c_str());
            char umsg[512];
            snprintf(umsg, sizeof(umsg), tr(S_UPDATE_OK), tag.c_str());
            this->CreateShowDialog(tr(S_TITLE_UPDATE), umsg,
                                   {tr(S_OK)}, true, {}, style_dialog);
        } else {
            this->CreateShowDialog(
                tr(S_TITLE_UPDATE), std::string("Install failed. New build kept at:\n") + dl,
                {tr(S_OK)}, true, {}, style_dialog);
        }
    } else {
        remove(dl.c_str());
        this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_FAIL), {tr(S_OK)}, true, {}, style_dialog);
    }
    this->GotoSettings();
}
