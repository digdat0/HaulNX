#include <MainApplication.hpp>
#include <pu/ui/extras/extras_Toast.hpp>
#include "version.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <fstream>
#include <map>
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
// Per-repo "installed?" context: the folder index and the installed-md5 map.
// Both depend only on the target folder, not the filter/sort, so they're built
// once when a repo opens and reused across filter/sort rebuilds (rebuilding them
// per keystroke re-read the whole download log and re-scanned the SD directory
// on the UI thread). g_dl_md5 (below) holds the md5 side.
static std::vector<std::string> g_inst_idx;
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
static std::vector<DirEnt> g_rompick; // subfolders in the ROM-folder picker
static std::vector<int> g_home_map; // grouped Browse: visible row -> console index
static std::vector<int> g_repos_map; // Repos screen: visible row -> repo array index
static std::string g_launch_path;   // argv[0] from main(), for self-update
static bool g_net_ok = true;        // last connectivity poll (RefreshStatus)

// ---- theme ----------------------------------------------------------------
struct AppTheme {
    pu::ui::Color bg;           // layout background
    pu::ui::Color header_bg;    // header rectangle
    pu::ui::Color tab_bar_bg;   // tab strip
    pu::ui::Color footer_bg;    // footer rectangle
    pu::ui::Color title_clr;    // "HaulNX" title text
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

// Logo-derived palette: charcoal shell, electric green for activity, blue for
// selection/values. (Green = the logo's download arrow; blue = the "+".)
static const AppTheme g_theme_dark = {
    {12,12,14,255},       {23,25,30,255},      {16,17,21,255},
    {23,25,30,255},       {255,255,255,255},   {198,205,215,255},
    {168,176,188,255},    {14,16,18,255},      {146,214,36,255},
    {192,199,210,255},    {150,160,185,255},   {232,234,240,255},
    {26,28,34,255},       {255,255,255,255},   {205,212,222,255},
    {255,255,255,255},    {54,86,20,255},
    {22,23,27,255},       {28,30,36,255},      {40,44,53,255},
    {80,86,100,255},      {42,56,30,255},
};

// Light theme keeps the charcoal header/tab shell (the logo's "case") over a
// light content area; the same green pill/pulse reads on the dark shell.
static const AppTheme g_theme_light = {
    {228,231,237,255},    {30,33,40,255},      {23,25,31,255},
    {30,33,40,255},       {255,255,255,255},   {200,207,217,255},
    {150,158,172,255},    {14,16,18,255},      {146,214,36,255},
    {185,192,204,255},    {88,98,116,255},     {26,30,38,255},
    {240,242,246,255},    {26,30,40,255},      {50,60,80,255},
    {26,30,40,255},       {206,232,210,255},
    {252,253,255,255},    {244,246,250,255},   {212,234,214,255},
    {170,178,192,255},    {192,224,200,255},
};

static const AppTheme *g_theme = &g_theme_dark;

static bool is_light_theme() { return strcmp(g_prefs.theme, "light") == 0; }

// ---- console icons --------------------------------------------------------
// Loaded once from romfs at startup and shared (borrowed) into list rows.
static std::map<std::string, pu::sdl2::Texture> g_console_icons;
static pu::sdl2::Texture g_header_logo = nullptr; // app badge in the header

static std::string icon_key(const char *s) {
    std::string r;
    for (; s && *s; s++) {
        r += (*s >= 'A' && *s <= 'Z') ? (char)(*s + 32) : *s;
    }
    return r;
}

static void load_console_icons() {
    static const char *keys[] = {
        "nes", "snes", "n64", "gb", "gbc", "gba", "3ds", "nds", "gc", "wii",
        "genesis", "master-system", "game-gear", "sega-cd", "saturn", "dc",
        "atomiswave", "naomi", "psx", "ps2", "psp", "default",
        // consoles added with the 52-console expansion
        "fds", "virtual-boy", "pokemon-mini", "game-and-watch", "sg-1000",
        "sega-32x", "pc-engine", "pc-engine-cd", "supergrafx", "pc-fx",
        "neo-geo", "neo-geo-cd", "neo-geo-pocket", "neo-geo-pocket-color",
        "atari-2600", "atari-5200", "atari-7800", "atari-lynx", "atari-jaguar",
        "wonderswan", "wonderswan-color", "colecovision", "intellivision",
        "odyssey2", "vectrex", "3do", "cd-i", "supervision",
        "channel-f", "arcade", "fbneo",
        // settings-screen card icons (same cache, "set-" prefixed keys)
        "set-updates", "set-ui", "set-advanced", "set-logs", "set-data",
        "set-credits"};
    for (const char *k : keys) {
        auto tex = pu::ui::render::LoadImageFromFile(std::string("romfs:/icons/") +
                                                     k + ".png");
        if (tex) {
            g_console_icons[k] = tex;
        }
    }
    g_header_logo = pu::ui::render::LoadImageFromFile("romfs:/header_logo.png");
}

// Icon for a console folder key (e.g. "snes"); the generic "default" icon for
// custom/unknown folders, or nullptr if icons failed to load.
static pu::sdl2::Texture console_icon(const char *key) {
    if (!key || !key[0]) {
        return nullptr;
    }
    auto it = g_console_icons.find(icon_key(key));
    if (it != g_console_icons.end()) {
        return it->second;
    }
    auto d = g_console_icons.find("default");
    return d != g_console_icons.end() ? d->second : nullptr;
}

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

// Logo green, theme-adjusted: bright on the dark theme, deeper on light so it
// keeps contrast on light rows.
static pu::ui::Color accent_green() {
    return is_light_theme() ? pu::ui::Color(30, 124, 54, 255)
                            : pu::ui::Color(146, 214, 36, 255);
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
        return accent_green();
    }
    return light ? pu::ui::Color(40, 120, 200, 255)
                 : pu::ui::Color(150, 205, 255, 255);
}

static pu::ui::Color onoff_color(bool on); // defined with the settings helpers

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
        // Nintendo
        {"nes", "Nintendo Entertainment System"},
        {"fds", "Famicom Disk System"},
        {"snes", "Super Nintendo Entertainment System"},
        {"n64", "Nintendo 64"},
        {"gb", "Game Boy"},
        {"gbc", "Game Boy Color"},
        {"gba", "Game Boy Advance"},
        {"nds", "Nintendo DS"},
        {"3ds", "Nintendo 3DS"},
        {"gc", "Nintendo GameCube"},
        {"wii", "Nintendo Wii"},
        {"virtual-boy", "Nintendo Virtual Boy"},
        {"pokemon-mini", "Pokemon Mini"},
        {"game-and-watch", "Nintendo Game & Watch"},
        // Sega
        {"sg-1000", "Sega SG-1000"},
        {"master-system", "Sega Master System"},
        {"game-gear", "Sega Game Gear"},
        {"genesis", "Sega Genesis"},
        {"sega-cd", "Sega CD"},
        {"sega-32x", "Sega 32X"},
        {"saturn", "Sega Saturn"},
        {"dc", "Sega Dreamcast"},
        // Sony
        {"psx", "Sony PlayStation"},
        {"ps2", "Sony PlayStation 2"},
        {"psp", "Sony PlayStation Portable"},
        // NEC
        {"pc-engine", "NEC PC Engine"},
        {"pc-engine-cd", "NEC PC Engine CD"},
        {"supergrafx", "NEC SuperGrafx"},
        {"pc-fx", "NEC PC-FX"},
        // SNK
        {"neo-geo", "SNK Neo Geo"},
        {"neo-geo-cd", "SNK Neo Geo CD"},
        {"neo-geo-pocket", "SNK Neo Geo Pocket"},
        {"neo-geo-pocket-color", "SNK Neo Geo Pocket Color"},
        // Atari
        {"atari-2600", "Atari 2600"},
        {"atari-5200", "Atari 5200"},
        {"atari-7800", "Atari 7800"},
        {"atari-lynx", "Atari Lynx"},
        {"atari-jaguar", "Atari Jaguar"},
        // Bandai
        {"wonderswan", "Bandai WonderSwan"},
        {"wonderswan-color", "Bandai WonderSwan Color"},
        // Other home consoles
        {"colecovision", "ColecoVision"},
        {"intellivision", "Mattel Intellivision"},
        {"odyssey2", "Magnavox Odyssey 2"},
        {"vectrex", "GCE Vectrex"},
        {"channel-f", "Fairchild Channel F"},
        {"3do", "3DO Interactive Multiplayer"},
        {"cd-i", "Philips CD-i"},
        {"supervision", "Watara Supervision"},
        // Arcade
        {"atomiswave", "Sammy Atomiswave"},
        {"naomi", "Sega NAOMI"},
        {"arcade", "Arcade"},
        {"fbneo", "FinalBurn Neo"},
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

// Destructive-action dialog: same as style_dialog but with a red title so it
// clearly reads as "danger" at a glance.
static void style_dialog_danger(pu::ui::Dialog::Ref &d) {
    d->SetDialogColor(g_theme->dialog_bg);
    d->SetTitleColor(pu::ui::Color(224, 78, 78, 255));
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
    case Q_DOWNLOADING: return light ? accent_green()
                                     : pu::ui::Color(245, 246, 250, 255);
    case Q_PAUSED:      return light ? pu::ui::Color(40, 120, 200, 255)
                                     : pu::ui::Color(150, 205, 255, 255);
    case Q_VERIFYING:
    case Q_AWAIT_EXTRACT:
    case Q_EXTRACTING:  return light ? pu::ui::Color(150, 100, 15, 255)
                                     : pu::ui::Color(210, 185, 120, 255);
    case Q_DONE:        return accent_green();
    case Q_SAVED:       return light ? pu::ui::Color(95, 110, 25, 255)
                                     : pu::ui::Color(190, 205, 130, 255);
    case Q_FAILED:      return light ? pu::ui::Color(185, 35, 35, 255)
                                     : pu::ui::Color(240, 110, 110, 255);
    case Q_CANCELLED:   return light ? pu::ui::Color(40, 44, 52, 255)
                                     : pu::ui::Color(150, 150, 162, 255);
    case Q_QUEUED:
    default:            return light ? pu::ui::Color(80, 90, 110, 255)
                                     : pu::ui::Color(205, 212, 225, 255);
    }
}

// Header one-liner for the queue screen ("1 active · 3 waiting · 1 failed");
// empty when the queue holds only finished items.
static std::string queue_summary(const QueueView *qv, int n) {
    int act = 0, wait = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        switch (qv[i].item.status) {
        case Q_DOWNLOADING:
        case Q_VERIFYING:
        case Q_AWAIT_EXTRACT:
        case Q_EXTRACTING:  act++; break;
        case Q_QUEUED:
        case Q_PAUSED:      wait++; break;
        case Q_FAILED:      fail++; break;
        default:            break;
        }
    }
    std::string s;
    char buf[64];
    const struct { int n; int key; } parts[] = {
        {act, S_QUEUE_N_ACTIVE},
        {wait, S_QUEUE_N_WAITING},
        {fail, S_QUEUE_N_FAILED},
    };
    for (const auto &p : parts) {
        if (p.n > 0) {
            snprintf(buf, sizeof(buf), tr(p.key), p.n);
            s += (s.empty() ? "" : " · ") + std::string(buf);
        }
    }
    return s;
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
// Repos screen: translate a visible row to its repo array index. Pinned repos
// float to the top for display only (see GotoRepos); this reverses that map.
// Falls back to the row itself if the map is empty/stale.
static int repos_ref(int row) {
    if (row >= 0 && row < (int)g_repos_map.size()) return g_repos_map[row];
    return row;
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
static int g_inst_sort = SORT_DEFAULT; // Installed browser sort (session-persistent)
static const int g_sort_keys[] = {
    S_SORT_DEFAULT, S_SORT_NAME_AZ, S_SORT_NAME_ZA,
    S_SORT_SIZE_DESC, S_SORT_SIZE_ASC
};

// ASCII lower-fold (locale-free) for case-insensitive path matching, matching
// FAT's case-insensitivity as strcasecmp/fs_exists do.
static void ascii_lower(std::string &s) {
    for (auto &c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 32);
        }
    }
}

// Build a case-folded, sorted index of the console's roms/<target> directory in
// ONE scan, so the file list can test "already installed?" per row with a binary
// search instead of an opendir()/readdir() scan per file — the latter made a big
// repo's list build cost O(files * dir_size) of SD traffic on the UI thread.
static void build_installed_index(const char *target,
                                  std::vector<std::string> &out) {
    out.clear();
    char dir[1200];
    snprintf(dir, sizeof(dir), "%s/%s", roms_root(&g_tico), target);
    DIR *d = opendir(dir);
    if (!d) {
        return; // no folder yet: nothing installed
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' ||
             (e->d_name[1] == '.' && e->d_name[2] == '\0'))) {
            continue; // skip "." and ".."
        }
        std::string s(e->d_name);
        ascii_lower(s);
        out.push_back(std::move(s));
    }
    closedir(d);
    std::sort(out.begin(), out.end());
}

// Installed test against the prebuilt index. Mirrors file_installed(): an exact
// (case-insensitive) name match, or — for archives — any entry whose name starts
// with the archive's base name (its name minus the last extension), e.g.
// "game.zip" already unpacked to "game.sfc" or a "game/" folder.
static bool index_has_installed(const std::vector<std::string> &idx,
                                const char *fname) {
    std::string low(fname);
    ascii_lower(low);
    if (std::binary_search(idx.begin(), idx.end(), low)) {
        return true;
    }
    if (!is_archive_name(fname)) {
        return false;
    }
    std::string base = low;
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot != 0) {
        base.resize(dot);
    }
    if (base.empty()) {
        return false;
    }
    // The first entry >= base is the lexicographically smallest string that
    // could start with base; if it does, an installed match exists.
    auto it = std::lower_bound(idx.begin(), idx.end(), base);
    return it != idx.end() && it->size() >= base.size() &&
           it->compare(0, base.size(), base) == 0;
}

// The md5 we last installed for each file, read back from the download history
// (downloads.jsonl). Keyed "target/lowercased-name". Lets the file list flag a
// file whose repo now advertises a different md5 than the copy already on the SD
// card — i.e. an update — without a second index or a new on-disk store.
static std::map<std::string, std::string> g_dl_md5;
static void load_dl_md5() {
    g_dl_md5.clear();
    std::ifstream jf(DLLOG_JSON);
    if (!jf.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(jf, line)) {
        if (line.empty() || line[0] != '{') {
            continue;
        }
        const char *js = line.c_str();
        int ntok = 0;
        jsmntok_t *tok = json_parse_alloc(js, line.size(), &ntok);
        if (!tok || tok[0].type != JSMN_OBJECT) {
            free(tok);
            continue;
        }
        char st[16] = "", target[64] = "", name[512] = "", md5[33] = "";
        json_copy(js, tok, json_obj_get(js, tok, 0, "st"), st, sizeof(st));
        // Only a completed install ("done" / "saved-raw") leaves a file whose
        // md5 is meaningful; skip cancelled/failed rows.
        if (strncmp(st, "done", 4) == 0 || strncmp(st, "saved", 5) == 0) {
            json_copy(js, tok, json_obj_get(js, tok, 0, "target"), target,
                      sizeof(target));
            json_copy(js, tok, json_obj_get(js, tok, 0, "name"), name,
                      sizeof(name));
            json_copy(js, tok, json_obj_get(js, tok, 0, "md5"), md5,
                      sizeof(md5));
            if (target[0] && name[0] && md5[0]) {
                std::string key = std::string(target) + "/" + name;
                ascii_lower(key);
                g_dl_md5[key] = md5; // later lines win: the most recent install
            }
        }
        free(tok);
    }
}

// reload_ctx: rebuild the per-target install context (folder index + md5 map).
// True when a repo opens; false for filter/sort rebuilds, which only re-slice the
// already-loaded metadata and can reuse the cached context — no SD scan, no
// re-parse of the download log.
static void rebuild_files(MainLayout *lay, const char *target,
                          bool reload_ctx = true) {
    lay->ClearMenu();
    g_files.clear();
    g_marks.clear();
    if (!g_have_item) {
        lay->AddRow(tr(S_META_FAILED));
        return;
    }
    for (int i = 0; i < g_item.file_count; i++) {
        // Hide sidecar/metadata files (.torrent, .xml, ...) per the Settings >
        // UI extension filter, so they never show as downloadable ROMs.
        if (prefs_ext_hidden(&g_prefs, g_item.files[i].name)) {
            continue;
        }
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
    // Install context: one directory scan + one download-log parse, cached and
    // reused across filter/sort rebuilds (see reload_ctx). Only the initial
    // repo-open pass pays for them.
    if (reload_ctx) {
        build_installed_index(target, g_inst_idx);
        load_dl_md5(); // md5 of what we installed, to spot repo-updated files
    }
    int updates = 0;
    for (int k = 0; k < (int)g_files.size(); k++) {
        ArchiveFile *f = &g_item.files[g_files[k]];
        bool inst = index_has_installed(g_inst_idx, f->name);
        // 0 = not installed, 1 = installed & current, 2 = installed but the
        // repo now advertises a different md5 than the copy on disk.
        int mark = inst ? 1 : 0;
        if (inst && f->md5[0]) {
            std::string key = std::string(target) + "/" + f->name;
            ascii_lower(key);
            auto it = g_dl_md5.find(key);
            if (it != g_dl_md5.end() &&
                strcasecmp(it->second.c_str(), f->md5) != 0) {
                mark = 2;
                updates++;
            }
        }
        g_marks.push_back((char)mark);
        char name[540];
        snprintf(name, sizeof(name), "%s%s",
                 mark == 2 ? "↑ " : mark == 1 ? "* " : "", f->name);
        lay->AddRow2(name, human_size(f->size),
                     g_theme->row_text, size_color(f->size));
    }
    if (g_files.empty()) {
        lay->AddRow(tr(S_NO_FILES_MATCH));
    }
    // Persistent indicator for a non-default sort and/or an active filter —
    // the toast announcing them vanishes, and an active filter is otherwise
    // invisible ("where did my files go?").
    std::string info;
    if (updates > 0) {
        char ub[80]; // roomy: some localized forms are multi-byte
        snprintf(ub, sizeof(ub), tr(S_UPDATES_AVAIL), updates);
        info = ub;
    }
    if (g_sort_mode != SORT_DEFAULT) {
        if (!info.empty()) {
            info += "  ·  ";
        }
        info += tr(g_sort_keys[g_sort_mode]);
    }
    if (!g_filter.empty()) {
        char fb[120];
        snprintf(fb, sizeof(fb), "%s\"%s\" (%d)",
                 info.empty() ? "" : "  ·  ", g_filter.c_str(),
                 (int)g_files.size());
        info += fb;
    }
    lay->SetRomInfo(info);
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
    self->meta.done = true;
}

void MainApplication::StartMetaLoad(const std::string &id,
                                    const std::string &base,
                                    const std::string &target, bool force,
                                    const std::string &done_subtitle) {
    // A previously-cancelled fetch may still be finishing on the shared worker;
    // reap it (briefly, bounded by the network timeout) and drop its result
    // before reusing the thread for this load.
    if (this->meta.running) {
        this->meta.Join();
        ia_free(&g_item);
        g_have_item = false;
        this->meta_discard = false;
    }
    snprintf(g_files_id, sizeof(g_files_id), "%s", id.c_str());
    snprintf(g_files_base, sizeof(g_files_base), "%s", base.c_str());
    snprintf(g_files_target, sizeof(g_files_target), "%s", target.c_str());
    if (g_have_item) {
        ia_free(&g_item);
        g_have_item = false;
    }
    this->meta_force = force;
    this->meta_done_subtitle = done_subtitle;
    this->meta_ok = false;

    this->layout->SetSubtitle(tr(S_LOADING_META));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_LOADING_META));

    if (this->meta.Start(&MainApplication::MetaThread, this)) {
        return;
    }
    // Couldn't spawn: fall back to a synchronous fetch so the list still loads.
    this->layout->HideSpinner();
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
    if (!this->meta.done) {
        return; // the spinner overlay animates itself
    }
    this->layout->HideSpinner();
    this->meta.Join();
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
// fall back to the default. This handles both sdmc:/switch/HaulNX/...nro
// and sdmc:/switch/HaulNX.nro.
static std::string resolve_self_path() {
    if (g_launch_path.size() >= 4 &&
        strcasecmp(g_launch_path.c_str() + g_launch_path.size() - 4, ".nro") ==
            0 &&
        fs_exists(g_launch_path.c_str())) {
        return g_launch_path;
    }
    const char *candidates[] = {"sdmc:/switch/HaulNX/HaulNX.nro",
                                "sdmc:/switch/HaulNX.nro"};
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

// Total bytes under a folder (files at any depth, capped so a huge tree
// can't stall navigation).
static uint64_t dir_total_size(const std::string &path, int depth = 3) {
    uint64_t total = 0;
    DIR *d = opendir(path.c_str());
    if (!d) {
        return 0;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        std::string full = path + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (depth > 1) {
                total += dir_total_size(full, depth - 1);
            }
        } else {
            total += (uint64_t)st.st_size;
        }
    }
    closedir(d);
    return total;
}

// Installed-tab folder chips need each console folder's app count and total
// size. The recursive size walk stats every file underneath, so recomputing it
// on every visit made navigating the Installed tab laggy. Cache it per folder,
// keyed by the folder's mtime and immediate entry count: the cheap check (one
// stat + one readdir, no per-file stat) skips the recursive walk unless the
// folder actually changed. Adds/removes bump both keys; a same-size rename
// leaves a stale-but-correct size.
struct InstStat {
    time_t mtime;
    int imm; // immediate entry count (the displayed "N apps")
    uint64_t size;
};
static std::map<std::string, InstStat> g_inst_stat;
static bool g_inst_stat_loaded = false;
static bool g_inst_stat_dirty = false;

#define INST_SIZES_PATH CONFIG_DIR "/inst_sizes.json"

// Persist the folder-size cache across launches. The recursive size walk is the
// Installed tab's only real load cost; without persistence the whole cache is
// cold on every launch, so the first Installed visit re-walks (stats every file
// under) every console folder. Entries are still revalidated per folder by
// mtime + immediate count in inst_dir_stats, so a folder that changed while the
// app was closed — e.g. a fresh download — is the only one re-walked.
static void inst_stat_load(void) {
    if (g_inst_stat_loaded) {
        return;
    }
    g_inst_stat_loaded = true;
    size_t len = 0;
    char *body = json_read_file(INST_SIZES_PATH, &len);
    if (!body) {
        return;
    }
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (tok && tok[0].type == JSMN_ARRAY) {
        int child = 1;
        for (int i = 0; i < tok[0].size; i++) {
            if (tok[child].type == JSMN_OBJECT) {
                char path[1024] = "";
                json_copy(body, tok, json_obj_get(body, tok, child, "p"), path,
                          sizeof(path));
                if (path[0]) {
                    InstStat s;
                    s.mtime = (time_t)json_u64(
                        body, tok, json_obj_get(body, tok, child, "m"));
                    s.imm = (int)json_u64(
                        body, tok, json_obj_get(body, tok, child, "n"));
                    s.size = json_u64(
                        body, tok, json_obj_get(body, tok, child, "s"));
                    g_inst_stat[path] = s;
                }
            }
            child = json_tok_skip(tok, child);
        }
    }
    free(tok);
    free(body);
}

// Write the cache back only when a folder was actually (re)walked this visit.
static void inst_stat_save(void) {
    if (!g_inst_stat_dirty) {
        return;
    }
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(INST_SIZES_PATH, "wb");
    if (!f) {
        return;
    }
    fputc('[', f);
    bool first = true;
    for (const auto &kv : g_inst_stat) {
        if (!first) {
            fputc(',', f);
        }
        first = false;
        fputs("{\"p\":", f);
        json_write_escaped(f, kv.first.c_str());
        fprintf(f, ",\"m\":%llu,\"n\":%d,\"s\":%llu}",
                (unsigned long long)kv.second.mtime, kv.second.imm,
                (unsigned long long)kv.second.size);
    }
    fputc(']', f);
    fclose(f);
    g_inst_stat_dirty = false;
}

static void inst_dir_stats(const std::string &path, int *count,
                           uint64_t *size) {
    struct stat ds;
    time_t mt = (stat(path.c_str(), &ds) == 0) ? ds.st_mtime : 0;
    int imm = count_dir_entries(path);
    auto it = g_inst_stat.find(path);
    if (it != g_inst_stat.end() && it->second.mtime == mt &&
        it->second.imm == imm) {
        *count = imm;
        *size = it->second.size;
        return;
    }
    uint64_t sz = dir_total_size(path);
    g_inst_stat[path] = {mt, imm, sz};
    g_inst_stat_dirty = true; // a folder was (re)walked: persist on exit
    *count = imm;
    *size = sz;
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

    // App badge in the top-left, then the title text to its right.
    const s32 logo_sz = 60, logo_x = 40, logo_y = 16;
    this->header_logo = IconElement::New(logo_x, logo_y, logo_sz);
    this->header_logo->SetTexture(g_header_logo);
    this->Add(this->header_logo);

    const s32 title_x = g_header_logo ? logo_x + logo_sz + 16 : 45;
    // Two-tone "HaulNX" wordmark echoing the icon lockup; constant colours
    // since the header shell stays charcoal in both themes.
    s32 wx = title_x;
    // "Haul" in green, "NX" in accent-blue — the icon's palette
    // (--green #8fd329, --accent-lite #5aa0f5). wm_tico/wm_dl/wm_plus are
    // legacy member names carried over from the old three-part lockup; the
    // third block is now unused (empty).
    this->wm_tico = pu::ui::elm::TextBlock::New(wx, 24, "Haul");
    this->wm_tico->SetColor(pu::ui::Color(143, 211, 41, 255));
    this->Add(this->wm_tico);
    wx += this->wm_tico->GetWidth();
    this->wm_dl = pu::ui::elm::TextBlock::New(wx, 24, "NX");
    this->wm_dl->SetColor(pu::ui::Color(90, 160, 245, 255));
    this->Add(this->wm_dl);
    wx += this->wm_dl->GetWidth();
    this->wm_plus = pu::ui::elm::TextBlock::New(wx, 24, "");
    this->wm_plus->SetColor(pu::ui::Color(90, 160, 245, 255));
    this->Add(this->wm_plus);
    wx += this->wm_plus->GetWidth();
    this->title = pu::ui::elm::TextBlock::New(wx + 24, 24, " ");
    this->title->SetColor(g_theme->title_clr);
    this->title_x0 = wx + 24; // fixed anchor: SetTitle re-bases from here
    this->Add(this->title);
    for (int i = 0; i < 2; i++) {
        auto sp = pu::ui::elm::TextBlock::New(-100, 24, " ");
        sp->SetColor(pu::ui::Color(146, 214, 36, 255));
        this->Add(sp);
        this->bc_seps.push_back(sp);
        auto pt = pu::ui::elm::TextBlock::New(-100, 24, " ");
        pt->SetColor(g_theme->title_clr);
        this->Add(pt);
        this->bc_parts.push_back(pt);
    }

    // Console icon shown after the title breadcrumb (hidden unless set).
    this->title_icon = IconElement::New(0, 20, 46);
    this->Add(this->title_icon);

    this->status = pu::ui::elm::TextBlock::New(sw - 400, 30, "");
    this->status->SetColor(g_theme->status_clr);
    this->Add(this->status);

    this->net_bars = NetBarsElement::New(sw - 440, 32);
    this->Add(this->net_bars);

    this->bat_icon = BatteryElement::New(sw - 140, 36);
    this->Add(this->bat_icon);

    this->bat_info = pu::ui::elm::TextBlock::New(sw - 100, 30, "");
    this->bat_info->SetColor(g_theme->status_clr);
    this->Add(this->bat_info);

    const s32 strip_y = 80;
    const s32 strip_h = 70;
    this->tab_bar = pu::ui::elm::Rectangle::New(
        0, strip_y, sw, strip_h, g_theme->tab_bar_bg);
    this->Add(this->tab_bar);

    // Rounded highlight behind the active tab label (added before the labels
    // so it renders underneath them).
    this->tab_pill = PillElement::New(0, strip_y + 8, 120, strip_h - 16, 11,
                                      g_theme->tab_under);
    this->Add(this->tab_pill);

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

    // The icon's ring gradient as a signature strip along the bottom edge of
    // the charcoal shell (constant in both themes — the shell stays charcoal).
    this->accent_line = GradientLineElement::New(
        0, strip_y + strip_h, sw, 3, pu::ui::Color(146, 214, 36, 255),
        pu::ui::Color(56, 130, 225, 255));
    this->Add(this->accent_line);

    const s32 footer_h = 64;
    const s32 list_y = 158;
    const s32 row_h = 84;
    const s32 avail = sh - list_y - footer_h;
    const s32 rows_visible = avail / row_h;
    this->list = TableList::New(0, list_y, sw, row_h, rows_visible);
    this->Add(this->list);

    // Card view for the console lists; empty (and thus invisible) unless a
    // screen populates it via SetCardsMode(true) + AddCard.
    this->grid = CardGrid::New(0, list_y, sw, avail);
    this->Add(this->grid);

    // Empty-state visuals (big centred icon + message), hidden by default.
    this->empty_icon = IconElement::New(sw / 2 - 90, list_y + avail / 2 - 150,
                                        180);
    this->empty_icon->SetTexture(nullptr);
    this->Add(this->empty_icon);
    this->empty_text = pu::ui::elm::TextBlock::New(0, list_y + avail / 2 + 50,
                                                   "");
    this->empty_text->SetColor(g_theme->rom_info_clr);
    this->Add(this->empty_text);
    this->empty_hint = pu::ui::elm::TextBlock::New(0, list_y + avail / 2 + 98,
                                                   "");
    this->empty_hint->SetFont(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
    this->empty_hint->SetColor(g_theme->rom_info_clr);
    this->Add(this->empty_hint);
    // Accent chip (filled pill + dark text) sits under the hint; laid out and
    // shown on demand by SetEmptyState, hidden otherwise. Added after the hint
    // so the text draws over the pill.
    this->empty_chip =
        pu::ui::elm::Rectangle::New(0, 0, 0, 0, accent_green(), 14);
    this->empty_chip->SetVisible(false);
    this->Add(this->empty_chip);
    this->empty_chip_text = pu::ui::elm::TextBlock::New(0, 0, "");
    this->empty_chip_text->SetFont(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
    this->empty_chip_text->SetColor(g_theme->bg); // dark text on the green pill
    this->empty_chip_text->SetVisible(false);
    this->Add(this->empty_chip_text);

    // Background-work spinner overlay, centred in the content area.
    this->spinner = SpinnerElement::New(0, list_y, sw, avail);
    this->Add(this->spinner);

    this->rom_info = pu::ui::elm::TextBlock::New(45, sh - footer_h - 38, "");
    this->rom_info->SetColor(g_theme->rom_info_clr);
    this->Add(this->rom_info);

    this->footer = pu::ui::elm::Rectangle::New(0, sh - footer_h, sw, footer_h,
                                               g_theme->footer_bg);
    this->Add(this->footer);
    for (int i = 0; i < 8; i++) {
        auto seg = FooterHintElement::New(0, sh - footer_h, footer_h);
        seg->SetLabelColor(g_theme->footer_clr);
        this->Add(seg);
        this->footer_segs.push_back(seg);
    }

    // "Downloads running" pulse on the Queue tab (positioned in SetActiveTab).
    this->queue_dot = PulseDotElement::New(0, 0, 6);
    // Bright logo green in both themes: the dot sits on the charcoal shell.
    this->queue_dot->SetColor(pu::ui::Color(146, 214, 36, 255));
    this->Add(this->queue_dot);

    // "Update available" pulse on the Settings tab (positioned in SetActiveTab).
    this->settings_dot = PulseDotElement::New(0, 0, 6);
    this->settings_dot->SetColor(pu::ui::Color(146, 214, 36, 255));
    this->Add(this->settings_dot);

    this->SetActiveTab(0);
}

void MainLayout::ApplyTheme() {
    this->SetBackgroundColor(g_theme->bg);
    this->header->SetColor(g_theme->header_bg);
    this->tab_bar->SetColor(g_theme->tab_bar_bg);
    this->footer->SetColor(g_theme->footer_bg);
    this->title->SetColor(g_theme->title_clr);
    for (auto &p : this->bc_parts)
        p->SetColor(g_theme->title_clr);
    this->status->SetColor(g_theme->status_clr);
    this->bat_info->SetColor(g_theme->status_clr);
    this->rom_info->SetColor(g_theme->rom_info_clr);
    this->tab_pill->SetColor(g_theme->tab_under);
    this->queue_dot->SetColor(pu::ui::Color(146, 214, 36, 255));
    this->settings_dot->SetColor(pu::ui::Color(146, 214, 36, 255));
    this->empty_text->SetColor(g_theme->rom_info_clr);
    this->empty_hint->SetColor(g_theme->rom_info_clr);
    this->spinner->SetColors(accent_green(), g_theme->rom_info_clr);
    for (auto &s : this->footer_segs)
        s->SetLabelColor(g_theme->footer_clr);
    for (auto &t : this->tabs)
        t->SetColor(g_theme->tab_clr);
    // Activity = logo green: progress bars and the active-download hero tint.
    this->list->SetThemeColors(g_theme->tl_row_bg, g_theme->tl_row_alt,
                               g_theme->tl_focus, g_theme->tl_scroll,
                               g_theme->tl_mark,
                               accent_green(),
                               is_light_theme()
                                   ? pu::ui::Color(198, 232, 204, 255)
                                   : pu::ui::Color(34, 54, 20, 255),
                               // Darkening chip: reads consistently on normal,
                               // accent (active-download) and selected rows —
                               // and never matches the blue progress bar.
                               is_light_theme()
                                   ? pu::ui::Color(0, 0, 0, 34)
                                   : pu::ui::Color(0, 0, 0, 95),
                               // Page bg drives the list's enter fade-in.
                               g_theme->bg);
    // Card subtitle must stay readable on BOTH the card background and the
    // blue selection fill, so it gets its own shade per theme.
    this->grid->SetThemeColors(g_theme->tl_row_alt, g_theme->tl_focus,
                               g_theme->row_text,
                               is_light_theme()
                                   ? pu::ui::Color(45, 55, 75, 255)
                                   : pu::ui::Color(195, 205, 225, 255),
                               accent_green(),
                               is_light_theme() ? pu::ui::Color(0, 0, 0, 34)
                                                : pu::ui::Color(0, 0, 0, 95),
                               // Page bg drives the grid's enter fade-in.
                               g_theme->bg,
                               // Ring track: white@20 vanishes on the light
                               // theme's pale cards, so darken it there.
                               is_light_theme()
                                   ? pu::ui::Color(0, 0, 0, 40)
                                   : pu::ui::Color(255, 255, 255, 20));
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
    // Pill wraps the active label with a little horizontal padding.
    const s32 pad = 26;
    s32 lx = this->tabs[idx]->GetX();
    s32 lw = this->tabs[idx]->GetWidth();
    this->tab_pill->SetBounds(lx - pad, 88, lw + 2 * pad, 54);
    // Park the Queue-tab pulse just after the Queue (index 2) label.
    if (this->tabs.size() > 2) {
        this->queue_dot->SetPos(this->tabs[2]->GetX() +
                                    this->tabs[2]->GetWidth() + 10,
                                92);
    }
    // Park the "update available" pulse just after the Settings (index 3) label.
    if (this->tabs.size() > 3) {
        this->settings_dot->SetPos(this->tabs[3]->GetX() +
                                       this->tabs[3]->GetWidth() + 10,
                                   92);
    }
}
void MainLayout::SetQueueActivity(bool active) {
    this->queue_dot->SetActive(active);
}
void MainLayout::SetUpdateAvailable(bool avail) {
    this->settings_dot->SetActive(avail);
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
    // The wordmark blocks stay put; the breadcrumb is split on " > " so the
    // separators can render as green chevrons between the segments.
    std::vector<std::string> parts;
    size_t pos = 0;
    for (;;) {
        if (parts.size() == 2) { // last slot: keep any remaining tail intact
            parts.push_back(t.substr(pos));
            break;
        }
        size_t sep = t.find(" > ", pos);
        if (sep == std::string::npos) {
            parts.push_back(t.substr(pos));
            break;
        }
        parts.push_back(t.substr(pos, sep - pos));
        pos = sep + 3;
    }
    // (Space, not empty: TextBlock re-renders its texture on every SetText.)
    this->title->SetText(parts[0].empty() ? std::string(" ") : parts[0]);
    // Re-base at the fixed anchor: SetTitleIcon shifts the title right to
    // make room for the icon, so anchoring at GetX() would compound that
    // shift on every screen change and walk the breadcrumb off to the right.
    this->title->SetX(this->title_x0);
    s32 x = this->title_x0 + this->title->GetWidth();
    for (size_t i = 0; i < this->bc_seps.size(); i++) {
        bool has = (i + 1) < parts.size();
        this->bc_seps[i]->SetText(has ? "›" : " ");
        this->bc_parts[i]->SetText(has && !parts[i + 1].empty() ? parts[i + 1]
                                                                : " ");
        if (has) {
            this->bc_seps[i]->SetX(x + 14);
            x += 14 + this->bc_seps[i]->GetWidth() + 14;
            this->bc_parts[i]->SetX(x);
            x += this->bc_parts[i]->GetWidth();
        } else {
            this->bc_seps[i]->SetX(-100);
            this->bc_parts[i]->SetX(-100);
        }
    }
    this->bc_end_x = x;
    // Default: no console icon (screens with one call SetTitleIcon after this).
    this->title_icon->SetTexture(nullptr);
}
void MainLayout::SetTitleIcon(pu::sdl2::Texture tex) {
    this->title_icon->SetTexture(tex);
    if (!tex) {
        return;
    }
    // The icon leads the breadcrumb: place it at the breadcrumb start and
    // shift the text segments right to make room. (SetTitle always runs
    // first and resets the positions, so the shift applies exactly once.)
    const s32 d = 46 + 14;
    this->title_icon->SetPos(this->title->GetX(), 20);
    this->title->SetX(this->title->GetX() + d);
    for (auto &s : this->bc_seps) {
        if (s->GetX() != -100) {
            s->SetX(s->GetX() + d);
        }
    }
    for (auto &p : this->bc_parts) {
        if (p->GetX() != -100) {
            p->SetX(p->GetX() + d);
        }
    }
    this->bc_end_x += d;
}
void MainLayout::SetRomInfo(const std::string &t) {
    // Skip when unchanged: the queue card view calls this every frame, and
    // TextBlock::SetText re-renders its texture unconditionally.
    if (this->rom_info->GetText() != t) {
        this->rom_info->SetText(t);
    }
}
static void layout_status_bar(pu::ui::elm::TextBlock::Ref &storage,
                              NetBarsElement::Ref &net,
                              BatteryElement::Ref &bat_ic,
                              pu::ui::elm::TextBlock::Ref &bat) {
    // Right-aligned, left-to-right: network | storage | battery icon + %.
    const s32 margin = 30, gap = 12;
    s32 sw = (s32)pu::ui::render::ScreenWidth;
    s32 bw = bat->GetWidth();
    bat->SetX(sw - margin - bw);
    // Sit the drawn icons on the digits' baseline: the text block height
    // includes the font descender (~1/5 line), which digits never reach.
    s32 base = bat->GetY() + bat->GetHeight() - bat->GetHeight() / 5;
    bat_ic->SetPos(bat->GetX() - 8 - bat_ic->GetWidth(),
                   base - bat_ic->GetHeight());
    s32 stw = storage->GetWidth();
    // Extra breathing room between the storage (GB) text and the battery icon.
    storage->SetX(bat_ic->GetX() - (gap + 10) - stw);
    net->SetPos(storage->GetX() - gap - net->GetWidth(),
                base - net->GetHeight());
}

void MainLayout::SetStatus(const std::string &t) {
    this->status->SetText(t);
    layout_status_bar(this->status, this->net_bars, this->bat_icon,
                      this->bat_info);
}
void MainLayout::SetNetLevel(int lit) { this->net_bars->SetLevel(lit); }
void MainLayout::SetBattery(int pct, bool charging) {
    this->bat_icon->Set(pct, charging);
}
void MainLayout::SetBatInfo(const std::string &t) {
    this->bat_info->SetText(t);
    layout_status_bar(this->status, this->net_bars, this->bat_icon,
                      this->bat_info);
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
    const s32 seg_gap = 14; // minimum air between neighbouring segments
    s32 prev_right = margin - seg_gap;
    int n = (int)segs.size();
    for (int k = 0; k < (int)this->footer_segs.size(); k++) {
        if (k < n) {
            this->footer_segs[k]->SetHint(segs[k]);
            s32 cell = (sw - 2 * margin) / (n > 0 ? n : 1);
            s32 center = margin + cell * k + cell / 2;
            s32 w = this->footer_segs[k]->Width();
            s32 x = center - w / 2;
            // A segment wider than its cell must not overlap its neighbour:
            // push it right past the previous segment, and clamp to the
            // screen margins rather than sliding off the left/right edge.
            if (x < prev_right + seg_gap) {
                x = prev_right + seg_gap;
            }
            if (x + w > sw - margin) {
                x = sw - margin - w;
            }
            if (x < margin) {
                x = margin;
            }
            this->footer_segs[k]->SetX(x);
            prev_right = x + w;
        } else {
            this->footer_segs[k]->SetHint("");
        }
    }
}
void MainLayout::ClearMenu(bool fade) {
    this->list->Clear(fade);
    this->grid->Clear();
    this->cards_mode = false; // card screens opt back in after ClearMenu
    this->rom_info->SetText("");
    this->ClearEmptyState();
    this->HideSpinner();
}
void MainLayout::SetEmptyState(pu::sdl2::Texture icon, const std::string &msg,
                               const std::string &hint, bool spacious,
                               const std::string &note) {
    this->empty_icon->SetTexture(icon); // pointer store, cheap every frame

    // Two layouts share these three elements. Ordinary empty lists use a
    // compact centred block; the LAN-import page uses a roomier "instruction
    // sheet" — icon lifted, a gap below the address, and larger step text — so
    // it reads like directions rather than an empty-state notice. Positions are
    // reset on every call so switching screens restores the right layout.
    const s32 list_y = 158, footer_h = 64;
    const s32 cy = list_y +
                   ((s32)pu::ui::render::ScreenHeight - list_y - footer_h) / 2;
    this->empty_icon->SetPos(this->empty_icon->GetX(),
                             cy - (spacious ? 210 : 150));
    this->empty_text->SetY(cy + (spacious ? -5 : 50));
    this->empty_hint->SetY(cy + (spacious ? 100 : 98));
    this->empty_hint->SetFont(pu::ui::GetDefaultFont(
        spacious ? pu::ui::DefaultFontSize::MediumLarge
                 : pu::ui::DefaultFontSize::Small));

    if (this->empty_text->GetText() != msg) {
        this->empty_text->SetText(msg);
        // Centre the message under the icon.
        this->empty_text->SetX((s32)pu::ui::render::ScreenWidth / 2 -
                               this->empty_text->GetWidth() / 2);
    }
    if (this->empty_hint->GetText() != hint) {
        this->empty_hint->SetText(hint);
        this->empty_hint->SetX((s32)pu::ui::render::ScreenWidth / 2 -
                               this->empty_hint->GetWidth() / 2);
    }

    // Optional accent chip beneath the hint block.
    if (note.empty()) {
        this->empty_chip->SetVisible(false);
        this->empty_chip_text->SetVisible(false);
    } else {
        if (this->empty_chip_text->GetText() != note) {
            this->empty_chip_text->SetText(note);
        }
        const s32 padx = 24, pady = 11;
        s32 cw = this->empty_chip_text->GetWidth() + padx * 2;
        s32 ch = this->empty_chip_text->GetHeight() + pady * 2;
        s32 cx = (s32)pu::ui::render::ScreenWidth / 2 - cw / 2;
        // Sit below the (multi-line) hint, using its rendered height.
        s32 cy = this->empty_hint->GetY() + this->empty_hint->GetHeight() + 26;
        this->empty_chip->SetX(cx);
        this->empty_chip->SetY(cy);
        this->empty_chip->SetWidth(cw);
        this->empty_chip->SetHeight(ch);
        this->empty_chip_text->SetX(cx + padx);
        this->empty_chip_text->SetY(cy + pady);
        this->empty_chip->SetVisible(true);
        this->empty_chip_text->SetVisible(true);
    }
}
void MainLayout::ClearEmptyState() {
    this->empty_icon->SetTexture(nullptr);
    this->empty_text->SetText("");
    this->empty_hint->SetText("");
    this->empty_chip->SetVisible(false);
    this->empty_chip_text->SetVisible(false);
}
void MainLayout::ShowSpinner(const std::string &msg) {
    this->spinner->Show(msg);
}
void MainLayout::HideSpinner() { this->spinner->Hide(); }
void MainLayout::SetCardsMode(bool on) { this->cards_mode = on; }
void MainLayout::AddCard(const std::string &title, const std::string &subtitle,
                         pu::sdl2::Texture icon, bool pinned, bool dim) {
    this->grid->AddCard(title, subtitle, icon, pinned, dim);
}
void MainLayout::SetSingleCard(bool on) { this->grid->SetSingle(on); }
void MainLayout::SetQueueCount(s32 n) { this->grid->SetQueueCount(n); }
void MainLayout::SetQueueCard(s32 i, const std::string &console,
                              pu::sdl2::Texture icon,
                              const std::string &status, pu::ui::Color st_clr,
                              const std::string &size, const std::string &speed,
                              const std::string &eta, const std::string &file,
                              float prog, bool hero, s32 ring, s32 qpos,
                              bool refresh_text) {
    this->grid->SetQueueCard(i, console, icon, status, st_clr, size, speed,
                             eta, file, prog, hero, ring, qpos, refresh_text);
}
void MainLayout::CardMove(s32 dx, s32 dy) { this->grid->Move(dx, dy); }
void MainLayout::AddRow(const std::string &name) {
    this->AddRow(name, g_theme->row_text);
}
void MainLayout::AddRow(const std::string &name, pu::ui::Color clr,
                        pu::sdl2::Texture icon, bool pin) {
    this->list->AddRow(name, clr, icon, pin);
}
void MainLayout::AddRow2(const std::string &left, const std::string &right,
                         pu::ui::Color lclr, pu::ui::Color rclr, float progress,
                         pu::sdl2::Texture icon, const std::string &prefix,
                         bool accent, bool pill, bool pin, s32 bar) {
    this->list->AddRow2(left, right, lclr, rclr, progress, icon, prefix, accent,
                        pill, pin, bar);
}
void MainLayout::SetRowRight(s32 i, const std::string &right,
                             pu::ui::Color rclr) {
    this->list->SetRowRight(i, right, rclr);
}
s32 MainLayout::Sel() {
    return this->cards_mode ? this->grid->GetSelected()
                            : this->list->GetSelected();
}
void MainLayout::SetSel(s32 i) {
    if (this->cards_mode) {
        this->grid->SetSelected(i);
    } else {
        this->list->SetSelected(i);
    }
}
bool MainLayout::ConsumeTouchActivate() {
    // Consume both so the inactive one can't hold a stale activation.
    bool g = this->grid->ConsumeTouchActivate();
    bool l = this->list->ConsumeTouchActivate();
    return this->cards_mode ? g : l;
}
s32 MainLayout::RowCount() {
    return this->cards_mode ? this->grid->Count() : this->list->Count();
}
void MainLayout::MoveBy(s32 delta) { this->list->MoveBy(delta); }
void MainLayout::Step(s32 delta) { this->list->Step(delta); }
void MainLayout::MoveUp() { this->MoveBy(-1); }
void MainLayout::MoveDown() { this->MoveBy(1); }
void MainLayout::PageUp() {
    if (this->cards_mode) {
        this->grid->PageMove(-1);
    } else {
        this->MoveBy(-this->list->RowsVisible());
    }
}
void MainLayout::PageDown() {
    if (this->cards_mode) {
        this->grid->PageMove(1);
    } else {
        this->MoveBy(this->list->RowsVisible());
    }
}
void MainLayout::ToggleMark(s32 i) { this->list->ToggleMark(i); }
int MainLayout::MarkedCount() { return this->list->MarkedCount(); }
const std::set<s32> &MainLayout::Marked() { return this->list->Marked(); }
void MainLayout::ClearMarks() { this->list->ClearMarks(); }

// ---- app: feedback --------------------------------------------------------
void MainApplication::Toast(const std::string &msg) {
    auto tb = pu::ui::elm::TextBlock::New(0, 0, msg);
    tb->SetColor(pu::ui::Color(255, 255, 255, 255));
    auto t = pu::ui::extras::Toast::New(tb, pu::ui::Color(58, 110, 22, 240));
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

bool MainApplication::ConfirmDanger(const std::string &title,
                                    const std::string &msg, bool permanent) {
    std::string m = msg;
    if (permanent) {
        m += "\n\n";
        m += tr(S_CANT_UNDO);
    }
    // Cancel first (safe default); the red title flags the destructive intent.
    int r = this->CreateShowDialog(title, m, {tr(S_CANCEL), tr(S_YES)}, false,
                                   {}, style_dialog_danger);
    return r == 1;
}

bool MainApplication::SpaceOkToQueue(uint64_t add_size) {
    if (add_size == 0) return true; // size unknown from metadata: don't block

    // FAT32 can't hold a single file >= 4 GiB. Most Switch SD cards are FAT32
    // (exFAT needs the optional firmware update), and there is no reliable way
    // to tell the SD's format at runtime, so warn rather than silently let the
    // download truncate at the 4 GiB boundary. exFAT users (or anyone who
    // accepts the risk) can proceed; the ack is remembered for the session so
    // queueing several big files doesn't nag on every one.
    const uint64_t FAT32_MAX_FILE = 0xFFFFFFFFULL; // 4 GiB - 1
    if (add_size > FAT32_MAX_FILE && !this->fat32_ack) {
        if (!this->Confirm(tr(S_FAT32_WARN), tr(S_FAT32_WARN_MSG)))
            return false;
        this->fat32_ack = true;
    }

    uint64_t freeb = fs_free_bytes("sdmc:/");
    if (freeb == UINT64_MAX) return true; // statvfs failed: don't block
    // Sum what the queue still has to pull (metadata size minus any .part
    // already on disk) so the check accounts for items queued earlier.
    uint64_t need = add_size;
    static QueueView v[QUEUE_MAX]; // ~130KB; keep off the UI-thread stack
    int n = queue_snapshot(v, QUEUE_MAX);
    for (int i = 0; i < n; i++) {
        QStatus s = v[i].item.status;
        if (s == Q_DONE || s == Q_SAVED || s == Q_FAILED || s == Q_CANCELLED)
            continue; // finished/failed items no longer need space
        if (v[i].item.size > v[i].item.now)
            need += v[i].item.size - v[i].item.now;
    }
    if (need <= freeb) return true;
    // "needed > free" mirrors the warning sentence ("total size exceeds free
    // space") and reads the same in any language, so no new strings are needed.
    return this->Confirm(tr(S_FREE_SPACE_WARN),
                         human_size(need) + "  >  " + human_size(freeb));
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
    char s[80];
    snprintf(s, sizeof(s), "%s/%s", sf.c_str(), st.c_str());
    this->layout->SetStatus(s);
    char bs[32];
    snprintf(bs, sizeof(bs), "%u%%", (unsigned)bat);
    this->layout->SetBatInfo(bs);
    this->layout->SetBattery((int)bat,
                             charger != PsmChargerType_Unconnected);

    NifmInternetConnectionType ntype = (NifmInternetConnectionType)0;
    u32 wstr = 0;
    NifmInternetConnectionStatus nst = (NifmInternetConnectionStatus)0;
    bool net = R_SUCCEEDED(nifmGetInternetConnectionStatus(&ntype, &wstr, &nst)) &&
               nst == NifmInternetConnectionStatus_Connected;
    g_net_ok = net;
    if (net) {
        // Wired (LAN adapter) reports wireless strength 0; show full bars.
        int lvl = (ntype == NifmInternetConnectionType_Ethernet) ? 3
                  : (wstr > 3)                                   ? 3
                                                                 : (int)wstr;
        this->layout->SetNetLevel(lvl + 1); // wstr 0 still means "connected"
    } else {
        this->layout->SetNetLevel(-1);
    }
}

static bool console_has_pin(const ConsoleGroup *g) {
    for (int i = 0; i < g->repo_count; i++)
        if (g->repos[i].pinned) return true;
    return false;
}

// Name the import path for the empty Home screen: the welcome dialog is
// one-shot per launch, and Y is not a discoverable way to be told about it.
// Built from the menu's own strings so it can't drift from what the menus say —
// in any language.
static std::string import_hint() {
    char h[160];
    snprintf(h, sizeof(h), "%s → %s → %s", tr(S_TITLE_SETTINGS),
             tr(S_MANAGE_DATA), tr(S_IMPORT_COLLECTION));
    return h;
}

// ---- screens --------------------------------------------------------------
void MainApplication::GotoHome() {
    this->screen = Screen::Home;
    this->layout->ClearMenu();
    if (g_prefs.group_consoles) {
        bool cards = g_prefs.card_view;
        this->layout->SetTitle(tr(S_TITLE_CONSOLES));
        this->layout->SetSubtitle(cards ? tr(S_SUB_HOME_CARDS)
                                        : tr(S_SUB_HOME_GROUPED));
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
            if (cards) {
                // Card: full name as the (wrappable) title; counts beneath.
                const char *cname = g_cfg.consoles[row.idx].console;
                const char *full = console_full_name(cname);
                this->layout->AddCard(full ? full : cname, cnt,
                                      console_icon(cname), row.pinned);
            } else {
                this->layout->AddRow2(
                    row.label, cnt, g_theme->row_text, count_color(), -1.0f,
                    console_icon(g_cfg.consoles[row.idx].console), "", false,
                    true, row.pinned);
            }
            g_home_map.push_back(row.idx);
        }
        if (g_home_map.empty()) {
            this->layout->SetEmptyState(console_icon("default"),
                                        tr(S_NO_COLLECTIONS), import_hint());
        } else if (cards) {
            this->layout->SetCardsMode(true);
        }
    } else {
        bool cards = g_prefs.card_view;
        this->layout->SetTitle(tr(S_TITLE_REPOS));
        this->layout->SetSubtitle(cards ? tr(S_SUB_HOME_FLAT_CARDS)
                                        : tr(S_SUB_HOME_FLAT));
        struct FlatRow {
            std::string cname; // full console name
            std::string repo;  // repo label
            std::string key;
            bool pinned;
            bool enabled;
        };
        std::vector<FlatRow> flat_rows;
        for (int c = 0; c < g_cfg.console_count; c++) {
            if (!g_cfg.consoles[c].shown) continue;
            for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
                Repo *rp = &g_cfg.consoles[c].repos[r];
                const char *cname = g_cfg.consoles[c].console;
                const char *full = console_full_name(cname);
                flat_rows.push_back({full ? full : cname, rp->label, cname,
                                     rp->pinned, rp->enabled != 0});
            }
        }
        for (const auto &fr : flat_rows) {
            if (cards) {
                // One card per repo: console name as title, repo beneath
                // (with the off state noted, since there is no chip here).
                std::string sub = fr.enabled
                                      ? fr.repo
                                      : fr.repo + " · " + tr(S_OFF);
                // Disabled repos also dim their icon so on/off scans
                // without reading the subtitle.
                this->layout->AddCard(fr.cname, sub,
                                      console_icon(fr.key.c_str()),
                                      fr.pinned, !fr.enabled);
            } else {
                // "Full Console Name › repo label", matching the breadcrumb;
                // the on/off state moves to a right-hand chip.
                this->layout->AddRow2(fr.cname + " › " + fr.repo,
                                      fr.enabled ? tr(S_ON) : tr(S_OFF),
                                      g_theme->row_text,
                                      onoff_color(fr.enabled), -1.0f,
                                      console_icon(fr.key.c_str()), "", false,
                                      true, fr.pinned);
            }
        }
        if (flat_count() == 0) {
            this->layout->SetEmptyState(console_icon("default"),
                                        tr(S_NO_REPOS), import_hint());
        } else if (cards) {
            this->layout->SetCardsMode(true);
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
    this->layout->SetTitleIcon(console_icon(g->console));
    this->layout->SetSubtitle(tr(S_SUB_REPOS));
    this->layout->ClearMenu();
    // Float pinned repos to the top for display only, keeping the stored array
    // order untouched so unpinning returns a repo to its original position.
    // g_repos_map maps each visible row back to its repo array index.
    g_repos_map.clear();
    for (int i = 0; i < g->repo_count; i++)
        if (g->repos[i].pinned) g_repos_map.push_back(i);
    for (int i = 0; i < g->repo_count; i++)
        if (!g->repos[i].pinned) g_repos_map.push_back(i);
    for (int idx : g_repos_map) {
        // On/off state as a coloured right-hand chip, matching the flat
        // Browse rows (the console is already in the title, so no icon).
        this->layout->AddRow2(g->repos[idx].label,
                              g->repos[idx].enabled ? tr(S_ON) : tr(S_OFF),
                              g_theme->row_text,
                              onoff_color(g->repos[idx].enabled), -1.0f,
                              nullptr, "", false, true, g->repos[idx].pinned);
    }
    if (g->repo_count == 0) {
        this->layout->AddRow(tr(S_NO_REPOS));
    }
    this->layout->SetSel(ci == this->repos_sel_ci ? this->repos_sel : 0);
}

void MainApplication::GotoFiles(int ci, int ri, bool force) {
    g_sort_mode = SORT_DEFAULT;
    g_files_manual = false;
    this->sel_ci = ci;
    this->sel_ri = ri;
    ConsoleGroup *g = &g_cfg.consoles[ci];
    Repo *rp = &g->repos[ri];
    this->layout->SetTitle(std::string(g->console) + " > " + rp->label);
    this->layout->SetTitleIcon(console_icon(g->console));
    this->screen = Screen::Files;
    this->StartMetaLoad(rp->id, rp->download_base, g->target, force,
                        FILES_SUBTITLE);
}

// ---- tabs -----------------------------------------------------------------
MainApplication::Tab MainApplication::CurrentTab() {
    switch (this->screen) {
    case Screen::Installed:
    case Screen::InstSearch: return Tab::Installed;
    case Screen::Queue:     return Tab::Queue;
    case Screen::Settings:
    case Screen::Log:
    case Screen::Manage:
    case Screen::Creds:
    case Screen::Advanced:
    case Screen::UISettings:
    case Screen::ExtFilter:
    case Screen::RomPicker:
    case Screen::Downloads:
    case Screen::Language:
    case Screen::Cache:
    case Screen::ManageData:
    case Screen::ViewLogs:
    case Screen::DebugLog:
    case Screen::Import:
    case Screen::ReleaseNotes:
    case Screen::ReleaseNote:
    case Screen::QueueState: return Tab::Settings;
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
    this->layout->SetSubtitle(
        tr(g_prefs.card_view ? S_SUB_QUEUE_CARDS : S_SUB_QUEUE));
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

// Strip the trailing value placeholder from a "Label: %s" settings string so
// label and value can live in separate columns; also trims a trailing colon.
static std::string settings_label(const char *fmt) {
    std::string s = fmt ? fmt : "";
    size_t pct = s.find('%');
    if (pct != std::string::npos) {
        s.erase(pct);
    }
    while (!s.empty()) {
        char c = s.back();
        if (c == ' ' || c == '\t' || c == ':') {
            s.pop_back();
            continue;
        }
        // Fullwidth colon U+FF1A (EF BC 9A), used by CJK strings.
        if (s.size() >= 3 && (unsigned char)s[s.size() - 3] == 0xEF &&
            (unsigned char)s[s.size() - 2] == 0xBC &&
            (unsigned char)s[s.size() - 1] == 0x9A) {
            s.erase(s.size() - 3);
            continue;
        }
        break;
    }
    return s;
}
// Download-rate presets (KiB/s; 0 = unlimited), shared by both throttle rows in
// Advanced settings. A press cycles to the next; Left/Right step by one.
static const int kRatePresets[] = {0, 64, 128, 256, 512, 1024, 2048, 5120, 10240};
static const int kRatePresetCount =
    (int)(sizeof(kRatePresets) / sizeof(kRatePresets[0]));

static int rate_preset_index(int kbps) {
    // Exact preset, else the highest one not exceeding kbps (0 as the fallback).
    int idx = 0;
    for (int i = 0; i < kRatePresetCount; i++) {
        if (kRatePresets[i] == kbps) return i;
        if (kRatePresets[i] < kbps) idx = i;
    }
    return idx;
}

static int rate_step(int kbps, int dir) {
    int i = rate_preset_index(kbps) + dir;
    i = ((i % kRatePresetCount) + kRatePresetCount) % kRatePresetCount; // wrap
    return kRatePresets[i];
}

static std::string rate_display(int kbps) {
    if (kbps <= 0) return tr(S_RATE_UNLIMITED);
    char v[32];
    if (kbps >= 1024 && (kbps % 1024) == 0)
        snprintf(v, sizeof(v), "%d MB/s", kbps / 1024);
    else
        snprintf(v, sizeof(v), "%d KB/s", kbps);
    return v;
}

// Push the current throttle prefs (KiB/s) to the queue as bytes/sec.
static void apply_rate_limits(void) {
    queue_set_rate_limits(g_prefs.rate_all_kbps * 1024,
                          g_prefs.rate_item_kbps * 1024);
}

// Value column colours (theme-aware).
static pu::ui::Color onoff_color(bool on) {
    bool light = is_light_theme();
    if (on) {
        return accent_green();
    }
    return light ? pu::ui::Color(120, 122, 132, 255)
                 : pu::ui::Color(135, 140, 155, 255);
}
static pu::ui::Color value_color() {
    return is_light_theme() ? pu::ui::Color(40, 90, 155, 255)
                            : pu::ui::Color(150, 190, 240, 255);
}
static pu::ui::Color chevron_color() {
    return pu::ui::Color(125, 132, 150, 255);
}
static const char *CHEVRON = "›"; // › — marks a row that opens a screen

void MainApplication::GotoSettings() {
    this->screen = Screen::Settings;
    this->layout->SetTitle(std::string(tr(S_TITLE_SETTINGS)) + "   (v" + APP_VERSION_STR + ")");
    this->layout->SetSubtitle(tr(S_SUB_SETTINGS));
    this->layout->ClearMenu();
    // Row order here is the contract for the A-press switch in OnInput; the
    // card grid indexes the same way, so both views share it.
    static const struct { int str; const char *icon; } kEntries[] = {
        {S_CHECK_UPDATES, "set-updates"}, // 0
        {S_UI_SETTINGS, "set-ui"},        // 1
        {S_ADVANCED, "set-advanced"},     // 2
        {S_VIEW_LOGS, "set-logs"},        // 3
        {S_MANAGE_DATA, "set-data"},      // 4
        {S_CREDITS, "set-credits"},       // 5
    };
    if (g_prefs.card_view) {
        for (const auto &e : kEntries) {
            // Row 0 (Check for updates) shows an "Update available" subtitle when
            // the silent startup check found one; the card renders it as the same
            // darkening pill used for the download cards' size/speed chip.
            const char *sub =
                (e.str == S_CHECK_UPDATES &&
                 (this->update_available || this->update_installed))
                    ? tr(this->update_installed ? S_RESTART_TO_UPDATE
                                                : S_UPDATE_AVAIL)
                    : "";
            this->layout->AddCard(tr(e.str), sub, console_icon(e.icon), false);
        }
        this->layout->SetCardsMode(true);
    } else {
        pu::ui::Color lbl = g_theme->row_text;
        pu::ui::Color chv = chevron_color();
        for (const auto &e : kEntries) {
            if (e.str == S_CHECK_UPDATES &&
                (this->update_available || this->update_installed)) {
                // Actionable chip far-right, in the same pill the list's
                // size/status values use (pill = true), tinted affirmative
                // green. Once a build is staged it becomes "Restart to update".
                this->layout->AddRow2(
                    tr(e.str),
                    tr(this->update_installed ? S_RESTART_TO_UPDATE
                                              : S_UPDATE_AVAIL),
                    lbl, onoff_color(true), -1.0f, nullptr, "", false, true);
            } else {
                this->layout->AddRow2(tr(e.str), CHEVRON, lbl, chv, -1.0f,
                                      nullptr, "", false, false);
            }
        }
    }
    char ri[600];
    snprintf(ri, sizeof(ri), tr(S_ROM_FOLDER), roms_root(&g_tico));
    this->layout->SetRomInfo(ri);
}

void MainApplication::GotoAdvanced() {
    this->screen = Screen::Advanced;
    this->layout->SetTitle(tr(S_TITLE_ADVANCED));
    this->layout->SetSubtitle(tr(S_SUB_ADVANCED));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    bool b;
    {
        char v[16];
        snprintf(v, sizeof(v), "%d", g_prefs.max_downloads);
        this->layout->AddRow2(settings_label(tr(S_MAX_DOWNLOADS)), v, lbl,
                              value_color());                      // 0
    }
    {
        bool lim = g_prefs.rate_all_kbps > 0;
        this->layout->AddRow2(settings_label(tr(S_MAX_TOTAL_RATE)),
                              rate_display(g_prefs.rate_all_kbps), lbl,
                              lim ? value_color() : onoff_color(false)); // 1
    }
    {
        bool lim = g_prefs.rate_item_kbps > 0;
        this->layout->AddRow2(settings_label(tr(S_MAX_ITEM_RATE)),
                              rate_display(g_prefs.rate_item_kbps), lbl,
                              lim ? value_color() : onoff_color(false)); // 2
    }
    b = g_prefs.prevent_sleep;
    this->layout->AddRow2(settings_label(tr(S_STAY_AWAKE)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 3
    b = g_prefs.net_check;
    this->layout->AddRow2(settings_label(tr(S_NET_CHECK_STARTUP)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 4
    b = g_prefs.chk_updates;
    this->layout->AddRow2(settings_label(tr(S_CHK_UPDATES_STARTUP)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 5
    b = g_creds.access_key[0] != '\0';
    this->layout->AddRow2(settings_label(tr(S_ARCHIVE_CREDS)),
                          b ? tr(S_SET) : tr(S_UNSET), lbl, onoff_color(b)); // 6
    /* Metadata cache toggle and ROM folder moved to Manage data. */
}

/* Browse the SD card and choose a folder to use as the ROM root. Shows only
 * directories (you're picking a folder, not a file). */
void MainApplication::GotoRomPicker(const std::string &path) {
    this->screen = Screen::RomPicker;
    this->picker_path = path;
    this->layout->SetTitle(tr(S_TITLE_ROM_PICKER));
    this->layout->SetSubtitle(tr(S_SUB_ROM_PICKER));
    this->layout->ClearMenu();

    g_rompick = list_dir(path);
    /* Directories only. */
    g_rompick.erase(std::remove_if(g_rompick.begin(), g_rompick.end(),
                                   [](const DirEnt &e) { return !e.is_dir; }),
                    g_rompick.end());

    pu::ui::Color lbl = g_theme->row_text;
    if (g_rompick.empty()) {
        this->layout->AddRow(tr(S_NO_SUBFOLDERS));
    } else {
        for (const auto &e : g_rompick) {
            this->layout->AddRow2(std::string(tr(S_DIR_PREFIX)) + e.name,
                                  CHEVRON, lbl, chevron_color(), -1.0f, nullptr,
                                  "", false, false);
        }
    }
    char info[600];
    snprintf(info, sizeof(info), tr(S_ROMS_CURRENT), this->picker_path.c_str());
    this->layout->SetRomInfo(info);
}

void MainApplication::GotoUISettings() {
    this->screen = Screen::UISettings;
    this->layout->SetTitle(tr(S_TITLE_UI_SETTINGS));
    this->layout->SetSubtitle(tr(S_SUB_UI_SETTINGS));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    bool b = g_prefs.card_view;
    this->layout->AddRow2(settings_label(tr(S_CARD_VIEW)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 0
    this->layout->AddRow2(settings_label(tr(S_THEME)),
                          is_light_theme() ? tr(S_THEME_LIGHT) : tr(S_THEME_DARK),
                          lbl, value_color());                                 // 1
    const char *cur = g_prefs.lang[0] ? g_prefs.lang : "en";
    this->layout->AddRow2(settings_label(tr(S_LANGUAGE)), lang_display_name(cur),
                          lbl, value_color());                                 // 2
    b = g_prefs.group_consoles;
    this->layout->AddRow2(settings_label(tr(S_GROUP_CONSOLES)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 3
    this->layout->AddRow2(tr(S_MANAGE_CONSOLES), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);       // 4
    b = g_prefs.filter_exts;
    this->layout->AddRow2(settings_label(tr(S_FILTER_EXTS)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 5
}

// Browse file-view extension filter editor: a master ON/OFF switch, one
// toggle per excluded extension, and a row to add a custom one. Reached from
// UI settings. The per-extension states persist whether or not the master
// switch is on (it only gates whether they are applied — see prefs_ext_hidden).
void MainApplication::GotoExtFilter() {
    this->screen = Screen::ExtFilter;
    this->layout->SetTitle(tr(S_TITLE_EXT_FILTER));
    this->layout->SetSubtitle(tr(S_SUB_EXT_FILTER));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    bool on = g_prefs.filter_exts;
    this->layout->AddRow2(settings_label(tr(S_FILTER_EXTS)),
                          on ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(on)); // 0
    for (int i = 0; i < g_prefs.exclude_ext_count; i++) {
        const FilterExt *fe = &g_prefs.exclude_exts[i];
        std::string name = std::string(".") + fe->ext;
        this->layout->AddRow2(name, fe->enabled ? tr(S_ON) : tr(S_OFF), lbl,
                              onoff_color(fe->enabled));           // 1..N
    }
    this->layout->AddRow2(tr(S_ADD_EXTENSION), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);       // N+1
    this->layout->SetRomInfo(tr(S_EXT_FILTER_INFO));
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
    pu::ui::Color lbl = g_theme->row_text;
    this->layout->AddRow(tr(S_IMPORT_COLLECTION));  // 0 receive dl_sources.json from a PC
    this->layout->AddRow(tr(S_RESTORE_COLLECTION)); // 1 restore previous collection
    {                                               // 2 ROM folder (moved from Advanced)
        bool custom = g_prefs.roms_override[0] != '\0';
        this->layout->AddRow2(settings_label(tr(S_ROMS_OVERRIDE)),
                              custom ? roms_root(&g_tico) : tr(S_ROMS_AUTO), lbl,
                              custom ? value_color() : onoff_color(false));
    }
    this->layout->AddRow(tr(S_MANAGE_DOWNLOADS));   // 3 manage downloads folder
    this->layout->AddRow(tr(S_MANAGE_CACHE));       // 4 manage metadata cache
    {                                               // 5 metadata cache on/off (moved from Advanced)
        bool b = g_prefs.use_cache;
        this->layout->AddRow2(settings_label(tr(S_META_CACHE)),
                              b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b));
    }
    this->layout->AddRow(tr(S_REFRESH_ALL));        // 6 refresh all metadata
}

// Append a timestamped line to the collection-transfer log. An import replaces
// dl_sources.json outright, so what arrived and what became of it is worth a
// durable record — the one .bak slot only survives until the next import.
static void xfer_log(const char *fmt, ...) {
    fs_mkdir_p(CONFIG_DIR);
    FILE *f = fopen(XFERLOG_PATH, "a");
    if (!f) {
        return;
    }
    char ts[32] = "";
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *tm = localtime_r(&t, &tmv);
    if (tm) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    }
    fprintf(f, "%s  ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---- import a collection from a PC on the same LAN ------------------------
// The console serves a one-page upload form for as long as this screen is up;
// the user drops dl_sources.json into it from their browser. Nothing leaves the
// local network and no third-party service is involved.
void MainApplication::ImportStart(bool onboarding) {
    char ip[64];
    if (!httpsrv_local_ip(ip, sizeof(ip))) {
        this->ToastErr(tr(S_IMPORT_NO_NET));
        return;
    }
    if (!httpsrv_open(&this->imp_srv)) {
        this->ToastErr(tr(S_IMPORT_SRV_FAIL));
        return;
    }
    this->imp_open = true;
    this->imp_grace = 0; // nothing pending from a previous import
    // Only once the server is up: the early returns above leave the caller
    // where it was, so the flags must not outlive a start that never happened.
    this->imp_onboard = onboarding;
    this->imp_nro = false;
    this->imp_prog = false;
    this->screen = Screen::Import;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d", ip, HTTPSRV_PORT);

    xfer_log("listening  %s", url);

    this->layout->SetTitle(tr(S_TITLE_IMPORT));
    this->layout->SetSubtitle(tr(S_SUB_IMPORT));
    this->layout->ClearMenu();
    // The console is the instruction sheet until the user reaches the page, so
    // it carries the address and the steps: badge, URL, then what to do. The
    // accent chip points power users at the other way in — pushing straight from
    // the app utility on GitHub.
    this->layout->SetEmptyState(g_header_logo, url, tr(S_IMPORT_STEPS), true,
                                tr(S_IMPORT_REPO_NOTE));
}

// The Wi-Fi half of Settings > Check for updates: the exact Import receiver,
// dressed for an app build — the served page and the on-screen steps talk
// about the .nro, and the flow returns to Settings instead of Manage data.
// The receiver itself still routes by content, so either screen forgives a
// file meant for the other.
void MainApplication::UpdateWifiStart() {
    char ip[64];
    if (!httpsrv_local_ip(ip, sizeof(ip))) {
        this->ToastErr(tr(S_IMPORT_NO_NET));
        return;
    }
    if (!httpsrv_open(&this->imp_srv)) {
        this->ToastErr(tr(S_IMPORT_SRV_FAIL));
        return;
    }
    this->imp_srv.nro_page = true; // browser gets the update page, not import
    this->imp_open = true;
    this->imp_grace = 0;
    this->imp_onboard = false;
    this->imp_nro = true;
    this->imp_prog = false;
    this->screen = Screen::Import;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d", ip, HTTPSRV_PORT);

    xfer_log("listening  %s (app update)", url);

    this->layout->SetTitle(tr(S_UPDATE_WIFI_TITLE));
    this->layout->SetSubtitle(tr(S_SUB_IMPORT));
    this->layout->ClearMenu();
    this->layout->SetEmptyState(g_header_logo, url, tr(S_UPDATE_WIFI_STEPS),
                                true, tr(S_IMPORT_REPO_NOTE));
}

// Every way out of the import flow comes through here. A first-run import is
// launched from the welcome dialog on Home, so landing back in Manage data
// would strand a new user in a settings submenu instead of showing them the
// collections they just imported.
void MainApplication::ImportReturn() {
    bool onboard = this->imp_onboard;
    bool nro = this->imp_nro;
    this->imp_onboard = false; // one-shot: only the import it was set for
    this->imp_nro = false;
    if (onboard) {
        this->GotoHome();
    } else if (nro) {
        this->GotoSettings(); // update-over-Wi-Fi came from Settings
    } else {
        this->GotoManageData();
    }
}

void MainApplication::ImportStop() {
    if (this->imp_open) {
        this->layout->ClearEmptyState();
        httpsrv_close(&this->imp_srv);
        this->imp_open = false;
    }
}

// How long to keep answering the browser after a file lands, so its redirect
// to /sent is served before the confirm dialog takes over the render loop.
// ~1s at 60fps, but it ends as soon as the page is collected.
static const int IMPORT_GRACE_FRAMES = 60;

// Serve one request and record the transfers worth keeping. Doing the logging
// here rather than in httpsrv.c keeps that module a plain transport.
int MainApplication::ImportPoll() {
    int r = httpsrv_poll(&this->imp_srv);
    if (r == 3) {
        xfer_log("export     dl_sources.json downloaded to a browser");
    }
    return r;
}

void MainApplication::ImportTick() {
    if (this->imp_grace > 0) {
        // A file is in hand and the browser is being redirected off its POST.
        // Serve until that lands: once ImportApply opens a dialog, nothing here
        // answers requests, and a browser left on a POST re-sends it on reload.
        if (this->ImportPoll() != 2 && --this->imp_grace > 0) {
            return;
        }
        this->ImportApply();
        return;
    }
    if (this->ImportPoll() == 1) {
        this->imp_grace = IMPORT_GRACE_FRAMES;
        return;
    }
    // Live progress while an upload is arriving (the receiver reads a slice
    // per frame). Put the waiting text back once the transfer ends however it
    // ends — completion is handled above, but an aborted client just vanishes.
    size_t now = 0, total = 0;
    if (httpsrv_receiving(&this->imp_srv, &now, &total)) {
        char sub[128];
        snprintf(sub, sizeof(sub), tr(S_RECV_PROGRESS),
                 (int)((now * 100) / total), human_size(now).c_str(),
                 human_size(total).c_str());
        this->layout->SetSubtitle(sub);
        this->imp_prog = true;
    } else if (this->imp_prog) {
        this->layout->SetSubtitle(tr(S_SUB_IMPORT));
        this->imp_prog = false;
    }
}

void MainApplication::ImportApply() {
    // Take the file and stop listening: the import is a one-shot, and the
    // confirm dialog must not run with a live socket behind it.
    char *body = this->imp_srv.body;
    size_t len = this->imp_srv.body_len;
    this->imp_srv.body = NULL; // taken over here; httpsrv_close must not free it
    this->imp_grace = 0;
    this->ImportStop();
    if (!body) {
        this->ImportReturn();
        return;
    }

    // An .nro build pushed instead of a collection: same receiver, different
    // ending — it goes through the self-update staging, not the config import.
    if (len > 0x18 && memcmp(body + 0x10, "NRO0", 4) == 0) {
        this->NroApply(body, len); // takes ownership of body
        return;
    }

    // Check it before saying anything about it: the confirm dialog quotes what
    // the file holds, and nothing on disk is touched until the user agrees.
    int consoles = 0, repos = 0;
    if (!config_probe_json(body, len, &consoles, &repos)) {
        xfer_log("rejected   upload of %zu bytes: no collections in it", len);
        free(body);
        this->ToastErr(tr(S_IMPORT_BAD_FILE));
        this->ImportReturn();
        return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), tr(S_IMPORT_CONFIRM), consoles, repos);
    if (!this->ConfirmDanger(tr(S_IMPORT_COLLECTION), msg)) {
        xfer_log("cancelled  upload of %d console(s), %d repo(s) not applied",
                 consoles, repos);
        free(body);
        this->ImportReturn();
        return;
    }
    bool ok = config_import_json(&g_cfg, body, len, &consoles, &repos);
    free(body);
    if (!ok) {
        // The file parsed a moment ago, so this is the write failing — a full
        // card, most likely. config_import_json has put the old file back.
        xfer_log("FAILED     import could not be written to %s", SOURCES_PATH);
        this->ToastErr(tr(S_IMPORT_SAVE_FAIL));
        this->ImportReturn();
        return;
    }
    xfer_log("import     applied %d console(s), %d repo(s); previous %d kept for "
             "restore", consoles, repos, SOURCES_BAK_SLOTS);
    config_seed_rom_folders(&g_cfg, roms_root(&g_tico));

    char done[96];
    snprintf(done, sizeof(done), tr(S_IMPORT_DONE), consoles, repos);
    this->Toast(done);
    this->ImportReturn();
}

// ---- an .nro build pushed over the same receiver --------------------------
// For testing new builds without plugging the console in over USB: the repo
// editor (or the upload page) sends an .nro, and it goes through the exact
// staging the GitHub self-update uses — written to "<self>.new", swapped in by
// main() on the next launch. Same version as the installed one is fine; that
// is the point when iterating on a build.

// Read the display version out of an NRO's embedded NACP, straight from the
// upload buffer. Best-effort: homebrew NROs carry an "ASET" section after the
// image (icon/nacp/romfs); the NACP's display_version lives at 0x3060.
static bool nro_buf_version(const char *b, size_t n, char *out, size_t out_sz) {
    out[0] = '\0';
    if (n < 0x40 || memcmp(b + 0x10, "NRO0", 4) != 0) {
        return false;
    }
    u32 img = 0;
    memcpy(&img, b + 0x18, 4); // total image size; the ASET header follows it
    if (img == 0 || (size_t)img + 0x38 > n || memcmp(b + img, "ASET", 4) != 0) {
        return false;
    }
    u64 noff = 0, nsz = 0;
    memcpy(&noff, b + img + 0x18, 8); // NACP offset within the asset section
    memcpy(&nsz, b + img + 0x20, 8);
    size_t avail = n - img; // bytes on hand after the image
    if (nsz < 0x3070 || noff > avail || nsz > avail - noff) {
        return false;
    }
    const char *dv = b + img + noff + 0x3060; // NacpStruct.display_version
    size_t m = out_sz - 1 < 0x10 ? out_sz - 1 : 0x10; // 0x10 bytes, NUL-padded
    memcpy(out, dv, m);
    out[m] = '\0';
    return out[0] != '\0';
}

void MainApplication::NroApply(char *body, size_t len) {
    char ver[24];
    if (!nro_buf_version(body, len, ver, sizeof(ver))) {
        snprintf(ver, sizeof(ver), "?");
    }
    xfer_log("received   app build v%s (%zu bytes)", ver, len);

    char msg[512];
    snprintf(msg, sizeof(msg), tr(S_NRO_CONFIRM), ver, human_size(len).c_str(),
             APP_VERSION_STR);
    if (!this->ConfirmDanger(tr(S_TITLE_UPDATE), msg)) {
        xfer_log("cancelled  app build v%s not installed", ver);
        free(body);
        this->ImportReturn();
        return;
    }

    // Stage next to the running NRO. It can't be replaced while it runs (the
    // loader keeps it open), so main() finishes the swap on the next launch —
    // keeping the old build as "<self>.previous", like the GitHub update.
    std::string selfp = resolve_self_path();
    std::string stage = selfp + ".new";
    remove(stage.c_str()); // clear a stale stage so the write can land
    bool ok = false;
    FILE *f = fopen(stage.c_str(), "wb");
    if (f) {
        ok = fwrite(body, 1, len, f) == len;
        if (fclose(f) != 0) {
            ok = false;
        }
    }
    free(body);
    if (ok) {
        ok = looks_like_nro(stage.c_str());
    }
    if (!ok) {
        remove(stage.c_str()); // don't leave a half-written stage behind
        xfer_log("FAILED     app build could not be staged at %s",
                 stage.c_str());
        this->ToastErr(tr(S_NRO_STAGE_FAIL));
        this->ImportReturn();
        return;
    }
    xfer_log("staged     app build v%s -> %s", ver, stage.c_str());

    char done[256];
    snprintf(done, sizeof(done), tr(S_NRO_STAGED), ver);
    if (this->StagedRestartPrompt(done)) {
        return; // closing to restart
    }
    this->ImportReturn();
}

bool MainApplication::StagedRestartPrompt(const std::string &msg) {
    // Same state the GitHub updater leaves behind: the Settings chip flips to
    // "Restart to update" and the tab dot stays lit until the relaunch.
    this->update_installed = true;
    this->layout->SetUpdateAvailable(true);

    // "Restart now" only where a chainload can happen (hbloader); without it
    // the dialog still says the swap lands on the next start.
    bool can_restart = envHasNextLoad();
    std::vector<std::string> opts;
    if (can_restart) {
        opts.push_back(tr(S_NRO_RESTART_NOW));
    }
    opts.push_back(tr(S_NRO_LATER)); // last option = cancel, returns -1
    int r = this->CreateShowDialog(tr(S_TITLE_UPDATE), msg, opts, true, {},
                                   style_dialog);
    if (can_restart && r == 0) {
        // Relaunch ourselves: the next boot of this NRO runs main()'s
        // apply_staged_update, which swaps the files and chainloads the new
        // build — the same route a manual exit-and-reopen takes.
        std::string selfp = resolve_self_path();
        char qargv[1104];
        snprintf(qargv, sizeof(qargv), "\"%s\"", selfp.c_str());
        envSetNextLoad(selfp.c_str(), qargv);
        this->Close();
        return true;
    }
    return false;
}

// Offer the two ways to get collections onto a console that has none. The app
// ships with an empty dl_sources.json by design, so a new user's first screen is
// otherwise an empty list, and nothing on it mentions that the LAN import or the
// app utility exist.
//
// Gated on having no collections rather than a "seen it" pref: an empty app is
// unusable, so the prompt can never be in the way, and it stops for good the
// moment anything is added. A pref would also desync — wiping dl_sources.json
// while keeping prefs.json would spend the guidance and never offer it again.
void MainApplication::Welcome() {
    // Last option as cancel, so it and B both come back as -1 (see the note on
    // CreateShowDialog above) — a real index is never returned for "Not now".
    int r = this->CreateShowDialog(
        tr(S_WELCOME_TITLE), tr(S_WELCOME_BODY),
        {tr(S_WELCOME_IMPORT), tr(S_WELCOME_MANUAL), tr(S_WELCOME_LATER)}, true,
        {}, style_dialog);
    if (r == 0) {
        this->ImportStart(true); // come back to Home, not into Manage data
    } else if (r == 1) {
        this->GotoPicker(Pending::AddRepo); // same flow Y opens on Home
    }
}

// Put one of the backups kept by past imports back. These files are unreachable
// from the console otherwise, so this is the only way out of an import that turned
// out to be wrong.
void MainApplication::RestoreBackup() {
    // Offer only the slots that hold something restorable: slot 1 is empty until
    // a second import has happened, and an interrupted rotation can leave gaps.
    int slot[SOURCES_BAK_SLOTS], scon[SOURCES_BAK_SLOTS], srep[SOURCES_BAK_SLOTS];
    std::vector<std::string> opts;
    int n = 0;
    for (int i = 0; i < SOURCES_BAK_SLOTS; i++) {
        int c = 0, r = 0;
        if (!config_backup_info(i, &c, &r)) {
            continue;
        }
        char lbl[96];
        snprintf(lbl, sizeof(lbl), tr(i == 0 ? S_RESTORE_RECENT : S_RESTORE_OLDER),
                 c, r);
        opts.push_back(lbl);
        slot[n] = i;
        scon[n] = c;
        srep[n] = r;
        n++;
    }
    if (n == 0) {
        this->ToastErr(tr(S_RESTORE_NONE));
        return;
    }

    int pick = 0;
    if (n > 1) {
        opts.push_back(tr(S_CANCEL));
        // Last option as cancel, so it and B both come back as -1.
        int r = this->CreateShowDialog(tr(S_RESTORE_COLLECTION),
                                       tr(S_RESTORE_PICK), opts, true, {},
                                       style_dialog);
        if (r < 0 || r >= n) {
            return;
        }
        pick = r;
    }

    int consoles = scon[pick], repos = srep[pick];
    char msg[256];
    snprintf(msg, sizeof(msg), tr(S_RESTORE_CONFIRM), consoles, repos);
    if (!this->ConfirmDanger(tr(S_RESTORE_COLLECTION), msg)) {
        xfer_log("cancelled  restore of %d console(s), %d repo(s) from slot %d",
                 consoles, repos, slot[pick]);
        return;
    }
    if (!config_restore_backup(&g_cfg, slot[pick], &consoles, &repos)) {
        xfer_log("FAILED     restore could not be written to %s", SOURCES_PATH);
        this->ToastErr(tr(S_IMPORT_SAVE_FAIL));
        return;
    }
    xfer_log("restore    applied %d console(s), %d repo(s) from backup slot %d",
             consoles, repos, slot[pick]);
    config_seed_rom_folders(&g_cfg, roms_root(&g_tico));

    char done[96];
    snprintf(done, sizeof(done), tr(S_RESTORE_DONE), consoles, repos);
    this->Toast(done);
    this->GotoManageData();
}

// ---- bulk metadata refresh (Manage data -> Refresh all metadata) ----------
static std::vector<std::string> g_ra_ids;
static std::atomic<int> g_ra_next{0}; // next id index a worker claims

// One refresh worker: its own curl connection, pulls ids off g_ra_next until
// they run out (or the user cancels). Several run at once so archive.org's slow
// per-item latency (mostly the TLS handshake + server response) overlaps
// instead of stacking up one repo at a time. ra_ok/ra_fail/ra_idx are atomics,
// safe to bump from any worker; g_ra_ids is read-only for the run's duration.
void MainApplication::RaWorker(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    void *conn = net_conn_new();
    for (;;) {
        if (self->ra_cancel) {
            break;
        }
        int i = g_ra_next.fetch_add(1);
        if (i >= (int)g_ra_ids.size()) {
            break;
        }
        ArchiveItem item;
        // use_cache=false forces a refetch; a successful parse replaces the
        // cache file (bad responses never overwrite a good cache).
        if (ia_fetch_on(conn, g_ra_ids[i].c_str(), &item, false, CACHE_DIR)) {
            ia_free(&item);
            self->ra_ok.fetch_add(1);
        } else {
            self->ra_fail.fetch_add(1);
        }
        // Atomic RMW: several workers bump these concurrently. `x = x + 1` on an
        // atomic is a separate load and store, so it would drop counts here.
        self->ra_idx.fetch_add(1); // completed count, for the readout
    }
    net_conn_free(conn);
}

void MainApplication::RaThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    // Coordinator: fan out to a few workers, then reap them. Runs on the `ra`
    // task thread, so RaTick's done/Join handling stays exactly as before.
    static const int RA_WORKERS = 3;
    Thread th[RA_WORKERS];
    int n = 0;
    for (int i = 0; i < RA_WORKERS; i++) {
        if (R_FAILED(threadCreate(&th[i], &MainApplication::RaWorker, self, NULL,
                                  0x40000, 0x2C, -2)) ||
            R_FAILED(threadStart(&th[i]))) {
            break;
        }
        n++;
    }
    if (n == 0) {
        // Couldn't spawn any worker: still run the refresh inline on this thread.
        MainApplication::RaWorker(self);
    } else {
        for (int i = 0; i < n; i++) {
            threadWaitForExit(&th[i]);
            threadClose(&th[i]);
        }
    }
    self->ra.done = true;
}

void MainApplication::RaStart() {
    // Every enabled repo, deduplicated (the same archive.org item may back
    // repos on several consoles — one fetch covers them all).
    g_ra_ids.clear();
    for (int c = 0; c < g_cfg.console_count; c++) {
        for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
            Repo *rp = &g_cfg.consoles[c].repos[r];
            if (rp->enabled && rp->id[0]) {
                g_ra_ids.push_back(rp->id);
            }
        }
    }
    std::sort(g_ra_ids.begin(), g_ra_ids.end());
    g_ra_ids.erase(std::unique(g_ra_ids.begin(), g_ra_ids.end()),
                   g_ra_ids.end());
    if (g_ra_ids.empty()) {
        this->ToastErr(tr(S_NO_REPOS));
        return;
    }

    this->ra_cancel = false;
    this->ra_idx = 0;
    this->ra_total = (int)g_ra_ids.size();
    this->ra_ok = 0;
    this->ra_fail = 0;
    g_ra_next = 0;

    if (!this->ra.Start(&MainApplication::RaThread, this)) {
        this->ToastErr(tr(S_META_FAILED));
        return;
    }
    this->layout->SetTitle(tr(S_REFRESH_ALL));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_REFRESH_ALL));
}

void MainApplication::RaTick() {
    if (!this->ra.done) {
        // Several repos are in flight at once now, so show completed/total
        // rather than a single "current" id.
        int done_n = (int)this->ra_idx;
        if (done_n > (int)this->ra_total) done_n = (int)this->ra_total;
        char s[160];
        snprintf(s, sizeof(s), "(%d/%d)   B %s", done_n, (int)this->ra_total,
                 tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    this->ra.Join();
    char t[96];
    snprintf(t, sizeof(t), tr(S_REFRESH_DONE), (int)this->ra_ok,
             (int)this->ra_fail);
    if (this->ra_fail > 0) {
        this->ToastErr(t);
    } else {
        this->Toast(t);
    }
    this->GotoManageData();
}

void MainApplication::GotoViewLogs() {
    this->screen = Screen::ViewLogs;
    this->layout->SetTitle(tr(S_TITLE_VIEW_LOGS));
    this->layout->SetSubtitle(tr(S_SUB_VIEW_LOGS));
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_VIEW_LOG));      // 0: download history
    this->layout->AddRow(tr(S_DEBUG_LOG));     // 1: debug.log
    this->layout->AddRow(tr(S_QUEUE_STATE));   // 2: persisted queue.json
    this->layout->AddRow(tr(S_XFER_LOG));      // 3: transfers.log
    this->layout->AddRow(tr(S_RELEASE_NOTES)); // 4: GitHub release history
}

// Rows truncate long log lines; pressing A shows the full text in a dialog.
// The dialog doesn't auto-wrap, so break at UTF-8 boundaries (preferring
// spaces) every ~64 characters.
static std::string wrap_for_dialog(const std::string &s) {
    const size_t maxc = 64;
    std::string out;
    size_t col = 0, last_sp = std::string::npos; // out-index of last space
    for (size_t i = 0; i < s.size();) {
        size_t cl = 1;
        while (i + cl < s.size() && ((u8)s[i + cl] & 0xC0) == 0x80) {
            cl++;
        }
        if (s[i] == '\n') {
            col = 0;
            last_sp = std::string::npos;
        } else if (col >= maxc) {
            if (last_sp != std::string::npos) {
                out[last_sp] = '\n'; // break at the last space instead
                col = out.size() - last_sp - 1;
            } else {
                out += '\n';
                col = 0;
            }
            last_sp = std::string::npos;
        }
        if (s[i] == ' ') {
            last_sp = out.size();
        }
        out.append(s, i, cl);
        col++;
        i += cl;
    }
    return out;
}

// Lines shown in the debug-log viewer (newest first), kept for the detail
// dialog on A.
static std::vector<std::string> g_debug_lines;

void MainApplication::GotoDebugLog() {
    this->GotoTextLog(LOG_PATH, tr(S_TITLE_DEBUG_LOG), S_CLEAR_DEBUG_CONFIRM);
}

void MainApplication::GotoXferLog() {
    this->GotoTextLog(XFERLOG_PATH, tr(S_TITLE_XFER_LOG),
                      S_CLEAR_XFER_CONFIRM);
}

// Plain-text log viewer, shared by the debug and transfer logs: same rows, same
// A-to-expand, same X-to-clear — only the file and its labels differ.
// Takes its strings by value: reloading passes the members back in, so they
// must be copied before being assigned over.
void MainApplication::GotoTextLog(std::string path, std::string title,
                                  int clear_msg) {
    this->screen = Screen::DebugLog;
    this->log_view_path = path;
    this->log_view_title = title;
    this->log_clear_msg = clear_msg;
    this->layout->SetTitle(title);
    this->layout->SetSubtitle(tr(S_SUB_DEBUG_LOG));
    this->layout->ClearMenu();
    // Newest first, capped so a huge log doesn't stall the UI.
    std::ifstream f(path);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(f, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    const int max_lines = 500;
    int shown = 0;
    g_debug_lines.clear();
    for (int i = (int)lines.size() - 1; i >= 0 && shown < max_lines; i--) {
        this->layout->AddRow(lines[i]);
        g_debug_lines.push_back(lines[i]);
        shown++;
    }
    if (shown == 0) {
        this->layout->AddRow(tr(S_NO_LOG));
    } else if ((int)lines.size() > shown) {
        char info[64];
        snprintf(info, sizeof(info), "%d / %d", shown, (int)lines.size());
        this->layout->SetRomInfo(info);
    }
}

// Persisted queue data (queue.json) shown in the viewer, one detail string
// per row for the A dialog.
static std::vector<std::string> g_qstate_details;

void MainApplication::GotoQueueState() {
    this->screen = Screen::QueueState;
    this->layout->SetTitle(tr(S_TITLE_QUEUE_STATE));
    this->layout->SetSubtitle(tr(S_SUB_QUEUE_STATE));
    this->layout->ClearMenu();
    g_qstate_details.clear();
    size_t len = 0;
    char *body = json_read_file(QUEUE_STATE_PATH, &len);
    if (body) {
        int ntok = 0;
        jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
        int arr = (tok && tok[0].type == JSMN_OBJECT)
                      ? json_obj_get(body, tok, 0, "items")
                      : -1;
        if (arr >= 0 && tok[arr].type == JSMN_ARRAY) {
            int child = arr + 1;
            for (int i = 0; i < tok[arr].size; i++) {
                if (tok[child].type == JSMN_OBJECT) {
                    char name[520] = "", target[64] = "", url[1200] = "";
                    json_copy(body, tok,
                              json_obj_get(body, tok, child, "name"), name,
                              sizeof(name));
                    json_copy(body, tok,
                              json_obj_get(body, tok, child, "target"),
                              target, sizeof(target));
                    json_copy(body, tok,
                              json_obj_get(body, tok, child, "url"), url,
                              sizeof(url));
                    uint64_t size = json_u64(
                        body, tok, json_obj_get(body, tok, child, "size"));
                    bool downloaded = json_bool(
                        body, tok,
                        json_obj_get(body, tok, child, "downloaded"));
                    char left[600];
                    snprintf(left, sizeof(left), "[%s] %s", target, name);
                    this->layout->AddRow2(left, human_size(size),
                                          g_theme->row_text,
                                          size_color(size), -1.0f,
                                          console_icon(target),
                                          downloaded ? "wait-unz" : "wait");
                    std::string d = std::string(name) + "\n[" + target +
                                    "]  " + human_size(size) +
                                    (downloaded ? "  ·  wait-unz" : "") +
                                    "\n" + url;
                    g_qstate_details.push_back(d);
                }
                child = json_tok_skip(tok, child);
            }
        }
        free(tok);
        free(body);
    }
    if (g_qstate_details.empty()) {
        this->layout->SetEmptyState(console_icon("default"),
                                    tr(S_QUEUE_EMPTY));
    }
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
// Set by the UI thread when B is pressed during a search; polled by the scan
// loops (cache + installed) so a long walk bails out promptly. One flag is
// enough since only one search runs at a time.
static std::atomic<bool> g_search_cancel{false};

// Result-cap state shared between the scan and the finalize step. Capping is
// decided during the scan (off the main thread); FinishSearch reads it.
static bool g_search_capped = false;

// The heavy part of a search: walk the metadata cache on disk, parse each file
// and collect matching entries into g_search_results. Touches no UI, so it is
// safe to run on a background thread.
static void run_search_scan(const std::string &query, int scope_ci,
                            int scope_ri) {
    g_search_results.clear();
    g_search_capped = false;

    // Map repo id -> target console folder for download context, limited to the
    // requested scope (a single console, or a single repo within it).
    struct RepoRef { std::string id; std::string target; std::string base; };
    std::vector<RepoRef> repos;
    for (int c = 0; c < g_cfg.console_count; c++) {
        if (scope_ci >= 0 && c != scope_ci) continue;
        for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
            if (scope_ci >= 0 && scope_ri >= 0 && r != scope_ri) continue;
            Repo *rp = &g_cfg.consoles[c].repos[r];
            if (!rp->enabled || !rp->id[0]) continue;
            repos.push_back({rp->id, g_cfg.consoles[c].target,
                             rp->download_base});
        }
    }

    // Scan all cached metadata files. Results are capped; keep scanning just
    // far enough past the cap to know it was hit, then tell the user.
    const int max_results = 200;
    bool capped = false;
    DIR *d = opendir(CACHE_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && !capped && !g_search_cancel) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".json") != 0) continue;

            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", CACHE_DIR, e->d_name);
            size_t len = 0;
            char *body = json_read_file(path, &len);
            if (!body) continue;

            // Cheap reject before the costly jsmn parse: rom file names appear
            // verbatim (ASCII) in the raw metadata, so if the query isn't
            // anywhere in the file text it can't match any name. This skips
            // tokenizing the many caches that hold no match — the dominant cost
            // when walking a large cache.
            if (!ci_contains(body, query.c_str())) { free(body); continue; }

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
                for (int i = 0; i < n && !capped; i++) {
                    if (tok[ch].type == JSMN_OBJECT) {
                        char fname[512];
                        json_copy(body, tok,
                                  json_obj_get(body, tok, ch, "name"),
                                  fname, sizeof(fname));
                        if (fname[0] && ci_contains(fname, query.c_str())) {
                            if ((int)g_search_results.size() >= max_results) {
                                capped = true; // one more match proves it
                                break;
                            }
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
    g_search_capped = capped;
}

void MainApplication::GotoSearch(const std::string &query, int scope_ci,
                                 int scope_ri) {
    // A previous scan may still be unwinding after a B-cancel: it keeps running
    // until it notices the cancel flag, and wasn't joined then (it was still
    // alive). Reap it before touching the shared query/result globals, or the
    // old worker and this one race on g_search_results (concurrent vector writes
    // corrupt the heap) and Start() would clobber its live Thread handle.
    if (this->search.running) {
        g_search_cancel = true;
        this->search.Join();
    }
    this->screen = Screen::Search;
    this->search_ci = scope_ci;
    this->search_ri = scope_ri;
    g_search_query = query;
    g_search_results.clear();
    this->layout->SetTitle(tr(S_TITLE_SEARCH));
    this->layout->ClearMenu();
    this->layout->SetRomInfo("");
    // Footer tells the user the scan is interruptible (the spinner overlay
    // already shows "Searching…"); B cancels it (see HandleInput).
    this->layout->SetSubtitle(tr(S_SUB_SEARCHING));

    // Run the cache scan on a background thread so the "Searching..." spinner
    // animates instead of the whole UI freezing while a large metadata cache
    // is walked and parsed. B during the scan cancels it (see HandleInput).
    g_search_cancel = false;
    this->search_discard = false;
    this->layout->ShowSpinner(tr(S_SEARCHING));
    if (this->search.Start(&MainApplication::SearchThread, this)) {
        return;
    }
    // Couldn't spawn a thread: fall back to a synchronous scan.
    this->layout->HideSpinner();
    run_search_scan(query, scope_ci, scope_ri);
    this->FinishSearch();
}

void MainApplication::SearchThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    run_search_scan(g_search_query, self->search_ci, self->search_ri);
    self->search.done = true;
}

void MainApplication::SearchTick() {
    if (!this->search.done) {
        return; // the spinner overlay animates itself
    }
    this->layout->HideSpinner();
    this->search.Join();
    this->FinishSearch();
}

// Build the result list on the main thread once the background scan finishes
// (Plutonium UI calls must not run off-thread).
void MainApplication::FinishSearch() {
    this->layout->ClearMenu();
    for (const auto &h : g_search_results) {
        // "* " marks a file already present in its console folder, so you can
        // avoid re-downloading (same cue the file list uses).
        bool inst = file_installed(h.target.c_str(), h.name.c_str());
        std::string label = std::string(inst ? "* " : "") + "[" + h.target +
                            "] " + h.name;
        this->layout->AddRow2(label, human_size(h.size),
                              g_theme->row_text, size_color(h.size), -1.0f,
                              console_icon(h.target.c_str()));
    }
    if (g_search_results.empty()) {
        this->layout->SetEmptyState(console_icon("default"),
                                    tr(S_SEARCH_NO_RESULTS));
    } else {
        char info[128];
        if (g_search_capped) {
            snprintf(info, sizeof(info), tr(S_SEARCH_CAPPED),
                     (int)g_search_results.size());
        } else {
            snprintf(info, sizeof(info), tr(S_SEARCH_N_RESULTS),
                     (int)g_search_results.size());
        }
        this->layout->SetRomInfo(info);
    }
    this->layout->SetSubtitle(tr(S_SUB_SEARCH));
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
            accent_green(),
            -1.0f, nullptr, "", false, false);
    }
}

// Colour of the Shown/Hidden state label on the Manage consoles rows. Shared
// by the initial build and the in-place toggle so the two never drift.
static pu::ui::Color manage_state_color(bool shown) {
    if (shown) {
        return accent_green();
    }
    return is_light_theme() ? pu::ui::Color(95, 95, 105, 255)
                            : pu::ui::Color(150, 150, 162, 255);
}

void MainApplication::GotoManage() {
    this->screen = Screen::Manage;
    this->layout->SetTitle(tr(S_TITLE_MANAGE));
    this->layout->SetSubtitle(tr(S_SUB_MANAGE));
    this->layout->ClearMenu();
    for (int i = 0; i < g_cfg.console_count; i++) {
        bool sh = g_cfg.consoles[i].shown;
        char clabel[160];
        console_label(g_cfg.consoles[i].console, clabel, sizeof(clabel));
        this->layout->AddRow2(
            clabel, sh ? tr(S_SHOWN) : tr(S_HIDDEN),
            g_theme->row_text, manage_state_color(sh),
            -1.0f, console_icon(g_cfg.consoles[i].console));
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

// Display name used for Installed sorting: root console folders sort by their
// full name (matching what's shown), everything else by its raw name.
static const char *inst_disp_name(const DirEnt &d, bool is_root) {
    if (is_root && d.is_dir) {
        const char *f = console_full_name(d.name.c_str());
        if (f) return f;
    }
    return d.name.c_str();
}

void MainApplication::GotoInstalled(const std::string &path) {
    this->screen = Screen::Installed;
    this->inst_path = path;
    inst_stat_load(); // warm the folder-size cache from disk (once per session)
    g_inst = list_dir(path);
    bool is_root = (path == roms_root(&g_tico));
    // Folders stay grouped above files, and pinned folders stay on top at the
    // root; the chosen sort orders within those groups (size applies to files).
    std::sort(g_inst.begin(), g_inst.end(), [is_root](const DirEnt &a, const DirEnt &b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        if (is_root && a.is_dir && b.is_dir) {
            bool pa = prefs_dir_pinned(&g_prefs, a.name.c_str());
            bool pb = prefs_dir_pinned(&g_prefs, b.name.c_str());
            if (pa != pb) return pa > pb;
        }
        if (!a.is_dir && !b.is_dir &&
            (g_inst_sort == SORT_SIZE_DESC || g_inst_sort == SORT_SIZE_ASC) &&
            a.size != b.size) {
            return g_inst_sort == SORT_SIZE_DESC ? a.size > b.size
                                                 : a.size < b.size;
        }
        int c = strcasecmp(inst_disp_name(a, is_root),
                           inst_disp_name(b, is_root));
        return g_inst_sort == SORT_NAME_ZA ? c > 0 : c < 0;
    });
    // Header: "<icon> Console Name › Installed roms" under a console folder,
    // or just "Installed roms" at the root (no filesystem path shown).
    std::string cons;
    if (!is_root) {
        std::string root = roms_root(&g_tico);
        if (path.rfind(root, 0) == 0) {
            std::string rel = path.substr(root.size());
            while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
            cons = rel.substr(0, rel.find('/'));
        }
    }
    if (cons.empty()) {
        this->layout->SetTitle(tr(S_TITLE_INSTALLED));
    } else {
        const char *cfull = console_full_name(cons.c_str());
        this->layout->SetTitle(std::string(cfull ? cfull : cons.c_str()) +
                               " > " + tr(S_TITLE_INSTALLED));
        this->layout->SetTitleIcon(console_icon(cons.c_str()));
    }
    // Card view applies to the roms root only (console folders); inside a
    // folder the file table remains the right tool.
    bool cards = g_prefs.card_view && is_root && !g_inst.empty();
    this->layout->SetSubtitle(cards      ? tr(S_SUB_INSTALLED_CARDS)
                              : is_root   ? tr(S_SUB_INSTALLED)
                                          : tr(S_SUB_INSTALLED_FOLDER));
    this->layout->ClearMenu();
    for (int i = 0; i < (int)g_inst.size(); i++) {
        DirEnt &e = g_inst[i];
        if (e.is_dir) {
            int n = 0;
            uint64_t bytes = 0;
            inst_dir_stats(path + "/" + e.name, &n, &bytes);
            char cnt[32];
            snprintf(cnt, sizeof(cnt), tr(S_N_APPS), n);
            // Chip text with the folder's total size: cards lead with the
            // size ("1.2 GB · 12 apps"), rows append it ("12 apps · 1.2 GB"),
            // both dot-joined like the Browse tab's chips.
            char card_sub[64], row_sub[64];
            if (bytes > 0) {
                snprintf(card_sub, sizeof(card_sub), "%s · %s",
                         human_size(bytes).c_str(), cnt);
                snprintf(row_sub, sizeof(row_sub), "%s · %s", cnt,
                         human_size(bytes).c_str());
            } else {
                snprintf(card_sub, sizeof(card_sub), "%s", cnt);
                snprintf(row_sub, sizeof(row_sub), "%s", cnt);
            }
            std::string label;
            bool pinned = path == roms_root(&g_tico) &&
                          prefs_dir_pinned(&g_prefs, e.name.c_str());
            const char *full = (path == roms_root(&g_tico))
                                   ? console_full_name(e.name.c_str())
                                   : nullptr;
            pu::sdl2::Texture ic = (path == roms_root(&g_tico))
                                       ? console_icon(e.name.c_str())
                                       : nullptr;
            if (cards) {
                // Card: full name title (wrappable) + size/app count beneath.
                this->layout->AddCard(full ? full : e.name.c_str(), card_sub,
                                      ic, pinned);
                continue;
            }
            if (full) {
                // Match the Browse list: "Full Name (NES)", uppercased abbr.
                char clbl[160];
                console_label(e.name.c_str(), clbl, sizeof(clbl));
                label = clbl;
            } else {
                label += e.name;
            }
            {
                this->layout->AddRow2(label, row_sub, g_theme->row_text,
                                      count_color(), -1.0f, ic, "", false,
                                      true, pinned);
            }
        } else if (cards) {
            // Stray file at the roms root: still a card so indices match.
            this->layout->AddCard(e.name, human_size(e.size),
                                  console_icon(e.name.c_str()));
        } else {
            // File: right column is the plain size, tinted by magnitude. The
            // "Size:" label lives on the open dialog, not in the dense list.
            this->layout->AddRow2(e.name, human_size(e.size),
                                  g_theme->row_text,
                                  size_color(e.size));
        }
    }
    if (g_inst.empty()) {
        this->layout->SetEmptyState(console_icon("default"), tr(S_EMPTY),
                                    tr(S_INSTALLED_EMPTY_HINT));
    } else if (cards) {
        this->layout->SetCardsMode(true);
    }
    if (g_inst_sort != SORT_DEFAULT) {
        this->layout->SetRomInfo(tr(g_sort_keys[g_inst_sort]));
    }
    inst_stat_save(); // persist any folder sizes (re)computed this visit
}

// ---- installed search (recursive scan of the roms folder) -----------------
struct InstHit {
    std::string name; // file name
    std::string dir;  // absolute folder holding it
    std::string console; // top-level roms subfolder (for the [tag])
    uint64_t size;
};
static std::vector<InstHit> g_inst_hits;
static std::string g_inst_query;
static const int INST_SEARCH_MAX = 300;

// Collect files under `dir` whose name matches `query`. Bounded by depth and
// the result cap so a huge library can't stall the UI.
static void inst_search_walk(const std::string &dir, const std::string &console,
                             const std::string &query, int depth) {
    if (depth > 8 || (int)g_inst_hits.size() >= INST_SEARCH_MAX ||
        g_search_cancel) {
        return;
    }
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && !g_search_cancel &&
           (int)g_inst_hits.size() < INST_SEARCH_MAX) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        std::string full = dir + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            inst_search_walk(full, console, query, depth + 1);
        } else if (ci_contains(e->d_name, query.c_str())) {
            g_inst_hits.push_back(
                {e->d_name, dir, console, (uint64_t)st.st_size});
        }
    }
    closedir(d);
}

// The heavy part: walk the installed ROM folders on disk, filling g_inst_hits.
// Touches no UI, so it runs on a background thread. `base` scopes the search:
// the ROM root spans every console, a console folder stays within that console.
static std::string g_isearch_base; // folder the current scan is rooted at
static void run_inst_search(const std::string &base, const std::string &query) {
    g_inst_hits.clear();
    std::string roots = roms_root(&g_tico);
    if (base == roots) {
        DIR *d = opendir(roots.c_str());
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL && !g_search_cancel) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
                    continue;
                }
                std::string full = roots + "/" + e->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) != 0) {
                    continue;
                }
                if (S_ISDIR(st.st_mode)) {
                    inst_search_walk(full, e->d_name, query, 0);
                } else if (ci_contains(e->d_name, query.c_str())) {
                    g_inst_hits.push_back(
                        {e->d_name, roots, "", (uint64_t)st.st_size});
                }
            }
            closedir(d);
        }
    } else {
        // The first path segment below the root is the console (for the icon).
        std::string console = base.substr(roots.size());
        while (!console.empty() && console[0] == '/') console.erase(0, 1);
        size_t slash = console.find('/');
        if (slash != std::string::npos) console = console.substr(0, slash);
        inst_search_walk(base, console, query, 0);
    }
}

void MainApplication::InstSearchThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    run_inst_search(g_isearch_base, g_inst_query);
    self->isearch.done = true;
}

void MainApplication::GotoInstSearch(const std::string &query) {
    // Reap a prior scan still unwinding after a B-cancel before touching the
    // shared globals, so the old worker can't race this one on g_inst_hits (see
    // GotoSearch).
    if (this->isearch.running) {
        g_search_cancel = true;
        this->isearch.Join();
    }
    this->screen = Screen::InstSearch;
    g_inst_query = query;
    g_inst_hits.clear();
    g_isearch_base = this->inst_path;
    this->layout->SetTitle(tr(S_TITLE_INST_SEARCH));
    this->layout->SetSubtitle(tr(S_SUB_SEARCHING)); // "B cancel" while scanning
    this->layout->ClearMenu();

    // Scan off the main thread so a big ROM folder shows the "Searching..."
    // spinner instead of freezing; B cancels it (see HandleInput).
    g_search_cancel = false;
    this->isearch_discard = false;
    this->layout->ShowSpinner(tr(S_SEARCHING));
    if (this->isearch.Start(&MainApplication::InstSearchThread, this)) {
        return;
    }
    // Couldn't spawn a thread: fall back to a synchronous scan.
    this->layout->HideSpinner();
    run_inst_search(g_isearch_base, query);
    this->FinishInstSearch();
}

void MainApplication::ISearchTick() {
    if (!this->isearch.done) {
        return; // the spinner overlay animates itself
    }
    this->layout->HideSpinner();
    this->isearch.Join();
    this->FinishInstSearch();
}

// Build the result list on the main thread once the scan finishes (Plutonium UI
// calls must not run off-thread).
void MainApplication::FinishInstSearch() {
    this->layout->SetSubtitle(tr(S_SUB_INST_SEARCH));
    this->layout->ClearMenu();
    std::sort(g_inst_hits.begin(), g_inst_hits.end(),
              [](const InstHit &a, const InstHit &b) {
                  return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
              });
    bool capped = (int)g_inst_hits.size() >= INST_SEARCH_MAX;
    for (const auto &h : g_inst_hits) {
        std::string label =
            h.console.empty() ? h.name : "[" + h.console + "] " + h.name;
        this->layout->AddRow2(label, human_size(h.size), g_theme->row_text,
                              size_color(h.size), -1.0f,
                              console_icon(h.console.c_str()));
    }
    if (g_inst_hits.empty()) {
        this->layout->SetEmptyState(console_icon("default"),
                                    tr(S_SEARCH_NO_RESULTS));
    } else {
        char info[64];
        if (capped) {
            snprintf(info, sizeof(info), tr(S_SEARCH_CAPPED), INST_SEARCH_MAX);
        } else {
            snprintf(info, sizeof(info), tr(S_SEARCH_N_RESULTS),
                     (int)g_inst_hits.size());
        }
        this->layout->SetRomInfo(info);
    }
}

// ---- move installed file(s) up into the parent folder ---------------------

// Worker body: rename each queued file from mv_from into mv_to. Touches no UI,
// so it runs on a background thread. A destination that already exists is left
// alone (the parent's "proper" copy is never clobbered) and counted as skipped.
void MainApplication::MvThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    for (const auto &name : self->mv_names) {
        std::string from = self->mv_from + "/" + name;
        std::string to = self->mv_to + "/" + name;
        if (fs_exists(to.c_str())) {
            self->mv_fail.fetch_add(1); // don't overwrite an existing file
        } else if (fs_move(from.c_str(), to.c_str())) {
            self->mv_ok.fetch_add(1);
        } else {
            self->mv_fail.fetch_add(1);
        }
        self->mv_idx.fetch_add(1);
    }
    self->mv.done = true;
}

void MainApplication::MvStart(const std::vector<std::string> &names) {
    if (names.empty()) {
        return;
    }
    this->mv_from = this->inst_path;
    auto p = this->inst_path.find_last_of('/');
    this->mv_to = (p == std::string::npos) ? this->inst_path
                                           : this->inst_path.substr(0, p);
    this->mv_names = names;
    this->mv_idx = 0;
    this->mv_total = (int)names.size();
    this->mv_ok = 0;
    this->mv_fail = 0;
    this->layout->ShowSpinner(tr(S_MOVING));
    if (this->mv.Start(&MainApplication::MvThread, this)) {
        return; // MvTick drives the progress readout and the finish
    }
    // No worker thread available: move inline so the operation still happens,
    // then finish immediately.
    MvThread(this);
    this->layout->HideSpinner();
    this->MvFinish();
}

void MainApplication::MvTick() {
    if (!this->mv.done) {
        int n = (int)this->mv_idx;
        if (n > (int)this->mv_total) {
            n = (int)this->mv_total;
        }
        char s[96];
        snprintf(s, sizeof(s), tr(S_MOVING_N), n, (int)this->mv_total);
        this->layout->SetSubtitle(s);
        return;
    }
    this->mv.Join();
    this->layout->HideSpinner();
    this->MvFinish();
}

void MainApplication::MvFinish() {
    int ok = (int)this->mv_ok, fail = (int)this->mv_fail;
    if (fail > 0) {
        char t[96];
        snprintf(t, sizeof(t), tr(S_MOVE_PARTIAL), ok, fail);
        this->ToastErr(t);
    } else {
        char t[64];
        snprintf(t, sizeof(t), tr(S_MOVED_N), ok);
        this->Toast(t);
    }
    std::string folder = this->mv_from;
    // Empty-folder cleanup: if the wrapper folder now holds nothing at all (the
    // common unzip-leftover case), offer to remove it. Deletion is gated on the
    // folder being genuinely empty — list_dir() returning nothing, re-checked
    // right before the delete — so this can never sweep away remaining files.
    if (ok > 0 && folder != roms_root(&g_tico) && list_dir(folder).empty()) {
        // Land in the parent regardless of the delete choice: every file has
        // left this folder, so re-listing the empty folder would strand the
        // user on the "(empty)" view. The confirm dialog then sits over the
        // parent listing they moved the files into.
        this->GotoInstalled(this->mv_to);
        std::string base = folder.substr(folder.find_last_of('/') + 1);
        char msg[300];
        snprintf(msg, sizeof(msg), tr(S_EMPTY_FOLDER_DELETE), base.c_str());
        if (this->Confirm(tr(S_DELETE), msg) && list_dir(folder).empty() &&
            fs_rm_rf(folder.c_str())) {
            this->Toast(tr(S_FOLDER_DELETED));
            this->GotoInstalled(this->mv_to); // re-list so the folder drops off
        }
    } else {
        // Partial move (some files remained): re-list the folder we moved out
        // of so the moved files drop off it.
        this->GotoInstalled(folder);
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
    this->layout->AddRow(r);                     // 3
    this->layout->AddRow(tr(S_REFRESH_META));    // 4
    this->layout->AddRow(tr(S_DELETE_REPO));     // 5
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
                              count_color(), -1.0f, console_icon(name.c_str()));
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
void MainApplication::HandleInput(u64 down, u64 held,
                                  const pu::ui::TouchPoint &touch) {
    // One-shot startup dialogs, deferred from OnLoad so they render over a
    // live frame instead of a black screen.
    if (this->startup_checks) {
        this->startup_checks = false;
        // Bake the rounded tiles now (renderer is live) so the first list/card
        // screen doesn't pay the one-time bake as a visible load hitch.
        this->layout->PrewarmTiles();
        // (Removed the "TICO not detected" prompt: the app owns its ROM library
        // at sdmc:/roms and no longer depends on the TICO emulator being present.)
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
        // Silent update check, gated on its own advanced pref and on the
        // network actually being up (no dialog, no blocking — result only
        // lights the Settings-tab dot + chip; the user still acts manually).
        if (g_prefs.chk_updates) {
            NifmInternetConnectionType ntype = (NifmInternetConnectionType)0;
            u32 wstr = 0;
            NifmInternetConnectionStatus nst = (NifmInternetConnectionStatus)0;
            if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&ntype, &wstr,
                                                            &nst)) &&
                nst == NifmInternetConnectionStatus_Connected) {
                this->BgChkStart();
            }
        }
        // Last of the three: it offers a Wi-Fi transfer, so it must not come
        // before the network warning has had its say.
        if (g_cfg.console_count == 0) {
            this->Welcome();
        }
        return;
    }
    // Reap the silent startup update check (if any) and light the Settings dot.
    this->BgChkPoll();

    // A self-update download owns the UI while it runs: drive its progress /
    // finish and swallow all other input until it completes.
    if (this->upd.running) {
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
    if (this->chk.running) {
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
        if (this->chk.done) {
            this->chk.Join();
        }
    }

    // A bulk metadata refresh owns the UI while it runs: show (n/total)
    // progress; B cancels after the in-flight repo finishes.
    if (this->ra.running) {
        if (down & HidNpadButton_B) {
            this->ra_cancel = true;
        }
        this->RaTick();
        return;
    }

    // A repo's metadata is loading on a background thread: animate the indicator
    // and swallow input until it's ready. B cancels — the fetch can't be aborted
    // mid-request, so it finishes in the background and its result is discarded.
    if (this->meta.running) {
        (void)held;
        if (!this->meta_discard) {
            if (down & HidNpadButton_B) {
                this->meta_discard = true;
                this->layout->HideSpinner();
                if (g_files_manual || !g_prefs.group_consoles)
                    this->GotoHome();
                else
                    this->GotoRepos(this->sel_ci);
                return;
            }
            this->MetaTick();
            return;
        }
        // Dismissed: reap the fetch when it finishes and drop its result; input
        // flows to the screen we returned to in the meantime.
        if (this->meta.done) {
            this->meta.Join();
            ia_free(&g_item);
            g_have_item = false;
            this->meta_discard = false;
        }
    }

    // The search cache scan is running on a background thread: animate the
    // spinner and swallow input until the result list is ready. B cancels the
    // scan and returns to where the search was launched.
    if (this->search.running) {
        (void)held;
        if (!this->search_discard) {
            if (down & HidNpadButton_B) {
                this->search_discard = true;
                g_search_cancel = true;
                this->layout->HideSpinner();
                if (this->search_ci >= 0 && g_prefs.group_consoles)
                    this->GotoRepos(this->search_ci);
                else
                    this->GotoHome();
                return;
            }
            this->SearchTick();
            return;
        }
        if (this->search.done) {
            this->search.Join();
            this->search_discard = false;
        }
    }

    // The Installed-tab search is running on a background thread: same as above,
    // B cancels and returns to the folder the search was launched from.
    if (this->isearch.running) {
        (void)held;
        if (!this->isearch_discard) {
            if (down & HidNpadButton_B) {
                this->isearch_discard = true;
                g_search_cancel = true;
                this->layout->HideSpinner();
                this->GotoInstalled(g_isearch_base);
                return;
            }
            this->ISearchTick();
            return;
        }
        if (this->isearch.done) {
            this->isearch.Join();
            this->isearch_discard = false;
        }
    }

    // A "move to parent" batch owns the UI while it runs: show (n/total)
    // progress and swallow all input. There is intentionally no cancel — each
    // file move is atomic, so letting the batch finish is always safe, whereas
    // stopping halfway would just leave the folder half-moved.
    if (this->mv.running) {
        (void)down;
        (void)held;
        this->MvTick();
        return;
    }

    // Release notes are fetching on a background thread: animate the spinner and
    // swallow input until the history is ready.
    if (this->notes.running) {
        (void)down;
        (void)held;
        this->NotesTick();
        return;
    }

    // Waiting for a dl_sources.json upload: serve the LAN receiver a frame at a
    // time and swallow input except B, which gives up and closes the socket.
    if (this->imp_open) {
        if (down & HidNpadButton_B) {
            this->ImportStop();
            this->ImportReturn();
            return;
        }
        // Leaving via the tab bar also cancels the import: L/R cycle tabs and a
        // tap on the top strip jumps to one. Stop the receiver first so the
        // listening socket closes cleanly, then navigate.
        if (down & (HidNpadButton_L | HidNpadButton_R)) {
            this->ImportStop();
            this->SwitchTab((down & HidNpadButton_R) ? +1 : -1);
            return;
        }
        if (!touch.IsEmpty() && touch.y >= 80 && touch.y < 150) {
            s32 seg = (s32)pu::ui::render::ScreenWidth / 4;
            s32 idx = touch.x / (seg > 0 ? seg : 1);
            if (idx >= 0 && idx < 4) {
                this->ImportStop();
                this->GotoTab((Tab)idx);
                return;
            }
        }
        this->ImportTick();
        return;
    }

    // Touch: tapping a tab in the top strip switches to it, and tapping the
    // already-selected list row acts as an A press (TableList handles row
    // selection and drag-scrolling itself).
    {
        static bool tch_prev = false;
        bool tch_now = !touch.IsEmpty();
        if (tch_now && !tch_prev && touch.y >= 80 && touch.y < 150) {
            s32 seg = (s32)pu::ui::render::ScreenWidth / 4;
            s32 idx = touch.x / (seg > 0 ? seg : 1);
            if (idx >= 0 && idx < 4) {
                this->GotoTab((Tab)idx);
                tch_prev = tch_now;
                return;
            }
        }
        tch_prev = tch_now;
    }
    // Touch: a horizontal swipe across the content area flips tabs (swipe
    // left = next tab), matching the strip above it. The list/grid treat
    // horizontal movement as a drag, so the swipe never taps a row.
    {
        static bool sw_on = false;
        static s32 sw_x0 = 0, sw_y0 = 0, sw_x1 = 0, sw_y1 = 0;
        if (!touch.IsEmpty()) {
            if (!sw_on) {
                sw_on = true;
                sw_x0 = sw_x1 = touch.x;
                sw_y0 = sw_y1 = touch.y;
            } else {
                sw_x1 = touch.x;
                sw_y1 = touch.y;
            }
        } else if (sw_on) {
            sw_on = false;
            s32 dx = sw_x1 - sw_x0, dy = sw_y1 - sw_y0;
            s32 adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            if (sw_y0 >= 158 &&
                sw_y0 < (s32)pu::ui::render::ScreenHeight - 64 &&
                adx >= 140 && ady <= 60 && adx > 2 * ady) {
                this->SwitchTab(dx < 0 ? 1 : -1);
                return;
            }
        }
    }
    if (this->layout->ConsumeTouchActivate()) {
        down |= HidNpadButton_A;
    }

    // Keep the tab bar highlight in sync with whatever screen we're on.
    this->SyncTab();
    // One active-count scan per frame, reused by the toast detector below.
    int qac = queue_active_count();
    // Pulse the Queue tab when downloads are active and you're looking elsewhere.
    this->layout->SetQueueActivity(qac > 0 &&
                                   this->CurrentTab() != Tab::Queue);

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

    // One queue snapshot per frame, shared by the completion-toast detector and
    // the live queue-list refresh below. Each is a ~120KB locked copy; on the
    // Queue screen during downloads the two consumers used to snapshot
    // separately (two per frame). Filled lazily via snap() — taken at most once,
    // and only when a consumer actually needs it this frame.
    static QueueView frame_qv[QUEUE_MAX];
    int frame_qn = 0;
    bool have_qv = false;
    auto snap = [&]() -> int {
        if (!have_qv) {
            frame_qn = queue_snapshot(frame_qv, QUEUE_MAX);
            have_qv = true;
        }
        return frame_qn;
    };

    // Completion toasts: notice downloads reaching a terminal state on any
    // screen, so you know they finished even from another tab. Only do the
    // (locking) snapshot while the queue is active, plus a couple of frames
    // after it drains so the final done/failed transition is still caught.
    {
        static QStatus last[QUEUE_MAX];
        static bool init = false;
        static int idle = 1000;
        // Accumulated over the current active run, for a "queue finished"
        // summary once it drains (per-item toasts overwrite each other when
        // several finish together, so a batch needs one final tally).
        static int done_acc = 0, fail_acc = 0;
        static bool was_active = false;
        idle = qac > 0 ? 0 : (idle < 1000 ? idle + 1 : idle);
        if (idle <= 2) {
            int n = snap();
            QueueView *cqv = frame_qv;
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
                        if (s == Q_FAILED) {
                            fail_acc++;
                        } else {
                            done_acc++;
                        }
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
            // Drain edge: the queue just went idle. Summarize a multi-item run
            // (a lone download already got its own toast above).
            if (was_active && qac == 0) {
                if (done_acc + fail_acc > 1) {
                    char t[80];
                    snprintf(t, sizeof(t), tr(S_TOAST_ALL_DONE), done_acc,
                             fail_acc);
                    if (fail_acc > 0) {
                        this->ToastErr(t);
                    } else {
                        this->Toast(t);
                    }
                }
                done_acc = 0;
                fail_acc = 0;
            }
        }
        was_active = qac > 0;
    }

    // Live-refresh the queue list while it's open.
    if (this->screen == Screen::Queue && g_prefs.card_view) {
        int n = snap();
        QueueView *qv = frame_qv;
        if (n == 0) {
            // ClearMenu only on the emptying transition: it clears the empty
            // state too, so running it every frame would make SetEmptyState
            // re-render its texture at frame rate.
            if (this->layout->InCards() || this->layout->RowCount() != 0) {
                this->layout->ClearMenu();
            }
            this->layout->SetEmptyState(console_icon("default"),
                                        tr(S_QUEUE_EMPTY),
                                        tr(S_QUEUE_EMPTY_HINT));
        } else {
        if (!this->layout->InCards()) {
            this->layout->ClearMenu(); // drop list rows / empty state once
            this->layout->SetCardsMode(true);
        }
        this->layout->SetQueueCount(n);
        // Throttle the volatile %/speed/eta text rasterization to ~7Hz. It
        // changes every frame during a download, and re-rendering it per active
        // card at 60fps is what scaled the queue lag with active count. The
        // ring/progress still advance every frame; only the text is gated.
        static u64 qtxt_last = 0;
        u64 qtxt_now = armGetSystemTick();
        bool qrefresh = (qtxt_last == 0 ||
                         armTicksToNs(qtxt_now - qtxt_last) >= 150000000ULL);
        if (qrefresh) {
            qtxt_last = qtxt_now;
        }
        for (int i = 0; i < n; i++) {
            // Skip off-screen cards entirely: formatting every item's size /
            // speed / status every frame (incl. completed cards nobody can see)
            // is what made a page full of finished downloads drag. They build
            // when scrolled into view (the tick re-runs every frame).
            if (!this->layout->QueueCardVisible(i)) {
                continue;
            }
            const QueueItem *it = &qv[i].item;
            char c0[80] = "", c1[48] = "", c2[48] = "";
            float prog = -1.0f;
            if (it->status == Q_DOWNLOADING && it->total) {
                prog = (float)it->now / (float)it->total;
                snprintf(c0, sizeof(c0), "%s", human_size(it->total).c_str());
                if (it->speed) {
                    uint64_t eta = (it->total > it->now)
                                       ? (it->total - it->now) / it->speed
                                       : 0;
                    snprintf(c1, sizeof(c1), "%s/s",
                             human_size(it->speed).c_str());
                    snprintf(c2, sizeof(c2), "~%s", human_eta(eta).c_str());
                }
            } else if (it->status == Q_PAUSED && it->total) {
                snprintf(c0, sizeof(c0), "%s / %s",
                         human_size(it->now).c_str(),
                         human_size(it->total).c_str());
            } else if (it->status == Q_EXTRACTING) {
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                }
                if (it->ex_files > 0) {
                    snprintf(c0, sizeof(c0), "(%d)", it->ex_files);
                }
            } else if (it->status == Q_FAILED && it->fail_reason[0]) {
                snprintf(c0, sizeof(c0), "%s", it->fail_reason);
            } else if (it->status == Q_DONE || it->status == Q_SAVED) {
                if (it->total) {
                    snprintf(c0, sizeof(c0), "%s",
                             human_size(it->total).c_str());
                }
                if (it->overwrote > 1) {
                    snprintf(c1, sizeof(c1), "(repl %d)", it->overwrote);
                } else if (it->overwrote == 1) {
                    snprintf(c1, sizeof(c1), "(repl)");
                } else {
                    snprintf(c1, sizeof(c1), "(new)");
                }
            } else if (it->total) {
                snprintf(c0, sizeof(c0), "%s", human_size(it->total).c_str());
            }
            pu::ui::Color sc = qstatus_color(it->status);
            if (it->status == Q_DONE || it->status == Q_SAVED) {
                sc = it->overwrote > 0 ? pu::ui::Color(245, 170, 90, 255)
                                       : accent_green();
            }
            // Terminal states keep a full ring: solid green when done,
            // solid red when failed (ring 1/2 in CardGrid).
            int ring = 0;
            if (it->status == Q_DONE || it->status == Q_SAVED) {
                prog = 1.0f;
                ring = 1;
            } else if (it->status == Q_FAILED) {
                prog = 1.0f;
                ring = 2;
            }
            // Waiting cards show their place in line.
            int qpos = it->status == Q_QUEUED ? i + 1 : 0;
            // Status corner shows just the phase word ("Downloading" etc), no
            // percent: the ring already shows progress visually, and dropping
            // the number keeps this label static so it never re-rasterizes
            // during a download (the % changed it every frame per active card).
            char st[48];
            snprintf(st, sizeof(st), "%s", qstatus(it->status));
            // Hero (tint + ring shimmer) covers the actively-worked item:
            // downloading or unzipping.
            this->layout->SetQueueCard(i, it->target,
                                       console_icon(it->target),
                                       st, sc, c0, c1, c2,
                                       it->name, prog,
                                       it->status == Q_DOWNLOADING ||
                                           it->status == Q_EXTRACTING,
                                       ring, qpos, qrefresh);
        }
        // Offline with work pending: cards persist between frames, so also
        // clear the note once the network is back. Online, the slot shows
        // the queue summary instead.
        std::string note;
        if (!g_net_ok) {
            for (int i = 0; i < n; i++) {
                QStatus s = qv[i].item.status;
                if (s == Q_QUEUED || s == Q_PAUSED || s == Q_DOWNLOADING) {
                    note = tr(S_WAITING_NETWORK);
                    break;
                }
            }
        }
        if (note.empty()) {
            note = queue_summary(qv, n);
        }
        this->layout->SetRomInfo(note);
        }
    } else if (this->screen == Screen::Queue) {
        s32 keep = this->layout->Sel();
        int n = snap();
        QueueView *qv = frame_qv;
        this->layout->ClearMenu(false); // rebuilt every frame: no enter fade
        for (int i = 0; i < n; i++) {
            const QueueItem *it = &qv[i].item;
            char info[80] = "";
            float prog = -1.0f; // no bar unless actively downloading
            if (it->status == Q_DOWNLOADING && it->total) {
                prog = (float)it->now / (float)it->total;
                // The bottom bar shows progress; the text shows size (+ speed
                // and ETA), no percent — matching the card view.
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
            } else if (it->status == Q_EXTRACTING) {
                // Bar shows progress from archive bytes consumed; text shows
                // just the count of entries finished so far (no percent). This
                // also keeps the text static between file steps, so it stops
                // re-rasterizing every frame during a long extract.
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                }
                if (it->ex_files > 0) {
                    snprintf(info, sizeof(info), "(%d)", it->ex_files);
                }
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
            // Status becomes the prefix column; the console icon sits between
            // it and the "[target] name" text. The column has a fixed width
            // (TableList), so the icon aligns on every row.
            const char *pfx = qstatus(it->status);
            char left[560];
            snprintf(left, sizeof(left), "[%s] %s", it->target, it->name);
            pu::ui::Color c = qstatus_color(it->status);
            pu::ui::Color rc = c;
            // Colour the result column by outcome: orange = replaced, green = new.
            if (it->status == Q_DONE || it->status == Q_SAVED) {
                pu::ui::Color newc = accent_green();
                rc = it->overwrote > 0 ? pu::ui::Color(245, 170, 90, 255) : newc;
            }
            // Terminal states keep a full bar: solid green when done, red
            // when failed (bar 1/2 in TableList, matching the card ring).
            int bar = 0;
            if (it->status == Q_DONE || it->status == Q_SAVED) {
                prog = 1.0f;
                bar = 1;
            } else if (it->status == Q_FAILED) {
                prog = 1.0f;
                bar = 2;
            }
            // The actively-worked item (downloading or unzipping) is the
            // "hero" row: accent background, thicker bar, shimmer.
            bool accent = (it->status == Q_DOWNLOADING ||
                           it->status == Q_EXTRACTING);
            this->layout->AddRow2(left, info, c, rc, prog,
                                  console_icon(it->target), pfx, accent,
                                  true, false, bar);
        }
        if (n == 0) {
            this->layout->SetEmptyState(console_icon("default"),
                                        tr(S_QUEUE_EMPTY),
                                        tr(S_QUEUE_EMPTY_HINT));
        }
        this->layout->SetSel(keep);
        // Offline with work pending: say why nothing is moving (items sit at
        // "pause"/"wait" and resume automatically when the network returns).
        // Online, the slot shows the queue summary instead.
        bool offline_note = false;
        if (!g_net_ok) {
            for (int i = 0; i < n; i++) {
                QStatus s = qv[i].item.status;
                if (s == Q_QUEUED || s == Q_PAUSED || s == Q_DOWNLOADING) {
                    this->layout->SetRomInfo(tr(S_WAITING_NETWORK));
                    offline_note = true;
                    break;
                }
            }
        }
        if (!offline_note) {
            this->layout->SetRomInfo(queue_summary(qv, n));
        }
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
    const u64 NAV_LEFT = HidNpadButton_Left | HidNpadButton_StickLLeft;
    const u64 NAV_RIGHT = HidNpadButton_Right | HidNpadButton_StickLRight;
    const bool in_cards = this->layout->InCards();
    if (down & NAV_DOWN) {
        if (in_cards) {
            this->layout->CardMove(0, 1);
        } else {
            this->layout->Step(1);
        }
    }
    if (down & NAV_UP) {
        if (in_cards) {
            this->layout->CardMove(0, -1);
        } else {
            this->layout->Step(-1);
        }
    }
    if (in_cards) {
        // In the card grid the D-pad/stick moves in all four directions (the
        // per-screen Left/Right actions are list-mode only).
        if (down & NAV_LEFT) {
            this->layout->CardMove(-1, 0);
        }
        if (down & NAV_RIGHT) {
            this->layout->CardMove(1, 0);
        }
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
                if (in_cards) {
                    this->layout->CardMove(0, dir);
                } else {
                    this->layout->Step(dir);
                }
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
            } else if (valid &&
                       (in_cards ? (down & HidNpadButton_X) != 0
                                 : (down & HidNpadButton_Right) != 0)) {
                // Pin/unpin — D-pad Right in the list; X in the card grid
                // (where the D-pad navigates the grid).
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
            } else if (down & HidNpadButton_Minus) {
                // Global search, same as the grouped view. Repo delete stays
                // available in the app utility (X → delete).
                char q[256] = {0};
                if (prompt_raw(tr(S_SEARCH_PROMPT), nullptr, q, sizeof(q)) &&
                    q[0]) {
                    this->GotoSearch(q);
                    return;
                }
            } else if (!in_cards && (down & HidNpadButton_Right) &&
                       flat_ref(this->layout->Sel(), &ci, &ri)) {
                // Pin/unpin — D-pad Right, same as on every other screen.
                // (In the card grid Right navigates, so no pin toggle there.)
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
            this->GotoFiles(this->sel_ci, repos_ref(this->layout->Sel()));
        } else if ((down & HidNpadButton_X) && g->repo_count > 0) {
            this->GotoRepoEdit(this->sel_ci, repos_ref(this->layout->Sel()));
        } else if (down & HidNpadButton_Y) {
            char nm[64] = {0}, id[256] = {0};
            if (prompt(tr(S_HINT_NAME), nullptr, nm, sizeof(nm)) &&
                prompt(tr(S_HINT_ARCHIVE_ID), nullptr, id, sizeof(id))) {
                if (config_add_repo(g, nm, id)) {
                    config_save(&g_cfg);
                    this->Toast(tr(S_ADDED));
                }
            }
            this->GotoRepos(this->sel_ci);
        } else if (down & HidNpadButton_Minus) {
            // Search across every repo in this console. (Repo deletion now lives
            // in the edit screen, X.)
            char q[256] = {0};
            if (prompt_raw(tr(S_SEARCH_CONSOLE), nullptr, q, sizeof(q)) &&
                q[0]) {
                this->GotoSearch(q, this->sel_ci, -1);
                return;
            }
        } else if ((down & HidNpadButton_Right) && g->repo_count > 0) {
            int ri = repos_ref(this->layout->Sel());
            if (ri >= 0 && ri < g->repo_count) {
                // Only flip the flag; GotoRepos floats pinned repos to the top
                // at render time, so the stored order stays put and unpinning
                // returns the repo to its original slot.
                g->repos[ri].pinned = !g->repos[ri].pinned;
                config_save(&g_cfg);
                this->GotoRepos(this->sel_ci);
                // Follow the toggled repo to wherever it now renders.
                for (int p = 0; p < (int)g_repos_map.size(); p++)
                    if (g_repos_map[p] == ri) { this->layout->SetSel(p); break; }
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
                    if (!this->SpaceOkToQueue(f->size)) return;
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
        } else if ((down & HidNpadButton_Minus) && !g_files_manual) {
            // Search within the opened repo.
            char q[256] = {0};
            if (prompt_raw(tr(S_SEARCH_REPO), nullptr, q, sizeof(q)) && q[0]) {
                this->GotoSearch(q, this->sel_ci, this->sel_ri);
                return;
            }
        } else if (down & HidNpadButton_Y) {
            char fb[64] = {0};
            if (prompt_raw(tr(S_FILTER_GUIDE), g_filter.c_str(), fb,
                           sizeof(fb))) {
                g_filter = fb;
                rebuild_files(this->layout.get(), g_files_target, false);
            }
        } else if (down & HidNpadButton_X) {
            // Sort picker — same dialog as the Installed browser.
            int s = this->CreateShowDialog(
                tr(g_sort_keys[g_sort_mode]), "",
                {tr(S_SORT_DEFAULT), tr(S_SORT_NAME_AZ), tr(S_SORT_NAME_ZA),
                 tr(S_SORT_SIZE_DESC), tr(S_SORT_SIZE_ASC), tr(S_CANCEL)},
                false, {}, style_dialog);
            if (s >= 0 && s < SORT__COUNT) {
                g_sort_mode = s;
                s32 keep = this->layout->Sel();
                rebuild_files(this->layout.get(), g_files_target, false);
                if (keep >= 0 && keep < this->layout->RowCount())
                    this->layout->SetSel(keep);
            }
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
            // Queue-level actions (batch): retry all failed / clear finished.
            int r = this->CreateShowDialog(
                tr(S_TITLE_QUEUE), "",
                {tr(S_RETRY_ALL), tr(S_CLEAR_FINISHED), tr(S_CANCEL)}, false, {},
                style_dialog);
            if (r == 0) {
                int n = queue_retry_all();
                char t[48];
                snprintf(t, sizeof(t), tr(S_RETRIED_N), n);
                this->Toast(t);
            } else if (r == 1) {
                queue_clear_finished();
                this->Toast(tr(S_CLEARED));
            }
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
                } else if (!in_cards &&
                           (down & (HidNpadButton_Left | HidNpadButton_Right))) {
                    // Jump to top (just below the active download) / bottom,
                    // then follow the item to its new row. (Card grid: the
                    // D-pad navigates instead, so no jump there.)
                    bool to_bottom = (down & HidNpadButton_Right) != 0;
                    if (queue_move_end(qv[i].slot, to_bottom)) {
                        static QueueView qv2[QUEUE_MAX];
                        int n2 = queue_snapshot(qv2, QUEUE_MAX);
                        for (int k = 0; k < n2; k++) {
                            if (qv2[k].slot == qv[i].slot) {
                                this->layout->SetSel(k);
                                break;
                            }
                        }
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
            case 0: { // Check for updates: pick the source first. GitHub is the
                      // normal release path; Wi-Fi receives a pushed .nro build
                      // (for testing, so the same version is fine).
                int r = this->CreateShowDialog(
                    tr(S_TITLE_UPDATE), tr(S_UPDATE_HOW),
                    {tr(S_UPDATE_SRC_GITHUB), tr(S_UPDATE_SRC_WIFI),
                     tr(S_CANCEL)},
                    true, {}, style_dialog); // last option = cancel (-1)
                if (r == 0) {
                    // On a background thread: the release fetch retries
                    // transient errors and would freeze the UI.
                    this->ChkStart();
                } else if (r == 1) {
                    this->UpdateWifiStart();
                }
                return;
            }
            case 1: // User interface settings (theme/cards/consoles/language)
                this->GotoUISettings();
                return;
            case 2: // Advanced settings
                this->GotoAdvanced();
                return;
            case 3: // View logs (download history + debug log)
                this->GotoViewLogs();
                return;
            case 4: // Manage data (downloads folder + metadata cache)
                this->GotoManageData();
                return;
            case 5: { // Credits
                // Big app badge beside the text. Loaded fresh into a handle the
                // dialog owns and frees on close (so it can't touch the shared
                // header/console icon cache).
                pu::sdl2::TextureHandle::Ref logo = {};
                auto tex = pu::ui::render::LoadImageFromFile(
                    "romfs:/credits_logo.png");
                if (tex) {
                    logo = pu::sdl2::TextureHandle::New(tex);
                }
                // "Release notes" is a real option (index 0); OK is the cancel
                // option, so it and B both return -1.
                int cr = this->CreateShowDialog(
                    tr(S_CREDITS),
                    std::string("HaulNX v") + APP_VERSION_STR + " by digdat0\n\n"
                    "Plutonium UI library provided by XorTroll",
                    {tr(S_RELEASE_NOTES), tr(S_OK)}, true, logo, style_dialog);
                if (cr == 0) {
                    // return, not break: skip the GotoSettings() below so the
                    // release-notes fetch keeps its spinner instead of being
                    // torn down and redrawn as Settings.
                    this->GotoReleaseNotes();
                    return;
                }
                break;
            }
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
                g_prefs.max_downloads = (g_prefs.max_downloads % 10) + 1;
                queue_set_max_dl(g_prefs.max_downloads);
                prefs_save(&g_prefs);
                break;
            case 1: // Max total download rate — cycle through the presets
                g_prefs.rate_all_kbps = rate_step(g_prefs.rate_all_kbps, +1);
                apply_rate_limits();
                prefs_save(&g_prefs);
                break;
            case 2: // Max rate per download — cycle through the presets
                g_prefs.rate_item_kbps = rate_step(g_prefs.rate_item_kbps, +1);
                apply_rate_limits();
                prefs_save(&g_prefs);
                break;
            case 3:
                g_prefs.prevent_sleep = !g_prefs.prevent_sleep;
                prefs_save(&g_prefs);
                break;
            case 4:
                g_prefs.net_check = !g_prefs.net_check;
                prefs_save(&g_prefs);
                break;
            case 5:
                g_prefs.chk_updates = !g_prefs.chk_updates;
                prefs_save(&g_prefs);
                break;
            case 6:
                this->GotoCreds();
                return;
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
            int dir = (down & HidNpadButton_Right) ? +1 : -1;
            bool changed = true;
            if (i == 0) {
                if (dir > 0) {
                    g_prefs.max_downloads = (g_prefs.max_downloads % 10) + 1;
                } else {
                    g_prefs.max_downloads = (g_prefs.max_downloads <= 1) ? 10 : g_prefs.max_downloads - 1;
                }
                queue_set_max_dl(g_prefs.max_downloads);
            } else if (i == 1) {
                g_prefs.rate_all_kbps = rate_step(g_prefs.rate_all_kbps, dir);
                apply_rate_limits();
            } else if (i == 2) {
                g_prefs.rate_item_kbps = rate_step(g_prefs.rate_item_kbps, dir);
                apply_rate_limits();
            } else {
                changed = false;
            }
            if (changed) {
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoAdvanced();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::RomPicker: {
        // Apply a chosen ROM root (empty string = reset to auto), then return
        // to Manage data. queue.c holds a pointer into g_tico.roms_path, so
        // rewriting that buffer takes effect without restarting the queue.
        auto apply_roms = [&](const char *chosen) {
            char norm[512];
            roms_normalize_path(chosen, norm, sizeof(norm));
            snprintf(g_prefs.roms_override, sizeof(g_prefs.roms_override), "%s",
                     norm);
            prefs_save(&g_prefs);
            tico_init(&g_tico);
            tico_set_roms_override(&g_tico, g_prefs.roms_override);
            this->inst_path = roms_root(&g_tico);
            this->Toast(norm[0] ? tr(S_ROMS_OVERRIDE_SET)
                                : tr(S_ROMS_OVERRIDE_CLEARED));
            this->GotoManageData();
        };
        bool at_root = (this->picker_path == "sdmc:/");
        if (down & HidNpadButton_B) {
            if (at_root) {
                this->GotoManageData();
            } else {
                // Up one level (never above the SD root).
                std::string up = this->picker_path;
                while (up.size() > 6 && up.back() == '/') up.pop_back();
                auto p = up.find_last_of('/');
                this->GotoRomPicker((p == std::string::npos || p < 5)
                                        ? std::string("sdmc:/")
                                        : up.substr(0, p + 1));
            }
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_rompick.size()) {
                std::string next = this->picker_path;
                if (next.empty() || next.back() != '/') next += "/";
                next += g_rompick[i].name;
                this->GotoRomPicker(next);
            }
        } else if (down & HidNpadButton_X) {
            // Use the folder currently being browsed as the ROM root.
            if (at_root) {
                this->ToastErr(tr(S_ROMS_USE_ROOT_WARN));
            } else if (this->Confirm(tr(S_ROMS_OVERRIDE_TITLE),
                                     this->picker_path + "\n\n" +
                                         tr(S_ROMS_OVERRIDE_WARN))) {
                apply_roms(this->picker_path.c_str());
            }
        } else if (down & HidNpadButton_Y) {
            // Reset to the default ROM folder (sd:/roms).
            apply_roms("");
        }
        break;
    }

    case Screen::UISettings: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            switch (i) {
            case 0: // console lists as a card grid
                g_prefs.card_view = !g_prefs.card_view;
                prefs_save(&g_prefs);
                break;
            case 1: // Theme toggle
                if (is_light_theme())
                    strcpy(g_prefs.theme, "dark");
                else
                    strcpy(g_prefs.theme, "light");
                select_theme();
                this->layout->ApplyTheme();
                prefs_save(&g_prefs);
                this->SyncTab();
                break;
            case 2:
                this->GotoLanguage();
                return;
            case 3:
                g_prefs.group_consoles = !g_prefs.group_consoles;
                prefs_save(&g_prefs);
                break;
            case 4:
                this->GotoManage();
                return;
            case 5: // Filter out file extensions (opens the editor)
                this->GotoExtFilter();
                return;
            default:
                break;
            }
            if (this->screen == Screen::UISettings) {
                s32 sel = this->layout->Sel();
                this->GotoUISettings();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::ExtFilter: {
        int n = g_prefs.exclude_ext_count;
        if (down & HidNpadButton_B) {
            this->GotoUISettings();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i == 0) { // master switch
                g_prefs.filter_exts = !g_prefs.filter_exts;
                prefs_save(&g_prefs);
            } else if (i >= 1 && i <= n) { // one extension's enabled flag
                FilterExt *fe = &g_prefs.exclude_exts[i - 1];
                fe->enabled = !fe->enabled;
                prefs_save(&g_prefs);
            } else if (i == n + 1) { // add a custom extension
                char ext[16] = {0};
                if (prompt_raw(tr(S_ADD_EXT_PROMPT), nullptr, ext, sizeof(ext)) &&
                    ext[0]) {
                    if (prefs_ext_add(&g_prefs, ext)) {
                        prefs_save(&g_prefs);
                    } else {
                        this->ToastErr(tr(S_EXT_ADD_FAILED));
                    }
                }
            }
            if (this->screen == Screen::ExtFilter) {
                s32 sel = this->layout->Sel();
                this->GotoExtFilter();
                this->layout->SetSel(sel);
            }
        } else if (down & HidNpadButton_X) { // remove the selected extension
            s32 i = this->layout->Sel();
            if (i >= 1 && i <= n) {
                prefs_ext_remove(&g_prefs, i - 1);
                prefs_save(&g_prefs);
                this->GotoExtFilter();
                this->layout->SetSel(i > 1 ? i - 1 : 1);
            }
        }
        break;
    }

    case Screen::Language: {
        if (down & HidNpadButton_B) {
            this->GotoUISettings();
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
            case 0: this->ImportStart(); return;  // receive dl_sources.json
            case 1: this->RestoreBackup(); return;
            case 2: { // ROM folder — browse the SD card and pick a folder
                /* Start inside the current override if it still exists,
                 * otherwise at the SD-card root. */
                std::string start = "sdmc:/";
                if (g_prefs.roms_override[0] &&
                    fs_exists(g_prefs.roms_override)) {
                    start = g_prefs.roms_override;
                }
                this->GotoRomPicker(start);
                return;
            }
            case 3: this->GotoDownloads(); return;
            case 4: this->GotoCache(); return;
            case 5: // Metadata cache on/off
                g_prefs.use_cache = !g_prefs.use_cache;
                prefs_save(&g_prefs);
                break;
            case 6: this->RaStart(); return;      // refresh all metadata
            default: break;
            }
            if (this->screen == Screen::ManageData) {
                s32 sel = this->layout->Sel();
                this->GotoManageData();
                this->layout->SetSel(sel);
            }
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            if (this->layout->Sel() == 5) { // Metadata cache on/off
                g_prefs.use_cache = !g_prefs.use_cache;
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoManageData();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::ViewLogs: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->GotoLog(); return;          // download history
            case 1: this->GotoDebugLog(); return;     // debug.log
            case 2: this->GotoQueueState(); return;   // queue.json
            case 3: this->GotoXferLog(); return;      // transfers.log
            case 4: this->GotoReleaseNotes(); return; // release history
            default: break;
            }
        }
        break;
    }

    case Screen::DebugLog: {
        if (down & HidNpadButton_B) {
            this->GotoViewLogs();
        } else if (down & HidNpadButton_A) {
            // Rows truncate long lines: show the full entry in a dialog.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_debug_lines.size()) {
                this->CreateShowDialog(this->log_view_title,
                                       wrap_for_dialog(g_debug_lines[i]),
                                       {tr(S_OK)}, true, {}, style_dialog);
            }
        } else if (down & HidNpadButton_X) {
            if (this->ConfirmDanger(tr(S_CLEAR_LOG), tr(this->log_clear_msg))) {
                remove(this->log_view_path.c_str());
                this->Toast(tr(S_LOG_CLEARED));
                this->GotoTextLog(this->log_view_path, this->log_view_title,
                                  this->log_clear_msg);
            }
        }
        break;
    }

    case Screen::ReleaseNotes: { // version list
        if (down & HidNpadButton_B) {
            // Back to wherever it was opened from (View logs, or Settings when
            // reached via the credits dialog).
            if (this->notes_origin == Screen::Settings) {
                this->GotoSettings();
            } else {
                this->GotoViewLogs();
            }
        } else if (down & HidNpadButton_A) {
            this->ShowReleaseNote(this->layout->Sel());
        }
        break;
    }

    case Screen::ReleaseNote: { // one release's notes
        if (down & HidNpadButton_B) {
            this->ShowReleaseList(); // back to the list, no re-fetch
        } else if (down & HidNpadButton_A) {
            // Rows truncate long lines: show the full entry in a dialog.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_debug_lines.size()) {
                this->CreateShowDialog(this->log_view_title,
                                       wrap_for_dialog(g_debug_lines[i]),
                                       {tr(S_OK)}, true, {}, style_dialog);
            }
        }
        break;
    }

    case Screen::QueueState: {
        if (down & HidNpadButton_B) {
            this->GotoViewLogs();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_qstate_details.size()) {
                this->CreateShowDialog(tr(S_TITLE_QUEUE_STATE),
                                       wrap_for_dialog(g_qstate_details[i]),
                                       {tr(S_OK)}, true, {}, style_dialog);
            }
        } else if (down & HidNpadButton_X) {
            if (!g_qstate_details.empty() &&
                this->ConfirmDanger(tr(S_CLEAR_QUEUE_STATE),
                                    tr(S_CLEAR_QUEUE_CONFIRM))) {
                remove(QUEUE_STATE_PATH);
                this->Toast(tr(S_CLEARED));
                this->GotoQueueState();
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
            if (this->ConfirmDanger(tr(S_DELETE_ALL), msg)) {
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
                if (this->ConfirmDanger(tr(S_DELETE), full)) {
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
                    if (this->ConfirmDanger(tr(S_DELETE), msg)) {
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
            if (this->ConfirmDanger(tr(S_CLEAR_CACHE), dmsg)) {
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
                if (this->ConfirmDanger(tr(S_DELETE),
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
                    break;
                }
                // "Move up one folder" is offered only from a *sub*folder
                // (roms/<console>/<sub>/…), never from a console folder itself:
                // files belong in the console folder, so we don't let them
                // escape upward into the roms root.
                std::string root = roms_root(&g_tico);
                auto pp = this->inst_path.find_last_of('/');
                std::string parent = (pp == std::string::npos)
                                         ? this->inst_path
                                         : this->inst_path.substr(0, pp);
                bool can_move = this->inst_path != root && parent != root;
                // With files marked (Y) in a movable subfolder, the dialog lists
                // the whole selection and moves it as a batch; otherwise it acts
                // on the single file under the cursor.
                bool multi = can_move && this->layout->MarkedCount() > 0;

                std::string content;
                std::vector<std::string> targets;
                if (multi) {
                    char hdr[96];
                    snprintf(hdr, sizeof(hdr), tr(S_MOVE_UP_MULTI),
                             this->layout->MarkedCount());
                    content = hdr;
                    int shown = 0;
                    for (s32 idx : this->layout->Marked()) {
                        if (idx < 0 || idx >= (s32)g_inst.size()) continue;
                        targets.push_back(g_inst[idx].name);
                        if (shown < 12)
                            content += "\n• " + g_inst[idx].name;
                        else if (shown == 12)
                            content += "\n…";
                        shown++;
                    }
                } else {
                    char szline[64];
                    snprintf(szline, sizeof(szline), tr(S_SIZE_LABEL),
                             human_size(g_inst[i].size).c_str());
                    content = "Path: " + this->inst_path + "\nName: " +
                              g_inst[i].name + "\n" + szline;
                    targets.push_back(g_inst[i].name);
                }
                // OK stays the highlighted default (index 0); "Move up one
                // folder" is the deliberate right-hand action (index 1) the user
                // must select to execute. B cancels either way.
                int r;
                if (can_move) {
                    r = this->CreateShowDialog(tr(S_FILE), content,
                                               {tr(S_OK), tr(S_MOVE_UP)}, false,
                                               {}, style_dialog);
                } else {
                    r = this->CreateShowDialog(tr(S_FILE), content, {tr(S_OK)},
                                               true, {}, style_dialog);
                }
                if (can_move && r == 1 && !targets.empty()) {
                    this->MvStart(targets);
                }
            }
        } else if ((down & HidNpadButton_Y) && !in_cards &&
                   this->inst_path != roms_root(&g_tico)) {
            // Y marks roms for deletion — only inside a console folder, never
            // on the console list where selecting/deleting makes no sense.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                this->layout->ToggleMark(i);
            }
        } else if ((in_cards ? (down & HidNpadButton_X) != 0
                             : (down & HidNpadButton_Right) != 0)) {
            s32 i = this->layout->Sel();
            if (this->inst_path == roms_root(&g_tico)) {
                // Pin/unpin a top-level console folder — D-pad Right in the
                // list, X in the card grid (where the D-pad navigates).
                if (i >= 0 && i < (s32)g_inst.size() && g_inst[i].is_dir) {
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
            } else {
                // Inside a console folder: ▶ deletes roms. With items marked
                // (Y) it deletes the whole selection; with nothing marked it
                // deletes the one under the cursor. Either way a confirm guards
                // it, so a stray press can't wipe anything unattended.
                int mc = this->layout->MarkedCount();
                if (mc > 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), mc);
                    if (this->ConfirmDanger(tr(S_DELETE), msg, true)) {
                        // Delete in reverse order so indices stay valid.
                        auto marks = this->layout->Marked();
                        for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
                            s32 idx = *it;
                            if (idx >= 0 && idx < (s32)g_inst.size())
                                fs_rm_rf((this->inst_path + "/" +
                                          g_inst[idx].name).c_str());
                        }
                        char t[32];
                        snprintf(t, sizeof(t), tr(S_DELETED_N), mc);
                        this->Toast(t);
                        this->GotoInstalled(this->inst_path);
                    }
                } else if (i >= 0 && i < (s32)g_inst.size()) {
                    char msg[300];
                    snprintf(msg, sizeof(msg), tr(S_DELETE_ONE),
                             g_inst[i].name.c_str());
                    if (this->ConfirmDanger(tr(S_DELETE), msg, true)) {
                        fs_rm_rf((this->inst_path + "/" +
                                  g_inst[i].name).c_str());
                        char t[32];
                        snprintf(t, sizeof(t), tr(S_DELETED_N), 1);
                        this->Toast(t);
                        this->GotoInstalled(this->inst_path);
                    }
                }
            }
        } else if ((down & HidNpadButton_Left) && !in_cards) {
            // ◀ opens the sort picker (search now lives on −).
            int s = this->CreateShowDialog(
                tr(g_sort_keys[g_inst_sort]), "",
                {tr(S_SORT_DEFAULT), tr(S_SORT_NAME_AZ), tr(S_SORT_NAME_ZA),
                 tr(S_SORT_SIZE_DESC), tr(S_SORT_SIZE_ASC), tr(S_CANCEL)},
                false, {}, style_dialog);
            if (s >= 0 && s < SORT__COUNT) {
                g_inst_sort = s;
                s32 keep = this->layout->Sel();
                this->GotoInstalled(this->inst_path);
                if (keep >= 0 && keep < this->layout->RowCount())
                    this->layout->SetSel(keep);
            }
        } else if ((down & HidNpadButton_X) && !in_cards &&
                   this->inst_path != roms_root(&g_tico)) {
            // Rename only files inside a console folder — not the console
            // folders themselves at the top level.
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
            // − searches the current folder (scope handled by GotoInstSearch).
            // Prompt reflects the scope: the console list searches every
            // installed console, a console folder searches just that one.
            const char *sp = (this->inst_path == roms_root(&g_tico))
                                 ? tr(S_SEARCH_INSTALLED)
                                 : tr(S_SEARCH_CONSOLE);
            char q[256] = {0};
            // Fresh open: start blank. The prior query lingering here is
            // confusing on a new search (Y re-search below keeps it to tweak).
            if (prompt_raw(sp, nullptr, q, sizeof(q)) && q[0]) {
                this->GotoInstSearch(q);
                return;
            }
        }
        break;
    }

    case Screen::InstSearch: {
        if (down & HidNpadButton_B) {
            this->GotoInstalled(roms_root(&g_tico));
        } else if (down & HidNpadButton_A) {
            // Open the folder holding the hit, with the file selected, so it
            // can be inspected / renamed / deleted from there.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst_hits.size()) {
                std::string dir = g_inst_hits[i].dir, name = g_inst_hits[i].name;
                this->GotoInstalled(dir);
                for (s32 k = 0; k < (s32)g_inst.size(); k++) {
                    if (g_inst[k].name == name) {
                        this->layout->SetSel(k);
                        break;
                    }
                }
            }
        } else if (down & HidNpadButton_Y) {
            // Re-search keeps the scope of the folder the results came from.
            const char *sp = (this->inst_path == roms_root(&g_tico))
                                 ? tr(S_SEARCH_INSTALLED)
                                 : tr(S_SEARCH_CONSOLE);
            char q[256] = {0};
            if (prompt_raw(sp, g_inst_query.c_str(), q, sizeof(q)) && q[0]) {
                this->GotoInstSearch(q);
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
                if (prompt(tr(S_HINT_NAME), rp->label, v, sizeof(v))) {
                    snprintf(rp->label, sizeof(rp->label), "%s", v);
                    config_save(&g_cfg);
                }
                break;
            case 1:
                if (prompt(tr(S_HINT_ARCHIVE_ID), rp->id, v, sizeof(v))) {
                    snprintf(rp->id, sizeof(rp->id), "%s", v);
                    rp->download_base[0] = '\0';
                    repo_set_url_default(rp);
                    config_save(&g_cfg);
                }
                break;
            case 2:
                if (prompt(tr(S_HINT_DOWNLOAD_URL), rp->download_base, v, sizeof(v))) {
                    snprintf(rp->download_base, sizeof(rp->download_base), "%s",
                             v);
                    config_save(&g_cfg);
                }
                break;
            case 3:
                rp->enabled = !rp->enabled;
                config_save(&g_cfg);
                break;
            case 4: // hard refresh: refetch this repo's metadata, skip cache
                this->GotoFiles(this->sel_ci, this->sel_ri, true);
                return;
            case 5:
                if (this->ConfirmDanger(tr(S_DELETE_REPO), tr(S_DELETE_REPO_CONFIRM))) {
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
                    if (prompt(tr(S_HINT_NAME), nullptr, nm, sizeof(nm)) &&
                        prompt(tr(S_HINT_ARCHIVE_ID), nullptr, id, sizeof(id))) {
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
                this->GotoViewLogs(); // opened from Settings > View logs
            }
        } else if (down & HidNpadButton_A) {
            // Rows truncate long lines: show the full entry in a dialog,
            // with the re-download action inside it when available.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_log_entries.size()) {
                const LogEntry &e = g_log_entries[i];
                std::string body = e.display;
                if (!e.url.empty()) {
                    body += "\n" + e.url;
                }
                if (e.can_retry) {
                    int opt = this->CreateShowDialog(
                        tr(S_TITLE_LOG), wrap_for_dialog(body),
                        {tr(S_RETRY), tr(S_OK)}, true, {}, style_dialog);
                    if (opt == 0) {
                        if (!this->SpaceOkToQueue(e.size)) return;
                        char auth[320];
                        creds_auth_header(&g_creds, auth, sizeof(auth));
                        bool ok = queue_add(e.url.c_str(), e.name.c_str(),
                                            e.target.c_str(), auth, e.size,
                                            e.is_archive, e.md5.c_str());
                        if (ok)
                            this->Toast(std::string(tr(S_QUEUED)) + ": " +
                                        e.name);
                        else
                            this->ToastErr(tr(S_QUEUE_FULL));
                    }
                } else {
                    this->CreateShowDialog(tr(S_TITLE_LOG),
                                           wrap_for_dialog(body), {tr(S_OK)},
                                           true, {}, style_dialog);
                }
            }
        } else if (down & HidNpadButton_X) {
            if (this->ConfirmDanger(tr(S_CLEAR_LOG),
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
            this->GotoUISettings(); // manage consoles lives under UI settings
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < g_cfg.console_count) {
                bool sh = !g_cfg.consoles[i].shown;
                g_cfg.consoles[i].shown = sh;
                config_save(&g_cfg);
                // Update just this row's state label in place — a full
                // GotoManage() rebuild resets scroll_top, which then re-scrolls
                // the selected row to the bottom of the viewport.
                this->layout->SetRowRight(i, sh ? tr(S_SHOWN) : tr(S_HIDDEN),
                                          manage_state_color(sh));
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
                if (this->ConfirmDanger(tr(S_CLEAR_CREDS),
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
            // Return to wherever the search was launched from.
            if (this->search_ri >= 0) {
                this->GotoFiles(this->search_ci, this->search_ri);
            } else if (this->search_ci >= 0) {
                this->GotoRepos(this->search_ci);
            } else {
                this->GotoHome();
            }
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_search_results.size()) {
                const SearchHit &h = g_search_results[i];
                if (!this->SpaceOkToQueue(h.size)) return;
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
            // New search, keeping the current scope.
            int prompt_id = this->search_ri >= 0   ? S_SEARCH_REPO
                            : this->search_ci >= 0 ? S_SEARCH_CONSOLE
                                                   : S_SEARCH_PROMPT;
            char q[256] = {0};
            if (prompt_raw(tr(prompt_id), g_search_query.c_str(),
                           q, sizeof(q)) && q[0]) {
                this->GotoSearch(q, this->search_ci, this->search_ri);
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
    /* A user-set ROM folder overrides the default ROM root. */
    tico_set_roms_override(&g_tico, g_prefs.roms_override);
    /* Pre-create a folder for every supported console so they appear in the
     * Installed tab before their first download (downloads mkdir on their own,
     * but an untouched console would otherwise never show). */
    config_seed_rom_folders(&g_cfg, roms_root(&g_tico));
    select_theme();
    if (g_prefs.lang[0] && strcmp(g_prefs.lang, "en") != 0) {
        char lpath[256];
        snprintf(lpath, sizeof(lpath), LANG_DIR "/%s.json", g_prefs.lang);
        if (!fs_exists(lpath))
            snprintf(lpath, sizeof(lpath), "romfs:/lang/%s.json", g_prefs.lang);
        i18n_load(lpath);
    }
    queue_init(roms_root(&g_tico), g_prefs.max_downloads);
    apply_rate_limits(); /* seed the throttle from saved prefs */
    cleanup_stale_parts(); // drop unresumable old-format .part leftovers
    load_console_icons();  // romfs:/icons/<key>.png, shared into list rows

    this->screen = Screen::Home;
    this->sel_ci = 0;
    this->sel_ri = 0;
    this->pending = Pending::None;
    this->inst_path = roms_root(&g_tico);
    this->log_origin = Screen::Settings;

    this->layout = MainLayout::New();
    this->layout->ApplyTheme();
    this->LoadLayout(this->layout);

    // Startup dialogs (e.g. no network) must NOT run here: OnLoad
    // executes before Show() starts the render loop, so a dialog would wait
    // for input on a screen that is still black. Defer them to the first
    // frame of the input callback instead.
    this->startup_checks = true;

    this->GotoHome();
    this->RefreshStatus();

    this->SetOnInput([&](const u64 down, const u64 up, const u64 held,
                         const pu::ui::TouchPoint touch) {
        (void)up;
        this->HandleInput(down, held, touch);
    });
}

// Orderly teardown after the UI loop ends. The background queue thread MUST be
// joined (queue_exit) before the process exits, or libnx faults on a still-
// running thread — that was the "an error occurred" crash on +. Tear services
// down in reverse init order; queue_exit before net_exit since the worker uses
// sockets/curl.
void MainApplication::Shutdown() {
    this->ImportStop(); // the listening socket must go before net_exit()
    // Ask the only worker that polls a cancel flag to stop, then join them all.
    this->ra_cancel = true;
    for (BgTask *t : {&this->upd, &this->ra, &this->chk, &this->meta,
                      &this->search, &this->notes, &this->bgchk}) {
        t->Join();
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
    self->chk.done = true;
}

// Silent startup update check. Fills bgchk_tag/url off-thread; never touches
// Plutonium (result is applied on the main thread in BgChkPoll).
void MainApplication::BgChkThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->bgchk_ok = update_fetch_latest(UPDATE_REPO, self->bgchk_tag,
                                         sizeof(self->bgchk_tag), self->bgchk_url,
                                         sizeof(self->bgchk_url), NULL);
    self->bgchk.done = true;
}

void MainApplication::BgChkStart() {
    if (this->bgchk.running || this->update_available) {
        return; // already checking, or already found one this session
    }
    this->bgchk_ok = false;
    this->bgchk_tag[0] = '\0';
    this->bgchk_url[0] = '\0';
    // Best-effort: if the thread can't spawn, just skip the silent check (the
    // user can still check manually). Never fetch synchronously here — this runs
    // on the startup path and must not block the first frame.
    this->bgchk.Start(&MainApplication::BgChkThread, this);
}

void MainApplication::BgChkPoll() {
    if (!this->bgchk.running || !this->bgchk.done) {
        return;
    }
    this->bgchk.Join();
    if (this->bgchk_ok && version_cmp(APP_VERSION_STR, this->bgchk_tag) < 0) {
        this->update_available = true;
        this->layout->SetUpdateAvailable(true);
        // If Settings is already open, redraw so the chip appears immediately.
        if (this->screen == Screen::Settings) {
            s32 sel = this->layout->Sel();
            this->GotoSettings();
            this->layout->SetSel(sel);
        }
    }
}

void MainApplication::ChkStart() {
    if (this->chk.running) {
        // A dismissed check is still finishing: re-attach to it (progress UI
        // comes back) instead of spawning a second thread over the first.
        this->chk_discard = false;
        return;
    }
    this->chk_attempt = 1;
    this->chk_ok = false;
    this->chk_discard = false;
    this->chk_tag[0] = '\0';
    this->chk_url[0] = '\0';

    if (this->chk.Start(&MainApplication::ChkThread, this)) {
        return;
    }
    // Couldn't spawn: fetch synchronously so the check still works.
    this->chk_ok = update_fetch_latest(UPDATE_REPO, this->chk_tag,
                                       sizeof(this->chk_tag), this->chk_url,
                                       sizeof(this->chk_url), NULL);
    this->ChkFinish();
}

void MainApplication::ChkTick() {
    if (!this->chk.done) {
        // Live status with the attempt number, e.g. "Check for updates... (2/3)".
        char s[160];
        snprintf(s, sizeof(s), "%s...  (%d/3)  B %s", tr(S_CHECK_UPDATES),
                 (int)this->chk_attempt, tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    this->chk.Join();
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
        // Cancel(0) first so it's the safe default; "Release notes" lets the user
        // see what's in the new version before committing to the update. It opens
        // the release list (screen is Settings here, so B returns to Settings).
        int r = this->CreateShowDialog(tr(S_TITLE_UPDATE), umsg,
                                       {tr(S_CANCEL), tr(S_RELEASE_NOTES), tr(S_YES)},
                                       false, {}, style_dialog);
        if (r == 1) {
            this->GotoReleaseNotes();
            return;
        }
        if (r != 2) {
            this->GotoSettings();
            return;
        }
    }
    char dl[1024];
    snprintf(dl, sizeof(dl), "%s/downloads/update.nro", CONFIG_DIR);
    fs_ensure_parent(dl);
    this->UpdStart(this->chk_url, dl, this->chk_tag);
}

// One GitHub release, as shown in the notes viewer. body is the raw markdown;
// it is only walked line-by-line when a release is opened, so the list itself
// stays light.
struct RelNote {
    std::string tag;
    std::string date;
    std::string body;
};
static std::vector<RelNote> g_relnotes;

// Fetch the newest releases into g_relnotes (newest first). per_page is kept
// small: the list endpoint returns every body inline, so asking for 100 is what
// made the old viewer slow. Returns false if the fetch or parse failed.
static bool fetch_release_list(const char *repo) {
    g_relnotes.clear();
    char api[256];
    snprintf(api, sizeof(api),
             "https://api.github.com/repos/%s/releases?per_page=10", repo);
    char *body = nullptr;
    long code = 0;
    size_t len = 0;
    for (int a = 0; a < 3; a++) {
        body = http_get(api, &code, &len);
        if (body && code == 200 && len >= 2) {
            break;
        }
        free(body);
        body = nullptr;
        svcSleepThread(700000000ULL); // ~0.7s before retrying a transient error
    }
    if (!body) {
        return false;
    }
    int ntok = 0;
    jsmntok_t *tok = json_parse_alloc(body, len, &ntok);
    if (!tok || tok[0].type != JSMN_ARRAY) {
        free(tok);
        free(body);
        return false;
    }
    int nrel = tok[0].size, rel = 1;
    for (int r = 0; r < nrel; r++) {
        if (tok[rel].type != JSMN_OBJECT) {
            rel = json_tok_skip(tok, rel);
            continue;
        }
        if (json_bool(body, tok, json_obj_get(body, tok, rel, "draft"))) {
            rel = json_tok_skip(tok, rel); // drafts aren't public; skip them
            continue;
        }
        RelNote e;
        char tag[64] = "", date[32] = "";
        json_copy(body, tok, json_obj_get(body, tok, rel, "tag_name"), tag,
                  sizeof(tag));
        json_copy(body, tok, json_obj_get(body, tok, rel, "published_at"), date,
                  sizeof(date));
        date[10] = '\0'; // ISO 8601 timestamp -> yyyy-mm-dd
        e.tag = tag[0] ? tag : "(untagged)";
        e.date = date;
        int bi = json_obj_get(body, tok, rel, "body");
        if (bi >= 0 && tok[bi].type == JSMN_STRING) {
            int blen = tok[bi].end - tok[bi].start;
            if (blen > 0) {
                // Sized to the raw token; unescaping only ever shrinks it.
                std::vector<char> buf((size_t)blen + 1);
                json_copy(body, tok, bi, buf.data(), buf.size());
                e.body = buf.data();
            }
        }
        g_relnotes.push_back(std::move(e));
        rel = json_tok_skip(tok, rel);
    }
    free(tok);
    free(body);
    return !g_relnotes.empty();
}

// Flatten one line of GitHub-flavoured markdown into something legible in a
// plain-text row: no rich rendering (the UI has no styled runs), just strip the
// syntax that otherwise shows up as literal #, * and backticks.
static std::string md_line(const std::string &in) {
    std::string s = in;
    // Trim a trailing CR left by CRLF bodies.
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    // Leading block markers: measure indent, then the marker.
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') {
        i++;
    }
    std::string indent(i, ' ');
    std::string rest = s.substr(i);
    std::string prefix;
    if (!rest.empty() && rest[0] == '#') { // heading -> plain text
        size_t h = 0;
        while (h < rest.size() && rest[h] == '#') {
            h++;
        }
        while (h < rest.size() && rest[h] == ' ') {
            h++;
        }
        rest = rest.substr(h);
        indent.clear();
    } else if (rest.size() >= 2 && (rest[0] == '-' || rest[0] == '*' ||
                                    rest[0] == '+') &&
               rest[1] == ' ') {
        prefix = "• "; // bullet list item
        rest = rest.substr(2);
    } else if (rest.size() >= 2 && rest[0] == '>' && rest[1] == ' ') {
        prefix = "| "; // blockquote
        rest = rest.substr(2);
    }
    // Horizontal rule -> a visible divider.
    if (rest == "---" || rest == "***" || rest == "___") {
        return "────────────";
    }
    // Inline: drop emphasis/code markers and unwrap [text](url) -> text.
    // Only '*' and '`' — not '_', which is literal in identifiers these notes
    // are full of (dl_sources.json, roms_override) and rarely used as emphasis.
    std::string out;
    for (size_t k = 0; k < rest.size(); k++) {
        char c = rest[k];
        if (c == '*' || c == '`') {
            continue; // **bold**, *italic*, `code`
        }
        if (c == '[') {
            size_t close = rest.find(']', k);
            size_t lp = (close == std::string::npos) ? std::string::npos
                                                      : rest.find('(', close);
            if (close != std::string::npos && lp == close + 1) {
                size_t rp = rest.find(')', lp);
                if (rp != std::string::npos) {
                    out += rest.substr(k + 1, close - k - 1); // link text only
                    k = rp;
                    continue;
                }
            }
        }
        out += c;
    }
    return indent + prefix + out;
}

void MainApplication::NotesThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->notes_ok = fetch_release_list(UPDATE_REPO);
    self->notes.done = true;
}

void MainApplication::GotoReleaseNotes() {
    this->notes_ok = false;
    this->notes_origin = this->screen; // View logs, or Settings via the credits
    this->layout->ShowSpinner(tr(S_LOADING));
    if (this->notes.Start(&MainApplication::NotesThread, this)) {
        return; // NotesTick shows the list once the fetch lands
    }
    // Couldn't spawn a thread: fetch synchronously so it still works.
    this->notes_ok = fetch_release_list(UPDATE_REPO);
    this->NotesTick();
}

void MainApplication::NotesTick() {
    if (this->notes.running && !this->notes.done) {
        return; // the spinner overlay animates itself
    }
    this->notes.Join(); // no-op on the synchronous fallback
    this->layout->HideSpinner();
    if (!this->notes_ok) {
        this->CreateShowDialog(tr(S_RELEASE_NOTES), tr(S_UPDATE_FETCH_FAIL),
                               {tr(S_OK)}, true, {}, style_dialog);
        if (this->notes_origin == Screen::Settings) {
            this->GotoSettings();
        } else {
            this->GotoViewLogs();
        }
        return;
    }
    this->ShowReleaseList();
}

// The version list: one row per release. Cheap to (re)build, so backing out of a
// release's notes returns here without re-fetching.
void MainApplication::ShowReleaseList() {
    this->screen = Screen::ReleaseNotes;
    this->layout->SetTitle(tr(S_RELEASE_NOTES));
    this->layout->SetSubtitle(tr(S_SUB_VIEW_LOGS)); // "A select  B back"
    this->layout->ClearMenu();
    for (const auto &e : g_relnotes) {
        this->layout->AddRow2(e.tag, e.date, g_theme->row_text,
                              chevron_color());
    }
    if (g_relnotes.empty()) {
        this->layout->AddRow(tr(S_NO_LOG));
    }
}

// One release's notes, expanded only when opened. Markdown is flattened to
// legible rows; A opens the full (wrapped) line, like the log viewers.
void MainApplication::ShowReleaseNote(int idx) {
    if (idx < 0 || idx >= (int)g_relnotes.size()) {
        return;
    }
    const RelNote &e = g_relnotes[idx];
    this->screen = Screen::ReleaseNote;
    this->log_view_title = e.tag; // reused as the expand-dialog title
    this->layout->SetTitle(e.tag);
    this->layout->SetSubtitle(tr(S_SUB_VIEW_LOGS));
    this->layout->ClearMenu();
    g_debug_lines.clear();
    const int max_lines = 800;
    size_t pos = 0, n = 0;
    while (pos <= e.body.size() && (int)n < max_lines) {
        size_t nl = e.body.find('\n', pos);
        std::string raw = e.body.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        std::string line = md_line(raw);
        this->layout->AddRow(line.empty() ? " " : line);
        g_debug_lines.push_back(line);
        n++;
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    if (n == 0) {
        this->layout->AddRow(tr(S_NO_LOG));
    }
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
                            &MainApplication::UpdProgress, self, NULL, NULL, 0,
                            &code);
    self->upd_ok = ok;
    self->upd.done = true;
}

// Append a line to the debug log: the self-updater's install steps must be
// diagnosable on-device (a failure here previously left no trace at all).
static void upd_log(const char *fmt, ...) {
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

// App badge for the update-download card, loaded once (280px master, crisp
// at the enlarged card's icon size). Falls back to the header logo.
static pu::sdl2::Texture upd_card_icon() {
    static pu::sdl2::Texture t = nullptr;
    if (!t) {
        t = pu::ui::render::LoadImageFromFile("romfs:/credits_logo.png");
    }
    return t ? t : g_header_logo;
}

void MainApplication::UpdStart(const std::string &url, const std::string &dl,
                               const std::string &tag) {
    this->upd_url = url;
    this->upd_dl = dl;
    this->upd_tag = tag;
    this->upd_now = 0;
    this->upd_total = 0;
    this->upd_ok = false;
    this->upd_cancel = false;

    this->layout->SetTitle(tr(S_UPDATING));
    this->layout->SetSubtitle(std::string("Downloading ") + tag + "...  (B to cancel)");
    this->layout->ClearMenu();
    if (g_prefs.card_view) {
        // Card mode: one enlarged queue-style card, centred, with the app
        // badge as its icon — same look as the download-queue cards.
        this->layout->SetCardsMode(true);
        this->layout->SetSingleCard(true);
        this->layout->SetQueueCount(1);
        this->layout->SetQueueCard(0, "HaulNX", upd_card_icon(),
                                   qstatus(Q_DOWNLOADING),
                                   qstatus_color(Q_DOWNLOADING), tag, "", "",
                                   "HaulNX.nro", 0.0f, true);
    } else {
        this->layout->AddRow(tr(S_UPDATE_DL_CANCEL));
    }

    if (this->upd.Start(&MainApplication::UpdThread, this)) {
        return;
    }
    this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_START_FAIL), {tr(S_OK)},
                           true, {}, style_dialog);
    this->GotoSettings();
}

void MainApplication::UpdTick() {
    if (!this->upd.done) {
        // Still downloading: show live progress in the subtitle.
        u64 now = this->upd_now, total = this->upd_total;
        int pct = total ? (int)((now * 100) / total) : 0;
        char s[160];
        snprintf(s, sizeof(s), tr(S_UPDATE_DOWNLOADING),
                 this->upd_tag.c_str(), pct, human_size(now).c_str(),
                 total ? human_size(total).c_str() : "?");
        this->layout->SetSubtitle(s);
        if (g_prefs.card_view) {
            // ...and on the centred card: status corner gets "dl NN%", the
            // chip gets "vX.Y.Z · now / total", the ring fills.
            char st[32];
            snprintf(st, sizeof(st), "%s %d%%", qstatus(Q_DOWNLOADING), pct);
            char c1[64];
            snprintf(c1, sizeof(c1), "%s / %s", human_size(now).c_str(),
                     total ? human_size(total).c_str() : "?");
            this->layout->SetQueueCard(0, "HaulNX", upd_card_icon(), st,
                                       qstatus_color(Q_DOWNLOADING),
                                       this->upd_tag, c1, "",
                                       "HaulNX.nro",
                                       total ? (float)now / (float)total
                                             : 0.0f,
                                       true);
        }
        return;
    }

    // Download finished: join the worker and install on the main thread.
    this->upd.Join();

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
        // Don't touch the running NRO: the loader keeps it open for the
        // app's whole lifetime, so delete/rename/overwrite of it all fail
        // while we run. Instead stage the new build as "<self>.new"; main()
        // finishes the swap on the next launch, before anything opens us.
        std::string selfp = resolve_self_path();
        char stage[1100];
        snprintf(stage, sizeof(stage), "%s.new", selfp.c_str());
        remove(stage); // clear a stale stage so the rename can land
        bool inst = (rename(dl.c_str(), stage) == 0);
        if (!inst) {
            upd_log("upd: rename dl->stage failed (errno=%d), copying",
                    errno);
            inst = install_over(dl.c_str(), stage) && looks_like_nro(stage);
        }
        upd_log("upd: staged '%s' %s", stage, inst ? "ok" : "FAILED");
        if (inst) {
            remove(dl.c_str());
            // The new build is staged for the next launch; flip the chips and
            // offer to relaunch in place (same epilogue as a LAN-pushed .nro).
            char umsg[512];
            snprintf(umsg, sizeof(umsg), tr(S_UPDATE_OK), tag.c_str());
            if (this->StagedRestartPrompt(umsg)) {
                return; // closing to restart
            }
        } else {
            remove(stage); // don't leave a half-written stage behind
            this->CreateShowDialog(
                tr(S_TITLE_UPDATE),
                std::string("Could not stage the update (details in debug "
                            "log).\nDownloaded build kept at:\n") + dl,
                {tr(S_OK)}, true, {}, style_dialog);
        }
    } else {
        remove(dl.c_str());
        this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_FAIL), {tr(S_OK)}, true, {}, style_dialog);
    }
    this->GotoSettings();
}
