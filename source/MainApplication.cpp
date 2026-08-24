#include <MainApplication.hpp>
#include <pu/ui/extras/extras_Toast.hpp>
#include "version.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "boxart.h"
#include "config.h"
#include "i18n.h"
#include "iarchive.h"
#include "net.h"
#include "queue.h"
#include "extract.h"
#include "rar3.h"
#include "fsutil.h"
#include "idgame.h"
#include "hashx.h"
#include "hashcache.h"
#include "vfystatus.h"
#include "update.h"
#include "jsonutil.h"
#include "jsmn.h"
#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
}

#include <mtp/mtp.h> // C++ (namespace mtp); must not be inside extern "C"

// ---- backend state --------------------------------------------------------
static SourcesConfig g_cfg;
static Credentials g_creds;
static Prefs g_prefs;
static TicoState g_tico;
static ArchiveItem g_item;
static bool g_have_item = false;

static std::vector<int> g_files; // filtered indices into g_item.files
static std::vector<char> g_marks;
// Browse multi-select, keyed by index into g_item.files rather than by row.
// The list widget's own marks are row-indexed and dropped on every Clear(), so
// a filter or sort change would silently lose the selection; keying to files
// lets you filter, select, refilter and select again to build one set. Cleared
// whenever a different repo's metadata is loaded.
static std::set<int> g_sel;
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
    /* Absolute path this entry points at, when it isn't simply
     * <listed folder>/<name>. Set for the synthetic Installed-tab rows that
     * stand in for a console whose install folder is a custom location outside
     * the ROM root; empty for ordinary directory entries. */
    std::string path;
    /* Non-empty on a synthetic Installed-tab row that stands for a multi-file
     * game (a cue/bin set, a multi-disc set): the real filenames in the folder
     * it collapses. Empty on every ordinary entry, so the other users of this
     * struct (downloads, the ROM-folder picker, plain list_dir callers) are
     * simply never groups. See inst_detect_groups. */
    std::vector<std::string> group_members;
    /* On-disk mtime, captured by the same stat() list_dir already does. Used
     * as the freshness stamp for the persisted verify-status badge (see
     * vfystatus.h) — a file replaced since its last verify must not show a
     * stale badge. 0 on synthetic rows (custom-folder consoles, group rows),
     * which simply never get one. */
    uint64_t mtime = 0;
};
static std::vector<DirEnt> g_inst;
static std::vector<DirEnt> g_dlfiles; // files in the downloads temp folder
static std::vector<DirEnt> g_inbox_mfiles; // files staged in the Inbox folder
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
    {168,176,188,255},    {255,255,255,255},   {90,160,245,255},
    {192,199,210,255},    {150,160,185,255},   {232,234,240,255},
    {26,28,34,255},       {255,255,255,255},   {205,212,222,255},
    {255,255,255,255},    {70,130,200,255},
    {22,23,27,255},       {28,30,36,255},      {40,44,53,255},
    {80,86,100,255},      {42,56,30,255},
};

// Light theme keeps the charcoal header/tab shell (the logo's "case") over a
// light content area; the same blue underline/pulse reads on the dark shell.
static const AppTheme g_theme_light = {
    {228,231,237,255},    {30,33,40,255},      {23,25,31,255},
    {30,33,40,255},       {255,255,255,255},   {200,207,217,255},
    {150,158,172,255},    {255,255,255,255},   {90,160,245,255},
    {185,192,204,255},    {88,98,116,255},     {26,30,38,255},
    {240,242,246,255},    {26,30,40,255},      {50,60,80,255},
    {26,30,40,255},       {195,220,245,255},
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
        "wiiu",
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
        "set-credits",
        // v2 reorganized settings hierarchy
        "set-appearance", "set-downloads", "set-sources", "set-storage",
        "set-transfers", "set-install-pc", "set-account", "set-diagnostics",
        "set-logs", "set-getting-started", "set-dats"};
    for (const char *k : keys) {
        auto tex = pu::ui::render::LoadImageFromFile(std::string("romfs:/icons/") +
                                                     k + ".png");
        if (tex) {
            g_console_icons[k] = tex;
        }
    }
    g_header_logo = pu::ui::render::LoadImageFromFile("romfs:/header_logo.png");
}

// One frame — icon, wordmark, version — drawn straight to the renderer before
// any of OnLoad's slower init (nifm/psm/net/config/queue/icon-cache loading,
// a few seconds combined) runs. Application::Load() already calls
// renderer->Initialize() (fonts + romfs mount) before invoking OnLoad(), so
// everything this needs is ready; without this the user stares at a black
// screen for that whole stretch instead of seeing the app immediately.
static void draw_splash(pu::ui::render::Renderer::Ref &renderer) {
    namespace rnd = pu::ui::render;
    // credits_logo.png is the big 280px master badge (already used full-size
    // by the About screen); header_logo.png is the small header badge kept
    // as a fallback in case the bigger asset is ever missing from romfs.
    // The badge already bakes in the "HaulNX" wordmark, so no separate title.
    auto logo = rnd::LoadImageFromFile("romfs:/credits_logo.png");
    if (!logo) {
        logo = rnd::LoadImageFromFile("romfs:/header_logo.png");
    }
    const std::string vfont =
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    auto ver_tex = rnd::RenderText(
        vfont, std::string("v") + APP_VERSION_STR, g_theme_dark.rom_info_clr);

    const s32 SW = (s32)rnd::ScreenWidth, SH = (s32)rnd::ScreenHeight;
    const s32 logo_sz = (s32)(SH * 0.6); // ~60% of screen height
    const s32 ver_h = ver_tex ? rnd::GetTextureHeight(ver_tex) : 0;
    const s32 gap = 20; // logo -> version
    const s32 block_h = (logo ? logo_sz + gap : 0) + ver_h;
    s32 y = (SH - block_h) / 2;

    renderer->InitializeRender(g_theme_dark.bg);
    if (logo) {
        rnd::TextureRenderOptions opts;
        opts.width = logo_sz;
        opts.height = logo_sz;
        renderer->RenderTexture(logo, (SW - logo_sz) / 2, y, opts);
        y += logo_sz + gap;
    }
    if (ver_tex) {
        renderer->RenderTexture(ver_tex, (SW - rnd::GetTextureWidth(ver_tex)) / 2, y);
    }
    renderer->FinalizeRender();

    if (logo) {
        rnd::DeleteTexture(logo);
    }
    if (ver_tex) {
        rnd::DeleteTexture(ver_tex);
    }
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

// Lazily-loaded, LRU-bounded texture cache for library cover art (see
// boxart.h). Unlike g_console_icons this can't preload everything -- a big
// library can hold hundreds of distinct titles, and each cover is a full
// decoded texture -- so it's capped well below anything that would pressure
// the Switch's GPU memory, evicting the least-recently-used entry once full.
// Keyed by title only (not console), matching boxart.c's own cache key: two
// consoles sharing an exact title share one resolved cover and one texture.
static const int kBoxArtCacheCap = 48;
static std::vector<std::pair<std::string, pu::sdl2::Texture>> g_boxart_cache; // MRU-front

// Games only, extension + region/revision tag stripped -- the search term
// sent to SteamGridDB and the key its result is cached under (see boxart.h).
// Same tag-splitting rule as onegr_base_title but case-preserved: this is a
// user-facing query and report label, not a fold-case dedup key. Declared up
// here (rather than by BoxArtScanThread, its main caller) so GotoInstalled's
// row-icon lookup, defined earlier in the file, can share the exact same key.
static std::string boxart_query_title(const std::string &name) {
    size_t paren = name.find(" (");
    return (paren != std::string::npos) ? name.substr(0, paren)
                                        : name.substr(0, name.find_last_of('.'));
}

// Pending-title queue for the auto-fetch feature (Appearance -> "Auto-Fetch
// New Art", g_prefs.box_art_auto_fetch): boxart_auto_on_landed below pushes
// into this from whichever download/extract worker thread just landed an
// item, and MainApplication::BoxArtAutoThread (its own background thread,
// UI-thread-started -- see BoxArtAutoPoll) pops from it one title at a time.
// A plain mutex, not std::atomic, since the payload is a string, not a flag;
// mirrors queue.c's own Mutex-guarded work queues rather than introducing
// std::mutex as a second convention for the same job.
static Mutex g_boxart_auto_mtx;
static std::vector<std::string> g_boxart_auto_pending;

// Bridge from queue.c's plain-C queue_on_landed hook into the pending queue
// above. Must return fast and must not touch app state that belongs to the UI
// thread (g_prefs, g_creds, this->boxart's index) -- it runs on a download or
// extract worker thread, immediately before the item is marked Q_DONE, so
// anything slower than a lock+push here delays that worker picking up its
// next queued item. All of the actual decision-making (is the feature on, is
// a key set, is a manual scan using the index right now) happens later, on
// the UI thread, in BoxArtAutoPoll.
extern "C" void boxart_auto_on_landed(const char *name, const char *path,
                                      bool is_dir) {
    (void)path;
    (void)is_dir;
    if (!name || !name[0]) {
        return;
    }
    std::string title = boxart_query_title(name);
    if (title.empty()) {
        return;
    }
    mutexLock(&g_boxart_auto_mtx);
    g_boxart_auto_pending.push_back(title);
    mutexUnlock(&g_boxart_auto_mtx);
}

static pu::sdl2::Texture boxart_icon_for(const std::string &title) {
    if (title.empty()) {
        return nullptr;
    }
    for (size_t i = 0; i < g_boxart_cache.size(); i++) {
        if (g_boxart_cache[i].first == title) {
            auto tex = g_boxart_cache[i].second;
            if (i != 0) { // bump to front (most-recently-used)
                g_boxart_cache.erase(g_boxart_cache.begin() + i);
                g_boxart_cache.insert(g_boxart_cache.begin(), {title, tex});
            }
            return tex;
        }
    }
    char path[768];
    if (!boxart_lookup(title.c_str(), path, sizeof(path))) {
        return nullptr; // never scanned, or scanned with no match
    }
    auto tex = pu::ui::render::LoadImageFromFile(path);
    if (!tex) {
        return nullptr; // cached path is stale (file removed out-of-band)
    }
    if ((int)g_boxart_cache.size() >= kBoxArtCacheCap) {
        pu::ui::render::DeleteTexture(g_boxart_cache.back().second);
        g_boxart_cache.pop_back();
    }
    g_boxart_cache.insert(g_boxart_cache.begin(), {title, tex});
    return tex;
}

// Drop a title's runtime texture (Manage Box Art's delete action): boxart.c
// only owns the file on disk, so without this the Library list and this
// cache would keep showing the deleted cover until the app restarted.
static void boxart_cache_forget(const std::string &title) {
    for (size_t i = 0; i < g_boxart_cache.size(); i++) {
        if (g_boxart_cache[i].first == title) {
            pu::ui::render::DeleteTexture(g_boxart_cache[i].second);
            g_boxart_cache.erase(g_boxart_cache.begin() + i);
            return;
        }
    }
}

// Drop every runtime cover texture (Art Cache's "Clear all"): same reasoning
// as boxart_cache_forget above, just for the whole cache at once rather than
// one title.
static void boxart_cache_forget_all() {
    for (auto &kv : g_boxart_cache) {
        pu::ui::render::DeleteTexture(kv.second);
    }
    g_boxart_cache.clear();
}

// Same idea, but only the "console:<target>" entries (Scan Console Art's
// "Reset All to Default") -- game covers are untouched.
static void boxart_cache_forget_consoles() {
    for (size_t i = 0; i < g_boxart_cache.size();) {
        if (!strncmp(g_boxart_cache[i].first.c_str(), "console:", 8)) {
            pu::ui::render::DeleteTexture(g_boxart_cache[i].second);
            g_boxart_cache.erase(g_boxart_cache.begin() + i);
        } else {
            i++;
        }
    }
}

// Mirror of boxart_cache_forget_consoles for the other direction (Scan Box
// Art's own "Reset All to Default"): drops every entry that ISN'T a
// "console:<target>" key, leaving console art untouched.
static void boxart_cache_forget_games() {
    for (size_t i = 0; i < g_boxart_cache.size();) {
        if (strncmp(g_boxart_cache[i].first.c_str(), "console:", 8)) {
            pu::ui::render::DeleteTexture(g_boxart_cache[i].second);
            g_boxart_cache.erase(g_boxart_cache.begin() + i);
        } else {
            i++;
        }
    }
}

// Below this score (see ba_name_score in boxart.c: 100 exact, 60 substring
// containment, 10-50 scaled token overlap, 0 no name info at all) a fresh
// auto-pick is more guess than match -- worth flagging in the results list
// rather than silently trusting it the way a plain "Found" implied before.
static const int kLowConfidenceScore = 60;

// Memory-only check for whether `title` has a cached cover, with the already-
// decoded texture if one happens to be warm in the runtime cache. Never opens
// or decodes a file (boxart_lookup is a scan of the already-loaded in-memory
// index -- see boxart.c -- so this is cheap even called once per row).
// Returns true when a cover exists on disk; *out is null when it hasn't been
// decoded into a texture yet, which is the caller's cue to queue it for
// BoxArtIconsPoll instead of decoding right here in the middle of a list
// build. This split is what keeps GotoInstalled fast on a large library: a
// row's icon used to be a full PNG decode at AddRow2 time regardless of
// whether that row was ever scrolled into view, which thrashed the
// texture cache's 48-slot budget every time a folder with more than 48
// distinct titles was opened.
static bool boxart_row_icon(const std::string &title, pu::sdl2::Texture *out) {
    *out = nullptr;
    if (title.empty() || !g_prefs.box_art_enabled) {
        return false;
    }
    for (size_t i = 0; i < g_boxart_cache.size(); i++) {
        if (g_boxart_cache[i].first == title) {
            *out = g_boxart_cache[i].second;
            return true;
        }
    }
    char path[768];
    return boxart_lookup(title.c_str(), path, sizeof(path));
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

// Accent color presets: each pairs a "primary" (the original logo-green
// role) and "secondary" (the original tab-underline-blue role) color,
// theme-adjusted the same way the original hardcoded green/blue were --
// deeper/saturated on the light theme for contrast, brighter on dark. Every
// ring, glow, progress bar and pulse dot in the app routes through
// accent_green()/accent_blue() below, so switching the preset recolors all
// of it from this one table -- see Settings > Appearance > Accent Color
// (GotoAccent).
struct AccentPreset {
    const char *key;   // stored in g_prefs.accent
    int name_str;       // i18n key for the picker's display name
    pu::ui::Color light_a, dark_a; // primary (was the hardcoded accent_green)
    pu::ui::Color light_b, dark_b; // secondary (was the hardcoded accent_blue)
};
static const AccentPreset g_accents[] = {
    // Signature (default): the app's original green/blue, unchanged -- so
    // existing installs look exactly the same until a user opts into another.
    {"signature", S_ACCENT_SIGNATURE,
     pu::ui::Color(30, 124, 54, 255),  pu::ui::Color(146, 214, 36, 255),
     pu::ui::Color(30, 100, 190, 255), pu::ui::Color(90, 160, 245, 255)},
    {"violet", S_ACCENT_VIOLET,
     pu::ui::Color(120, 60, 180, 255), pu::ui::Color(190, 140, 255, 255),
     pu::ui::Color(170, 50, 130, 255), pu::ui::Color(255, 140, 210, 255)},
    {"ember", S_ACCENT_EMBER,
     pu::ui::Color(180, 110, 20, 255), pu::ui::Color(245, 175, 95, 255),
     pu::ui::Color(180, 60, 40, 255),  pu::ui::Color(255, 130, 90, 255)},
    {"aqua", S_ACCENT_AQUA,
     pu::ui::Color(20, 130, 140, 255), pu::ui::Color(80, 220, 230, 255),
     pu::ui::Color(20, 110, 110, 255), pu::ui::Color(90, 200, 190, 255)},
    {"rose", S_ACCENT_ROSE,
     pu::ui::Color(170, 50, 110, 255), pu::ui::Color(255, 130, 180, 255),
     pu::ui::Color(170, 40, 60, 255),  pu::ui::Color(255, 110, 120, 255)},
    {"slate", S_ACCENT_SLATE,
     pu::ui::Color(70, 90, 110, 255),  pu::ui::Color(160, 180, 200, 255),
     pu::ui::Color(50, 90, 140, 255),  pu::ui::Color(130, 170, 220, 255)},
};
static const int g_accent_count = sizeof(g_accents) / sizeof(g_accents[0]);

// Index of g_prefs.accent in g_accents, defaulting to 0 ("signature") for an
// empty or unrecognized preference -- covers both a fresh install and an old
// config.json saved before this field existed.
static int accent_preset_index() {
    for (int i = 0; i < g_accent_count; i++) {
        if (strcmp(g_accents[i].key, g_prefs.accent) == 0) {
            return i;
        }
    }
    return 0;
}

// Primary accent (was the hardcoded "logo green"), theme-adjusted: bright on
// the dark theme, deeper on light so it keeps contrast on light rows.
// Sourced from the selected Appearance > Accent Color preset.
static pu::ui::Color accent_green() {
    const AccentPreset &p = g_accents[accent_preset_index()];
    return is_light_theme() ? p.light_a : p.dark_a;
}

// Secondary accent (was the hardcoded "logo blue"/tab underline),
// theme-adjusted the same way as accent_green(). Sourced from the same
// preset.
static pu::ui::Color accent_blue() {
    const AccentPreset &p = g_accents[accent_preset_index()];
    return is_light_theme() ? p.light_b : p.dark_b;
}

// Danger red, theme-adjusted the same way accent_green() is: deeper/more
// saturated on the light theme so it keeps contrast on the light dialog
// panel/row backgrounds, brighter on dark. Every "destructive" or "failed"
// accent in the app should route through this instead of a raw literal, so
// none of them silently lose contrast on the light theme the way green
// used to before it got the same treatment.
static pu::ui::Color warn_red() {
    return is_light_theme() ? pu::ui::Color(180, 60, 60, 255)
                            : pu::ui::Color(235, 120, 120, 255);
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
        {"wiiu", "Nintendo Wii U"},
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

// Row/card icon for the console at `target` (its target folder or display
// name -- config_find_console matches either): its chosen SteamGridDB cover
// if it opted in (ConsoleGroup::use_boxart, set via ConsoleArtMenu) and one
// is already cached, else the same built-in square icon console_icon always
// returned. Synchronous decode via boxart_icon_for is fine here -- unlike the
// per-game path (BoxArtIconsPoll's lazy queue, needed because a library can
// hold hundreds of distinct titles cycling through the 48-slot texture
// cache), the console list is capped at MAX_CONSOLES and only the handful a
// user actually opts into ever get decoded, so they simply stay warm for the
// app's whole run once loaded. Falls straight through to console_icon for any
// key that isn't a live console (a settings card, "default", a game/file
// name) -- safe to call anywhere console_icon was called before.
//
// `is_art`, if given, is set true only when the returned texture is a real
// SteamGridDB cover (as opposed to the built-in square console_icon/badge) --
// callers feeding a poster-mode AddCard/SetCardIcon need this to pick the
// aspect-preserving cover path over the centred-square icon path (see
// CardGrid's Card::art); a plain console_icon lookup can ignore it.
//
// `allow_boxart` (default true): pass false to force the built-in stock icon
// even when the console opted into custom box art. The download queue uses
// this -- a queue card is a small, uniform-grid status tile, not a library
// browse card, so it deliberately stays on the stock icon set for every
// console and only ever shows a custom image for the one non-console entry,
// the HaulNX self-update badge (handled below, unaffected by this flag).
//
// Also gated on g_prefs.box_art_enabled (Settings > Appearance): that's the
// global "show box art" switch boxart_row_icon already honors for per-game
// covers, so turning it off has to fall every console row back to its stock
// icon the same way, not just leave whichever cover a console had already
// opted into (via ConsoleArtMenu) showing regardless.
static pu::sdl2::Texture console_display_icon(const char *target,
                                              bool *is_art = nullptr,
                                              bool allow_boxart = true) {
    if (is_art) {
        *is_art = false;
    }
    // The self-update Queue entry (see UpdStart) carries the literal target
    // "HaulNX" -- never a real console key -- so it can be told apart here
    // and shown the app's own badge instead of falling through to the
    // generic "default" folder icon.
    if (target && !strcmp(target, "HaulNX") && g_header_logo) {
        return g_header_logo;
    }
    if (allow_boxart && g_prefs.box_art_enabled && target && target[0]) {
        ConsoleGroup *g = config_find_console(&g_cfg, target);
        if (g && g->use_boxart) {
            pu::sdl2::Texture tex =
                boxart_icon_for(std::string("console:") + g->target);
            if (tex) {
                if (is_art) {
                    *is_art = true;
                }
                return tex;
            }
        }
    }
    return console_icon(target);
}

static void style_dialog(pu::ui::Dialog::Ref &d) {
    d->SetDialogColor(g_theme->dialog_bg);
    d->SetTitleColor(g_theme->dialog_title);
    d->SetContentColor(g_theme->dialog_body);
    d->SetOptionColor(g_theme->dialog_opt);
    // Was g_theme->dialog_over, a fixed blue literal ignoring the selected
    // accent preset -- every dialog in the app uses this style, so it was
    // the single biggest "accent didn't really apply" gap.
    d->SetOverColor(accent_blue());
}

// Destructive-action dialog: same as style_dialog but with a red title so it
// clearly reads as "danger" at a glance.
static void style_dialog_danger(pu::ui::Dialog::Ref &d) {
    d->SetDialogColor(g_theme->dialog_bg);
    d->SetTitleColor(warn_red());
    d->SetContentColor(g_theme->dialog_body);
    d->SetOptionColor(g_theme->dialog_opt);
    d->SetOverColor(accent_blue());
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

// Status verb for a queue card/row. An external transfer (self-update, DAT sync,
// PC->Switch receive) shows a kind-specific verb while in progress; its terminal
// and every real-download state fall through to the phase code above.
static const char *xfer_verb(const QueueItem *it) {
    if (it->external && it->status == Q_DOWNLOADING) {
        switch (it->xkind) {
        case 1: return "recv";
        case 2: return "updt";
        case 3: return "sync";
        case 4: return "unzip";
        default: break;
        }
    }
    return qstatus(it->status);
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

// Card/row tint for a queue item, mirroring xfer_verb's phase text: once
// queue_ext_set_kind has flagged an external item "extracting" (kind 4), give
// it the same amber a real download's vrfy/unzip phase gets, instead of the
// plain "downloading" tint it started with.
static pu::ui::Color xfer_color(const QueueItem *it) {
    if (it->external && it->xkind == 4 && it->status == Q_DOWNLOADING) {
        return qstatus_color(Q_EXTRACTING);
    }
    return qstatus_color(it->status);
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
    // Nothing is starting because the card is nearly full. Say so here rather
    // than letting the queue look mysteriously stalled — no item has failed and
    // the hold lifts by itself once space is freed.
    if (queue_space_hold()) {
        s += (s.empty() ? "" : " · ") + std::string(tr(S_SPACE_HOLD));
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

// How many rows the last rebuild flagged as updatable, kept so the info line can
// be recomposed (on every selection change) without redoing the whole list.
static int g_files_updates = 0;

// Persistent indicator under the file list: selection, available updates, a
// non-default sort and/or an active filter. The toast announcing sort/filter
// vanishes, and an active filter is otherwise invisible ("where did my files
// go?"). Cheap enough to call on every A/Y press — it touches no SD card.
static void files_info_line(MainLayout *lay) {
    std::string info;
    // Selection first: while files are marked it's what you're acting on, and
    // its running byte total is the whole point of selecting before queueing.
    if (!g_sel.empty()) {
        uint64_t bytes = 0;
        for (int fi : g_sel) {
            if (fi >= 0 && fi < g_item.file_count) {
                bytes += g_item.files[fi].size;
            }
        }
        char sb[96];
        snprintf(sb, sizeof(sb), tr(S_N_SELECTED), (int)g_sel.size());
        info = std::string(sb) + "  ·  " + human_size(bytes);
    }
    if (g_files_updates > 0) {
        char ub[80]; // roomy: some localized forms are multi-byte
        snprintf(ub, sizeof(ub), tr(S_UPDATES_AVAIL), g_files_updates);
        if (!info.empty()) {
            info += "  ·  ";
        }
        info += ub;
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
    // A repo whose URL override points into a subfolder of the item (rather
    // than the item root) only browses files under that subfolder, matching
    // what ia_file_url() actually downloads them as -- instead of showing
    // every file across the whole (possibly multi-console) item.
    char subfolder[512];
    bool scoped = ia_item_subfolder(&g_item, subfolder, sizeof(subfolder));
    size_t sub_len = scoped ? strlen(subfolder) : 0;
    for (int i = 0; i < g_item.file_count; i++) {
        // Hide sidecar/metadata files (.torrent, .xml, ...) per the Settings >
        // UI extension filter, so they never show as downloadable ROMs.
        if (prefs_ext_hidden(&g_prefs, g_item.files[i].name)) {
            continue;
        }
        if (scoped &&
            !(strncmp(g_item.files[i].name, subfolder, sub_len) == 0 &&
              g_item.files[i].name[sub_len] == '/')) {
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
        // Display-only: within a scoped subfolder, show names relative to it
        // instead of repeating the same folder path on every row. Lookups
        // above (install index, md5) and the URL builder still use the full
        // f->name, so this has no effect beyond the row label.
        const char *disp = f->name;
        if (scoped && strncmp(disp, subfolder, sub_len) == 0 &&
            disp[sub_len] == '/') {
            disp += sub_len + 1;
        }
        char name[540];
        snprintf(name, sizeof(name), "%s%s",
                 mark == 2 ? "↑ " : mark == 1 ? "* " : "", disp);
        lay->AddRow2(name, human_size(f->size),
                     g_theme->row_text, size_color(f->size));
        // Re-apply the selection: ClearMenu() wiped the widget's row marks, but
        // g_sel is keyed to the file, so a row that reappears under a different
        // filter or sort comes back still selected.
        if (g_sel.count(g_files[k])) {
            lay->SetMark(k, true);
        }
    }
    if (g_files.empty()) {
        lay->AddRow(tr(S_NO_FILES_MATCH));
    }
    g_files_updates = updates;
    files_info_line(lay);
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
    g_sel.clear(); // a new repo's file indices mean nothing to the old selection
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
    g_sel.clear(); // a new repo's file indices mean nothing to the old selection
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
    int imm; // immediate entry count — the cheap cache key
    uint64_t size;
    /* Rows the Installed browser shows inside: a multi-file game (a .cue and
     * its .bin tracks) counts once, not once per piece. This is what the
     * console chips display while grouping is on. -1 = not computed yet, so
     * turning the setting on recomputes instead of showing a stale number. */
    int games;
};
static std::map<std::string, InstStat> g_inst_stat;
static bool g_inst_stat_loaded = false;
static bool g_inst_stat_dirty = false;

// INST_SIZES_PATH is defined in config.h alongside the other app paths.

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
                    int gi = json_obj_get(body, tok, child, "g");
                    s.games = gi >= 0 ? (int)json_u64(body, tok, gi) : -1;
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
    fs_mkdir_p(DATA_DIR);
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
        fprintf(f, ",\"m\":%llu,\"n\":%d,\"s\":%llu,\"g\":%d}",
                (unsigned long long)kv.second.mtime, kv.second.imm,
                (unsigned long long)kv.second.size, kv.second.games);
    }
    fputc(']', f);
    fclose(f);
    g_inst_stat_dirty = false;
}

// A disc dump is usually several files that are collectively one game: a .cue
// beside 25 .bin tracks, or an .m3u naming one .cue per disc. The Installed
// browser collapses each set into one row (see inst_detect_groups further
// down); WriteInventoryJson uses the same struct to tell the desktop
// companion which files are one game, so a delete/move from there doesn't
// orphan the rest of a set. Declared up here, ahead of both users.
struct InstGroup {
    std::string name;                 // row label: the set's title
    std::vector<std::string> members; // the real filenames it collapses
    uint64_t size = 0;                // their total
};
static std::vector<InstGroup> inst_detect_groups(const std::string &dir,
                                                 const std::vector<DirEnt> &ents);

// Rows the Installed browser would show inside a folder, collapsing each
// multi-file game to one. Defined with the grouping code further down.
static int inst_group_row_count(const std::string &path);

// `raw_out`, when non-null, always gets the folder's plain file/subfolder
// count regardless of grouping — WriteInventoryJson wants that alongside the
// (possibly grouped) `count` for the companion, and used to get it by calling
// count_dir_entries() a second time right after this returned; passing it
// through here instead means a cache hit costs one stat(), not two readdirs.
static void inst_dir_stats(const std::string &path, int *count,
                           uint64_t *size, int *raw_out = nullptr) {
    struct stat ds;
    time_t mt = (stat(path.c_str(), &ds) == 0) ? ds.st_mtime : 0;
    auto it = g_inst_stat.find(path);
    // A cached entry from before grouping was on carries no game count, so it
    // misses here and gets recomputed rather than reporting raw files.
    //
    // Validated by mtime alone: this used to also re-run count_dir_entries()
    // (a full readdir) on every single call just to cross-check `imm`, even
    // on a cache hit -- so the "cheap check" guarding the expensive recursive
    // walk cost almost as much as a lighter recompute would have. Every
    // console-folder row on the Library tab's root view calls this, so that
    // readdir ran per console on every switch to that tab. `imm` is still
    // recorded below (unrelated readers may want the raw count later), just
    // no longer re-verified on a read -- a rename that swaps same-size files
    // without changing mtime was already the one gap the old comment noted
    // ("a same-size rename leaves a stale-but-correct size"), so this trades
    // nothing new away.
    if (it != g_inst_stat.end() && it->second.mtime == mt &&
        (!g_prefs.group_sets || it->second.games >= 0)) {
        *count = g_prefs.group_sets ? it->second.games : it->second.imm;
        *size = it->second.size;
        if (raw_out) *raw_out = it->second.imm;
        return;
    }
    int imm = count_dir_entries(path);
    uint64_t sz = dir_total_size(path);
    // Counting games means listing the folder, which is cheap next to the
    // recursive size walk we just did — and both are cached behind the same
    // mtime key, so neither runs again until the folder actually changes.
    int games = g_prefs.group_sets ? inst_group_row_count(path) : -1;
    g_inst_stat[path] = {mt, imm, sz, games};
    if (raw_out) *raw_out = imm;
    g_inst_stat_dirty = true; // a folder was (re)walked: persist on exit
    *count = games >= 0 ? games : imm;
    *size = sz;
}

// Home-tab console chips need a rough game/file count for every shown
// console on every visit — including a plain L/R tab cycle, not just an
// explicit Browse-tab open. The count used to be recomputed from scratch
// every time (a readdir of the whole console folder at minimum, a
// stat-per-file grouping pass at worst — see inst_group_row_count), which
// made switching to Browse laggy on any console folder with more than a
// couple hundred ROMs. Cached here by mtime alone: unlike g_inst_stat (whose
// size chip is authoritative once the Library tab has visited a folder,
// hence the extra readdir cross-check there), a stale chip count for a few
// seconds right after an import is purely cosmetic — it catches up the next
// time this console is visited — so trusting the single cheap stat() to mean
// "nothing changed" and skipping the readdir entirely is a safe trade. Kept
// in its own map rather than reusing g_inst_stat so this mtime-only refresh
// can never leave that cache's size/games stale under a matching key. Also
// keyed on the current grouping pref so toggling it in Settings (Appearance)
// is reflected on the very next visit instead of showing a stale count
// computed under the old mode.
struct HomeCount {
    time_t mtime;
    bool grouped;
    int n;
};
static std::map<std::string, HomeCount> g_home_count_cache;
static int inst_home_count(const std::string &path) {
    struct stat ds;
    time_t mt = (stat(path.c_str(), &ds) == 0) ? ds.st_mtime : 0;
    bool grouped = g_prefs.group_sets;
    auto it = g_home_count_cache.find(path);
    if (it != g_home_count_cache.end() && it->second.mtime == mt &&
        it->second.grouped == grouped) {
        return it->second.n;
    }
    int n = grouped ? inst_group_row_count(path) : count_dir_entries(path);
    g_home_count_cache[path] = {mt, grouped, n};
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
        // Folder rows never show size/mtime here (folder totals come from the
        // separate persisted stat cache — see inst_stat_load — since a real
        // folder size needs a recursive walk, not a raw stat() on the folder
        // itself), so a directory only needs the is_dir classification.
        // libnx hands that back for free as dirent::d_type in the same
        // readdir() batch that just listed the entry, so skip stat() (its own
        // FS IPC round-trip) whenever d_type already answers it, and only pay
        // for stat() on files, which do need size + mtime. Every screen that
        // lists a folder (Library, ROM picker, Downloads, Inbox, Cache,
        // Backups, DATs, box art) goes through this, so cutting a stat() per
        // directory entry adds up across an entire console folder at once.
        if (e->d_type == DT_DIR) {
            de.is_dir = true;
        } else if (e->d_type == DT_REG) {
            struct stat st;
            if (stat(full.c_str(), &st) == 0) {
                de.size = (uint64_t)st.st_size;
                de.mtime = (uint64_t)st.st_mtime;
            }
        } else {
            struct stat st;
            if (stat(full.c_str(), &st) == 0) {
                de.is_dir = S_ISDIR(st.st_mode);
                de.size = (uint64_t)st.st_size;
                de.mtime = (uint64_t)st.st_mtime;
            }
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

// Offset of the "this is one piece of a larger dump" tag in a filename —
// "(Track 01)", "(Disc 2)", "(Side A)" … — or npos when there is none. These
// are the fixed No-Intro/Redump tokens, so a plain substring search is enough
// (the same style onegr_score uses for "(Beta"). The earliest tag wins, so the
// text in front of it is the whole game's title even for a name carrying two
// of them ("Game (USA) (Disc 1) (Track 03).bin").
//
// One definition, two users: the Installed browser groups a set into a single
// row from it, and 1G1R uses it to keep a part from ever being mistaken for a
// duplicate copy.
static size_t rom_multipart_tag(const std::string &n) {
    static const char *const tags[] = {"(Track ", "(Disc ", "(Disk ",
                                       "(Side ",  "(Tape ", "(CD "};
    size_t best = std::string::npos;
    for (const char *t : tags) {
        size_t p = n.find(t);
        if (p != std::string::npos && (best == std::string::npos || p < best))
            best = p;
    }
    return best;
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
        sp->SetColor(accent_green()); // re-colored per accent in ApplyTheme()
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

    // Soft background capsule behind the active tab's whole label (bounds set
    // per active tab in SetActiveTab). Added before the labels so it renders
    // behind the text -- unlike the thin underline pill below, this one does
    // overlap the label vertically.
    this->tab_chip = PillElement::New(0, strip_y + 6, 120, 64, 10,
                                      pu::ui::Color(accent_blue().r,
                                                    accent_blue().g,
                                                    accent_blue().b, 42));
    this->Add(this->tab_chip);

    // Accent underline beneath the active tab label (its bounds are set per
    // active tab in SetActiveTab). Added before the labels, but it now sits
    // below them rather than behind, so ordering no longer matters.
    this->tab_pill = PillElement::New(0, strip_y + 8, 120, 6, 3,
                                      accent_blue());
    this->Add(this->tab_pill);

    // Library first (front door), then Add — see the Tab enum comment.
    const char *labels[] = {tr(S_TAB_INSTALLED), tr(S_TAB_BROWSE), tr(S_TAB_QUEUE), tr(S_TAB_SETTINGS)};
    const s32 tab_y = strip_y + 16;
    const s32 seg = sw / 4;
    for (int i = 0; i < 4; i++) {
        auto tb = pu::ui::elm::TextBlock::New(0, tab_y, labels[i]);
        tb->SetColor(g_theme->tab_clr);
        tb->SetX(seg * i + (seg - tb->GetWidth()) / 2);
        this->Add(tb);
        this->tabs.push_back(tb);
    }

    // Thin, faint strip along the bottom edge of the charcoal tab shell
    // (constant in both themes — the shell stays charcoal).
    this->accent_line = PillElement::New(0, strip_y + strip_h, sw, 2, 0,
                                         pu::ui::Color(150, 155, 165, 160));
    this->Add(this->accent_line);

    // Ambient progress sliver: a touch thicker than accent_line and drawn
    // after it, so it reads as an overlay riding on top of the strip rather
    // than a second competing line. Starts hidden/zero-width; SetQueueProgress
    // drives it every frame while something is actually moving bytes.
    this->queue_progress = PillElement::New(0, strip_y + strip_h - 1, 0, 4, 0,
                                            accent_blue());
    this->queue_progress->SetVisible(false);
    this->Add(this->queue_progress);

    const s32 footer_h = 64;
    // A little breathing room under the line below the tabs before the
    // list/card content starts.
    const s32 list_y = 172;
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
    this->empty_icon->SetBreathe(true);
    this->Add(this->empty_icon);
    this->empty_text = pu::ui::elm::TextBlock::New(0, list_y + avail / 2 + 50,
                                                   "");
    this->empty_text->SetColor(g_theme->rom_info_clr);
    this->Add(this->empty_text);
    // Big accented transfer code, shown under the URL on the LAN pages so it can
    // be typed into the app utility's separate Code field (see SetEmptyState).
    this->empty_code = pu::ui::elm::TextBlock::New(0, 0, "");
    this->empty_code->SetFont(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large));
    this->empty_code->SetColor(accent_green());
    this->empty_code->SetVisible(false);
    this->Add(this->empty_code);
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
    // Accent-blue in both themes: the dot sits on the charcoal shell.
    // ApplyTheme() re-colors this on every accent change, but it's set here
    // via accent_blue() too (not the old fixed literal) so it's never wrong
    // even for the single frame before the first ApplyTheme() call.
    this->queue_dot->SetColor(accent_blue());
    this->Add(this->queue_dot);

    // "Update available" pulse on the Settings tab (positioned in
    // SetActiveTab). Same accent-blue as queue_dot — both are "there's
    // something to look at on this tab" notifications.
    this->settings_dot = PulseDotElement::New(0, 0, 6);
    this->settings_dot->SetColor(accent_blue());
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
    // Breadcrumb "›" chevrons were set once at construction and never
    // revisited (a pre-existing gap predating accent theming); refresh them
    // here too so they track both the theme and the selected accent.
    for (auto &s : this->bc_seps)
        s->SetColor(accent_green());
    this->status->SetColor(g_theme->status_clr);
    this->bat_info->SetColor(g_theme->status_clr);
    this->rom_info->SetColor(g_theme->rom_info_clr);
    this->tab_pill->SetColor(accent_blue());
    this->tab_chip->SetColor(pu::ui::Color(accent_blue().r,
                                           accent_blue().g,
                                           accent_blue().b, 42));
    // Was a hardcoded dark-theme blue literal (it happened to equal the old
    // accent_blue() dark value) -- now routed through accent_blue() so the
    // dots pick up both the light-theme deepened shade and any accent
    // preset the user picks in Appearance > Accent Color.
    this->queue_dot->SetColor(accent_blue());
    this->settings_dot->SetColor(accent_blue());
    // The ambient queue-progress sliver on the tab strip was set once at
    // construction and never revisited; recolor it here too so it follows
    // theme/accent changes the same as everything else.
    this->queue_progress->SetColor(accent_blue());
    this->empty_text->SetColor(g_theme->rom_info_clr);
    this->empty_code->SetColor(accent_green());
    this->empty_hint->SetColor(g_theme->rom_info_clr);
    this->spinner->SetColors(accent_blue(), g_theme->rom_info_clr);
    for (auto &s : this->footer_segs)
        s->SetLabelColor(g_theme->footer_clr);
    for (auto &t : this->tabs)
        t->SetColor(g_theme->tab_clr);
    // Force the next SetActiveTab (its cache would otherwise see the same
    // idx and skip re-highlighting the active tab in the new theme's color).
    this->last_tab_idx = -1;
    // Selection border/glow, marks, pins and the progress-bar success color:
    // accent blue (matches the tab underline), not the logo green.
    this->list->SetThemeColors(g_theme->tl_row_bg, g_theme->tl_row_alt,
                               g_theme->tl_focus, g_theme->tl_scroll,
                               g_theme->tl_mark,
                               accent_blue(),
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
                               g_theme->bg,
                               // Failed-transfer bar: theme-adjusted red, same
                               // helper the dialogs use, instead of a raw
                               // literal that stayed too pale on light rows.
                               warn_red(),
                               // Progress-bar gradient's far stop: the other
                               // half of the selected accent pair (was a
                               // fixed blue literal, ignoring both theme and
                               // any accent preset).
                               accent_green());
    // Card subtitle must stay readable on BOTH the card background and the
    // blue selection fill, so it gets its own shade per theme.
    this->grid->SetThemeColors(g_theme->tl_row_alt, g_theme->tl_focus,
                               g_theme->row_text,
                               is_light_theme()
                                   ? pu::ui::Color(45, 55, 75, 255)
                                   : pu::ui::Color(195, 205, 225, 255),
                               accent_blue(),
                               is_light_theme() ? pu::ui::Color(0, 0, 0, 34)
                                                : pu::ui::Color(0, 0, 0, 95),
                               // Page bg drives the grid's enter fade-in.
                               g_theme->bg,
                               // Ring track: white@20 vanishes on the light
                               // theme's pale cards, so darken it there.
                               is_light_theme()
                                   ? pu::ui::Color(0, 0, 0, 40)
                                   : pu::ui::Color(255, 255, 255, 20),
                               // Failed-transfer ring: same theme-adjusted red
                               // as the list and the dialogs.
                               warn_red(),
                               // Ring/scrollbar gradient's far stop: same
                               // accent pairing as the list view above.
                               accent_green());
}

void MainLayout::SetActiveTab(int idx) {
    if (idx < 0 || idx >= (int)this->tabs.size()) {
        return;
    }
    if (idx == this->last_tab_idx) {
        // Called every frame via SyncTab(); nothing to redo when the active
        // tab hasn't moved (see last_tab_idx's declaration for why this
        // matters -- SetColor() below is not a cheap no-op).
        return;
    }
    this->last_tab_idx = idx;
    for (int i = 0; i < (int)this->tabs.size(); i++) {
        this->tabs[i]->SetColor(i == idx
                                    ? g_theme->tab_active
                                    : g_theme->tab_clr);
    }
    // Underline + background capsule are both a fixed size, centred on the
    // tab's segment rather than sized to the label -- deliberately NOT
    // tied to this->tabs[idx]->GetWidth(), so "Queue" and "Settings" get
    // the exact same indicator instead of the mark growing/shrinking as the
    // active tab changes. Both are comfortably wider than any label at this
    // font. Sits just above the strip's accent line so the active tab
    // "owns" that segment.
    const s32 seg = 1920 / 4; // matches the tab layout in OnLoad/RefreshTabs
    const s32 seg_cx = seg * idx + seg / 2;
    const s32 pill_w = 200, chip_w = 280;
    this->tab_pill->SetBounds(seg_cx - pill_w / 2, 140, pill_w, 6);
    // Wider, taller capsule behind the whole label -- see the tab_chip
    // declaration for why this exists alongside the thin underline above.
    // Tall enough (86 to 150) to run past the underline's own bottom edge
    // (140-146), so the underline sits fully inside the capsule instead of
    // poking out below it as two separate shapes.
    this->tab_chip->SetBounds(seg_cx - chip_w / 2, 86, chip_w, 64);
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
void MainLayout::SetQueueProgress(float frac) {
    if (frac < 0.0f) {
        this->queue_progress->SetVisible(false);
        return;
    }
    if (frac > 1.0f) frac = 1.0f;
    const s32 strip_y = 80, strip_h = 70;
    const s32 sw = (s32)pu::ui::render::ScreenWidth;
    this->queue_progress->SetBounds(0, strip_y + strip_h - 1,
                                    (s32)(sw * frac), 4);
    this->queue_progress->SetVisible(true);
}
void MainLayout::SetUpdateAvailable(bool avail) {
    this->settings_dot->SetActive(avail);
}

void MainLayout::RefreshTabs() {
    const char *labels[] = {tr(S_TAB_INSTALLED), tr(S_TAB_BROWSE), tr(S_TAB_QUEUE), tr(S_TAB_SETTINGS)};
    const s32 seg = 1920 / 4;
    for (int i = 0; i < 4 && i < (int)this->tabs.size(); i++) {
        this->tabs[i]->SetText(labels[i]);
        this->tabs[i]->SetX(seg * i + (seg - this->tabs[i]->GetWidth()) / 2);
    }
    // Label widths just changed (new language), which the pulse dots are
    // parked relative to -- force SetActiveTab to recompute even if the
    // active idx itself didn't change.
    this->last_tab_idx = -1;
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
// Map a footer hint glyph to the button it stands for. D-pad arrows are left
// out: a tap can't say which direction, and the tab strip already handles that.
static u64 footer_token_button(const std::string &t) {
    if (t == "A") return HidNpadButton_A;
    if (t == "B") return HidNpadButton_B;
    if (t == "X") return HidNpadButton_X;
    if (t == "Y") return HidNpadButton_Y;
    if (t == "+") return HidNpadButton_Plus;
    if (t == "-") return HidNpadButton_Minus;
    if (t == "L") return HidNpadButton_L;
    if (t == "R") return HidNpadButton_R;
    if (t == "ZL") return HidNpadButton_ZL;
    if (t == "ZR") return HidNpadButton_ZR;
    if (t == "L/R") return HidNpadButton_L | HidNpadButton_R;
    if (t == "ZL/ZR") return HidNpadButton_ZL | HidNpadButton_ZR;
    return 0;
}
u64 MainLayout::FooterButtonAt(s32 tx, s32 ty) const {
    const s32 footer_h = 64;
    const s32 top = (s32)pu::ui::render::ScreenHeight - footer_h;
    if (ty < top) return 0;
    for (const auto &s : this->footer_segs) {
        s32 x = s->GetX(), w = s->GetWidth();
        if (w > 0 && tx >= x && tx < x + w) return footer_token_button(s->Token());
    }
    return 0;
}
void MainLayout::ClearMenu(bool fade) {
    // Whole-list/grid fade-in was previously removed outright (stuttered
    // under download load: re-rendering everything for ~8 frames competes
    // with real I/O). Re-enabled, but only while nothing is actually moving
    // bytes — queue_io_active() excludes merely-queued/paused items, so a
    // populated-but-idle queue still gets the fade.
    const bool animate = fade && !queue_io_active();
    this->list->Clear(animate);
    this->grid->Clear(animate);
    this->cards_mode = false; // card screens opt back in after ClearMenu
    this->rom_info->SetText("");
    this->ClearEmptyState();
    this->HideSpinner();
}
// Defined further down (near the SideMenu helpers); the empty-state hint uses it
// to wrap prose to a reading column.
static std::string wrap_to_px(const std::string &font, const std::string &text,
                              s32 max_px);
void MainLayout::SetEmptyState(pu::sdl2::Texture icon, const std::string &msg,
                               const std::string &hint, bool spacious,
                               const std::string &note,
                               const std::string &code) {
    this->empty_icon->SetTexture(icon); // pointer store, cheap every frame
    const bool has_code = !code.empty();

    // Two layouts share these three elements. Ordinary empty lists use a
    // compact centred block; the LAN-import page uses a roomier "instruction
    // sheet" — icon lifted, a gap below the address, and larger step text — so
    // it reads like directions rather than an empty-state notice. Positions are
    // reset on every call so switching screens restores the right layout.
    const s32 list_y = 172, footer_h = 64;
    // The spacious "instruction sheet" is bottom-heavy — tall step text plus an
    // optional accent chip (e.g. the "push from the app utility" note) — so
    // anchor it higher than dead-centre; otherwise the chip runs into the footer
    // bar. The compact empty-state stays centred.
    const s32 cy = list_y +
                   ((s32)pu::ui::render::ScreenHeight - list_y - footer_h) / 2 -
                   (spacious ? 90 : 0);
    this->empty_icon->SetPos(this->empty_icon->GetX(),
                             cy - (spacious ? 210 : 150));
    this->empty_text->SetY(cy + (spacious ? -5 : 50));
    // A transfer code (LAN pages) sits big between the URL and the steps, which
    // then drop to make room. Prefixed with a label so it reads as the code the
    // app utility asks for, not part of the address.
    if (has_code) {
        std::string cline = std::string(tr(S_TRANSFER_CODE)) + "   " + code;
        if (this->empty_code->GetText() != cline) {
            this->empty_code->SetText(cline);
            this->empty_code->SetX((s32)pu::ui::render::ScreenWidth / 2 -
                                   this->empty_code->GetWidth() / 2);
        }
        this->empty_code->SetY(cy + (spacious ? 55 : 95));
        this->empty_code->SetVisible(true);
    } else {
        this->empty_code->SetVisible(false);
    }
    this->empty_hint->SetY(cy + (has_code ? 175 : (spacious ? 100 : 98)));
    const std::string hint_font = pu::ui::GetDefaultFont(
        spacious ? pu::ui::DefaultFontSize::MediumLarge
                 : pu::ui::DefaultFontSize::Small);

    if (this->empty_text->GetText() != msg) {
        this->empty_text->SetText(msg);
        // Centre the message under the icon.
        this->empty_text->SetX((s32)pu::ui::render::ScreenWidth / 2 -
                               this->empty_text->GetWidth() / 2);
    }
    // Plutonium's TextBlock only breaks on explicit '\n' — prose hints (e.g. the
    // DAT-receive steps) are a single long sentence that would run off both
    // edges, so word-wrap to a centred reading column first. Hints that already
    // carry '\n' (the numbered Wi-Fi steps) are preserved as hard breaks.
    // Screens that re-assert their empty state every frame would otherwise pay
    // the measuring pass and a full texture re-render each time, so both the
    // font swap and the wrap are gated on the inputs actually changing.
    if (this->empty_hint_font != hint_font || this->empty_hint_raw != hint) {
        // SetFont re-renders the texture with the text it already holds, so
        // only swap when the size actually changed; SetText below does the one
        // render that matters.
        if (this->empty_hint_font != hint_font) {
            this->empty_hint->SetFont(hint_font);
        }
        this->empty_hint_font = hint_font;
        this->empty_hint_raw = hint;
        this->empty_hint->SetText(wrap_to_px(
            hint_font, hint, (s32)pu::ui::render::ScreenWidth - 2 * 300));
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
    this->empty_code->SetVisible(false);
    this->empty_hint->SetText("");
    // The text is gone, so the "already applied" cache must go with it or the
    // next SetEmptyState with the same hint would skip re-rendering it.
    this->empty_hint_raw.clear();
    this->empty_chip->SetVisible(false);
    this->empty_chip_text->SetVisible(false);
}
void MainLayout::ShowSpinner(const std::string &msg) {
    this->spinner->Show(msg);
}
void MainLayout::HideSpinner() { this->spinner->Hide(); }
void MainLayout::SetCardsMode(bool on) { this->cards_mode = on; }
void MainLayout::AddCard(const std::string &title, const std::string &subtitle,
                         pu::sdl2::Texture icon, bool pinned, bool dim,
                         bool art) {
    this->grid->AddCard(title, subtitle, icon, pinned, dim, art);
}
void MainLayout::SetCardCols(s32 n) { this->grid->SetCols(n); }
void MainLayout::SetCardPoster(bool on) { this->grid->SetPoster(on); }
void MainLayout::SetCardIcon(s32 i, pu::sdl2::Texture icon) {
    this->grid->SetCardIcon(i, icon);
}
s32 MainLayout::CardFirstVisible() { return this->grid->FirstVisibleCard(); }
s32 MainLayout::CardVisibleCount() { return this->grid->VisibleCardCount(); }
void MainLayout::SetSingleCard(bool on) { this->grid->SetSingle(on); }
void MainLayout::SetQueueCount(s32 n) { this->grid->SetQueueCount(n); }
void MainLayout::SetQueueCard(s32 i, const std::string &console,
                              pu::sdl2::Texture icon,
                              const std::string &status, pu::ui::Color st_clr,
                              const std::string &size, const std::string &speed,
                              const std::string &eta, const std::string &file,
                              float prog, bool hero, s32 ring, s32 qpos,
                              bool refresh_text, bool logo_icon, bool art) {
    this->grid->SetQueueCard(i, console, icon, status, st_clr, size, speed,
                             eta, file, prog, hero, ring, qpos, refresh_text,
                             logo_icon, art);
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
void MainLayout::SetRowIcon(s32 i, pu::sdl2::Texture icon) {
    this->list->SetRowIcon(i, icon);
}
s32 MainLayout::ScrollTop() { return this->list->ScrollTop(); }
s32 MainLayout::RowsVisible() { return this->list->RowsVisible(); }
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
// Marks route to whichever element is actually showing selection - the card
// grid needs its own set for the Installed poster view's Y multi-select
// (blue border), rather than always hitting the (empty, hidden) list.
void MainLayout::ToggleMark(s32 i) {
    if (this->cards_mode) this->grid->ToggleMark(i);
    else this->list->ToggleMark(i);
}
void MainLayout::SetMark(s32 i, bool on) {
    if (this->cards_mode) this->grid->SetMark(i, on);
    else this->list->SetMark(i, on);
}
int MainLayout::MarkedCount() {
    return this->cards_mode ? this->grid->MarkedCount() : this->list->MarkedCount();
}
const std::set<s32> &MainLayout::Marked() {
    return this->cards_mode ? this->grid->Marked() : this->list->Marked();
}
void MainLayout::ClearMarks() {
    this->grid->ClearMarks();
    this->list->ClearMarks();
}

// ---- app: feedback --------------------------------------------------------
void MainApplication::Toast(const std::string &msg) {
    auto tb = pu::ui::elm::TextBlock::New(0, 0, msg);
    tb->SetColor(pu::ui::Color(255, 255, 255, 255));
    // Was a fixed blue literal, independent of the selected accent preset --
    // this is the toast every "Queued: X" / settings-saved confirmation shows
    // app-wide, so a hardcoded color here reads as "the accent didn't really
    // apply" no matter which screen it pops up on.
    pu::ui::Color ac = accent_blue();
    auto t = pu::ui::extras::Toast::New(tb, pu::ui::Color(ac.r, ac.g, ac.b, 240));
    this->StartOverlayWithTimeout(t, 1200);
}

void MainApplication::ToastErr(const std::string &msg) {
    auto tb = pu::ui::elm::TextBlock::New(0, 0, msg);
    tb->SetColor(pu::ui::Color(255, 255, 255, 255));
    auto t = pu::ui::extras::Toast::New(tb, pu::ui::Color(160, 52, 52, 240));
    this->StartOverlayWithTimeout(t, 1500);
}

bool MainApplication::Confirm(const std::string &title, const std::string &msg,
                              bool yes_default) {
    // Default-highlight the safe option ("Cancel" first) so a stray A press
    // doesn't act. Callers where "yes" is the expected answer (e.g. confirming
    // a cancel the user just asked for) pass yes_default to put "Yes" first.
    // Either way B dismisses the dialog as "no".
    if (yes_default) {
        int r = this->CreateShowDialog(title, msg, {tr(S_YES), tr(S_CANCEL)},
                                       false, {}, style_dialog);
        return r == 0;
    }
    int r = this->CreateShowDialog(title, msg, {tr(S_CANCEL), tr(S_YES)}, false,
                                   {}, style_dialog);
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
    // Route straight to SideMenu with the danger accent (the CreateShowDialog
    // shim would otherwise drop the red styling).
    return this->SideMenu(title, {tr(S_CANCEL), tr(S_YES)}, 0, m, true) == 1;
}

// App-wide funnel: every centered CreateShowDialog now presents as the slide-out
// panel. icon/prepare_cb are intentionally ignored — the panel styles itself.
// use_last_opt_as_cancel is preserved: base callers expect a cancel (B) to come
// back as the last option's index, not -1.
s32 MainApplication::CreateShowDialog(const std::string &title,
                                      const std::string &content,
                                      const std::vector<std::string> &opts,
                                      bool use_last_opt_as_cancel,
                                      pu::sdl2::TextureHandle::Ref icon,
                                      pu::ui::Application::DialogPrepareCallback
                                          prepare_cb) {
    (void)icon;
    (void)prepare_cb;
    s32 r = this->SideMenu(title, opts, 0, content);
    if (r < 0 && use_last_opt_as_cancel && !opts.empty())
        return (s32)opts.size() - 1;
    return r;
}

// Plutonium's Dialog packs its options horizontally and wraps them into an
// unreadable grid once there are more than three, so long action menus (the
// per-console X-menu, the sort picker) get their own right-side panel instead:
// a vertical list that slides in from the screen edge. D-pad up/down moves, A
// confirms the highlighted row, B or + closes. Blocking, mirroring
// CreateShowDialog: returns the chosen index, or -1 if cancelled. Drives the
// same CallForRenderWithRenderOver loop Dialog::Show uses.
// Word-wrap `text` to fit within max_px using `font`, inserting '\n' at word
// boundaries and honouring any '\n' already present as a hard break. Plutonium's
// RenderText only *truncates* at max_width (appending "..."); it never wraps, so
// multi-line body text has to be broken up here before rendering.
static std::string wrap_to_px(const std::string &font, const std::string &text,
                              s32 max_px) {
    namespace rnd = pu::ui::render;
    std::string out, line, word;
    auto flush_word = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (line.empty() || rnd::GetTextWidth(font, cand) <= max_px) {
            line = cand;
        } else {
            out += line;
            out += '\n';
            line = word;
        }
        word.clear();
    };
    for (size_t i = 0; i <= text.size(); i++) {
        const char c = (i < text.size()) ? text[i] : '\0';
        if (c == ' ' || c == '\n' || c == '\0') {
            flush_word();
            if (c == '\n') {
                out += line;
                out += '\n';
                line.clear();
            }
        } else {
            word += c;
        }
    }
    out += line;
    return out;
}

s32 MainApplication::SideMenu(const std::string &title,
                              const std::vector<std::string> &opts, s32 sel,
                              const std::string &body, bool danger,
                              bool from_left, pu::sdl2::Texture icon,
                              SideMenuLive *live, u64 switch_btn,
                              pu::sdl2::Texture backdrop) {
    namespace rnd = pu::ui::render;
    // Destructive confirmations get a red title and highlight pill; everything
    // else uses the theme's dialog colors.
    const pu::ui::Color title_clr = danger ? warn_red() : g_theme->dialog_title;
    // Was g_theme->dialog_over (a fixed blue literal) for the non-danger case
    // -- edge_clr right below already followed the accent, so the fill and
    // the outline of the same selection pill were visibly different colors
    // whenever a non-default accent was chosen.
    const pu::ui::Color hi_clr =
        danger ? pu::ui::Color(150, 40, 40, 255) : accent_blue();
    // Selected-row outline/glow, matching the "lit" treatment TableList and
    // CardGrid use for their selection — SideMenu is the most-seen selection
    // state in the app (every settings toggle, every confirmation) but used
    // to be the one place selection was just a flat fill. Danger menus get
    // the same red as the title instead of blue, so the outline doesn't
    // contradict the "this is destructive" cue.
    const pu::ui::Color edge_clr = danger ? warn_red() : accent_blue();
    const s32 SW = rnd::ScreenWidth, SH = rnd::ScreenHeight;
    const s32 panel_w = 640;   // ~1/3 of the 1920px canvas
    const s32 pad = 40;
    const s32 title_y = 56;
    const s32 row_h = 76;
    const s32 row_gap = 8;     // gap between the highlight pills

    if (opts.empty()) return -1;
    if (sel < 0 || sel >= (s32)opts.size()) sel = 0;

    const std::string tfont =
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge);
    const std::string bfont =
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    const std::string ofont =
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    pu::sdl2::Texture title_tex = rnd::RenderText(tfont, title, title_clr);
    // Optional body under the title, word-wrapped to the panel width (see
    // wrap_to_px — RenderText itself only truncates). A divider then separates
    // this top section from the selectable options below it.
    const s32 body_y = 132;
    pu::sdl2::Texture body_tex =
        body.empty()
            ? nullptr
            : rnd::RenderText(bfont, wrap_to_px(bfont, body, panel_w - 2 * pad),
                              g_theme->dialog_body);
    const s32 top_bottom = body_tex
                               ? body_y + rnd::GetTextureHeight(body_tex)
                               : title_y + rnd::GetTextureHeight(title_tex);
    const s32 div_y = top_bottom + 20;
    const s32 list_top = div_y + 20;
    // How many option rows fit between the divider and the panel's bottom edge;
    // a longer list scrolls (see `top` below) instead of running off-screen.
    int visible = (SH - 40 - list_top) / row_h;
    if (visible < 1) visible = 1;
    const pu::ui::Color div_clr = pu::ui::Color(
        g_theme->dialog_body.r, g_theme->dialog_body.g, g_theme->dialog_body.b, 60);
    std::vector<pu::sdl2::Texture> opt_texs;
    opt_texs.reserve(opts.size());
    for (const auto &o : opts)
        opt_texs.push_back(rnd::RenderText(ofont, o, g_theme->dialog_opt));
    // Rendered once and reused each frame: "more above/below" scroll chevrons.
    pu::sdl2::Texture up_tex = rnd::RenderText(bfont, "▲", g_theme->dialog_body);
    pu::sdl2::Texture dn_tex = rnd::RenderText(bfont, "▼", g_theme->dialog_body);
    // Live toggle row: an ON (green) / OFF (red) badge and a bottom status line,
    // both driven by `live->state`. The footer texture is (re)built lazily via
    // foot_dirty so httpsrv_local_ip() runs on open and per toggle, not per frame.
    pu::sdl2::Texture on_tex = nullptr, off_tex = nullptr, foot_tex = nullptr;
    bool foot_dirty = live != nullptr;
    if (live) {
        on_tex = rnd::RenderText(ofont, "ON", accent_green());
        off_tex = rnd::RenderText(ofont, "OFF", warn_red());
    }
    // Hero backdrop: `backdrop`'s art faux-blurred to fill the panel, then a
    // top-to-bottom scrim fading from a light haze into the panel's own
    // background colour so the option rows stay fully legible over it. Baked
    // once per open (not per frame) — see BakeBlurredFill/BakeVGradient.
    pu::sdl2::Texture bd_blur_tex = nullptr, bd_scrim_tex = nullptr;
    if (backdrop) {
        bd_blur_tex = BakeBlurredFill(backdrop, panel_w, SH);
        bd_scrim_tex = BakeVGradient(
            SH, pu::ui::Color(0, 0, 0, 90),
            pu::ui::Color(g_theme->dialog_bg.r, g_theme->dialog_bg.g,
                         g_theme->dialog_bg.b, 235));
    }

    s32 result = -1;
    bool closing = false;
    bool tch_prev = false;
    int repeat_dir = 0; // held scroll direction: -1 up, +1 down, 0 none
    int repeat_ctr = 0; // frames until the next auto-move while held
    int top = 0;        // index of the first visible row (scroll offset)
    double anim = 0.0; // 0 = off-screen right, 1 = fully slid out
    u64 foot_poll_ns = 0; // next tick to rebuild a live footer (connection state)
    // Selection-highlight fade: restarted whenever `sel` moves, matching
    // TableList/CardGrid's sel_alpha so the glow eases in instead of
    // teleporting between rows here too.
    s32 anim_sel = sel;
    s32 sel_alpha = 255;
    while (true) {
        const bool ok = this->CallForRenderWithRenderOver(
            [&](rnd::Renderer::Ref &d) -> bool {
                // Keep a live background service (the inventory server) polled
                // while this modal owns the render loop; see SideMenuLive::tick.
                if (live && live->tick) live->tick();
                const u64 k = this->GetButtonsDown();
                const u64 held = this->GetButtonsHeld();
                const auto tst = this->GetTouchState();
                const bool tch_now = tst.count > 0;
                if (!closing) {
                    // Hold-to-repeat vertical scroll: move once on the initial
                    // press, then auto-repeat after a short delay while up/down
                    // stays held, so long lists don't need one press per row.
                    int dir = (held & HidNpadButton_AnyUp)     ? -1
                              : (held & HidNpadButton_AnyDown) ? 1
                                                               : 0;
                    if (dir == 0) {
                        repeat_dir = 0;
                    } else {
                        bool step;
                        bool initial = false;
                        if (dir != repeat_dir) {
                            repeat_dir = dir;
                            repeat_ctr = 24; // ~0.4s before repeat kicks in
                            step = true;
                            initial = true;
                        } else if (--repeat_ctr <= 0) {
                            repeat_ctr = 4; // ~15 rows/sec while held
                            step = true;
                        } else {
                            step = false;
                        }
                        if (step) {
                            // Wrap top<->bottom, but only on a fresh press so a
                            // held direction stops at the edge instead of looping.
                            const s32 n = (s32)opts.size();
                            if (dir < 0) {
                                if (sel > 0) sel--;
                                else if (initial) sel = n - 1;
                            } else {
                                if (sel < n - 1) sel++;
                                else if (initial) sel = 0;
                            }
                        }
                    }
                    if (k & HidNpadButton_A) {
                        if (live && sel == live->row) {
                            // Flip in place — the panel stays out, no slide.
                            live->state =
                                live->on_toggle ? live->on_toggle() : !live->state;
                            foot_dirty = true;
                        } else {
                            result = sel;
                            closing = true;
                        }
                    } else if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                        result = -1;
                        closing = true;
                    } else if (switch_btn && (k & switch_btn)) {
                        // Sibling-panel hotkey (X in Tools, Y in Options): close
                        // and tell the caller to flip to the other panel.
                        result = SIDEMENU_SWITCH;
                        closing = true;
                    } else if (tch_now && !tch_prev && anim >= 1.0) {
                        // Edge-triggered tap. Only accept once the panel is fully
                        // out so rows are where we hit-test them.
                        const s32 tx =
                            (s32)((double)tst.touches[0].x * rnd::ScreenFactor);
                        const s32 ty =
                            (s32)((double)tst.touches[0].y * rnd::ScreenFactor);
                        // The panel hugs one edge; a tap on the dimmed strip on
                        // the other side cancels. (For a left panel the dead
                        // zone is to its right, and vice versa.)
                        const bool outside =
                            from_left ? tx >= panel_w : tx < SW - panel_w;
                        if (outside) {
                            result = -1;
                            closing = true;
                        } else {
                            // Tap on an option row picks it (matches an A press).
                            // Only the visible window is hit-tested; rows scrolled
                            // out of view aren't on screen to be tapped.
                            for (u32 i = top;
                                 i < opts.size() && (s32)i < top + visible; i++) {
                                const s32 ry =
                                    list_top + (s32)(i - top) * row_h;
                                if (ty >= ry && ty < ry + row_h) {
                                    sel = (s32)i;
                                    if (live && sel == live->row) {
                                        live->state = live->on_toggle
                                                          ? live->on_toggle()
                                                          : !live->state;
                                        foot_dirty = true;
                                    } else {
                                        result = sel;
                                        closing = true;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
                tch_prev = tch_now;
                anim += closing ? -0.18 : 0.18;
                if (anim > 1.0) anim = 1.0;
                if (anim < 0.0) return false; // slid fully out: end the loop

                const s32 panel_x = from_left ? (s32)(panel_w * anim) - panel_w
                                              : SW - (s32)(panel_w * anim);
                // Screen dim behind the panel, fading in step with the slide.
                d->RenderRectangleFill(pu::ui::Color(0, 0, 0, (u8)(150 * anim)),
                                       0, 0, SW, SH);
                if (bd_blur_tex) {
                    // Flat fill first (covers the sliver the stretch-blit's
                    // rounding can miss at the panel edge), then the blurred
                    // art, then the scrim -- all stretched to the full panel
                    // rect each frame so they track the slide with it.
                    d->RenderRectangleFill(g_theme->dialog_bg, panel_x, 0,
                                           panel_w, SH);
                    pu::ui::render::TextureRenderOptions bo;
                    bo.width = panel_w;
                    bo.height = SH;
                    d->RenderTexture(bd_blur_tex, panel_x, 0, bo);
                    if (bd_scrim_tex) {
                        d->RenderTexture(bd_scrim_tex, panel_x, 0, bo);
                    }
                } else {
                    d->RenderRectangleFill(g_theme->dialog_bg, panel_x, 0,
                                           panel_w, SH);
                }
                // Accent bar down the panel's leading edge, in the same green as
                // the tab-header underline, so the slide-out is clearly a panel
                // over the screen rather than part of it. The leading edge is the
                // right edge for a left panel, the left edge for a right one.
                d->RenderRectangleFill(accent_blue(),
                                       from_left ? panel_x + panel_w - 6 : panel_x,
                                       0, 6, SH);
                // Optional console icon left of the title (per-console Options),
                // vertically centered on the title text; the title shifts right
                // to make room.
                s32 title_x_off = 0;
                if (icon) {
                    const s32 icon_sz = 52;
                    const s32 th = rnd::GetTextureHeight(title_tex);
                    rnd::TextureRenderOptions iopts;
                    iopts.width = icon_sz;
                    iopts.height = icon_sz;
                    d->RenderTexture(icon, panel_x + pad,
                                     title_y + (th - icon_sz) / 2, iopts);
                    title_x_off = icon_sz + 16;
                }
                d->RenderTexture(title_tex, panel_x + pad + title_x_off, title_y);
                if (body_tex)
                    d->RenderTexture(body_tex, panel_x + pad, body_y);
                // Divider between the top section and the options.
                d->RenderRectangleFill(div_clr, panel_x + pad, div_y,
                                       panel_w - 2 * pad, 2);

                // Keep the selected row inside the visible window, scrolling by
                // the minimum needed when it moves past either edge.
                if (sel < top) top = sel;
                else if (sel >= top + visible) top = sel - visible + 1;
                // Advance the selection fade (restart when the selection moved).
                if (anim_sel != sel) {
                    anim_sel = sel;
                    sel_alpha = 90;
                } else if (sel_alpha < 255) {
                    sel_alpha = sel_alpha + 30 > 255 ? 255 : sel_alpha + 30;
                }
                s32 y = list_top;
                for (u32 i = top;
                     i < opt_texs.size() && (s32)i < top + visible; i++) {
                    if ((s32)i == sel) {
                        const s32 hrx = panel_x + pad - 12, hry = y,
                                  hrw = panel_w - 2 * (pad - 12),
                                  hrh = row_h - row_gap, hrr = 10;
                        auto fill = hi_clr;
                        fill.a = (u8)((s32)hi_clr.a * sel_alpha / 255);
                        d->RenderRoundedRectangleFill(fill, hrx, hry, hrw, hrh,
                                                      hrr);
                        // Soft outer glow rings + a crisp edge outline, same
                        // layered technique as TableList/CardGrid selection.
                        for (s32 g = 1; g <= 3; g++) {
                            auto gc = edge_clr;
                            gc.a = (u8)((36 - g * 10) * sel_alpha / 255);
                            d->RenderRoundedRectangle(gc, hrx - g, hry - g,
                                                      hrw + 2 * g, hrh + 2 * g,
                                                      hrr + g);
                        }
                        auto edge = edge_clr;
                        edge.a = (u8)sel_alpha;
                        for (s32 t = 0; t < 2; t++) {
                            d->RenderRoundedRectangle(
                                edge, hrx + t, hry + t, hrw - 2 * t,
                                hrh - 2 * t, hrr - t > 4 ? hrr - t : 4);
                        }
                    }
                    const s32 th = rnd::GetTextureHeight(opt_texs[i]);
                    d->RenderTexture(opt_texs[i], panel_x + pad,
                                     y + (row_h - row_gap - th) / 2);
                    // ON/OFF badge, right-aligned in the row, for the live toggle.
                    if (live && (s32)i == live->row) {
                        pu::sdl2::Texture bt = live->state ? on_tex : off_tex;
                        const s32 bw = rnd::GetTextureWidth(bt);
                        const s32 bh = rnd::GetTextureHeight(bt);
                        d->RenderTexture(bt, panel_x + panel_w - pad - bw,
                                         y + (row_h - row_gap - bh) / 2);
                    }
                    y += row_h;
                }
                // Chevrons when rows extend beyond the window, so it's clear the
                // list continues above/below what's shown.
                if (top > 0)
                    d->RenderTexture(up_tex, panel_x + panel_w - pad,
                                     list_top - 4);
                if (top + visible < (s32)opt_texs.size())
                    d->RenderTexture(dn_tex, panel_x + panel_w - pad,
                                     list_top + visible * row_h - 24);
                // Status line pinned to the panel bottom (e.g. the live address),
                // rebuilt on toggle and, while a live footer is showing, about
                // once a second so its connection state stays current.
                if (live && live->state) {
                    const u64 now_ns = armTicksToNs(armGetSystemTick());
                    if (now_ns >= foot_poll_ns) {
                        foot_poll_ns = now_ns + 1000000000ULL;
                        foot_dirty = true;
                    }
                }
                if (foot_dirty) {
                    foot_dirty = false;
                    if (foot_tex) {
                        rnd::DeleteTexture(foot_tex);
                        foot_tex = nullptr;
                    }
                    std::string fs = live->footer ? live->footer() : "";
                    if (!fs.empty())
                        foot_tex = rnd::RenderText(
                            bfont, wrap_to_px(bfont, fs, panel_w - 2 * pad),
                            g_theme->rom_info_clr);
                }
                if (foot_tex) {
                    const s32 fh = rnd::GetTextureHeight(foot_tex);
                    d->RenderTexture(foot_tex, panel_x + pad, SH - 40 - fh);
                }
                return true;
            });
        if (!ok) {
            // One trailing blank frame so the panel doesn't linger, matching
            // Dialog::Show's teardown.
            this->CallForRenderWithRenderOver(
                [](rnd::Renderer::Ref &) -> bool { return false; });
            break;
        }
    }

    rnd::DeleteTexture(title_tex);
    if (body_tex) rnd::DeleteTexture(body_tex);
    if (up_tex) rnd::DeleteTexture(up_tex);
    if (dn_tex) rnd::DeleteTexture(dn_tex);
    if (on_tex) rnd::DeleteTexture(on_tex);
    if (off_tex) rnd::DeleteTexture(off_tex);
    if (foot_tex) rnd::DeleteTexture(foot_tex);
    if (bd_blur_tex) rnd::DeleteTexture(bd_blur_tex);
    if (bd_scrim_tex) rnd::DeleteTexture(bd_scrim_tex);
    for (auto &t : opt_texs) rnd::DeleteTexture(t);
    return result;
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
    // Add what the queue still owes (metadata size minus whatever each .part
    // already holds) so the check accounts for items queued earlier.
    uint64_t need = add_size + queue_pending_bytes();
    if (need <= freeb) return true;
    // "needed > free" mirrors the warning sentence ("total size exceeds free
    // space") and reads the same in any language, so no new strings are needed.
    return this->Confirm(tr(S_FREE_SPACE_WARN),
                         human_size(need) + "  >  " + human_size(freeb));
}

// X in the file list. Filter and sort moved in here so Y could become "select";
// one menu now covers everything that changes what the list shows or what is
// picked out of it.
void MainApplication::FilesViewMenu() {
    enum { ACT_FILTER, ACT_SORT, ACT_ALL, ACT_NONE };
    std::vector<std::string> opts;
    std::vector<int> acts;
    opts.push_back(tr(S_FILTER)); acts.push_back(ACT_FILTER);
    opts.push_back(tr(S_SORT));   acts.push_back(ACT_SORT);
    if (!g_files.empty()) {
        char lb[96];
        snprintf(lb, sizeof(lb), tr(S_SELECT_ALL_SHOWN), (int)g_files.size());
        opts.push_back(lb); acts.push_back(ACT_ALL);
    }
    if (!g_sel.empty()) {
        opts.push_back(tr(S_CLEAR_SELECTION)); acts.push_back(ACT_NONE);
    }
    opts.push_back(tr(S_CANCEL));

    int r = this->SideMenu(tr(S_VIEW), opts);
    if (r < 0 || r >= (int)acts.size()) {
        return;
    }
    switch (acts[r]) {
    case ACT_FILTER: {
        char fb[64] = {0};
        if (prompt_raw(tr(S_FILTER_GUIDE), g_filter.c_str(), fb, sizeof(fb))) {
            g_filter = fb;
            rebuild_files(this->layout.get(), g_files_target, false);
        }
        break;
    }
    case ACT_SORT: {
        int s = this->SideMenu(
            tr(g_sort_keys[g_sort_mode]),
            {tr(S_SORT_DEFAULT), tr(S_SORT_NAME_AZ), tr(S_SORT_NAME_ZA),
             tr(S_SORT_SIZE_DESC), tr(S_SORT_SIZE_ASC), tr(S_CANCEL)},
            g_sort_mode);
        if (s >= 0 && s < SORT__COUNT) {
            g_sort_mode = s;
            s32 keep = this->layout->Sel();
            rebuild_files(this->layout.get(), g_files_target, false);
            if (keep >= 0 && keep < this->layout->RowCount())
                this->layout->SetSel(keep);
        }
        break;
    }
    case ACT_ALL:
        // "Shown", not "all": with a filter active this selects exactly what
        // the filter left, which is how a 500-file set gets built in one press.
        // Anything selected under an earlier filter stays selected.
        for (int k = 0; k < (int)g_files.size(); k++) {
            g_sel.insert(g_files[k]);
            this->layout->SetMark(k, true);
        }
        files_info_line(this->layout.get());
        break;
    case ACT_NONE:
        g_sel.clear();
        this->layout->ClearMarks();
        files_info_line(this->layout.get());
        break;
    }
}

// Resolved custom install folder for a console, honoring the master switch:
// with per-console folders turned off, every console falls back to the default
// <ROM root>/<console>, so the stored paths are ignored (but kept for later).
// Snapshotted onto each queue item at enqueue, so a mid-flight toggle can't
// redirect a download already running.
static const char *install_folder_for(const char *target) {
    return g_prefs.custom_folders ? config_console_folder(&g_cfg, target) : "";
}

// A with files marked: queue the whole selection, but total it up first. Every
// size here comes from the repo metadata, so the count, the bytes and what will
// actually fit are all known before a single byte is transferred.
void MainApplication::QueueSelection() {
    if (!g_have_item || g_sel.empty()) {
        return;
    }
    // Walk in list order so the queue mirrors the screen; selected files the
    // current filter happens to hide are still part of the set and go last.
    std::set<int> shown(g_files.begin(), g_files.end());
    std::vector<int> pick;
    pick.reserve(g_sel.size());
    for (int fi : g_files) {
        if (g_sel.count(fi)) pick.push_back(fi);
    }
    for (int fi : g_sel) {
        if (!shown.count(fi)) pick.push_back(fi);
    }

    std::vector<int> add;
    uint64_t bytes = 0;
    int skipped = 0;
    bool any_archive = false, any_huge = false;
    for (int fi : pick) {
        if (fi < 0 || fi >= g_item.file_count) continue;
        ArchiveFile *f = &g_item.files[fi];
        if (g_prefs.skip_installed && index_has_installed(g_inst_idx, f->name)) {
            skipped++;
            continue;
        }
        add.push_back(fi);
        bytes += f->size;
        if (is_archive_name(f->name)) any_archive = true;
        if (f->size > 0xFFFFFFFFULL) any_huge = true;
    }
    if (add.empty()) {
        this->Toast(tr(S_ALL_ALREADY_INSTALLED));
        return;
    }
    int slots = queue_free_slots();
    if (slots <= 0) {
        this->ToastErr(tr(S_QUEUE_FULL));
        return;
    }

    const uint64_t freeb = fs_free_bytes("sdmc:/");
    const uint64_t pending = queue_pending_bytes();
    const int n = (int)add.size();
    const int cap = n < slots ? n : slots; // what "queue everything" can take

    // How many, in order, fit in both the free slots and the free space. This
    // is a lower bound on the footprint: archives are extracted after download,
    // and while that runs the .part and the unpacked files coexist.
    int fit = 0;
    uint64_t run = pending;
    // Budget once, then compare by subtraction. Declared sizes come from the
    // collection file, so `run + sz + QUEUE_SPACE_RESERVE` had two additions
    // that could wrap past UINT64_MAX and report that everything fits.
    const uint64_t budget =
        freeb > QUEUE_SPACE_RESERVE ? freeb - QUEUE_SPACE_RESERVE : 0;
    for (int i = 0; i < cap; i++) {
        uint64_t sz = g_item.files[add[i]].size;
        if (freeb != UINT64_MAX && (run > budget || sz > budget - run)) break;
        run += sz;
        fit++;
    }

    char lb[160];
    snprintf(lb, sizeof(lb), tr(S_N_FILES), n);
    std::string msg = std::string(lb) + "  ·  " + human_size(bytes);
    if (freeb != UINT64_MAX) {
        msg += "\n" + std::string(tr(S_FREE_SPACE)) + "  " + human_size(freeb);
    }
    if (pending > 0) {
        msg += "\n" + std::string(tr(S_QUEUE_PENDING)) + "  " +
               human_size(pending);
    }
    if (skipped > 0) {
        snprintf(lb, sizeof(lb), tr(S_N_SKIPPED_INSTALLED), skipped);
        msg += "\n"; msg += lb;
    }
    if (n > cap) {
        snprintf(lb, sizeof(lb), tr(S_ONLY_N_SLOTS), slots);
        msg += "\n"; msg += lb;
    }
    if (any_archive) {
        msg += "\n"; msg += tr(S_ARCHIVES_EXPAND);
    }

    std::vector<std::string> opts;
    std::vector<int> counts;
    if (fit >= cap) {
        snprintf(lb, sizeof(lb), tr(S_QUEUE_N), cap);
        opts.push_back(lb); counts.push_back(cap);
    } else {
        if (fit > 0) {
            snprintf(lb, sizeof(lb), tr(S_QUEUE_N_THAT_FIT), fit);
            opts.push_back(lb); counts.push_back(fit);
        }
        snprintf(lb, sizeof(lb), tr(S_QUEUE_N_ANYWAY), cap);
        opts.push_back(lb); counts.push_back(cap);
    }
    opts.push_back(tr(S_CANCEL));

    int r = this->CreateShowDialog(tr(S_QUEUE_SELECTED), msg, opts, false, {},
                                   fit >= cap ? style_dialog
                                              : style_dialog_danger);
    if (r < 0 || r >= (int)counts.size()) {
        return;
    }
    // Same FAT32 caveat as a single add, asked once for the whole batch.
    if (any_huge && !this->fat32_ack) {
        if (!this->Confirm(tr(S_FAT32_WARN), tr(S_FAT32_WARN_MSG))) return;
        this->fat32_ack = true;
    }

    const int want = counts[r];
    char auth[320];
    creds_auth_header(&g_creds, auth, sizeof(auth));
    int done = 0;
    // One queue-state write for the whole batch: queue_add persists on every
    // call, so without this, queueing N items rewrites the file N times.
    queue_batch_begin();
    for (int i = 0; i < n && done < want; i++) {
        // The dialog above was modal; re-check the index rather than trusting
        // that the metadata behind it is still the same size.
        if (!g_have_item || add[i] < 0 || add[i] >= g_item.file_count) {
            break;
        }
        ArchiveFile *f = &g_item.files[add[i]];
        char url[1024];
        ia_file_url(&g_item, f, url, sizeof(url));
        if (!queue_add(url, f->name, g_files_target, auth, f->size,
                       is_archive_name(f->name), f->md5,
                       install_folder_for(g_files_target))) {
            break; // queue filled under us; report what did land
        }
        g_sel.erase(add[i]); // queued items drop out of the selection
        done++;
    }
    queue_batch_end();
    // One rebuild for the batch: it re-applies marks from what's left selected.
    s32 keep = this->layout->Sel();
    rebuild_files(this->layout.get(), g_files_target, false);
    if (keep >= 0 && keep < this->layout->RowCount()) {
        this->layout->SetSel(keep);
    }
    snprintf(lb, sizeof(lb), tr(S_QUEUED_N), done);
    if (done > 0) {
        this->Toast(lb);
    } else {
        this->ToastErr(tr(S_QUEUE_FULL));
    }
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
        // 6-wide/2-row poster geometry, matching the Installed library's
        // card view -- these are plain logo icons (never real art), so they
        // fall through poster's "no cover" centred-icon path rather than
        // stretching into a cover-art rectangle. See CardGrid::SetPoster.
        if (cards) {
            this->layout->SetCardCols(6);
            this->layout->SetCardPoster(true);
        }
        for (int i = 0; i < g_cfg.console_count; i++) {
            if (!g_cfg.consoles[i].shown) {
                continue;
            }
            char label[160];
            console_label(g_cfg.consoles[i].console, label, sizeof(label));
            rows.push_back({label, i, g_cfg.consoles[i].pinned});
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
            // Games rather than files, to agree with the Library chips. Home
            // re-renders on every tab switch (including a plain L/R cycle),
            // so this is cached by folder mtime and only re-walks a console
            // folder when it actually changed — see inst_home_count.
            int fc = inst_home_count(rdir);
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
                bool is_art = false;
                pu::sdl2::Texture cic = console_display_icon(cname, &is_art);
                this->layout->AddCard(full ? full : cname, cnt, cic,
                                      row.pinned, false, is_art);
            } else {
                this->layout->AddRow2(
                    row.label, cnt, g_theme->row_text, count_color(), -1.0f,
                    console_display_icon(g_cfg.consoles[row.idx].console), "",
                    false, true, row.pinned);
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
        if (cards) {
            this->layout->SetCardCols(6);
            this->layout->SetCardPoster(true);
        }
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
                bool is_art = false;
                pu::sdl2::Texture cic =
                    console_display_icon(fr.key.c_str(), &is_art);
                this->layout->AddCard(fr.cname, sub, cic, fr.pinned,
                                      !fr.enabled, is_art);
            } else {
                // "Full Console Name › repo label", matching the breadcrumb;
                // the on/off state moves to a right-hand chip.
                this->layout->AddRow2(fr.cname + " › " + fr.repo,
                                      fr.enabled ? tr(S_ON) : tr(S_OFF),
                                      g_theme->row_text,
                                      onoff_color(fr.enabled), -1.0f,
                                      console_display_icon(fr.key.c_str()), "",
                                      false, true, fr.pinned);
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
    this->layout->SetTitleIcon(console_display_icon(g->console));
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
    this->layout->SetTitleIcon(console_display_icon(g->console));
    this->screen = Screen::Files;
    this->StartMetaLoad(rp->id, rp->download_base, g->target, force,
                        FILES_SUBTITLE);
}

// ---- tabs -----------------------------------------------------------------
MainApplication::Tab MainApplication::CurrentTab() {
    switch (this->screen) {
    case Screen::Installed:
    case Screen::Verify:
    case Screen::VerifyMissing:
    case Screen::VerifyAll:
    case Screen::SortInbox:
    case Screen::UsbMtp:
    case Screen::Tidy:
    case Screen::BoxArtResults:
    case Screen::BoxArtPicker:
    case Screen::InstSearch: return Tab::Installed;
    case Screen::Queue:     return Tab::Queue;
    case Screen::Settings:
    case Screen::Log:
    case Screen::Manage:
    case Screen::Creds:
    case Screen::DlPrefs:
    case Screen::Appearance:
    case Screen::ExtFilter:
    case Screen::RomPicker:
    case Screen::Downloads:
    case Screen::Language:
    case Screen::Accent:
    case Screen::Cache:
    case Screen::Transfers:
    case Screen::RecvConsole:
    case Screen::Sources:
    case Screen::Storage:
    case Screen::Dats:
    case Screen::DataFiles:
    case Screen::MetaCache:
    case Screen::InboxFiles:
    case Screen::RegionOrder:
    case Screen::LargestFiles:
    case Screen::InstallFolders:
    case Screen::BoxArtManageConsoles:
    case Screen::BoxArtManageList:
    case Screen::Backups:
    case Screen::Account:
    case Screen::Updates:
    case Screen::AppUpdates:
    case Screen::Diagnostics:
    case Screen::About:
    case Screen::ViewLogs:
    case Screen::DebugLog:
    case Screen::Import:
    case Screen::ReleaseNotes:
    case Screen::ReleaseNote:
    case Screen::QueueState:
    case Screen::Help:
    case Screen::HelpTopics:
    case Screen::HelpArticle:
    case Screen::HelpSearch: return Tab::Settings;
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

// Cycle the extraction chunk size through the offered sizes (1/2/4 MB). dir>0
// steps up, dir<0 steps down, both wrapping.
static int ex_chunk_step(int mb, int dir) {
    static const int sizes[] = {1, 2, 4};
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int i = 0;
    for (int k = 0; k < n; k++) {
        if (sizes[k] == mb) { i = k; break; }
    }
    i = (((i + dir) % n) + n) % n;
    return sizes[i];
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

// Push the extraction-benchmark knobs to the extractor. Called at startup and
// whenever one of the Advanced toggles changes so the next archive picks them up.
static void apply_extract_tunables(void) {
    ExtractTunables t;
    t.bench = g_prefs.ex_bench;
    t.prealloc = g_prefs.ex_prealloc;
    t.chunk_mb = g_prefs.ex_chunk_mb;
    extract_set_tunables(&t);
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
    // Was a fixed blue literal, independent of the selected accent preset;
    // now the same accent_blue() every ring/glow/progress-bar routes
    // through, so a settings row's "value" text follows Appearance > Accent
    // Color too.
    return accent_blue();
}
// Amber "needs attention" (an update is waiting) and red "problem" (source
// unreachable) — distinct from the green up-to-date / grey no-source cues.
static pu::ui::Color attention_color() {
    return is_light_theme() ? pu::ui::Color(190, 120, 20, 255)
                            : pu::ui::Color(245, 190, 90, 255);
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
    // card grid indexes the same way, so both views share it. Each row opens a
    // single-concern sub-screen — no setting lives at this level. Icon slugs are
    // placeholders until art lands; console_icon() falls back to "default".
    // Console show/hide + the file-extension filter moved into Appearance, and
    // "Install from PC" moved into PC Sync, so neither has a top-level row now.
    static const struct { int str; const char *icon; } kEntries[] = {
        {S_SEC_APPEARANCE,  "set-appearance"},  // 0
        {S_SEC_DOWNLOADS,   "set-downloads"},   // 1
        {S_SEC_STORAGE,     "set-storage"},     // 2
        {S_SEC_DATA_FILES,  "set-data"},        // 3 — DAT files + metadata cache
        {S_SEC_TRANSFERS,   "set-transfers"},   // 4 — PC Sync (now hosts Install from PC)
        {S_SEC_ACCOUNT,     "set-account"},     // 5
        {S_SEC_UPDATES,     "set-updates"},     // 6 — carries the update chip
        {S_SEC_LOGS,        "set-logs"},        // 7
        {S_SEC_DIAGNOSTICS, "set-diagnostics"}, // 8
        {S_SEC_HELP,        "set-help"},      // 9 — Getting Started/How-To/Troubleshooting
        {S_SEC_ABOUT,       "set-credits"},     // 10
    };
    // The "Update available" / "Restart to update" chip rides the Updates row,
    // the section that now owns checking and installing.
    auto has_chip = [&](int str) {
        return str == S_SEC_UPDATES &&
               (this->update_available || this->update_installed);
    };
    const char *chip = tr(this->update_installed ? S_RESTART_TO_UPDATE
                                                  : S_UPDATE_AVAIL);
    if (g_prefs.card_view) {
        this->layout->SetCardCols(6);
        this->layout->SetCardPoster(true);
        for (const auto &e : kEntries) {
            this->layout->AddCard(tr(e.str), has_chip(e.str) ? chip : "",
                                  console_icon(e.icon), false);
        }
        this->layout->SetCardsMode(true);
    } else {
        pu::ui::Color lbl = g_theme->row_text;
        pu::ui::Color chv = chevron_color();
        for (const auto &e : kEntries) {
            // Same section icon the card grid uses, shown before the label so
            // the list view matches the cards.
            pu::sdl2::Texture ic = console_icon(e.icon);
            if (has_chip(e.str)) {
                // Actionable chip far-right, in the same pill the list's
                // size/status values use (pill = true), tinted affirmative
                // green. Once a build is staged it becomes "Restart to update".
                this->layout->AddRow2(tr(e.str), chip, lbl, onoff_color(true),
                                      -1.0f, ic, "", false, true);
            } else {
                this->layout->AddRow2(tr(e.str), CHEVRON, lbl, chv, -1.0f,
                                      ic, "", false, false);
            }
        }
    }
    // The ROM folder lives under Storage now; no longer echoed in the footer.
    this->layout->SetRomInfo("");
}

// Downloads: how many run at once, the rate caps, and the two behaviours that
// belong to a download in flight (skip-installed on a bulk add, keep-awake).
// Startup checks, credentials and the extraction knobs moved to their own
// sections — this screen is only about pulling files down.
void MainApplication::GotoDlPrefs() {
    this->screen = Screen::DlPrefs;
    this->layout->SetTitle(tr(S_TITLE_DLPREFS));
    this->layout->SetSubtitle(tr(S_SUB_DLPREFS));
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
    this->layout->AddRow2(settings_label(tr(S_KEEP_AWAKE)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 3
    // Two knobs that shape how a download lands on disk (moved here from the
    // old Diagnostics > Extraction tuning sub-screen).
    b = g_prefs.ex_prealloc;
    this->layout->AddRow2(settings_label(tr(S_EX_PREALLOC)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 4
    {
        char v[16];
        snprintf(v, sizeof(v), "%d MB", g_prefs.ex_chunk_mb);
        this->layout->AddRow2(settings_label(tr(S_EX_CHUNK)), v, lbl,
                              value_color());                             // 5
    }
    b = g_prefs.keep_archives;
    this->layout->AddRow2(settings_label(tr(S_KEEP_ARCHIVES)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 6
    // Post-import converter toggle: only shown when an add-on module has
    // registered the hook (queue_post_import != NULL); a build without it
    // hides the row entirely. Row index (7 or 8) depends on whether this row
    // is present — see the case-7/8 split in the input handler below.
    if (queue_post_import) {
        b = g_prefs.convert_import;
        this->layout->AddRow2(settings_label(tr(S_CONVERT_IMPORT)),
                              b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 7
    }
    // Last row always: skip-installed is a bulk-add behavior, not a download
    // mechanic, so it reads better grouped at the bottom of this list.
    b = g_prefs.skip_installed;
    this->layout->AddRow2(settings_label(tr(S_SKIP_INSTALLED)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 8 (7 if the row above is hidden)
}

/* Browse the SD card and choose a folder to use as the ROM root. Shows only
 * directories (you're picking a folder, not a file). */
void MainApplication::GotoRomPicker(const std::string &path) {
    this->screen = Screen::RomPicker;
    this->picker_path = path;
    if (this->picker_console >= 0 &&
        this->picker_console < g_cfg.console_count) {
        // Picking a console's custom install folder: name it in the title.
        this->layout->SetTitle(
            std::string(g_cfg.consoles[this->picker_console].console) + " > " +
            tr(S_TITLE_ROM_PICKER));
    } else {
        this->layout->SetTitle(tr(S_TITLE_ROM_PICKER));
    }
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

// Appearance: what the app looks like and reads as. Theme, card/list layout,
// grouped-vs-flat Browse, and language. Console visibility and the file-type
// filter used to live here but are catalogue concerns — they moved to Sources.
void MainApplication::GotoAppearance() {
    this->screen = Screen::Appearance;
    this->layout->SetTitle(tr(S_TITLE_APPEARANCE));
    this->layout->SetSubtitle(tr(S_SUB_APPEARANCE));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    bool b = g_prefs.card_view;
    this->layout->AddRow2(settings_label(tr(S_CARD_VIEW)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 0
    this->layout->AddRow2(settings_label(tr(S_THEME)),
                          is_light_theme() ? tr(S_THEME_LIGHT) : tr(S_THEME_DARK),
                          lbl, value_color());                                 // 1
    // Recolors every ring/glow/progress bar app-wide (see g_accents); opens
    // a picker the same way Language does below.
    this->layout->AddRow2(
        settings_label(tr(S_ACCENT)),
        tr(g_accents[accent_preset_index()].name_str), lbl, value_color());    // 2
    const char *cur = g_prefs.lang[0] ? g_prefs.lang : "en";
    this->layout->AddRow2(settings_label(tr(S_LANGUAGE)), lang_display_name(cur),
                          lbl, value_color());                                 // 3
    b = g_prefs.group_consoles;
    this->layout->AddRow2(settings_label(tr(S_GROUP_CONSOLES)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 4
    // Curation folded in from the old Filters section: the file-type filter
    // that hides junk in a repo's file list.
    b = g_prefs.filter_exts;
    this->layout->AddRow2(settings_label(tr(S_FILTER_EXTS)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b));      // 5
    // Off lists a disc set's raw files again — the way to reach one track.
    b = g_prefs.group_sets;
    this->layout->AddRow2(settings_label(tr(S_GROUP_SETS)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b));      // 6
    // Off skips box-art lookups entirely (list stays plain/fast; cached
    // covers already on disk are untouched, just not shown or fetched).
    b = g_prefs.box_art_enabled;
    this->layout->AddRow2(settings_label(tr(S_BOX_ART_TOGGLE)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b));      // 7
    // Independent of the toggle above: this only decides whether a newly
    // landed game queues a quiet background fetch, not whether cached covers
    // are shown (see g_prefs.box_art_auto_fetch's own comment in config.h).
    b = g_prefs.box_art_auto_fetch;
    this->layout->AddRow2(settings_label(tr(S_BOX_ART_AUTO_TOGGLE)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b));      // 8
    // Console show/hide: last row — a navigation-heavy sub-screen, so it sits
    // below the toggles/values that are actionable right on this list.
    this->layout->AddRow2(tr(S_MANAGE_CONSOLES), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);                   // 9
}

// Accent color picker: one row per g_accents preset, "◀" marking the active
// one -- same list-of-options pattern as GotoLanguage.
void MainApplication::GotoAccent() {
    this->screen = Screen::Accent;
    this->layout->SetTitle(tr(S_TITLE_ACCENT));
    this->layout->SetSubtitle(tr(S_SUB_ACCENT));
    this->layout->ClearMenu();
    int cur = accent_preset_index();
    for (int i = 0; i < g_accent_count; i++) {
        bool active = (i == cur);
        this->layout->AddRow2(
            tr(g_accents[i].name_str), active ? "◀" : "",
            g_theme->row_text,
            accent_blue(),
            -1.0f, nullptr, "", false, false);
    }
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

// Storage > Manage Inbox folder: a plain view/select/delete file list over
// INBOX_DIR, for junk or wrongly-received files the user wants gone without
// running the full identify-and-file flow (Tools > View Inbox does that one;
// see Screen::SortInbox, which gets the same select+delete on top of it).
void MainApplication::GotoInboxFiles() {
    this->screen = Screen::InboxFiles;
    this->layout->SetTitle(tr(S_TITLE_INBOX_FILES));
    this->layout->SetSubtitle(tr(S_SUB_INBOX_FILES));
    this->layout->ClearMenu();
    fs_mkdir_p(INBOX_DIR);
    g_inbox_mfiles = list_dir(INBOX_DIR);
    g_inbox_mfiles.erase(
        std::remove_if(g_inbox_mfiles.begin(), g_inbox_mfiles.end(),
                       [](const DirEnt &e) {
                           if (e.is_dir) return true;
                           size_t nl = e.name.size(); // hide in-flight receives
                           return nl >= 5 && strcasecmp(e.name.c_str() + nl - 5,
                                                        ".part") == 0;
                       }),
        g_inbox_mfiles.end());
    uint64_t total = 0;
    for (auto &e : g_inbox_mfiles) {
        this->layout->AddRow2(e.name, human_size(e.size), g_theme->row_text,
                              size_color(e.size));
        total += e.size;
    }
    if (g_inbox_mfiles.empty()) {
        this->layout->AddRow(tr(S_INBOX_FILES_EMPTY));
    } else {
        char info[128];
        snprintf(info, sizeof(info), tr(S_DL_N_TOTAL),
                 (int)g_inbox_mfiles.size(), human_size(total).c_str());
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

// Collection Management: the Wi-Fi hub for moving a collection between this
// console and a PC on the same LAN (import / export / restore). Pushing an
// .nro build over Wi-Fi is an app update, so it lives under Updates now;
// Storage/ROM-folder concerns live under Storage.
void MainApplication::GotoTransfers() {
    this->screen = Screen::Transfers;
    this->layout->SetTitle(tr(S_TITLE_TRANSFERS));
    this->layout->SetSubtitle(tr(S_SUB_TRANSFERS));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    // Receive a game from a PC — the two transports the old "Install from PC"
    // submenu dialog offered, now direct rows (no menu in between) so each is
    // one press away. Sit at the top since they're the most frequently-used
    // PC Sync actions.
    this->layout->AddRow2(tr(S_INSTALL_USB_CONN), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);      // 0 install from USB
    this->layout->AddRow2(tr(S_INSTALL_WIFI), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);      // 1 install from Wi-Fi
    this->layout->AddRow(tr(S_IMPORT_COLLECTION));  // 2 receive dl_sources.json from a PC
    this->layout->AddRow(tr(S_EXPORT_COLLECTION));  // 3 serve dl_sources.json to a PC
    this->layout->AddRow(tr(S_RESTORE_COLLECTION)); // 4 restore previous collection
    // Read-only companion inventory server: a toggle plus the address the app
    // utility connects to (shown only while it's on).
    bool inv = g_prefs.inv_server;
    this->layout->AddRow2(settings_label(tr(S_INV_SERVER)),
                          inv ? tr(S_ON) : tr(S_OFF), lbl,
                          onoff_color(inv));          // 5 toggle
    {
        char addr[96];
        if (inv) {
            char ip[64];
            if (httpsrv_local_ip(ip, sizeof(ip))) {
                snprintf(addr, sizeof(addr), "%s:%d/%s", ip, HTTPSRV_INV_PORT,
                         g_prefs.inv_code);
            } else {
                snprintf(addr, sizeof(addr), "%s", tr(S_IMPORT_NO_NET));
            }
        } else {
            snprintf(addr, sizeof(addr), "%s", tr(S_OFF));
        }
        this->layout->AddRow2(tr(S_INV_ADDRESS), addr, lbl,
                              g_theme->rom_info_clr);  // 6 read-only address
    }
    // Full SD card access: unscoped read/write/delete over the whole card
    // (like mounting it in Windows Explorer over MTP), for the desktop
    // companion's SD Card tab — over Wi-Fi (this inventory server) and over
    // USB (the embedded MTP responder, next USB connection). Off by default;
    // its own toggle, independent of the inventory server above, since it
    // also applies to a USB-only connection with the server off.
    {
        bool sd = g_prefs.sd_full_access;
        this->layout->AddRow2(settings_label(tr(S_SD_FULL_ACCESS)),
                              sd ? tr(S_ON) : tr(S_OFF), lbl,
                              onoff_color(sd));          // 7 toggle
    }
    // The emulator/app-list push moved to Settings > Data Files — it's a
    // catalog push, not a transfer-session concern.
}

// Install from PC: pick which console to receive a game into, then open the LAN
// receiver for it (RomRecvStart). A game is per-console — its install folder is
// resolved from the console picked here — so choosing the console up front is
// the whole job of this screen. Row order is g_cfg.consoles order, so the
// A-press in OnInput maps a row straight to a console index.
void MainApplication::GotoRecvConsole() {
    this->screen = Screen::RecvConsole;
    this->layout->SetTitle(tr(S_TITLE_RECV_CONSOLE));
    this->layout->SetSubtitle(tr(S_SUB_RECV_CONSOLE));
    this->layout->ClearMenu();
    if (g_cfg.console_count == 0) {
        this->layout->AddRow(tr(S_NO_CONSOLES));
        return;
    }
    pu::ui::Color lbl = g_theme->row_text;
    // Row 0 is the console-agnostic path: receive straight into the inbox and let
    // the sorter identify and file it, so the user can push any game without
    // picking a console first. The console rows follow (index i maps to i-1).
    this->layout->AddRow2(tr(S_INBOX_LABEL), CHEVRON, lbl, chevron_color(), -1.0f,
                          console_icon("default"), "", false, false);
    for (int i = 0; i < g_cfg.console_count; i++) {
        ConsoleGroup &c = g_cfg.consoles[i];
        // Icon + "Full Name (SHORT)", exactly as the Installed console list
        // renders each console, so the two stay visually consistent.
        char clbl[160];
        console_label(c.target, clbl, sizeof(clbl));
        this->layout->AddRow2(clbl, CHEVRON, lbl, chevron_color(), -1.0f,
                              console_icon(c.target), "", false, false);
    }
}

// Sources: the catalogue you pull from. Which consoles show on Browse, the
// repos under them, and the file-type filter that hides junk in a repo's file
// list. Split out of Appearance because curating what you download is a
// different job from choosing how the app looks.
void MainApplication::GotoSources() {
    this->screen = Screen::Sources;
    this->layout->SetTitle(tr(S_TITLE_SOURCES));
    this->layout->SetSubtitle(tr(S_SUB_SOURCES));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    this->layout->AddRow2(tr(S_MANAGE_CONSOLES), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);       // 0
    bool b = g_prefs.filter_exts;
    this->layout->AddRow2(settings_label(tr(S_FILTER_EXTS)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 1
}

// Storage: where space goes and how to reclaim it. A live SD-card readout, the
// ROM install folder, the download scratch area, and the metadata cache. The
// SD row is a status line (A opens a used/free breakdown); the rest drill in.
void MainApplication::GotoStorage() {
    this->screen = Screen::Storage;
    this->layout->SetTitle(tr(S_TITLE_STORAGE));
    this->layout->SetSubtitle(tr(S_SUB_STORAGE));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    {                                               // 0 SD free space (status)
        uint64_t fb = fs_free_bytes("sdmc:/");
        uint64_t tb = fs_total_bytes("sdmc:/");
        char v[64];
        if (fb == UINT64_MAX || tb == UINT64_MAX) {
            snprintf(v, sizeof(v), "?");
        } else {
            snprintf(v, sizeof(v), tr(S_SD_FREE_OF), human_size(fb).c_str(),
                     human_size(tb).c_str());
        }
        this->layout->AddRow2(settings_label(tr(S_SD_CARD)), v, lbl,
                              value_color());
    }
    {                                               // 1 ROM folder
        bool custom = g_prefs.roms_override[0] != '\0';
        this->layout->AddRow2(settings_label(tr(S_ROMS_OVERRIDE)),
                              custom ? roms_root(&g_tico) : tr(S_ROMS_AUTO), lbl,
                              custom ? value_color() : onoff_color(false));
    }
    {                                               // 2 install-folder mode
        bool cf = g_prefs.custom_folders;
        this->layout->AddRow2(settings_label(tr(S_INSTALL_MODE)),
                              cf ? tr(S_INSTALL_MODE_CUSTOM)
                                 : tr(S_INSTALL_MODE_DEFAULT),
                              lbl, onoff_color(cf));
    }
    {                                               // 3 per-console folders
        // Only actionable when custom mode is on; otherwise it reads as a locked
        // hint so the "unlock" relationship with the row above is visible.
        bool cf = g_prefs.custom_folders;
        this->layout->AddRow2(settings_label(tr(S_CONSOLE_FOLDERS)),
                              cf ? tr(S_OPEN) : tr(S_LOCKED), lbl,
                              cf ? value_color() : onoff_color(false));
    }
    this->layout->AddRow(tr(S_MANAGE_DOWNLOADS));   // 4 download scratch folder
    this->layout->AddRow(tr(S_MANAGE_INBOX));       // 5 view/select/delete Inbox files
    // DAT files + metadata cache moved up to their own top-level Settings
    // section ("Data Files").
    this->layout->AddRow(tr(S_MANAGE_BACKUPS));     // 6 emulator/app rollback backups
    this->layout->AddRow(tr(S_LARGE_FILES));        // 7 whole-library biggest files
    this->layout->AddRow(tr(S_MANAGE_BOX_ART));     // 8 view/delete cached covers
}

// Storage sub-screen: the list of consoles, each showing its install folder
// (default or a custom path). A opens the SD folder picker for that console.
// Only reachable when the per-console mode is on (see GotoStorage row 2).
void MainApplication::GotoInstallFolders() {
    this->screen = Screen::InstallFolders;
    this->layout->SetTitle(tr(S_TITLE_CONSOLE_FOLDERS));
    this->layout->SetSubtitle(tr(S_SUB_CONSOLE_FOLDERS));
    this->layout->ClearMenu();
    for (int i = 0; i < g_cfg.console_count; i++) {
        ConsoleGroup *g = &g_cfg.consoles[i];
        char label[128];
        console_label(g->console, label, sizeof(label));
        this->layout->AddRow2(label,
                              g->folder[0] ? g->folder
                                           : tr(S_INSTALL_FOLDER_DEFAULT),
                              g_theme->row_text, onoff_color(g->folder[0] != 0),
                              -1.0f, console_icon(g->console));
    }
    if (g_cfg.console_count == 0) {
        this->layout->AddRow(tr(S_NO_CONSOLES));
    }
}

// Sum the sizes of the files directly in a folder (non-recursive, files only).
// The download scratch and metadata cache are both flat, so this is enough to
// report what the app itself is holding without walking the whole card.
static uint64_t dir_total_size(const char *path) {
    uint64_t total = 0;
    for (const auto &e : list_dir(path)) {
        if (!e.is_dir) total += e.size;
    }
    return total;
}

// A on the SD-card status row: a breakdown of what the app itself is holding
// (download scratch + metadata cache) against the card's free/total.
void MainApplication::StorageDetail() {
    uint64_t fb = fs_free_bytes("sdmc:/");
    uint64_t tb = fs_total_bytes("sdmc:/");
    uint64_t dl = dir_total_size(DL_TMP_DIR);
    uint64_t ca = dir_total_size(CACHE_DIR);
    char body[320];
    snprintf(body, sizeof(body), tr(S_STORAGE_DETAIL),
             (fb == UINT64_MAX) ? "?" : human_size(fb).c_str(),
             (tb == UINT64_MAX) ? "?" : human_size(tb).c_str(),
             human_size(dl).c_str(), human_size(ca).c_str());
    this->CreateShowDialog(tr(S_STORAGE_TITLE), body, {tr(S_OK)}, true, {},
                           style_dialog);
}

// Account Credentials: the two credentials that reach off-device. archive.org S3
// keys (only ever sent to archive.org over HTTPS) and an optional GitHub token
// used to raise the rate limit on the emulator/app update checks. The old
// startup network-warning toggle moved to Diagnostics.
void MainApplication::GotoAccount() {
    this->screen = Screen::Account;
    this->layout->SetTitle(tr(S_TITLE_ACCOUNT));
    this->layout->SetSubtitle(tr(S_SUB_ACCOUNT));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    bool b = g_creds.access_key[0] != '\0';
    this->layout->AddRow2(settings_label(tr(S_ARCHIVE_CREDS)),
                          b ? tr(S_SET) : tr(S_UNSET), lbl, onoff_color(b)); // 0
    bool gh = g_creds.github_token[0] != '\0';
    this->layout->AddRow2(settings_label(tr(S_GITHUB_TOKEN)),
                          gh ? tr(S_SET) : tr(S_UNSET), lbl, onoff_color(gh)); // 1
    bool sgdb = g_creds.steamgriddb_key[0] != '\0';
    this->layout->AddRow2(settings_label(tr(S_STEAMGRIDDB_KEY)),
                          sgdb ? tr(S_SET) : tr(S_UNSET), lbl,
                          onoff_color(sgdb)); // 2
}

// Updates: check now (GitHub release or a pushed .nro over Wi-Fi) and whether
// to check silently at startup. The top-level Updates row carries the
// "Update available" chip; this is where you act on it.
void MainApplication::GotoUpdates() {
    this->screen = Screen::Updates;
    this->layout->SetTitle(tr(S_TITLE_UPDATES));
    this->layout->SetSubtitle(tr(S_SUB_UPDATES));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    // 0: receive a pushed .nro build over Wi-Fi (an app update pushed from the
    // desktop) — first because it's the most common path.
    this->layout->AddRow2(tr(S_UPDATE_OVER_WIFI), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);         // 0
    // 1: check GitHub for a new HaulNX build — carries the "update available" chip.
    if (this->update_available || this->update_installed) {
        this->layout->AddRow2(
            tr(S_CHECK_NOW),
            tr(this->update_installed ? S_RESTART_TO_UPDATE : S_UPDATE_AVAIL),
            lbl, onoff_color(true), -1.0f, nullptr, "", false, true); // 1
    } else {
        this->layout->AddRow(tr(S_CHECK_NOW));                        // 1
    }
    // 2/3: per-app update management — check each installed emulator/app against
    // its GitHub source and install/update/revert from the list.
    this->layout->AddRow2(tr(S_APPMAN_EMUS), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);         // 2
    this->layout->AddRow2(tr(S_APPMAN_APPS), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);         // 3
    // 4: release-notes history (moved here from About — it's about versions).
    // (The "push list to PC" action moved to Settings › PC Sync.)
    this->layout->AddRow2(tr(S_RELEASE_NOTES), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);         // 4
    bool b = g_prefs.chk_updates;
    this->layout->AddRow2(settings_label(tr(S_CHK_UPDATES_STARTUP)),
                          b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b)); // 5
}

// A companion is "connected" when it has read our inventory recently: over
// Wi-Fi the inventory server stamps last_inv_ns on each poll; over USB an MTP
// host is attached (the Connect-to-PC screen saw a link). Used to gate and
// label the "Push list to PC" action.
bool MainApplication::CompanionConnected() const {
    if (this->inv_srv.last_inv_ns != 0 &&
        armTicksToNs(armGetSystemTick()) - this->inv_srv.last_inv_ns <=
            15000000000ULL) {
        return true;
    }
    return this->usb_status == mtp::Status_Connected;
}

// Console-initiated push: the device can't open a socket back to the PC, so we
// bump a monotonic revision the companion is already polling (in inventory.json
// over Wi-Fi, or the MTP-read inventory.json over USB). On seeing it change the
// companion re-reads the installed list + versions and adopts update_sources.json
// — i.e. this "pushes" the app/emulator list to the PC. update_sources.json is
// always current on disk, so only inventory.json needs republishing here.
void MainApplication::PushListToPc() {
    if (!this->CompanionConnected()) {
        this->CreateShowDialog(tr(S_APPMAN_PUSH_PC), tr(S_APPMAN_PUSH_NOCONN),
                               {tr(S_OK)}, true, {}, style_dialog);
        return;
    }
    this->inv_push_rev = armTicksToNs(armGetSystemTick());
    this->WriteInventoryJson();
    this->Toast(tr(S_APPMAN_PUSH_SENT));
}

// Diagnostics: a network self-test, a speed test, the one extraction benchmark
// toggle (dev-only; the other two knobs moved to Downloads), and a factory
// reset. The log bundle export and the logs themselves live under the Logs
// section now. Nothing here changes day-to-day behaviour.
void MainApplication::GotoDiagnostics() {
    this->screen = Screen::Diagnostics;
    this->layout->SetTitle(tr(S_TITLE_DIAGNOSTICS));
    this->layout->SetSubtitle(tr(S_SUB_DIAGNOSTICS));
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_SPEEDTEST));      // 0 download throughput test
    this->layout->AddRow(tr(S_NET_SELFTEST));   // 1 LAN + archive.org check
    {                                            // 2 warn on startup if offline
        bool nc = g_prefs.net_check;             //   (moved here from Account)
        this->layout->AddRow2(settings_label(tr(S_NET_CHECK_STARTUP)),
                              nc ? tr(S_ON) : tr(S_OFF), g_theme->row_text,
                              onoff_color(nc));
    }
    // The one leftover extraction knob (the other two moved to Downloads): a
    // straight on/off, no longer its own sub-screen.
    bool b = g_prefs.ex_bench;
    this->layout->AddRow2(settings_label(tr(S_EX_BENCH)),
                          b ? tr(S_ON) : tr(S_OFF), g_theme->row_text,
                          onoff_color(b));        // 3 benchmark extraction toggle
    // Off, the console never presents as a USB file-transfer device -- neither
    // "Connect to PC over USB" nor the background auto-start the inventory
    // server otherwise does on a cable connect (see InvServerPoll).
    bool mtp = g_prefs.mtp_enabled;
    this->layout->AddRow2(settings_label(tr(S_MTP_ENABLED)),
                          mtp ? tr(S_ON) : tr(S_OFF), g_theme->row_text,
                          onoff_color(mtp));      // 4 USB file transfer toggle
    // 5: read-only OS status — whether "USB 3.0" is enabled in System Settings.
    // One-shot query (setsys isn't kept open); shows Unknown if it can't be read.
    const char *usb3 = tr(S_UNKNOWN);
    if (R_SUCCEEDED(setsysInitialize())) {
        bool en = false;
        if (R_SUCCEEDED(setsysGetUsb30EnableFlag(&en))) {
            usb3 = en ? tr(S_ENABLED) : tr(S_DISABLED);
        }
        setsysExit();
    }
    this->layout->AddRow2(tr(S_USB3_STATUS), usb3, g_theme->row_text,
                          g_theme->rom_info_clr); // 5 read-only info row
    this->layout->AddRow(tr(S_RESET_DEFAULTS)); // 6 restore default settings — bottom
}

// About: a single credits card with the app badge, shown as a modal over
// Settings. B (or OK) dismisses it straight back to Settings. Release notes
// moved to Updates and the getting-started walk-through is its own top-level
// section now, so About is credits only.
// About: a dedicated page (no menu) that shows the credits prominently under a
// big app badge — the empty-state "instruction sheet" layout, reused. B backs
// out to Settings (handled in the input dispatch).
void MainApplication::GotoAbout() {
    // Big app badge, loaded once (280px master, crisp at empty-state size).
    static pu::sdl2::Texture logo = nullptr;
    if (!logo) {
        logo = pu::ui::render::LoadImageFromFile("romfs:/credits_logo.png");
        if (!logo) {
            logo = g_header_logo;
        }
    }
    this->screen = Screen::About;
    this->layout->SetTitle(tr(S_TITLE_ABOUT));
    this->layout->SetSubtitle(tr(S_SUB_ABOUT)); // "B back"
    this->layout->ClearMenu();
    std::string msg = std::string("HaulNX v") + APP_VERSION_STR +
                      " by digdat0\n\nPlutonium UI library provided by XorTroll";
    this->layout->SetEmptyState(logo, msg, "", true);
}

// ---- Help hub: Getting Started / How-To / Troubleshooting -----------------
// Three flat category rows; each opens a scrollable article list. Content is
// static (see ShowHelpCategory), so every screen here just (re)builds off the
// category/index the caller passes in — nothing is fetched or cached.
void MainApplication::GotoHelp() {
    this->screen = Screen::Help;
    this->layout->SetTitle(tr(S_TITLE_HELP));
    this->layout->SetSubtitle(tr(S_SUB_HELP));
    this->layout->ClearMenu();
    this->layout->AddRow(tr(S_GETTING_STARTED));      // 0
    this->layout->AddRow(tr(S_HELP_HOWTO));            // 1
    this->layout->AddRow(tr(S_HELP_TROUBLESHOOTING));  // 2
    this->layout->AddRow(tr(S_HELP_SEARCH));           // 3: keyword search (see GotoHelpSearch)
}

// One category's article list. Getting Started's row 0 is a live action (replay
// the guided tour) rather than an article, so it's spliced in ahead of the
// static list — ShowHelpArticle's idx is always into kGetStarted/kHowTo/kTrouble
// directly (never offset by that action row; OnInput subtracts 1 for it).
struct HelpArticle { int title; int body; };
static const HelpArticle kHelpGetStarted[] = {
    {S_GS1_TITLE, S_GS1_BODY}, {S_GS2_TITLE, S_GS2_BODY},
    {S_GS3_TITLE, S_GS3_BODY}, {S_GS4_TITLE, S_GS4_BODY},
    {S_GS5_TITLE, S_GS5_BODY}, {S_GS6_TITLE, S_GS6_BODY},
};
static const HelpArticle kHelpHowTo[] = {
    {S_HOWTO1_TITLE, S_HOWTO1_BODY},   {S_HOWTO2_TITLE, S_HOWTO2_BODY},
    {S_HOWTO3_TITLE, S_HOWTO3_BODY},   {S_HOWTO4_TITLE, S_HOWTO4_BODY},
    {S_HOWTO5_TITLE, S_HOWTO5_BODY},   {S_HOWTO6_TITLE, S_HOWTO6_BODY},
    {S_HOWTO7_TITLE, S_HOWTO7_BODY},   {S_HOWTO8_TITLE, S_HOWTO8_BODY},
    {S_HOWTO9_TITLE, S_HOWTO9_BODY},   {S_HOWTO10_TITLE, S_HOWTO10_BODY},
    {S_HOWTO11_TITLE, S_HOWTO11_BODY}, {S_HOWTO12_TITLE, S_HOWTO12_BODY},
    {S_HOWTO13_TITLE, S_HOWTO13_BODY}, {S_HOWTO14_TITLE, S_HOWTO14_BODY},
    {S_HOWTO15_TITLE, S_HOWTO15_BODY}, {S_HOWTO16_TITLE, S_HOWTO16_BODY},
    {S_HOWTO17_TITLE, S_HOWTO17_BODY}, {S_HOWTO18_TITLE, S_HOWTO18_BODY},
};
static const HelpArticle kHelpTrouble[] = {
    {S_TS1_TITLE, S_TS1_BODY},   {S_TS2_TITLE, S_TS2_BODY},
    {S_TS3_TITLE, S_TS3_BODY},   {S_TS4_TITLE, S_TS4_BODY},
    {S_TS5_TITLE, S_TS5_BODY},   {S_TS6_TITLE, S_TS6_BODY},
    {S_TS7_TITLE, S_TS7_BODY},   {S_TS8_TITLE, S_TS8_BODY},
    {S_TS9_TITLE, S_TS9_BODY},   {S_TS10_TITLE, S_TS10_BODY},
    {S_TS11_TITLE, S_TS11_BODY}, {S_TS12_TITLE, S_TS12_BODY},
};
// Category -> {article array, count, screen title string id}. Getting Started
// is index 0, matching GotoHelp's row order and Screen::Help's Sel().
static const HelpArticle *help_articles(int cat, size_t *n) {
    switch (cat) {
    case 1:  *n = sizeof(kHelpHowTo) / sizeof(kHelpHowTo[0]);   return kHelpHowTo;
    case 2:  *n = sizeof(kHelpTrouble) / sizeof(kHelpTrouble[0]); return kHelpTrouble;
    default: *n = sizeof(kHelpGetStarted) / sizeof(kHelpGetStarted[0]);
             return kHelpGetStarted;
    }
}
static int help_category_title(int cat) {
    switch (cat) {
    case 1:  return S_HELP_HOWTO;
    case 2:  return S_HELP_TROUBLESHOOTING;
    default: return S_GETTING_STARTED;
    }
}

void MainApplication::ShowHelpCategory(int cat) {
    this->help_cat = cat;
    this->screen = Screen::HelpTopics;
    this->layout->SetTitle(tr(help_category_title(cat)));
    this->layout->SetSubtitle(tr(S_SUB_HELP_TOPICS));
    this->layout->ClearMenu();
    if (cat == 0) {
        this->layout->AddRow(tr(S_REPLAY_TOUR)); // live action, not an article
    }
    size_t n = 0;
    const HelpArticle *arts = help_articles(cat, &n);
    for (size_t i = 0; i < n; i++) {
        this->layout->AddRow(tr(arts[i].title));
    }
}

// One article, rendered as an "instruction sheet" (icon + wrapped body) --
// the same layout Import/Export/USB-transfer already use for short numbered
// how-to text, which is exactly what these articles are.
void MainApplication::ShowHelpArticle(int cat, int idx) {
    size_t n = 0;
    const HelpArticle *arts = help_articles(cat, &n);
    if (idx < 0 || (size_t)idx >= n) {
        return;
    }
    this->help_cat = cat;
    this->screen = Screen::HelpArticle;
    this->layout->SetTitle(tr(arts[idx].title));
    this->layout->SetSubtitle(tr(S_SUB_HELP_ARTICLE));
    this->layout->ClearMenu();
    this->layout->SetEmptyState(console_icon("default"), "", tr(arts[idx].body),
                                true, "", "");
}

// Keyword search across every Help article's title + body. A query can be one
// word or several; an article matches only if *every* word appears somewhere
// in it (order doesn't matter), so adding words narrows results rather than
// broadening them -- "emulator folder" only turns up articles mentioning both.
// Reuses ci_contains (see near the top of this file) for each word.
static bool help_article_matches(const HelpArticle &a,
                                 const std::vector<std::string> &words) {
    const std::string hay = std::string(tr(a.title)) + " " + tr(a.body);
    for (const auto &w : words) {
        if (!ci_contains(hay.c_str(), w.c_str())) {
            return false;
        }
    }
    return true;
}

void MainApplication::RunHelpSearch(const std::string &query) {
    this->help_query = query;
    this->screen = Screen::HelpSearch;
    this->layout->SetTitle(tr(S_TITLE_HELP_SEARCH));
    this->layout->ClearMenu();

    std::vector<std::string> words;
    std::string w;
    for (char c : query) {
        if (isspace((unsigned char)c)) {
            if (!w.empty()) { words.push_back(w); w.clear(); }
        } else {
            w += c;
        }
    }
    if (!w.empty()) {
        words.push_back(w);
    }

    this->help_hits.clear();
    for (int cat = 0; cat <= 2; cat++) {
        size_t n = 0;
        const HelpArticle *arts = help_articles(cat, &n);
        for (size_t i = 0; i < n; i++) {
            if (help_article_matches(arts[i], words)) {
                this->help_hits.push_back({cat, (int)i});
            }
        }
    }

    if (this->help_hits.empty()) {
        this->layout->SetSubtitle(tr(S_SUB_HELP_SEARCH));
        this->layout->SetEmptyState(console_icon("default"), "",
                                    tr(S_HELP_SEARCH_NO_RESULTS), true, "", "");
        return;
    }
    this->layout->SetSubtitle(tr(S_SUB_HELP_SEARCH));
    for (const auto &h : this->help_hits) {
        size_t n = 0;
        const HelpArticle *arts = help_articles(h.first, &n);
        char row[192];
        snprintf(row, sizeof(row), "%s — %s", tr(help_category_title(h.first)),
                 tr(arts[h.second].title));
        this->layout->AddRow(row);
    }
}

// Help hub row 3 ("Search"): prompt for a keyword with the OS keyboard, then
// build the results list. Declining the keyboard (empty/cancelled) just
// leaves the Help hub showing -- there's nothing to search for yet.
void MainApplication::GotoHelpSearch() {
    char q[128] = {0};
    if (!prompt(tr(S_HELP_SEARCH_GUIDE), this->help_query.c_str(), q, sizeof(q))) {
        return;
    }
    this->RunHelpSearch(q);
}

// First-time guided walkthrough: a short run of Next/Back/Close dialogs
// (SideMenu under the hood, same as every other confirm/pick flow), ending in
// the existing Welcome() import/manual/later prompt. Reachable at first launch
// (no repos configured on any console yet) and any time after from
// Help > Getting Started > "Replay the guided tour".
void MainApplication::GuidedTour() {
    // Steps that are about one specific screen name it here, so the tour
    // actually opens that screen behind the dialog panel while describing it
    // -- "here's the Queue tab" shows the Queue tab, not whatever screen the
    // tour happened to start from. Steps about the app in general (welcome,
    // the closing PC-companion note) leave the background screen alone.
    enum class Page { None, Library, Browse, Queue, InstallFolders, Storage, Transfers };
    struct Step { int title; int body; Page page; };
    static const Step kSteps[] = {
        {S_TOUR1_TITLE, S_TOUR1_BODY, Page::Library},
        {S_TOUR2_TITLE, S_TOUR2_BODY, Page::Library},
        {S_TOUR3_TITLE, S_TOUR3_BODY, Page::Browse},
        {S_TOUR4_TITLE, S_TOUR4_BODY, Page::Queue},
        {S_TOUR5_TITLE, S_TOUR5_BODY, Page::InstallFolders},
        {S_TOUR6_TITLE, S_TOUR6_BODY, Page::Storage},
        {S_TOUR7_TITLE, S_TOUR7_BODY, Page::Transfers},
    };
    const int n = (int)(sizeof(kSteps) / sizeof(kSteps[0]));
    int i = 0;
    while (i >= 0 && i < n) {
        switch (kSteps[i].page) {
        case Page::Library:        this->GotoInstalled(roms_root(&g_tico)); break;
        case Page::Browse:         this->GotoHome();                       break;
        case Page::Queue:          this->GotoQueue();                      break;
        case Page::InstallFolders: this->GotoInstallFolders();             break;
        case Page::Storage:        this->GotoStorage();                   break;
        case Page::Transfers:      this->GotoTransfers();                 break;
        case Page::None:                                                  break;
        }
        // Goto*() only sets screen/title/rows -- the tab-bar highlight is
        // synced separately, once per frame, from HandleInput's outer loop
        // (see the `this->SyncTab()` call there). GuidedTour() runs entirely
        // inside CreateShowDialog's own blocking render loop and never
        // returns to that outer frame body, so without this call the tab
        // strip stays frozen on whatever tab was active when the tour
        // started even though the screen underneath is switching correctly.
        if (kSteps[i].page != Page::None) {
            this->SyncTab();
        }
        const bool last = (i == n - 1);
        std::vector<std::string> opts;
        opts.push_back(tr(last ? S_TOUR_DONE : S_TOUR_NEXT)); // 0
        if (i > 0) {
            opts.push_back(tr(S_TOUR_BACK));                  // 1
        }
        opts.push_back(tr(S_TOUR_CLOSE));                     // last: B/cancel too
        int r = this->CreateShowDialog(tr(kSteps[i].title), tr(kSteps[i].body),
                                       opts, true, {}, style_dialog);
        if (r == 0) {
            i++;
        } else if (i > 0 && r == 1) {
            i--;
        } else {
            break; // Close, or B (use_last_opt_as_cancel maps both here)
        }
    }
    if (i >= n) {
        this->Welcome(); // finished the tour: offer to actually add a collection
    }
}

// Diagnostics -> Export debug bundle: fold every log the app keeps into one
// file the user can pull off with the export flow (or the SD card) and attach
// to a bug report, instead of hunting five separate logs. The actual writer
// (diag_bundle_write, config.c) is shared with the inventory server's
// /debug_bundle.txt route (httpsrv.c), which regenerates the same file on
// every desktop-companion pull -- this button is just the manual, on-screen
// way to trigger it.
void MainApplication::ExportBundle() {
    if (!diag_bundle_write()) {
        this->ToastErr(tr(S_BUNDLE_FAIL));
        return;
    }
    char t[128];
    snprintf(t, sizeof(t), tr(S_BUNDLE_DONE), DIAG_BUNDLE_PATH);
    this->Toast(t);
}

// Worker for the network self-test: does the LAN check (instant) and a single
// small GET to archive.org, on net_selftest's own curl handle so it can't
// stall behind unrelated http_get() traffic and B can actually cancel it.
void MainApplication::DiagThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->diag_lan = httpsrv_local_ip(self->diag_ip, sizeof(self->diag_ip));
    self->diag_net = false;
    if (self->diag_lan && !self->diag_cancel) {
        self->diag_net =
            net_selftest("https://archive.org/robots.txt", &self->diag_cancel);
    }
    self->diag_net_cancelled = self->diag_cancel != 0;
    self->diag.done = true;
}

// Diagnostics -> Network self-test: kick the worker and show a spinner. The
// screen stays put; DiagTick reaps the result and shows it in a dialog. B
// cancels while it runs (see the diag.running dispatch in OnInput).
void MainApplication::NetSelfTest() {
    this->diag_speed = false;
    this->diag_lan = false;
    this->diag_net = false;
    this->diag_ip[0] = '\0';
    this->diag_cancel = 0;
    this->diag_net_cancelled = false;
    if (!this->diag.Start(&MainApplication::DiagThread, this)) {
        this->ToastErr(tr(S_SELFTEST_NET_FAIL));
        return;
    }
    this->layout->ClearMenu();
    // Same "B cancel" footer convention as the other cancellable spinners
    // (Search, box art scan, etc.) — see the diag.running dispatch in OnInput.
    this->layout->SetSubtitle(tr(S_SPEEDTEST_CANCEL_HINT));
    this->layout->ShowSpinner(tr(S_SELFTEST_RUNNING));
}

// One timestamped line per speed-test run in its own log (see SPEEDLOG_PATH):
// date, per-direction bytes transferred and rate, and the outcome. Mirrors
// xfer_log — small and infrequent, so the rotate check runs every call.
static void speed_log(const char *fmt, ...) {
    fs_log_rotate(SPEEDLOG_PATH, LOG_ROTATE_XFER);
    fs_mkdir_p(LOGS_DIR);
    FILE *f = fopen(SPEEDLOG_PATH, "a");
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

// Worker for the speed test: a timed HTTPS download then a timed upload of a
// fixed payload, both discarded — throughput only. Blocks, so it lives off the
// UI thread. sp_prog carries the live byte/rate counters the UI reads.
void MainApplication::SpeedThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    bool ok = net_speedtest_live(&self->sp_prog);
    const SpeedProg &p = self->sp_prog;
    // bytes/sec -> megabits/sec for the result dialog.
    self->diag_mbps = p.dl_bps * 8.0 / 1000000.0;
    self->diag_ul_mbps = p.ul_bps * 8.0 / 1000000.0;
    self->diag_sp_cancelled = p.cancel != 0;
    self->diag_sp_ok = ok;
    // Record the run (including failures/cancels) so the Logs section keeps a
    // history of throughput readings with the data actually transferred.
    const char *outcome = ok ? "ok" : (p.cancel ? "cancelled" : "failed");
    speed_log("DL %s @ %.1f Mbps   UL %s @ %.1f Mbps   [%s, Cloudflare]",
              human_size(p.dl_now).c_str(), p.dl_bps * 8.0 / 1000000.0,
              human_size(p.ul_now).c_str(), p.ul_bps * 8.0 / 1000000.0,
              outcome);
    self->diag.done = true;
}

// Diagnostics -> Speed test: kick the worker and open the live meter screen.
// Shares the diag BgTask + DiagTick reaper with the self-test; diag_speed tags
// the mode. Unlike the self-test, this one shows live progress and B cancels.
void MainApplication::SpeedTest() {
    this->diag_speed = true;
    this->diag_sp_ok = false;
    this->diag_sp_cancelled = false;
    this->diag_mbps = 0.0;
    this->diag_ul_mbps = 0.0;
    memset(&this->sp_prog, 0, sizeof(this->sp_prog)); // reset counters + cancel
    if (!this->diag.Start(&MainApplication::SpeedThread, this)) {
        this->ToastErr(tr(S_SPEEDTEST_FAIL));
        return;
    }
    this->screen = Screen::SpeedTest;
    this->layout->SetTitle(tr(S_SPEEDTEST));
    this->layout->SetSubtitle(tr(S_SPEEDTEST_SUB));
    this->layout->ClearMenu();
    this->SpeedRender(); // draw the initial (zeroed) meters
}

// Redraw the download/upload meters while the test runs. Rebuilds the two rows
// every tick like the Queue live view, throttled to ~10 Hz so the volatile
// rate text doesn't re-rasterize every frame.
void MainApplication::SpeedRender() {
    static u64 last = 0;
    u64 now = armGetSystemTick();
    if (last != 0 && armTicksToNs(now - last) < 100000000ULL) {
        return;
    }
    last = now;

    const SpeedProg &p = this->sp_prog;
    // One row: a phase label on the left, its live rate (+ ETA) on the right,
    // and a progress bar underneath. Accent blue while it's the live phase, a
    // full blue bar once it's finished, dim "--" before it starts.
    auto meter = [&](int phase, const char *label, uint64_t nowb, uint64_t total,
                     double bps) {
        bool active = (p.phase == phase);
        bool done = (p.phase > phase);
        float prog = (total > 0) ? (float)((double)nowb / (double)total) : -1.0f;
        if (prog > 1.0f) prog = 1.0f;
        char right[64];
        double mbps = bps * 8.0 / 1000000.0;
        if (active || done) {
            if (!done && total > nowb && bps > 0.0) {
                uint64_t eta = (uint64_t)((double)(total - nowb) / bps);
                snprintf(right, sizeof(right), "%.1f Mbps  ~%s", mbps,
                         human_eta(eta).c_str());
            } else {
                snprintf(right, sizeof(right), "%.1f Mbps", mbps);
            }
        } else {
            snprintf(right, sizeof(right), "--");
        }
        pu::ui::Color lc = g_theme->row_text;
        pu::ui::Color rc = active ? accent_blue() : value_color();
        int bar = 0;
        if (done) {
            prog = 1.0f;
            bar = 1; // solid blue bar, matching a completed queue item
        }
        this->layout->AddRow2(label, right, lc, rc, prog, nullptr, "", active,
                              false, false, bar);
    };

    this->layout->ClearMenu(false); // rebuilt every tick: no enter fade
    meter(SP_DOWNLOAD, tr(S_SPEEDTEST_DOWNLOAD), p.dl_now, p.dl_total, p.dl_bps);
    meter(SP_UPLOAD, tr(S_SPEEDTEST_UPLOAD), p.ul_now, p.ul_total, p.ul_bps);
    this->layout->SetRomInfo(tr(S_SPEEDTEST_CANCEL_HINT));
}

void MainApplication::DiagTick() {
    // Speed test: keep the live meters ticking until the worker finishes.
    if (this->diag_speed && !this->diag.done) {
        this->SpeedRender();
        return;
    }
    if (!this->diag.done) {
        return;
    }
    this->diag.Join();
    if (this->diag_speed) {
        this->diag_speed = false;
        // Cancelled mid-transfer: just drop back to Diagnostics, no dialog.
        if (this->diag_sp_cancelled) {
            this->GotoDiagnostics();
            this->layout->SetSel(0); // land back on the Speed test row
            return;
        }
        char body[192];
        if (this->diag_sp_ok) {
            snprintf(body, sizeof(body), tr(S_SPEEDTEST_RESULT),
                     this->diag_mbps, this->diag_ul_mbps);
        } else {
            snprintf(body, sizeof(body), "%s", tr(S_SPEEDTEST_FAIL));
        }
        this->CreateShowDialog(tr(S_SPEEDTEST), body, {tr(S_OK)}, true, {},
                               style_dialog);
        this->GotoDiagnostics();
        this->layout->SetSel(0);
        return;
    }
    // Self-test cancelled mid-request: same as the speed test, just drop back
    // without a result dialog — a cancelled check has no verdict to report.
    if (this->diag_net_cancelled) {
        this->diag_net_cancelled = false;
        this->GotoDiagnostics();
        this->layout->SetSel(0);
        return;
    }
    char lan_line[96];
    if (this->diag_lan) {
        snprintf(lan_line, sizeof(lan_line), tr(S_SELFTEST_LAN_OK),
                 this->diag_ip);
    } else {
        snprintf(lan_line, sizeof(lan_line), "%s", tr(S_SELFTEST_LAN_FAIL));
    }
    const char *net_line =
        this->diag_net ? tr(S_SELFTEST_NET_OK) : tr(S_SELFTEST_NET_FAIL);
    char body[256];
    snprintf(body, sizeof(body), tr(S_SELFTEST_RESULT), lan_line, net_line);
    this->CreateShowDialog(tr(S_NET_SELFTEST), body, {tr(S_OK)}, true, {},
                           style_dialog);
    this->GotoDiagnostics();
}

// Diagnostics -> Reset settings to defaults: wipe prefs.json and reload the
// built-in defaults, then re-apply the runtime state that tracks a pref
// (queue slots, rate caps, theme, language, extraction knobs, ROM root).
// Collections, downloads and credentials are on their own files and untouched.
void MainApplication::ResetDefaults() {
    if (!this->ConfirmDanger(tr(S_RESET_DEFAULTS),
                             tr(S_RESET_DEFAULTS_CONFIRM))) {
        return;
    }
    remove(PREFS_PATH);
    prefs_load(&g_prefs);   // reproduces the first-run default state
    prefs_save(&g_prefs);
    // Push the reloaded defaults into everything that caches a pref.
    queue_set_max_dl(g_prefs.max_downloads);
    apply_rate_limits();
    apply_extract_tunables();
    queue_set_keep_archives(g_prefs.keep_archives);
    select_theme();
    this->layout->ApplyTheme();
    i18n_load(NULL);        // lang reset to English
    this->layout->RefreshTabs();
    this->SyncTab();
    tico_init(&g_tico);
    tico_set_roms_override(&g_tico, g_prefs.roms_override);
    this->inst_path = roms_root(&g_tico);
    this->Toast(tr(S_RESET_DONE));
    this->GotoDiagnostics();
}

// Append a timestamped line to the collection-transfer log. An import replaces
// dl_sources.json outright, so what arrived and what became of it is worth a
// durable record — the one .bak slot only survives until the next import.
static void xfer_log(const char *fmt, ...) {
    // A handful of lines per transfer, so the size check can run every time.
    fs_log_rotate(XFERLOG_PATH, LOG_ROTATE_XFER);
    fs_mkdir_p(LOGS_DIR);
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

// Read an NRO's display version from its embedded NACP without loading the whole
// file: homebrew NROs carry an "ASET" section after the image, and the NACP's
// display_version sits at 0x3060 inside it (see nro_buf_version). Best-effort —
// returns false (out="") for a non-NRO or a stripped build. Streams from disk so
// the inventory scan can version every /switch app cheaply.
static bool nro_file_version(const char *path, char *out, size_t out_sz) {
    out[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    char hdr[0x40];
    bool ok = false;
    if (fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
        memcmp(hdr + 0x10, "NRO0", 4) == 0) {
        u32 img = 0;
        memcpy(&img, hdr + 0x18, 4); // total image size; the ASET header follows
        char aset[0x38];
        if (img != 0 && fseek(f, (long)img, SEEK_SET) == 0 &&
            fread(aset, 1, sizeof(aset), f) == sizeof(aset) &&
            memcmp(aset, "ASET", 4) == 0) {
            u64 noff = 0, nsz = 0;
            memcpy(&noff, aset + 0x18, 8); // NACP offset in the asset section
            memcpy(&nsz, aset + 0x20, 8);
            if (nsz >= 0x3070 &&
                fseek(f, (long)((u64)img + noff + 0x3060), SEEK_SET) == 0) {
                char dv[0x11];
                size_t got = fread(dv, 1, 0x10, f); // 0x10 bytes, NUL-padded
                dv[got] = '\0';
                size_t m = out_sz - 1 < 0x10 ? out_sz - 1 : 0x10;
                memcpy(out, dv, m);
                out[m] = '\0';
                ok = out[0] != '\0';
            }
        }
    }
    fclose(f);
    return ok;
}

// ---- desktop-companion inventory ------------------------------------------
// Regenerate INVENTORY_PATH: a read-only snapshot of what's on the device for
// the app utility to pull over the LAN. Reuses the same scanners the Installed
// tab and Storage screen use, so the numbers match the app exactly. Written to
// a staging file and moved into place so a reader never sees a half-written
// document. Runs on the UI thread (see InvServerPoll), so it never races the
// server that serves the file.
void MainApplication::WriteInventoryJson() {
    // One-shot synchronous build (InvServerStart only), so inventory.json
    // exists before a companion's first GET. This just drives the same steps
    // InvServerPoll spreads across frames to completion in one call — see
    // inv_json_ci's header comment for why the split exists at all.
    this->InvJsonBegin();
    while (this->inv_json_ci >= 0) {
        this->InvJsonStep();
    }
}

void MainApplication::InvJsonBegin() {
    FILE *f = fopen(INVENTORY_TMP_PATH, "wb");
    if (!f) {
        return; // leave inv_json_ci at -1; the next due regen just retries
    }
    this->inv_json_f = f;
    uint64_t freeb = fs_free_bytes("sdmc:/");
    uint64_t totalb = fs_total_bytes("sdmc:/");
    // The USB-3 enable flag only changes via a System Settings toggle (which
    // needs a reboot), so read it once instead of opening a setsys session on
    // every regen. Safe as a static: this and the rest of the inv_json_* build
    // run only on the UI thread (see the header comment).
    static const char *usb3 = NULL;
    if (!usb3) {
        usb3 = "unknown";
        if (R_SUCCEEDED(setsysInitialize())) {
            bool en = false;
            if (R_SUCCEEDED(setsysGetUsb30EnableFlag(&en))) {
                usb3 = en ? "enabled" : "disabled";
            }
            setsysExit();
        }
    }

    fputs("{\n  \"app_version\": ", f);
    json_write_escaped(f, APP_VERSION_STR);
    // Console-initiated push signal: a connected companion baselines this and
    // adopts the device's sources/list when it changes (see PushListToPc). The
    // device can't open a socket back to the PC, so this rides the poll the
    // companion already does (Wi-Fi inventory.json or USB MTP read). 0 = never.
    fprintf(f, ",\n  \"push_rev\": %llu",
            (unsigned long long)this->inv_push_rev);
    fprintf(f, ",\n  \"free_bytes\": %llu,\n  \"total_bytes\": %llu,\n",
            (unsigned long long)(freeb == UINT64_MAX ? 0 : freeb),
            (unsigned long long)(totalb == UINT64_MAX ? 0 : totalb));
    fputs("  \"usb3\": ", f);
    json_write_escaped(f, usb3);
    fprintf(f, ",\n  \"sd_access\": %s",
            g_prefs.sd_full_access ? "true" : "false");
    fputs(",\n  \"consoles\": [", f);

    // Rebuilt in lockstep with the JSON, one console at a time in
    // InvJsonStepConsole — kept apart from inv_roots (which inv_srv.roots
    // borrows) until InvJsonFinish; see the header comment on inv_json_roots.
    this->inv_json_roots.clear();
    this->inv_json_first_console = true;
    this->inv_json_ci = 0; // next step scans console 0, or finalizes if there are none
}

void MainApplication::InvJsonStep() {
    if (this->inv_json_ci < g_cfg.console_count) {
        this->InvJsonStepConsole();
    } else {
        this->InvJsonFinish();
    }
}

// One console's worth of the sweep -- the expensive part (a directory listing
// plus set-detection over it, potentially thousands of entries), which is why
// this is the unit InvJsonStep spreads one-per-frame instead of the whole
// console loop running in a single call. Identical output to the old
// single-pass loop body, just addressing g_cfg.consoles[inv_json_ci] instead
// of iterating i itself.
void MainApplication::InvJsonStepConsole() {
    FILE *f = this->inv_json_f;
    int i = this->inv_json_ci;
    ConsoleGroup &c = g_cfg.consoles[i];
    const char *custom = install_folder_for(c.target);
    std::string dir = (custom && custom[0])
                          ? std::string(custom)
                          : std::string(roms_root(&g_tico)) + "/" + c.target;
    this->inv_json_roots += dir;
    this->inv_json_roots += '\n';
    int count = 0, raw = 0;
    uint64_t bytes = 0;
    // Recursive size, cached like Installed. The companion pairs "count"
    // with the "files" array written below, so it wants the raw file
    // count even when the Library chips show games — inst_dir_stats
    // already knows that number internally, so take it from raw_out
    // instead of paying for a second full readdir here.
    inst_dir_stats(dir, &count, &bytes, &raw);
    count = raw;

    std::string datp = std::string(DATS_DIR) + "/" + c.target + ".dat";
    struct stat ds;
    bool has_dat = stat(datp.c_str(), &ds) == 0;
    uint64_t dat_size = has_dat ? (uint64_t)ds.st_size : 0;

    int active = 0;
    for (int r = 0; r < c.repo_count; r++) {
        if (c.repos[r].enabled) {
            active++;
        }
    }
    const char *full = console_full_name(c.target);

    if (!this->inv_json_first_console) {
        fputc(',', f);
    }
    this->inv_json_first_console = false;
    fputs("\n    {\n      \"key\": ", f);
    json_write_escaped(f, c.target);
    fputs(",\n      \"name\": ", f);
    json_write_escaped(f, full ? full : c.target);
    fputs(",\n      \"folder\": ", f);
    json_write_escaped(f, dir.c_str()); // companion joins folder + file name
    fprintf(f,
            ",\n      \"count\": %d,\n      \"bytes\": %llu,\n"
            "      \"has_dat\": %s,\n      \"dat_bytes\": %llu,\n"
            "      \"repo_count\": %d,\n      \"active_repos\": %d,\n"
            "      \"shown\": %s,\n      \"shown_installed\": %s,\n",
            count, (unsigned long long)bytes, has_dat ? "true" : "false",
            (unsigned long long)dat_size, c.repo_count, active,
            c.shown ? "true" : "false",
            c.shown_installed ? "true" : "false");
    fputs("      \"files\": [", f);
    bool first = true;
    // Read once, reused below for "sets" too -- one listing per console,
    // same as before this existed.
    std::vector<DirEnt> dents = list_dir(dir);
    for (const auto &e : dents) {
        if (e.is_dir) {
            continue; // list the games, not any nested folders
        }
        fputs(first ? "\n        {\"name\": " : ",\n        {\"name\": ", f);
        first = false;
        json_write_escaped(f, e.name.c_str());
        fprintf(f, ", \"size\": %llu}", (unsigned long long)e.size);
    }
    // Multi-file games (a .cue with its .bin tracks, an m3u's per-disc
    // set): every member is still listed above in "files" so an older
    // companion build keeps working unchanged, but a set-aware one can use
    // this to show one row per game and, critically, to delete/move every
    // member together instead of orphaning the rest of a set. Empty when
    // grouping is off on the console, matching the Library's own display.
    fputs("\n      ],\n      \"sets\": [", f);
    if (g_prefs.group_sets) {
        bool sfirst = true;
        for (const auto &g : inst_detect_groups(dir, dents)) {
            fputs(sfirst ? "\n        {\"name\": " : ",\n        {\"name\": ",
                  f);
            sfirst = false;
            json_write_escaped(f, g.name.c_str());
            fprintf(f, ", \"size\": %llu, \"members\": [",
                    (unsigned long long)g.size);
            bool mfirst = true;
            for (const auto &m : g.members) {
                fputs(mfirst ? "" : ", ", f);
                mfirst = false;
                json_write_escaped(f, m.c_str());
            }
            fputs("]}", f);
        }
    }
    fputs("\n      ]\n    }", f);
    this->inv_json_ci++; // next step does the next console, or finalizes
}

// Everything after the per-console loop: the inbox listing, the switch-apps
// / retroarch scan, and the footer -- all flat, single-directory listings, so
// (unlike the per-console loop) doing them in one shot here isn't worth
// spreading further. Closes the file, publishes it, and swaps the just-built
// root list into inv_roots/inv_srv.roots.
void MainApplication::InvJsonFinish() {
    FILE *f = this->inv_json_f;
    fputs("\n  ],\n  \"inbox\": [", f);
    {
        bool first = true;
        for (const auto &e : list_dir(INBOX_DIR)) {
            if (e.is_dir) {
                continue;
            }
            fputs(first ? "\n    {\"name\": " : ",\n    {\"name\": ", f);
            first = false;
            json_write_escaped(f, e.name.c_str());
            fprintf(f, ", \"size\": %llu}", (unsigned long long)e.size);
        }
    }
    this->inv_json_roots += INBOX_DIR; // the inbox is a pull/delete root too
    this->inv_json_roots += '\n';
    fputs("\n  ],\n  \"inbox_folder\": ", f);
    json_write_escaped(f, INBOX_DIR);
    // Raw listings of the two common emulator locations. The app stays "dumb":
    // the companion's bundled manifest decides which of these are emulators.
    fputs(",\n  \"scan\": {\n    \"switch_apps\": [", f);
    // Every .nro under sdmc:/switch, including one level of subfolders (some
    // emulators live at sdmc:/switch/<name>/<name>.nro). Loader sidecars like
    // foo.nro.star are skipped — only real apps count. The name list stays a
    // plain string array for the older companion; versions ride alongside in
    // switch_app_versions, keyed by the same name.
    std::vector<std::pair<std::string, std::string>> app_vers;
    // Per-file records: the same .nro name can exist in several subfolders (e.g.
    // switch/dbi/dbi.nro vs switch/dbi_ru/dbi.nro at different versions), so keep
    // path + version per file. A name-keyed map would collide and drop one.
    struct AppRec { std::string name, path, ver; };
    std::vector<AppRec> app_list;
    {
        bool first = true;
        char vbuf[24];
        auto ends_nro = [](const std::string &n) {
            return n.size() > 4 &&
                   strcasecmp(n.c_str() + n.size() - 4, ".nro") == 0;
        };
        auto emit = [&](const std::string &full, const std::string &name) {
            fputs(first ? " " : ", ", f);
            first = false;
            json_write_escaped(f, name.c_str());
            std::string ver;
            if (nro_file_version(full.c_str(), vbuf, sizeof(vbuf)) && vbuf[0]) {
                ver = vbuf;
                app_vers.emplace_back(name, ver);
            }
            app_list.push_back({name, full, ver});
        };
        for (const auto &e : list_dir("sdmc:/switch")) {
            if (e.is_dir) {
                std::string sub = std::string("sdmc:/switch/") + e.name;
                for (const auto &f2 : list_dir(sub.c_str())) {
                    if (f2.is_dir || !ends_nro(f2.name)) {
                        continue;
                    }
                    emit(sub + "/" + f2.name, f2.name);
                }
            } else if (ends_nro(e.name)) {
                emit(std::string("sdmc:/switch/") + e.name, e.name);
            }
        }
    }
    fputs(" ],\n    \"switch_app_versions\": {", f);
    {
        bool first = true;
        for (const auto &p : app_vers) {
            fputs(first ? " " : ", ", f);
            first = false;
            json_write_escaped(f, p.first.c_str());
            fputs(": ", f);
            json_write_escaped(f, p.second.c_str());
        }
    }
    fputs(" },\n    \"switch_app_list\": [", f);
    {
        bool first = true;
        for (const auto &a : app_list) {
            fputs(first ? "\n      {\"name\": " : ",\n      {\"name\": ", f);
            first = false;
            json_write_escaped(f, a.name.c_str());
            fputs(", \"path\": ", f);
            json_write_escaped(f, a.path.c_str());
            fputs(", \"ver\": ", f);
            json_write_escaped(f, a.ver.c_str());
            fputc('}', f);
        }
    }
    fputs("\n    ],\n    \"retroarch_cores\": [", f);
    {
        bool first = true;
        for (const auto &e : list_dir("sdmc:/retroarch/cores")) {
            if (e.is_dir) {
                continue;
            }
            fputs(first ? " " : ", ", f);
            first = false;
            json_write_escaped(f, e.name.c_str());
        }
    }
    fputs(" ]\n  }\n}\n", f);
    fclose(f);
    this->inv_json_f = NULL;
    fs_move(INVENTORY_TMP_PATH, INVENTORY_PATH);
    // Swap the just-built root list in now that it's complete — a mid-build
    // frame never re-points inv_srv.roots, so httpsrv_poll (also UI-thread,
    // possibly running in between two of these frame-spread steps) always sees
    // either the previous full set or this new full set, never a partial one.
    this->inv_roots = std::move(this->inv_json_roots);
    this->inv_srv.roots = this->inv_roots.c_str();
    this->inv_json_ci = -1; // idle again
}

void MainApplication::InvServerStart() {
    if (this->inv_open) {
        return;
    }
    if (!httpsrv_open_port(&this->inv_srv, HTTPSRV_INV_PORT)) {
        this->ToastErr(tr(S_IMPORT_SRV_FAIL));
        g_prefs.inv_server = false;
        prefs_save(&g_prefs);
        return;
    }
    this->inv_srv.mode = HTTPSRV_MODE_INVENTORY;
    this->inv_srv.sd_access = g_prefs.sd_full_access;
    // A game pushed from the app utility while connected streams into the inbox,
    // where the sorter files it — the same landing spot USB/MTP drops use. Set
    // once: client_reset keeps dest_dir across uploads.
    fs_mkdir_p(INBOX_DIR);
    snprintf(this->inv_srv.dest_dir, sizeof(this->inv_srv.dest_dir), "%s",
             INBOX_DIR);
    // A persistent code (unlike the transfer server's per-session token), so the
    // companion can reconnect any time the toggle is on. Generated once, the same
    // rejection-sampled digits httpsrv_open uses, then shown in Settings.
    if (!g_prefs.inv_code[0]) {
        for (int i = 0; i < HTTPSRV_TOKEN_LEN; i++) {
            unsigned char b;
            do {
                randomGet(&b, 1);
            } while (b >= 250);
            g_prefs.inv_code[i] = (char)('0' + (b % 10));
        }
        g_prefs.inv_code[HTTPSRV_TOKEN_LEN] = '\0';
        prefs_save(&g_prefs);
    }
    snprintf(this->inv_srv.token, sizeof(this->inv_srv.token), "%s",
             g_prefs.inv_code);
    this->inv_open = true;
    this->inv_last_gen_ns = 0; // force a regen on the next poll
    // Baseline the sleep/wake link watcher: we just bound with the link up, so a
    // stale "down" from a previous session can't force a needless rebind.
    this->inv_link_up = true;
    this->inv_link_ck_ns = 0;
    this->inv_wake_ck_s = 0; // seed the resume watcher on the first poll
    this->WriteInventoryJson();
}

void MainApplication::InvServerStop() {
    if (!this->inv_open) {
        return;
    }
    httpsrv_close(&this->inv_srv);
    this->inv_open = false;
    // Abandon a mid-build regen rather than leaving its tmp file handle open
    // forever: the server is going away, so there's no companion left to read
    // the result anyway. The next InvServerStart does a fresh synchronous
    // WriteInventoryJson, which reopens INVENTORY_TMP_PATH from scratch.
    if (this->inv_json_f) {
        fclose(this->inv_json_f);
        this->inv_json_f = NULL;
    }
    this->inv_json_ci = -1;
    this->inv_json_roots.clear();
    // The USB companion link is part of the inventory server: tear the background
    // MTP instance down with it (frees usb:ds so the port can host a controller
    // again). If the connect screen is open it owns the link, so leave that alone.
    if (this->usb_bg && !this->usb_open) {
        this->UsbMtpStop();
    } else {
        this->usb_bg = false; // screen owns it now; leaving the screen will stop it
    }
}

// Per-frame service, called from the top of HandleInput so it runs on every
// screen. Regenerates the inventory at most ~every 4s, and never while a
// transfer upload is draining to the card (keep the two off each other's back).
void MainApplication::InvServerPoll() {
    if (!this->inv_open) {
        return;
    }
    uint64_t now = armGetSystemTick();
    // Resume-from-sleep recovery. The link-edge/lease check below misses the
    // common case: a short sleep that keeps the same DHCP lease. The app is frozen
    // while asleep, so on wake inv_link_up is still the pre-sleep "up" and the
    // address is unchanged — no edge fires — yet the listening socket died with the
    // interface sleep tore down, leaving the server "on" but deaf until the user
    // toggles it off/on. This poll runs every frame (~16ms), so a jump of several
    // seconds in the RTC (which keeps counting through sleep, unlike frame cadence)
    // between two consecutive polls can only be a suspend/resume. Force one
    // unconditional rebind on that gap. Skip it while a transfer is actively moving
    // bytes: that proves we're awake, and a rebind would needlessly drop it. ~8s is
    // safely above any UI hitch (a heavy library scan can stall frames for a few
    // seconds) yet well under any real sleep.
    int64_t wall = (int64_t)time(NULL);
    if (this->inv_wake_ck_s != 0 && wall - this->inv_wake_ck_s >= 8 &&
        !httpsrv_receiving(&this->inv_srv, NULL, NULL)) {
        if (httpsrv_rebind(&this->inv_srv)) {
            this->inv_srv.roots = this->inv_roots.c_str();
            xfer_log("inv        resumed from sleep (%llds gap); rebound on :%d",
                     (long long)(wall - this->inv_wake_ck_s), HTTPSRV_INV_PORT);
            if (this->inv_recv_active) {
                this->LiveRecvEnd(false);
            }
            this->inv_was_receiving = false;
            // A fresh socket on a possibly-new address: keep the link watcher in
            // step so it doesn't also rebind on the next tick.
            this->inv_link_up = this->inv_srv.ip[0] != '\0';
            this->inv_link_ck_ns = now;
        }
        // If the rebind failed (Wi-Fi still reassociating), leave inv_wake_ck_s
        // unchanged so the next poll retries; otherwise fall through to reseed.
    }
    if (this->inv_wake_ck_s == 0 || this->inv_srv.listen_fd >= 0) {
        this->inv_wake_ck_s = wall;
    }
    // Sleep/wake recovery. Putting the console to sleep (or into low-power "sleep"
    // from the power menu) drops Wi-Fi, which leaves inv_srv listening on an
    // interface that no longer exists — the toggle still reads "on" but the
    // companion can't connect until the user disables and re-enables it. Watch the
    // link and, on a down->up edge (or a lease change), recreate the socket on the
    // fresh interface. nifm is an IPC call, so poll it at ~2s, not every frame.
    if (this->inv_link_ck_ns == 0 ||
        armTicksToNs(now - this->inv_link_ck_ns) >= 2000000000ULL) {
        this->inv_link_ck_ns = now;
        char ip[46];
        bool up = httpsrv_local_ip(ip, sizeof(ip));
        // Rebind when the link returns after a drop, or when it's up but under a
        // different address than the socket bound to (a wake can renew the DHCP
        // lease without our ever seeing the link go fully down).
        bool addr_changed =
            up && this->inv_srv.ip[0] && strcmp(ip, this->inv_srv.ip) != 0;
        if (up && (!this->inv_link_up || addr_changed)) {
            if (httpsrv_rebind(&this->inv_srv)) {
                this->inv_srv.roots = this->inv_roots.c_str(); // re-point (belt & suspenders)
                xfer_log("inv        Wi-Fi returned; rebound inventory server on :%d",
                         HTTPSRV_INV_PORT);
                // Any push that was in flight when we slept died with the old
                // interface; rebind dropped it, so clear its Queue-tab item too.
                if (this->inv_recv_active) {
                    this->LiveRecvEnd(false);
                }
                this->inv_was_receiving = false;
            } else {
                // Wi-Fi is reassociating but not fully up yet: leave the link
                // marked down so the next poll retries the rebind.
                up = false;
            }
        }
        this->inv_link_up = up;
    }
    // Keep the USB/MTP responder up in the background while the inventory server
    // is on, so a USB-connected companion is recognized without opening the
    // connect screen (parity with the always-on Wi-Fi server). The screen, when
    // open, owns the link; only manage the background instance here. Bring-up
    // fails while docked (the system owns USB), so retry on a slow cadence to
    // catch an undock. PollXfers services the link once it's up.
    if (g_prefs.mtp_enabled && !this->usb_open && !this->usb_bg &&
        (this->usb_bg_retry_ns == 0 ||
         armTicksToNs(now - this->usb_bg_retry_ns) >= 5000000000ULL)) {
        this->usb_bg_retry_ns = now;
        // inventory.json already exists (InvServerStart wrote it), so it's present
        // for the MTP host's first root enumeration the moment the link comes up.
        if (this->UsbResponderStart()) {
            this->usb_bg = true;
            this->usb_seen_conn = false;
        }
    }
    // A live-link push cancelled from its Queue-tab item: drop the in-flight
    // connection (keeps the always-on server listening) and mark the item failed.
    if (this->live_xslot >= 0 && queue_ext_cancelled(this->live_xslot)) {
        httpsrv_abort(&this->inv_srv);
        this->LiveRecvEnd(false);
        this->inv_was_receiving = false;
        return;
    }
    size_t rnow = 0, rtot = 0;
    bool recv = httpsrv_receiving(&this->inv_srv, &rnow, &rtot);
    // "Busy" = something is actively moving bytes, so the filesystem sweep below
    // must stand down. queue_io_active() covers every real download, verify and
    // extract as well as every external receive (Wi-Fi ROM, USB copy, live-link
    // push all ride as Q_DOWNLOADING) — the earlier httpsrv-only check missed a
    // plain queue download or a USB copy, so the recursive rescan kept firing on
    // the render thread mid-transfer, hitching the UI and fighting the transfer
    // for SD I/O. The httpsrv_receiving checks stay as belt-and-suspenders for the
    // instant before an incoming body becomes a queue item.
    // A completed push (Wi-Fi game, collection, DAT, ...) leaves a short cooldown
    // (see inv_push_cd_tick) so the very next frame's regen check below can't land
    // in the gap between one file's response and the desktop's near-instant
    // reconnect for the next one in a multi-file queue.
    bool push_cooldown =
        this->inv_push_cd_tick != 0 &&
        armTicksToNs(now - this->inv_push_cd_tick) < 3000000000ULL;
    bool busy = (this->imp_open && httpsrv_receiving(&this->imp_srv, NULL, NULL)) ||
                recv || queue_io_active() || this->pxt.running || push_cooldown;
    // Only sweep the filesystem to rebuild the JSON while a companion is actually
    // reading it — i.e. it polled inventory.json within the last 15s (the same
    // window the Tools panel calls "connected"; last_inv_ns is stamped on each
    // GET). With the toggle left on but no companion open, this otherwise ran a
    // full recursive scan every 4s forever, which made the whole UI sluggish.
    //
    // The rebuild (per-console directory scan + two statvfs) runs on the render
    // thread, spread one console per frame (InvJsonStep) rather than one big
    // call, so a companion polling every few seconds no longer costs a single
    // fat hitch each time — just several much smaller ones. 15s staleness is
    // fine for a free-space / install-count readout, so kickoffs are still
    // throttled to that cadence on top. A data change (inv_last_gen_ns reset to
    // 0) still regenerates at once, so the companion still sees edits promptly.
    bool companion_active =
        this->inv_srv.last_inv_ns != 0 &&
        armTicksToNs(now) - this->inv_srv.last_inv_ns <= 15000000000ULL;
    if (this->inv_json_ci < 0) {
        // Idle: kick off a fresh regen once it's actually due. Stamped at
        // kickoff (not completion) so the still-mid-build frames below can't
        // be mistaken for "due again" while inv_last_gen_ns is still 0/stale.
        if (companion_active && !busy &&
            (this->inv_last_gen_ns == 0 ||
             armTicksToNs(now - this->inv_last_gen_ns) >= 15000000000ULL)) {
            this->inv_last_gen_ns = now;
            this->InvJsonBegin();
        }
    } else if (!busy) {
        // Mid-build: advance by exactly one console (or the finalize step)
        // this frame -- see inv_json_ci's header comment for why this isn't
        // just one big call like it used to be. Paused (not abandoned) while
        // busy, same as the old gate did for starting one at all.
        this->InvJsonStep();
    }
    // A push arriving over the live link gets a transient "Receiving from PC"
    // page — opened once per transfer (edge-detected on the receiving state),
    // updated each frame. It is informational only: the poll below advances and
    // finishes the transfer whatever screen is up, so leaving never interrupts it.
    if (recv && !this->inv_was_receiving) {
        this->LiveRecvBegin();
    }
    if (this->inv_recv_active && recv) {
        this->LiveRecvTick(rnow, rtot);
    }
    this->inv_was_receiving = recv;

    // The always-on server also accepts writes from the app utility while it is
    // connected: a buffered collection or .nro push (into s->body), or a game
    // streamed to the inbox (body NULL, part_path/recv_name set). Tell them apart
    // by which the poll left behind.
    int r = httpsrv_poll(&this->inv_srv);
    if (r == 1) {
        this->inv_push_cd_tick = now; // see the field comment: cooldown starts now
        if (this->inv_srv.body) {
            this->InvApplyPush();
        } else {
            this->InvApplyFile();
        }
        if (this->inv_recv_active) {
            this->LiveRecvEnd();
        }
    } else if (r == 4) {
        xfer_log("push       PC game transfer aborted: %s", this->inv_srv.last_err);
        this->ToastErr(tr(S_ROM_RECV_FAIL));
        if (this->inv_recv_active) {
            this->LiveRecvEnd(false);
        }
    }
}

// Defined below find_nro_in_dir (needs it); forward-declared here because
// InvApplyPush, which calls it, comes first in the file.
static bool try_unpack_nro_push(char **body, size_t *len);

// Apply a collection pushed to the always-on inventory server. Mirrors
// ImportApply's collection branch, minus the on-device confirm: the push is
// token-gated by the persistent code, and reversible via the backup
// config_import_json keeps (Settings › restore a previous collection). No modal
// here — InvServerPoll runs on every screen.
void MainApplication::InvApplyPush() {
    char *body = this->inv_srv.body;
    size_t len = this->inv_srv.body_len;
    bool is_dat = this->inv_srv.recv_dat;
    this->inv_srv.recv_dat = false;
    this->inv_srv.body = NULL; // owned here now; httpsrv_close must not free it
    if (!body) {
        return;
    }
    // A verification DAT pushed from the companion's DAT Files tab (X-Dat). It is
    // filed by its own header, so it can't land on the wrong console; apply it
    // without a modal, like the collection push below.
    if (is_dat) {
        this->InvApplyDat(body, len); // takes ownership of body
        return;
    }
    // The .nro can also arrive zipped (or in any archive extract_archive/RAR3
    // reads) -- unpack it and fall straight into the NRO0 check below with
    // the found build's bytes instead. A leave-alone no-op for anything that
    // isn't an archive containing an .nro.
    try_unpack_nro_push(&body, &len);
    // An .nro build pushed instead of a collection (app utility › Device Transfer
    // › App update): stage it as an update, same as the LAN receiver, but confirm
    // on-screen since it replaces the running app.
    if (len > 0x18 && memcmp(body + 0x10, "NRO0", 4) == 0) {
        this->InvApplyNro(body, len); // takes ownership of body
        return;
    }
    // The desktop can also push the shared update manifest (update_sources.json)
    // to keep the on-device sources in sync with what it edits — a sources
    // manifest is distinct from a collection (top-level "sources" array, no
    // consoles), so route it before the collection import below.
    {
        UpdSource *srcs = (UpdSource *)malloc(sizeof(UpdSource) * UPD_MAX);
        int n = srcs ? updman_parse_buf(body, len, srcs, UPD_MAX) : 0;
        if (n > 0) {
            bool ok = updman_save(srcs, n);
            free(srcs);
            free(body);
            if (ok) {
                xfer_log("push       PC updated %d update source(s)", n);
                this->inv_last_gen_ns = 0; // refresh inventory (repos changed)
                this->Toast(tr(S_UPDSRC_PUSHED));
            } else {
                this->ToastErr(tr(S_IMPORT_SAVE_FAIL));
            }
            return;
        }
        free(srcs);
    }
    int consoles = 0, repos = 0;
    if (!config_probe_json(body, len, &consoles, &repos)) {
        xfer_log("rejected   PC push of %zu bytes: no collections in it", len);
        free(body);
        this->ToastErr(tr(S_IMPORT_BAD_FILE));
        return;
    }
    bool ok = config_import_json(&g_cfg, body, len, &consoles, &repos);
    free(body);
    if (!ok) {
        xfer_log("FAILED     PC push could not be written to %s", SOURCES_PATH);
        this->ToastErr(tr(S_IMPORT_SAVE_FAIL));
        return;
    }
    xfer_log("push       PC applied %d console(s), %d repo(s); previous %d kept "
             "for restore", consoles, repos, SOURCES_BAK_SLOTS);
    config_seed_rom_folders(&g_cfg, roms_root(&g_tico));
    this->inv_last_gen_ns = 0; // regenerate the inventory with the new collection

    char done[96];
    snprintf(done, sizeof(done), tr(S_IMPORT_DONE), consoles, repos);
    this->Toast(done);
}

// A game streamed to the always-on server (app utility › Device Transfer › Send
// a game, while connected) landed in the inbox as "<name>.part". Finish it into
// place so the sorter can file it — the same landing spot a USB/MTP drop uses.
// body is NULL here; recv_name/part_path hold the finished temp and its name.
void MainApplication::InvApplyFile() {
    std::string name = this->inv_srv.recv_name;
    std::string part = this->inv_srv.part_path;
    std::string app = this->inv_srv.recv_app;
    std::string apath = this->inv_srv.recv_app_path;
    bool new_app = this->inv_srv.recv_app_new;
    std::string folder = this->inv_srv.recv_folder;
    std::string fsdest = this->inv_srv.recv_fs_dest;
    this->inv_srv.recv_name[0] = '\0';
    this->inv_srv.part_path[0] = '\0';
    this->inv_srv.recv_app[0] = '\0';
    this->inv_srv.recv_app_path[0] = '\0';
    this->inv_srv.recv_app_new = false;
    this->inv_srv.recv_folder[0] = '\0';
    this->inv_srv.recv_fs_dest[0] = '\0';
    if (name.empty() || part.empty() || !fs_exists(part.c_str())) {
        return; // never completed, or already consumed
    }
    // SD Card tab direct write (X-Fs-Path): move the finished temp straight to
    // its exact requested destination, already validated server-side. This is
    // a plain file write, not a ROM import -- no app/library/inbox routing,
    // no archive extraction, no toast (the desktop's own file browser already
    // reflects the write; a device-side toast for every drag-drop would be
    // noise). Checked first so it can never fall through into the game-push
    // logic below.
    if (!fsdest.empty()) {
        if (!fs_move(part.c_str(), fsdest.c_str())) {
            remove(part.c_str());
            xfer_log("FAILED     PC SD-card write of %s to %s", name.c_str(),
                     fsdest.c_str());
        } else {
            xfer_log("push       PC wrote %s (SD Card tab)", fsdest.c_str());
        }
        return;
    }
    // A fresh install from the companion's Emulators tab: X-App-Path names a new
    // .nro under sdmc:/switch (validated server-side) that isn't there yet, flagged
    // X-App-Install. Write it as a new app rather than treating it as an update.
    if (new_app && !apath.empty() && !fs_exists(apath.c_str())) {
        std::string bn = apath.substr(apath.find_last_of('/') + 1);
        this->InvApplyEmuNroAt(bn, apath, part, true);
        return;
    }
    // An Emulators/Apps-tab update carries its target in X-App-Path (an exact
    // path, so two same-named .nro in different folders update independently) or,
    // for older clients, X-App-Target (by name). Overwrite that installed .nro in
    // place rather than filing the body in the inbox.
    if (!apath.empty() && fs_exists(apath.c_str())) {
        std::string bn = apath.substr(apath.find_last_of('/') + 1);
        this->InvApplyEmuNroAt(bn, apath, part);
        return;
    }
    if (!app.empty()) {
        this->InvApplyEmuNro(app, part);
        return;
    }
    // A Library-tab push (X-Dest-Folder) names the console it came from. Land it
    // straight in that console's folder instead of the inbox -- extracting an
    // archive on arrival, same treatment a USB drop into a console folder gets
    // (see mtp/responder.cpp) -- so Wi-Fi and USB library sends behave alike
    // instead of Wi-Fi always leaving a raw archive for a manual Sort Inbox.
    // Falls through to the inbox below when the folder is empty or doesn't match
    // a console this device actually has configured (older desktop build, or the
    // console was since removed).
    if (!folder.empty()) {
        ConsoleGroup *g = config_find_console(&g_cfg, folder.c_str());
        if (g) {
            const char *custom = install_folder_for(g->target);
            std::string dir = (custom && custom[0])
                                  ? std::string(custom)
                                  : std::string(roms_root(&g_tico)) + "/" + g->target;
            fs_mkdir_p(dir.c_str());
            std::string fdest = dir + "/" + name;
            remove(fdest.c_str()); // a re-push of the same name replaces the earlier one
            if (!fs_move(part.c_str(), fdest.c_str())) {
                remove(part.c_str());
                xfer_log("FAILED     PC push of game %s into %s", name.c_str(),
                         g->target);
                this->ToastErr(tr(S_ROM_RECV_FAIL));
                return;
            }
            // Same fallback chain SortFileRow uses: libarchive first, then the
            // native RAR3 decoder for entries libarchive can't read. Unpacking
            // runs on a background thread (PushExtractThread/Tick) rather than
            // inline here: this whole method runs on InvServerPoll's thread, and
            // a synchronous extract_archive froze that thread -- and with it
            // httpsrv_poll's accept() -- for the entire unzip, so a second push
            // queued right behind this one connected into a server that couldn't
            // service it and got reset instead. A total failure just leaves the
            // raw archive sitting in the console folder.
            if (is_archive_name(name.c_str())) {
                if (this->pxt.running) {
                    // Shouldn't happen -- Wi-Fi pushes are serialized by the
                    // desktop's push queue -- but don't drop the extract if it
                    // somehow does; finish the prior job before starting this one.
                    this->pxt.Join();
                }
                this->pxt_path = fdest;
                this->pxt_dir = dir;
                this->pxt_name = name;
                this->pxt_target = g->target;
                this->pxt_cancel = false;
                if (!this->pxt.Start(&MainApplication::PushExtractThread, this)) {
                    // Couldn't spawn: fall back to doing it inline, same as before.
                    int ow = 0;
                    int n = extract_archive(fdest.c_str(), dir.c_str(), NULL, NULL, &ow);
                    if (n <= 0) {
                        size_t nl = name.size();
                        if (nl > 4 && strcasecmp(name.c_str() + nl - 4, ".rar") == 0) {
                            ow = 0;
                            n = rar3_extract(fdest.c_str(), dir.c_str(), NULL, NULL, &ow);
                        }
                    }
                    if (n > 0) {
                        remove(fdest.c_str());
                    }
                }
            }
            xfer_log("push       PC game %s -> %s", name.c_str(), g->target);
            this->inv_last_gen_ns = 0; // the console's install count changed
            const char *full = console_full_name(g->target);
            char t[160];
            snprintf(t, sizeof(t), tr(S_INV_GAME_FILED), name.c_str(),
                     full ? full : g->target);
            this->Toast(t);
            return;
        }
    }
    std::string dest = std::string(INBOX_DIR) + "/" + name;
    remove(dest.c_str()); // a re-push of the same name replaces the earlier one
    if (!fs_move(part.c_str(), dest.c_str())) {
        remove(part.c_str());
        xfer_log("FAILED     PC push of game %s into the inbox", name.c_str());
        this->ToastErr(tr(S_ROM_RECV_FAIL));
        return;
    }
    xfer_log("push       PC game %s -> inbox", name.c_str());
    this->inv_last_gen_ns = 0; // the inbox count changed; refresh the inventory
    char t[160];
    snprintf(t, sizeof(t), tr(S_INV_GAME_INBOX), name.c_str());
    this->Toast(t);
}

// Progress/cancel hook for PushExtractThread below. `ud` is the MainApplication
// instance, so Shutdown() setting pxt_cancel makes the eventual Join() return
// promptly instead of blocking on a long unpack.
bool MainApplication::PushExtractProgress(void *ud, const char *entry, int done,
                                          uint64_t bytes_read) {
    (void)entry; (void)done; (void)bytes_read;
    auto self = static_cast<MainApplication *>(ud);
    return !self->pxt_cancel;
}

// Worker: unpack a game a Wi-Fi push already moved into its console folder
// (pxt_path/pxt_dir), off InvServerPoll's thread -- see the comment at the
// InvApplyFile call site for why inline extraction stalled the desktop.
void MainApplication::PushExtractThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    int ow = 0;
    int n = extract_archive(self->pxt_path.c_str(), self->pxt_dir.c_str(),
                            &MainApplication::PushExtractProgress, self, &ow);
    // Same libarchive->RAR3 fallback the inline path used.
    if (n <= 0) {
        size_t nl = self->pxt_name.size();
        if (nl > 4 && strcasecmp(self->pxt_name.c_str() + nl - 4, ".rar") == 0) {
            ow = 0;
            n = rar3_extract(self->pxt_path.c_str(), self->pxt_dir.c_str(),
                             &MainApplication::PushExtractProgress, self, &ow);
        }
    }
    if (n > 0) {
        remove(self->pxt_path.c_str());
    }
    self->pxt.done = true;
}

// Reap the background unpack once it finishes. The "filed" toast already fired
// synchronously in InvApplyFile when the archive landed, so this is just the
// log line -- unpacking is a background detail, same as a USB drop's "unzip"
// phase not getting its own toast either.
void MainApplication::PushExtractTick() {
    if (!this->pxt.done) {
        return;
    }
    this->pxt.Join();
    xfer_log("push       PC game %s unpacked -> %s", this->pxt_name.c_str(),
             this->pxt_target.c_str());
}

// An emulator .nro pushed for an in-place update (app utility › Emulators ›
// Update). The body streamed to a temp .part; find the installed copy of <app>
// under sdmc:/switch (flat or one subfolder deep, matching the inventory scan),
// confirm the swap on-screen, then overwrite it. Unlike the HaulNX self-update
// this replaces a third-party app's own file and takes effect immediately — no
// staging, no restart.
void MainApplication::InvApplyEmuNro(const std::string &app,
                                     const std::string &part) {
    std::string dest;
    for (const auto &e : list_dir("sdmc:/switch")) {
        if (e.is_dir) {
            std::string sub = std::string("sdmc:/switch/") + e.name;
            for (const auto &f2 : list_dir(sub.c_str())) {
                if (!f2.is_dir &&
                    strcasecmp(f2.name.c_str(), app.c_str()) == 0) {
                    dest = sub + "/" + f2.name;
                    break;
                }
            }
        } else if (strcasecmp(e.name.c_str(), app.c_str()) == 0) {
            dest = std::string("sdmc:/switch/") + e.name;
        }
        if (!dest.empty()) {
            break;
        }
    }
    if (dest.empty()) {
        remove(part.c_str());
        xfer_log("rejected   emulator update: %s not installed", app.c_str());
        this->ToastErr(tr(S_EMU_UPD_MISSING));
        return;
    }
    this->InvApplyEmuNroAt(app, dest, part);
}

// Overwrite the .nro at `dest` with the freshly-streamed `part`, after proving
// it's a real NRO and confirming the swap on-screen. `app` names it for the
// prompt and log. Shared by the name-based search above and the exact-path
// (X-App-Path) route in InvApplyFile.
void MainApplication::InvApplyEmuNroAt(const std::string &app,
                                       const std::string &dest,
                                       const std::string &part, bool fresh) {
    // Prove the upload is a real NRO before overwriting a working app.
    if (!looks_like_nro(part.c_str())) {
        remove(part.c_str());
        xfer_log("rejected   emulator %s for %s: not an NRO",
                 fresh ? "install" : "update", app.c_str());
        this->ToastErr(tr(S_NRO_STAGE_FAIL));
        return;
    }
    char ver[24];
    if (!nro_file_version(part.c_str(), ver, sizeof(ver)) || !ver[0]) {
        snprintf(ver, sizeof(ver), "?");
    }
    char msg[512];
    snprintf(msg, sizeof(msg), tr(fresh ? S_EMU_INSTALL_CONFIRM : S_EMU_UPD_CONFIRM),
             app.c_str(), ver);
    int cr = this->CreateShowDialog(tr(fresh ? S_TITLE_INSTALL : S_TITLE_UPDATE),
                                    msg, {tr(S_YES), tr(S_CANCEL)}, false, {},
                                    style_dialog_danger);
    if (cr != 0) {
        remove(part.c_str());
        xfer_log(fresh ? "cancelled  emulator install: %s not installed"
                       : "cancelled  emulator update: %s left at its current build",
                 app.c_str());
        return;
    }
    if (fresh) {
        fs_ensure_parent(dest.c_str());
    }
    // Park the current build at <dest>.bak rather than deleting it outright, so a
    // failed or interrupted write can be rolled back to a working app. Cleared on
    // success; if it can't be parked, fall back to removing it to make room.
    std::string bak = dest + ".bak";
    bool restore = false;
    if (fs_exists(dest.c_str())) {
        remove(bak.c_str()); // drop any stale backup first
        if (fs_move(dest.c_str(), bak.c_str())) {
            restore = true;
        } else {
            remove(dest.c_str());
        }
    }
    if (!fs_move(part.c_str(), dest.c_str())) {
        remove(part.c_str());
        if (restore) {
            fs_move(bak.c_str(), dest.c_str()); // put the old build back
        }
        xfer_log("FAILED     emulator %s: %s could not be written to %s",
                 fresh ? "install" : "update", app.c_str(), dest.c_str());
        this->ToastErr(tr(S_NRO_STAGE_FAIL));
        return;
    }
    if (restore) {
        remove(bak.c_str()); // swap succeeded; drop the backup
    }
    xfer_log(fresh ? "installed  emulator %s -> v%s (%s)"
                   : "updated    emulator %s -> v%s (%s)",
             app.c_str(), ver, dest.c_str());
    this->inv_last_gen_ns = 0; // the installed set changed; refresh inventory
    char done[160];
    snprintf(done, sizeof(done), tr(fresh ? S_EMU_INSTALL_DONE : S_EMU_UPD_DONE),
             app.c_str(), ver);
    this->Toast(done);
}

// ==== Tools: emulator / app update manager =================================
// Reads the shared update_sources.json manifest (updman.c) and, for each entry,
// checks its GitHub release repo, then installs/updates/reverts on-device. The
// download runs on a worker thread feeding a Queue-tab item (like the self-
// update); the swap keeps a per-app backup so a build can be rolled back.

namespace {

struct NroFile {
    std::string name;
    std::string path;
};

// A .nro filename (case-insensitive suffix).
static bool is_nro_name(const std::string &n) {
    return n.size() > 4 && strcasecmp(n.c_str() + n.size() - 4, ".nro") == 0;
}

// True if `fname` (any case) contains any of the entry's comma-separated,
// lowercase detect tokens.
static bool detect_match(const std::string &detect, const std::string &fname) {
    std::string low = fname;
    for (auto &c : low) {
        c = (char)tolower((unsigned char)c);
    }
    size_t pos = 0;
    while (pos <= detect.size()) {
        size_t comma = detect.find(',', pos);
        std::string tok = detect.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);
        std::string t;
        for (char c : tok) {
            if (!isspace((unsigned char)c)) {
                t += (char)tolower((unsigned char)c);
            }
        }
        if (!t.empty() && low.find(t) != std::string::npos) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return false;
}

// Collect every .nro under `dir`, recursing up to `depth` more folder levels.
// Walks the directory itself instead of going through list_dir(): list_dir
// stat()s every entry to fill in size/mtime for the file-browser screens that
// need them, and each stat() is its own FS IPC round-trip on top of the
// readdir() that already listed the entry. This scan only needs is-a-folder
// vs is-a-file, which libnx hands back for free as dirent::d_type in the same
// readdir() batch, so skip the stat() in the common case (a scan across a few
// hundred sdmc:/switch folders is where that per-entry cost actually adds up).
// Falls back to stat() only if d_type ever comes back unknown, so this can't
// silently misclassify an entry if that ever stops being populated.
static void collect_nros(const std::string &dir, int depth,
                         std::vector<NroFile> &v) {
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        std::string full = dir + "/" + e->d_name;
        bool is_dir;
        if (e->d_type == DT_DIR) {
            is_dir = true;
        } else if (e->d_type == DT_REG) {
            is_dir = false;
        } else {
            struct stat st;
            is_dir = (stat(full.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
        }
        if (is_dir) {
            // Never descend into the update manager's rollback store: those are
            // old builds kept for revert, not installed apps — scanning them
            // would list every backup as a phantom entry in the App updates list.
            if (full == BACKUPS_DIR) {
                continue;
            }
            if (depth > 0) {
                collect_nros(full, depth - 1, v);
            }
        } else if (is_nro_name(e->d_name)) {
            v.push_back({e->d_name, full});
        }
    }
    closedir(d);
}

// Every .nro under sdmc:/switch. Recurse a few folders deep so apps and
// emulators that nest their build inside a category/name subfolder (e.g.
// sdmc:/switch/emus/melonds/melonds.nro) are still detected — the flat + one-
// deep scan used to miss them.
static std::vector<NroFile> scan_switch_nros() {
    std::vector<NroFile> v;
    collect_nros("sdmc:/switch", 3, v);
    return v;
}

// Locate the .nro inside an unpacked release archive. Returns its full path and
// its path *within* the archive (forward-slash separated, e.g. "hbmenu.nro" or
// "switch/linkalho/linkalho.nro") so a fresh install can honour the layout. When
// several .nro are present, prefer one filed under a "switch/" folder (that's the
// intended install path); otherwise take the first found. false if none.
static bool find_nro_in_dir(const std::string &root, std::string &path_out,
                            std::string &rel_out) {
    std::vector<NroFile> v;
    collect_nros(root, 8, v);
    if (v.empty()) {
        return false;
    }
    size_t pick = 0;
    for (size_t i = 0; i < v.size(); i++) {
        std::string rel = v[i].path.substr(root.size());
        while (!rel.empty() && rel[0] == '/') {
            rel.erase(0, 1);
        }
        std::string low = rel;
        for (char &c : low) {
            c = (char)tolower((unsigned char)c);
        }
        if (low.compare(0, 7, "switch/") == 0 ||
            low.find("/switch/") != std::string::npos) {
            pick = i;
            break;
        }
    }
    path_out = v[pick].path;
    rel_out = v[pick].path.substr(root.size());
    while (!rel_out.empty() && rel_out[0] == '/') {
        rel_out.erase(0, 1);
    }
    return true;
}

// The installed path + NACP version for one manifest entry (empty if not found).
static void match_installed(const UpdSource &e,
                            const std::vector<NroFile> &files,
                            std::string &path_out, std::string &ver_out) {
    path_out.clear();
    ver_out.clear();
    for (const auto &f : files) {
        if (detect_match(e.detect, f.name)) {
            path_out = f.path;
            break;
        }
    }
    if (!path_out.empty()) {
        char ver[24];
        if (nro_file_version(path_out.c_str(), ver, sizeof(ver)) && ver[0]) {
            ver_out = ver;
        }
    }
}

// Backups for one app, newest first, as {version-label, full path}.
static std::vector<NroFile> list_backups(const std::string &id) {
    std::string dir = std::string(BACKUPS_DIR) + "/" + id;
    std::vector<NroFile> v;
    std::vector<time_t> mt;
    for (const auto &f : list_dir(dir)) {
        if (!f.is_dir && is_nro_name(f.name)) {
            std::string p = dir + "/" + f.name;
            struct stat sb;
            time_t t = (stat(p.c_str(), &sb) == 0) ? sb.st_mtime : 0;
            // insertion sort, newest first (lists are tiny)
            size_t i = 0;
            while (i < mt.size() && mt[i] >= t) {
                i++;
            }
            v.insert(v.begin() + i, {f.name, p});
            mt.insert(mt.begin() + i, t);
        }
    }
    return v;
}

// Copy the current build into BACKUPS_DIR/<id>/<ver>.nro, then keep only the two
// most recent. Best-effort: a backup problem never blocks the update itself.
static void backup_keep2(const std::string &id, const std::string &cur_path,
                         const std::string &cur_ver) {
    if (id.empty() || cur_path.empty() || !fs_exists(cur_path.c_str())) {
        return;
    }
    std::string ver = cur_ver.empty() ? "unknown" : cur_ver;
    for (auto &c : ver) {
        if (c == '/' || c == '\\' || c == ':') {
            c = '_';
        }
    }
    std::string dest = std::string(BACKUPS_DIR) + "/" + id + "/" + ver + ".nro";
    fs_ensure_parent(dest.c_str());
    if (!fs_exists(dest.c_str())) {
        fs_copy_file(cur_path.c_str(), dest.c_str());
    }
    // Prune to 2 newest.
    auto baks = list_backups(id);
    for (size_t i = 2; i < baks.size(); i++) {
        remove(baks[i].path.c_str());
    }
}

} // namespace

// A pushed .nro can arrive zipped (or in any archive extract_archive/RAR3
// read) instead of raw -- e.g. a build zipped on the PC before the transfer.
// Stage `*body` to a temp file, try to unpack it, and if a usable .nro turns
// up inside, replace `*body`/`*len` with that .nro's bytes (freeing the
// original) so the caller's existing NRO0-magic check picks it up exactly
// like a raw push. Leaves `*body`/`*len` untouched and returns false for
// anything that isn't a recognizable archive, or one without an .nro in it --
// a plain collection/DAT/update-manifest push never touches the SD card for
// this, since none of those start with an archive signature.
static bool try_unpack_nro_push(char **body, size_t *len) {
    if (!body || !*body || !len || *len < 6) {
        return false;
    }
    const unsigned char *b = (const unsigned char *)*body;
    bool maybe_zip = b[0] == 'P' && b[1] == 'K';
    bool maybe_rar = memcmp(b, "Rar!", 4) == 0;
    bool maybe_7z = b[0] == 0x37 && b[1] == 0x7A && b[2] == 0xBC &&
                    b[3] == 0xAF && b[4] == 0x27 && b[5] == 0x1C;
    if (!maybe_zip && !maybe_rar && !maybe_7z) {
        return false;
    }
    fs_mkdir_p(DL_TMP_DIR);
    std::string tmp = std::string(DL_TMP_DIR) + "/pushnro.arc";
    remove(tmp.c_str());
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool wrote = fwrite(*body, 1, *len, f) == *len;
    if (fclose(f) != 0) {
        wrote = false;
    }
    if (!wrote) {
        remove(tmp.c_str());
        return false;
    }

    std::string exdir = std::string(DL_TMP_DIR) + "/pushnro_x";
    fs_rm_rf(exdir.c_str()); // clear any stale extraction
    int nfiles = extract_archive(tmp.c_str(), exdir.c_str(), NULL, NULL, NULL);
    if (nfiles <= 0 && maybe_rar) {
        // Same gap the download/MTP/sort paths cover: libarchive has never
        // implemented RAR3's "programmable filter" entries (see rar3.h); the
        // native fallback picks up exactly what it can't.
        nfiles = rar3_extract(tmp.c_str(), exdir.c_str(), NULL, NULL, NULL);
    }
    remove(tmp.c_str());
    std::string nro_path, rel;
    if (nfiles <= 0 || !find_nro_in_dir(exdir, nro_path, rel)) {
        fs_rm_rf(exdir.c_str());
        return false;
    }
    FILE *nf = fopen(nro_path.c_str(), "rb");
    if (!nf) {
        fs_rm_rf(exdir.c_str());
        return false;
    }
    fseek(nf, 0, SEEK_END);
    long nsz = ftell(nf);
    fseek(nf, 0, SEEK_SET);
    char *nbuf = (nsz > 0) ? (char *)malloc((size_t)nsz) : NULL;
    bool ok = nbuf && fread(nbuf, 1, (size_t)nsz, nf) == (size_t)nsz;
    fclose(nf);
    fs_rm_rf(exdir.c_str());
    if (!ok) {
        free(nbuf);
        return false;
    }
    free(*body);
    *body = nbuf;
    *len = (size_t)nsz;
    return true;
}

// Storage › Emulator/app backups: every rollback build the update manager kept
// (BACKUPS_DIR/<id>/<version>.nro — the two most recent per app), listed so the
// user can reclaim the space. A on a row deletes that one build; X clears them
// all. Cheap to rebuild, so a delete just re-enters this.
void MainApplication::GotoBackups() {
    this->screen = Screen::Backups;
    this->layout->SetTitle(tr(S_TITLE_BACKUPS));
    this->layout->ClearMenu();
    this->backup_rows.clear();
    uint64_t total = 0;
    for (const auto &d : list_dir(BACKUPS_DIR)) {
        if (!d.is_dir) {
            continue;
        }
        for (const auto &b : list_backups(d.name)) {
            std::string ver = b.name;
            if (is_nro_name(ver)) {
                ver = ver.substr(0, ver.size() - 4); // drop ".nro"
            }
            struct stat sb;
            uint64_t sz =
                (stat(b.path.c_str(), &sb) == 0) ? (uint64_t)sb.st_size : 0;
            total += sz;
            this->layout->AddRow2(d.name + std::string("  ·  ") + ver,
                                  human_size(sz), g_theme->row_text,
                                  value_color(), -1.0f, console_icon("default"));
            this->backup_rows.push_back({b.path, d.name + " " + ver});
        }
    }
    if (this->backup_rows.empty()) {
        this->layout->SetSubtitle(tr(S_SUB_BACKUPS));
        this->layout->SetEmptyState(console_icon("default"), tr(S_BACKUPS_EMPTY),
                                    "");
        return;
    }
    // Subtitle carries the total reclaimable size + the button hints.
    char sub[128];
    snprintf(sub, sizeof(sub), tr(S_SUB_BACKUPS_N), human_size(total).c_str());
    this->layout->SetSubtitle(sub);
}

// Worker: download the chosen release .nro to a temp file. Mirrors UpdThread.
// `ud`/`arg` is a UmiJob* (not `this`) so concurrent jobs never touch each
// other's state -- see the UmiJob comment in MainApplication.hpp.
int MainApplication::UmiProgress(void *ud, u64 now, u64 total) {
    auto job = static_cast<UmiJob *>(ud);
    job->now = now;
    job->total = total;
    return job->cancel ? 1 : 0;
}

void MainApplication::UmiThread(void *arg) {
    auto job = static_cast<UmiJob *>(arg);
    long code = 0;
    bool ok = http_download(job->url.c_str(), job->dl.c_str(), NULL,
                            &MainApplication::UmiProgress, job, NULL, NULL, 0,
                            &code, NULL);
    job->ok = ok && code >= 200 && code < 300;
    job->task.done = true;
}

void MainApplication::UmiStart(const UpdSource &e, const std::string &url,
                               const std::string &tag, const std::string &dest,
                               const std::string &cur_ver, bool fresh,
                               const std::string &asset) {
    // First free slot: one not currently running a download (its previous job,
    // if any, has already been reaped by UmiTick). Concurrent jobs each get
    // their own temp path (suffixed by slot) so they never collide on disk.
    int j = -1;
    for (int i = 0; i < UMI_MAX; i++) {
        if (!this->umi_jobs[i].task.running) {
            j = i;
            break;
        }
    }
    if (j < 0) {
        // All slots busy: rather than stomp an in-flight job's state (the bug
        // this pool replaced), make the user wait for one to finish.
        this->ToastErr(tr(S_UPDATE_TOO_MANY));
        return;
    }
    UmiJob &job = this->umi_jobs[j];
    job.url = url;
    job.asset = asset;
    // Some releases ship the .nro inside a .zip (flat, or nested under
    // switch/<app>/); an archive asset is downloaded, then unpacked in UmiTick.
    job.zip = is_archive_name(asset.c_str());
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "/appupd%d%s", j, job.zip ? ".zip" : ".part");
    job.dl = std::string(DL_TMP_DIR) + tmp;
    job.dest = dest;
    job.id = e.id;
    job.name = e.name;
    job.tag = tag;
    job.bakver = cur_ver;
    job.fresh = fresh;
    job.now = 0;
    job.total = 0;
    job.ok = false;
    job.cancel = false;
    fs_ensure_parent(job.dl.c_str());
    if (!job.task.Start(&MainApplication::UmiThread, &job)) {
        this->ToastErr(tr(S_UPDATE_START_FAIL));
        return;
    }
    // Shows as an ordinary Queue-tab item (xkind 2 = install/update); jump there.
    job.xslot = this->BeginXfer(e.name, e.id, 2);
}

// Finalize once job `j`'s download reports done (main thread): validate the
// NRO, back up the current build, and swap the new one in with the transient
// .bak safety.
void MainApplication::UmiTick(int j) {
    UmiJob &job = this->umi_jobs[j];
    if (!job.task.done) {
        return;
    }
    job.task.Join();
    std::string part = job.dl;
    if (job.cancel) {
        remove(part.c_str());
        queue_ext_finish(job.xslot, false, "cxl");
        job.xslot = -1;
        return;
    }
    if (!job.ok) {
        remove(part.c_str());
        xfer_log("FAILED     %s %s: download failed",
                 job.fresh ? "install" : "update", job.name.c_str());
        queue_ext_finish(job.xslot, false, "err");
        job.xslot = -1;
        return;
    }
    std::string dest = job.dest;
    // A .zip asset: unpack it, take the .nro inside, and (for a fresh install)
    // honour where the archive files it — a nested switch/<app>/ layout installs
    // to that same sdmc path, a flat .nro to sdmc:/switch/. exdir is cleaned up
    // on every exit path below.
    std::string exdir;
    if (job.zip) {
        exdir = std::string(DL_TMP_DIR) + "/appupd_x" + std::to_string(j);
        fs_rm_rf(exdir.c_str()); // clear any stale extraction
        int nfiles = extract_archive(part.c_str(), exdir.c_str(), NULL, NULL,
                                     NULL);
        remove(part.c_str()); // the downloaded archive is no longer needed
        std::string nro_path, rel;
        if (nfiles <= 0 || !find_nro_in_dir(exdir, nro_path, rel)) {
            fs_rm_rf(exdir.c_str());
            xfer_log("FAILED     %s %s: no .nro inside the release archive",
                     job.fresh ? "install" : "update", job.name.c_str());
            queue_ext_finish(job.xslot, false, "err");
            job.xslot = -1;
            return;
        }
        part = nro_path; // install the extracted .nro
        if (job.fresh) {
            std::string low = rel;
            for (char &c : low) {
                c = (char)tolower((unsigned char)c);
            }
            size_t cut = std::string::npos;
            if (low.compare(0, 7, "switch/") == 0) {
                cut = 0;
            } else {
                size_t p = low.find("/switch/");
                if (p != std::string::npos) {
                    cut = p + 1;
                }
            }
            dest = (cut != std::string::npos) ? ("sdmc:/" + rel.substr(cut))
                                              : ("sdmc:/switch/" + rel);
        }
    }
    if (!looks_like_nro(part.c_str())) {
        remove(part.c_str());
        if (!exdir.empty()) {
            fs_rm_rf(exdir.c_str());
        }
        xfer_log("FAILED     %s %s: not a valid NRO",
                 job.fresh ? "install" : "update", job.name.c_str());
        queue_ext_finish(job.xslot, false, "err");
        job.xslot = -1;
        return;
    }
    if (!job.fresh && fs_exists(dest.c_str())) {
        backup_keep2(job.id, dest, job.bakver);
    }
    if (job.fresh) {
        fs_ensure_parent(dest.c_str());
    }
    std::string bak = dest + ".bak";
    bool restore = false;
    if (fs_exists(dest.c_str())) {
        remove(bak.c_str());
        if (fs_move(dest.c_str(), bak.c_str())) {
            restore = true;
        } else {
            remove(dest.c_str());
        }
    }
    if (!fs_move(part.c_str(), dest.c_str())) {
        remove(part.c_str());
        if (restore) {
            fs_move(bak.c_str(), dest.c_str());
        }
        if (!exdir.empty()) {
            fs_rm_rf(exdir.c_str());
        }
        xfer_log("FAILED     %s %s: could not write %s",
                 job.fresh ? "install" : "update", job.name.c_str(),
                 dest.c_str());
        queue_ext_finish(job.xslot, false, "err");
        job.xslot = -1;
        return;
    }
    if (restore) {
        remove(bak.c_str());
    }
    if (!exdir.empty()) {
        fs_rm_rf(exdir.c_str()); // drop the rest of the unpacked archive
    }
    xfer_log(job.fresh ? "installed  %s -> %s (%s)"
                       : "updated    %s -> %s (%s)",
             job.name.c_str(), job.tag.c_str(), dest.c_str());
    this->inv_last_gen_ns = 0; // the installed set changed
    queue_ext_finish(job.xslot, true, NULL);
    job.xslot = -1;
}

// swkbd-edit one entry's GitHub repo (owner/name), persisted to the shared
// manifest so the change syncs to the desktop companion too.
void MainApplication::AppSetSource(const UpdSource &e) {
    char buf[80];
    if (!prompt_raw(tr(S_APPMAN_REPO_GUIDE), e.repo, buf, sizeof(buf))) {
        return;
    }
    // Trim surrounding whitespace.
    char *s = buf;
    while (*s == ' ') {
        s++;
    }
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') {
        s[--n] = '\0';
    }
    UpdSource all[UPD_MAX];
    int cnt = updman_load(all, UPD_MAX);
    int found = -1;
    for (int i = 0; i < cnt; i++) {
        if (strcasecmp(all[i].id, e.id) == 0) {
            found = i;
            break;
        }
    }
    bool recorded = false;
    if (found >= 0) {
        snprintf(all[found].repo, sizeof(all[found].repo), "%s", s);
        recorded = true;
    } else if (cnt < UPD_MAX) {
        // A synthetic entry (an installed .nro with no manifest row yet — this is
        // how every unmanaged app shows up in the list). It isn't in the manifest,
        // so persist it here, otherwise the source the user just set would be lost
        // on the next rebuild. e already carries id/name/kind/detect from the scan.
        all[cnt] = e;
        snprintf(all[cnt].repo, sizeof(all[cnt].repo), "%s", s);
        cnt++;
        recorded = true;
    }
    if (recorded && updman_save(all, cnt)) {
        this->Toast(tr(S_APPMAN_SOURCE_SAVED));
    }
}

// Roll back to one of the stored backups for this app.
void MainApplication::AppRevert(const UpdSource &e) {
    auto baks = list_backups(e.id);
    if (baks.empty()) {
        this->CreateShowDialog(e.name, tr(S_APPMAN_NO_BACKUPS), {tr(S_OK)}, true,
                               {}, style_dialog);
        return;
    }
    std::string cur_path, cur_ver;
    match_installed(e, scan_switch_nros(), cur_path, cur_ver);
    if (cur_path.empty()) {
        this->CreateShowDialog(e.name, tr(S_APPMAN_REVERT_NOTINST), {tr(S_OK)},
                               true, {}, style_dialog);
        return;
    }
    std::vector<std::string> opts;
    for (const auto &b : baks) {
        std::string lbl = b.name;
        if (is_nro_name(lbl)) {
            lbl = lbl.substr(0, lbl.size() - 4); // drop ".nro"
        }
        opts.push_back(lbl);
    }
    opts.push_back(tr(S_CANCEL));
    s32 r = this->SideMenu(e.name, opts, 0, tr(S_APPMAN_REVERT_PICK), true, false,
                           console_icon("default"), nullptr, 0);
    if (r < 0 || r >= (s32)baks.size()) {
        return;
    }
    std::string src = baks[r].path;
    if (!looks_like_nro(src.c_str())) {
        this->ToastErr(tr(S_NRO_STAGE_FAIL));
        return;
    }
    // Keep the build we're replacing (so the user can go forward again), then
    // copy the chosen backup into place with the transient .bak safety.
    backup_keep2(e.id, cur_path, cur_ver);
    std::string bak = cur_path + ".bak";
    remove(bak.c_str());
    bool restore = fs_move(cur_path.c_str(), bak.c_str());
    if (!fs_copy_file(src.c_str(), cur_path.c_str())) {
        if (restore) {
            fs_move(bak.c_str(), cur_path.c_str());
        }
        this->ToastErr(tr(S_NRO_STAGE_FAIL));
        return;
    }
    if (restore) {
        remove(bak.c_str());
    }
    this->inv_last_gen_ns = 0;
    xfer_log("reverted   %s -> %s (%s)", e.name, opts[r].c_str(),
             cur_path.c_str());
    char done[160];
    snprintf(done, sizeof(done), tr(S_APPMAN_REVERTED), e.name);
    this->Toast(done);
}

// One entry's action menu. Opening it does NOT touch the network (that was the
// slow part) — it's built from the last check's cached result. The update source
// is shown under the name, and "Check for updates" is an explicit option that
// kicks off the release check (the caller re-checks this one when it returns
// true). Update/Install only appear once a check has found a release.
bool MainApplication::AppEntryMenu(size_t idx) {
    if (idx >= this->appman_list.size()) {
        return false;
    }
    const UpdSource e = this->appman_list[idx]; // copy: actions may reload it
    // Reuse the install path/version the last list build (or recheck) already
    // found instead of re-walking sdmc:/switch here -- that scan is what made
    // opening this menu feel slow, since it ran synchronously on the UI thread
    // on every single press of A.
    std::string inst_path =
        (idx < this->appman_ipath.size()) ? this->appman_ipath[idx] : "";
    std::string inst_ver =
        (idx < this->appman_ver.size()) ? this->appman_ver[idx] : "";
    bool installed = !inst_path.empty();

    if (e.repo[0] == '\0') {
        // No update source yet: offer to set one on-device (the desktop is the
        // easier place, but the console can do it too). Still show the path
        // when installed -- missing a source shouldn't also hide where the
        // file actually lives.
        char msg[256];
        snprintf(msg, sizeof(msg), tr(S_APPMAN_NEEDS_SOURCE), e.name);
        std::string full_msg = msg;
        if (installed) {
            char path_line[256];
            snprintf(path_line, sizeof(path_line), tr(S_APPMAN_PATH_LINE),
                     inst_path.c_str());
            full_msg += "\n\n";
            full_msg += path_line;
        }
        int r = this->CreateShowDialog(
            e.name, full_msg, {tr(S_APPMAN_SET_SOURCE), tr(S_CANCEL)}, false, {},
            style_dialog);
        if (r == 0) {
            this->AppSetSource(e);
            return true; // source may have changed — caller re-checks this one
        }
        return false;
    }

    // Cached results from the last check (no network here). We can offer to
    // install/update only if a check already found a release with a download url.
    std::string tag = (idx < this->appman_latest.size()) ? this->appman_latest[idx] : "";
    std::string url = (idx < this->appman_url.size()) ? this->appman_url[idx] : "";
    std::string asset = (idx < this->appman_asset.size()) ? this->appman_asset[idx] : "";
    bool have_release = !tag.empty() && !url.empty();

    std::vector<std::string> opts;
    std::vector<int> acts; // 0 update/reinstall, 1 install, 2 revert, 3 source, 4 check

    // The update source, shown under the app name in the menu header, plus
    // the on-SD path and installed version when installed (the user has to
    // open the file browser and hunt for the path otherwise, since the list
    // screen only shows a name; the version answers "what am I on" without
    // having to squint at the pill color, and pairs with the "Update to X" /
    // "Reinstall X" button below for the full before/after picture).
    char body_line[160];
    snprintf(body_line, sizeof(body_line), tr(S_APPMAN_SOURCE_LINE), e.repo);
    std::string body = body_line;
    if (installed) {
        char path_line[256];
        snprintf(path_line, sizeof(path_line), tr(S_APPMAN_PATH_LINE),
                 inst_path.c_str());
        body += "\n";
        body += path_line;
        if (!inst_ver.empty()) {
            char ver_line[64];
            snprintf(ver_line, sizeof(ver_line), tr(S_APPMAN_INSTALLED_LINE),
                     inst_ver.c_str());
            body += "\n";
            body += ver_line;
        }
    }

    char u[96];
    if (have_release) {
        if (installed) {
            int cmp = inst_ver.empty() ? -1 : version_cmp(inst_ver.c_str(), tag.c_str());
            snprintf(u, sizeof(u),
                     tr(cmp < 0 ? S_APPMAN_UPDATE_TO : S_APPMAN_REINSTALL),
                     tag.c_str());
            opts.push_back(u);
            acts.push_back(0);
        } else {
            snprintf(u, sizeof(u), tr(S_APPMAN_INSTALL_V), tag.c_str());
            opts.push_back(u);
            acts.push_back(1);
        }
    }
    // Always offer an explicit release check (this is the version check that used
    // to run automatically on open).
    opts.push_back(tr(S_APPMAN_CHECK_UPDATES));
    acts.push_back(4);
    if (!list_backups(e.id).empty()) {
        opts.push_back(tr(S_APPMAN_REVERT));
        acts.push_back(2);
    }
    opts.push_back(tr(S_APPMAN_CHANGE_SOURCE));
    acts.push_back(3);
    opts.push_back(tr(S_CANCEL));
    acts.push_back(-1);

    s32 sel = this->SideMenu(e.name, opts, 0, body, false, false,
                             console_icon("default"), nullptr, 0);
    if (sel < 0 || sel >= (s32)acts.size()) {
        return false; // cancel — nothing changed, caller keeps the cached list
    }
    switch (acts[sel]) {
    case 0: // update / reinstall in place
        this->UmiStart(e, url, tag, inst_path, inst_ver, false, asset);
        break; // UmiStart jumps to the Queue tab; no list rebuild here
    case 1: { // fresh install into sdmc:/switch
        // For a plain .nro this names the destination; for a .zip it's just a
        // placeholder — UmiTick derives the real path from the archive layout.
        std::string fn = !asset.empty() ? asset : (std::string(e.id) + ".nro");
        this->UmiStart(e, url, tag, std::string("sdmc:/switch/") + fn, "", true,
                       asset);
        break;
    }
    case 2:
        this->AppRevert(e);
        return true; // installed build changed — re-check this one
    case 3:
        this->AppSetSource(e);
        return true; // source changed — re-check this one
    case 4:
        return true; // check for updates — caller runs AppRecheckOne on this one
    }
    return false;
}

// Per-entry status the release-check worker resolves, read back by
// AppUpdatesRender to colour each row (so outdated vs up-to-date look nothing
// alike).
enum {
    APST_UPDATE = 0, // installed, a newer release is available  (amber pill)
    APST_UPTODATE,   // installed, already the latest            (green)
    APST_NOTINST,    // listed but not on the SD card            (grey)
    APST_NOSRC,      // installed but no update source configured (grey)
    APST_ERR,        // installed, source set, but unreachable    (red)
    APST_RATELIMIT,  // GitHub rate limit hit (403/429)           (amber)
    APST_OFFLINE,    // no network / couldn't reach GitHub at all (red)
    APST_UNCHECKED,  // installed + sourced, not yet checked here (grey)
};

// ---- last-checked timestamps (persisted, keyed by entry id) ----------------
// The update list no longer hits GitHub on open, so each row shows when it was
// last checked instead. Times live in a tiny "id\tepoch" text file so the label
// survives leaving the screen and relaunching. Best-effort; any IO error just
// means a row reads "not checked".
static void load_check_times(std::map<std::string, uint64_t> &out) {
    out.clear();
    FILE *f = fopen(UPDCHK_PATH, "r");
    if (!f) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *tab = strchr(line, '\t');
        if (!tab) {
            continue;
        }
        *tab = '\0';
        uint64_t when = strtoull(tab + 1, NULL, 10);
        if (line[0] && when) {
            out[line] = when;
        }
    }
    fclose(f);
}

static void save_check_times(const std::map<std::string, uint64_t> &in) {
    fs_ensure_parent(UPDCHK_PATH);
    FILE *f = fopen(UPDCHK_PATH, "w");
    if (!f) {
        return;
    }
    for (const auto &kv : in) {
        fprintf(f, "%s\t%llu\n", kv.first.c_str(),
                (unsigned long long)kv.second);
    }
    fclose(f);
}

// A compact "when last checked" label ("just now", "5m ago", "3d ago", or a
// date for anything older than a week). when == 0 -> never checked.
static std::string checked_ago(uint64_t when) {
    if (when == 0) {
        return tr(S_APPMAN_NEVER_CHECKED);
    }
    time_t now = time(NULL);
    long d = ((uint64_t)now > when) ? (long)((uint64_t)now - when) : 0;
    char buf[48];
    if (d < 60) {
        snprintf(buf, sizeof(buf), "%s", tr(S_APPMAN_JUST_NOW));
    } else if (d < 3600) {
        snprintf(buf, sizeof(buf), tr(S_APPMAN_MIN_AGO), (int)(d / 60));
    } else if (d < 86400) {
        snprintf(buf, sizeof(buf), tr(S_APPMAN_HR_AGO), (int)(d / 3600));
    } else if (d < 7 * 86400) {
        snprintf(buf, sizeof(buf), tr(S_APPMAN_DAY_AGO), (int)(d / 86400));
    } else {
        time_t t = (time_t)when;
        struct tm tmv;
        struct tm *tm = localtime_r(&t, &tmv);
        if (tm) {
            strftime(buf, sizeof(buf), "%b %d", tm);
        } else {
            snprintf(buf, sizeof(buf), "?");
        }
    }
    return buf;
}

// The section list, its own Settings screen (reached from Settings › Updates).
// Opening it kicks off AppChkThread, which (a) rebuilds the entry list — the App
// section also surfaces every installed .nro no manifest entry claims, so all
// apps on the card show up — and (b) reads each installed build's version. It no
// longer hits GitHub on open (that was slow and rate-limit-prone); every row
// loads with its version + when it was last checked, and the user checks for
// updates explicitly (X = all, Y = one, or the entry menu). B returns to Updates
// once the list is built.
void MainApplication::GotoAppUpdates(uint8_t kind) {
    // Rapid re-entry (e.g. after an entry action): reap any in-flight check so we
    // never stack workers or read half-written results.
    if (this->appchk.running) {
        this->appchk_cancel = true;
        this->appchk.Join();
    }
    this->screen = Screen::AppUpdates;
    this->appman_kind = kind;
    this->appchk_net = false; // open = local versions only, no network
    if (!this->appman_checked_loaded) {
        load_check_times(this->appman_checked_at);
        this->appman_checked_loaded = true;
    }
    this->layout->SetTitle(kind == UPD_KIND_APP ? tr(S_APPMAN_APPS)
                                                : tr(S_APPMAN_EMUS));
    this->layout->SetSubtitle(tr(S_APPMAN_LOADING));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_APPMAN_LOADING));

    this->appchk_cancel = false;
    this->appchk_idx = 0;
    this->appchk_total = 0;
    if (!this->appchk.Start(&MainApplication::AppChkThread, this)) {
        // Couldn't spawn a thread: run inline (blocks) so the list still builds.
        AppChkThread(this);
        this->AppUpdatesRender();
    }
}

// X on the list: check every sourced+installed entry against GitHub. Same worker
// as the open, but with the network pass on and the checking spinner/progress.
void MainApplication::AppScanAll() {
    if (this->appman_list.empty()) {
        return;
    }
    if (this->appchk.running) {
        this->appchk_cancel = true;
        this->appchk.Join();
    }
    this->appman_sel = this->layout->Sel(); // keep the cursor where it was
    this->appchk_net = true;
    char sub0[128];
    snprintf(sub0, sizeof(sub0), "%s   %s", tr(S_APPMAN_CHECKING),
             tr(S_APPMAN_CHECK_CANCEL));
    this->layout->SetSubtitle(sub0);
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_APPMAN_CHECKING));
    this->appchk_cancel = false;
    this->appchk_idx = 0;
    this->appchk_total = 0;
    if (!this->appchk.Start(&MainApplication::AppChkThread, this)) {
        AppChkThread(this);
        this->AppUpdatesRender();
    }
}

// Off-thread: rebuild appman_list for the current section, read installed
// versions, and fetch the latest release tag for each installed+sourced entry.
// Touches no Plutonium — results are published to members and drawn on the main
// thread in AppUpdatesRender (mirrors BgChkThread's discipline).
void MainApplication::AppChkThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    uint8_t kind = self->appman_kind;

    UpdSource all[UPD_MAX];
    int cnt = updman_load(all, UPD_MAX);

    std::vector<UpdSource> list;
    for (int i = 0; i < cnt; i++) {
        if (all[i].kind == kind) {
            list.push_back(all[i]);
        }
    }

    auto files = scan_switch_nros();

    // The App section also lists every installed .nro that no manifest entry
    // (emulator or app) claims — the manifest only knows apps the desktop has
    // seen, so this is how the rest of the card's apps show up.
    if (kind == UPD_KIND_APP) {
        for (const auto &f : files) {
            bool known = false;
            for (int i = 0; i < cnt; i++) {
                if (detect_match(all[i].detect, f.name)) {
                    known = true;
                    break;
                }
            }
            if (known) {
                continue;
            }
            UpdSource s;
            memset(&s, 0, sizeof(s));
            s.kind = UPD_KIND_APP;
            std::string base = f.name;
            if (is_nro_name(base)) {
                base = base.substr(0, base.size() - 4);
            }
            snprintf(s.id, sizeof(s.id), "%s", base.c_str());
            snprintf(s.name, sizeof(s.name), "%s", base.c_str());
            snprintf(s.detect, sizeof(s.detect), "%s", f.name.c_str());
            list.push_back(s);
        }
    }

    std::sort(list.begin(), list.end(),
              [](const UpdSource &a, const UpdSource &b) {
                  return strcasecmp(a.name, b.name) < 0;
              });

    bool net = self->appchk_net.load();
    size_t n = list.size();
    std::vector<int8_t> state(n, APST_NOTINST);
    std::vector<std::string> ver(n), latest(n), ipath(n), rel_url(n), rel_asset(n);

    // First pass (local, fast): install state + version. Installed+sourced rows
    // start "not checked" — unless a prior check (this session) already found a
    // release for this id, in which case that cached result stands in until the
    // user asks for a fresh one: this is what makes leaving the list (e.g. an
    // update jumping to the Queue tab) and coming back keep showing the last
    // scan's Update/Up-to-date badges instead of blanking every row. The install
    // state/version above is still read fresh off the SD card every time, so an
    // entry updated elsewhere correctly flips to Up-to-date against the cached
    // tag without a new GitHub hit. When the network compare does run, tally how
    // many rows it covers so the progress bar has a real denominator.
    auto &net_cache = self->appman_net_cache[kind];
    int total = 0;
    for (size_t i = 0; i < n; i++) {
        match_installed(list[i], files, ipath[i], ver[i]);
        if (ipath[i].empty()) {
            state[i] = APST_NOTINST;
        } else if (list[i].repo[0] == '\0') {
            state[i] = APST_NOSRC;
        } else {
            state[i] = APST_UNCHECKED;
            if (net) {
                total++;
            } else {
                auto ci = net_cache.find(list[i].id);
                if (ci != net_cache.end()) {
                    latest[i] = ci->second.latest;
                    rel_url[i] = ci->second.url;
                    rel_asset[i] = ci->second.asset;
                    int cmp = ver[i].empty()
                                  ? -1
                                  : version_cmp(ver[i].c_str(), latest[i].c_str());
                    state[i] = (cmp < 0) ? APST_UPDATE : APST_UPTODATE;
                }
            }
        }
    }
    self->appchk_total = total;
    self->appchk_idx = 0;

    // Second pass (network, only when the user asked to check): compare installed
    // vs latest for sourced entries. A GitHub rate limit is account-wide, so once
    // one entry hits it the rest will too — flag it and mark the remainder without
    // hammering the API. A successful check stamps the entry's last-checked time.
    bool rate_limited = false;
    bool stamped = false;
    if (net) {
        for (size_t i = 0; i < n; i++) {
            if (ipath[i].empty() || list[i].repo[0] == '\0') {
                continue;
            }
            if (self->appchk_cancel) {
                state[i] = APST_UNCHECKED; // left un-checked; keep prior stamp
                continue;
            }
            if (rate_limited) {
                state[i] = APST_RATELIMIT;
                self->appchk_idx = self->appchk_idx + 1;
                continue;
            }
            char tag[64] = "", url[1024] = "", asset[256] = "";
            long code = 0;
            bool ok = update_fetch_latest_asset(
                list[i].repo, list[i].asset, tag, sizeof(tag), url, sizeof(url),
                asset, sizeof(asset), NULL, &code);
            if (!ok) {
                if (code == 403 || code == 429) {
                    rate_limited = true;
                    state[i] = APST_RATELIMIT;
                } else if (code == 0) {
                    state[i] = APST_OFFLINE; // never reached GitHub
                } else {
                    state[i] = APST_ERR;
                }
            } else {
                latest[i] = tag;
                rel_url[i] = url;
                rel_asset[i] = asset;
                int cmp = ver[i].empty() ? -1 : version_cmp(ver[i].c_str(), tag);
                state[i] = (cmp < 0) ? APST_UPDATE : APST_UPTODATE;
                self->appman_checked_at[list[i].id] = (uint64_t)time(NULL);
                stamped = true;
                net_cache[list[i].id] = {latest[i], rel_url[i], rel_asset[i]};
            }
            self->appchk_idx = self->appchk_idx + 1;
        }
    }
    if (stamped) {
        save_check_times(self->appman_checked_at);
    }

    // Publish (main thread reads these after Join, which fences the writes).
    self->appman_list = std::move(list);
    self->appman_state = std::move(state);
    self->appman_ver = std::move(ver);
    self->appman_ipath = std::move(ipath);
    self->appman_latest = std::move(latest);
    self->appman_url = std::move(rel_url);
    self->appman_asset = std::move(rel_asset);
    self->appchk.done = true;
}

// Per-frame while the check runs: show (n/total) progress under the spinner;
// build the list once it lands.
void MainApplication::AppChkTick() {
    if (!this->appchk.done) {
        int total = this->appchk_total.load();
        if (total > 0) {
            char s[128];
            snprintf(s, sizeof(s), "%s  (%d/%d)   %s", tr(S_APPMAN_CHECKING),
                     this->appchk_idx.load(), total, tr(S_APPMAN_CHECK_CANCEL));
            this->layout->SetSubtitle(s);
        }
        return; // the spinner overlay animates itself
    }
    this->appchk.Join();
    this->layout->HideSpinner();
    this->AppUpdatesRender();
}

// Stamp an entry as checked-just-now and persist it, so its row reads "checked
// just now" and the label survives leaving the screen / relaunching.
void MainApplication::AppMarkChecked(const std::string &id) {
    if (id.empty()) {
        return;
    }
    if (!this->appman_checked_loaded) {
        load_check_times(this->appman_checked_at);
        this->appman_checked_loaded = true;
    }
    this->appman_checked_at[id] = (uint64_t)time(NULL);
    save_check_times(this->appman_checked_at);
}

// "checked 5m ago" for an entry we have a stored time for; "" if we have none
// (so a not-installed / unsourced row shows nothing extra).
std::string MainApplication::AppCheckedLabel(const std::string &id) {
    if (!this->appman_checked_loaded) {
        load_check_times(this->appman_checked_at);
        this->appman_checked_loaded = true;
    }
    auto it = this->appman_checked_at.find(id);
    if (it == this->appman_checked_at.end()) {
        return "";
    }
    return checked_ago(it->second);
}

// Draw the rows from the worker's results: a clear Update / Up-to-date / no-
// source / unreachable badge per entry, colour-coded so the states don't blur.
// Installed rows also carry when they were last checked ("· 5m ago").
void MainApplication::AppUpdatesRender() {
    this->layout->SetSubtitle(tr(S_APPMAN_LIST_HINT));
    this->layout->ClearMenu();
    if (this->appman_list.empty()) {
        this->layout->SetEmptyState(console_icon("default"), tr(S_APPMAN_EMPTY),
                                    "");
        return;
    }
    pu::ui::Color lbl = g_theme->row_text;
    for (size_t i = 0; i < this->appman_list.size(); i++) {
        const auto &e = this->appman_list[i];
        int8_t st = (i < this->appman_state.size()) ? this->appman_state[i]
                                                     : (int8_t)APST_NOTINST;
        std::string v = (i < this->appman_ver.size()) ? this->appman_ver[i] : "";
        std::string tag;
        pu::ui::Color clr = onoff_color(false);
        bool pill = false;
        char buf[96];
        // Suffix showing when this entry was last checked. Only meaningful for
        // installed+sourced rows (the ones the check actually queries); the pill
        // "Update to X" chip keeps its own text, so the date is appended to the
        // other, plain-text states.
        std::string chk = this->AppCheckedLabel(e.id);
        switch (st) {
        case APST_UPDATE: {
            std::string lt =
                (i < this->appman_latest.size()) ? this->appman_latest[i] : "";
            // Show the current -> new version when the installed version is
            // known, so an update pill answers "what am I updating from"
            // without having to open the entry menu; falls back to the plain
            // "Update to X" wording when it isn't (nro_file_version failed).
            if (v.empty()) {
                snprintf(buf, sizeof(buf), tr(S_APPMAN_UPDATE_TO),
                         lt.empty() ? "?" : lt.c_str());
            } else {
                snprintf(buf, sizeof(buf), tr(S_APPMAN_UPDATE_ROW), v.c_str(),
                         lt.empty() ? "?" : lt.c_str());
            }
            tag = buf;
            clr = attention_color();
            pill = true; // a filled chip so an available update jumps out
            break;
        }
        case APST_UPTODATE:
            snprintf(buf, sizeof(buf), tr(S_APPMAN_UP_TO_DATE),
                     v.empty() ? tr(S_APPMAN_INSTALLED) : v.c_str());
            tag = buf;
            if (!chk.empty()) {
                tag += "  ·  " + chk;
            }
            clr = accent_green();
            break;
        case APST_UNCHECKED:
            // Installed + sourced, not checked yet this session: show the version
            // and when it was last checked (or "not checked").
            tag = (v.empty() ? std::string(tr(S_APPMAN_INSTALLED))
                             : (std::string("v") + v)) +
                  "  ·  " + (chk.empty() ? std::string(tr(S_APPMAN_UNCHECKED)) : chk);
            clr = onoff_color(false);
            break;
        case APST_NOSRC:
            tag = (v.empty() ? std::string()
                             : (std::string("v") + v + "  ·  ")) +
                  tr(S_APPMAN_NO_SOURCE);
            clr = onoff_color(false);
            break;
        case APST_ERR:
            tag = tr(S_APPMAN_SRC_UNREACHABLE);
            if (!chk.empty()) {
                tag += "  ·  " + chk;
            }
            clr = warn_red();
            break;
        case APST_RATELIMIT:
            tag = tr(S_APPMAN_RATE_LIMITED);
            clr = attention_color();
            break;
        case APST_OFFLINE:
            tag = tr(S_APPMAN_OFFLINE);
            clr = warn_red();
            break;
        default: // APST_NOTINST
            tag = tr(S_APPMAN_NOT_INSTALLED);
            clr = onoff_color(false);
            break;
        }
        this->layout->AddRow2(std::string(e.name), tag, lbl, clr, -1.0f,
                              console_icon("default"), "", false, pill);
    }
    s32 sel = this->appman_sel;
    if (sel >= (s32)this->appman_list.size()) {
        sel = (s32)this->appman_list.size() - 1;
    }
    if (sel < 0) {
        sel = 0;
    }
    this->layout->SetSel(sel);
}

// Re-check a single entry in place, instead of re-pulling the whole list from
// GitHub, after an entry action that may have changed it (source edited, or a
// revert swapped the installed build). Runs on the main thread: one release
// fetch with a brief toast, then just that row's state/version is refreshed.
// (Cancelling an entry menu changes nothing, so the caller re-renders the cached
// list without calling this at all.)
void MainApplication::AppRecheckOne(size_t idx) {
    if (idx >= this->appman_list.size()) {
        this->AppUpdatesRender();
        return;
    }
    UpdSource &e = this->appman_list[idx];
    // Pick up an edited source: AppSetSource persists to the manifest by id, so
    // reload the entry so a freshly-set repo is used for the check below.
    UpdSource all[UPD_MAX];
    int cnt = updman_load(all, UPD_MAX);
    for (int i = 0; i < cnt; i++) {
        if (strcasecmp(all[i].id, e.id) == 0) {
            e = all[i];
            break;
        }
    }
    std::string ipath, ver, latest, rel_url, rel_asset;
    match_installed(e, scan_switch_nros(), ipath, ver);
    int8_t st;
    if (ipath.empty()) {
        st = APST_NOTINST;
    } else if (e.repo[0] == '\0') {
        st = APST_NOSRC;
    } else {
        this->Toast(tr(S_APPMAN_CHECKING));
        char tag[64] = "", url[1024] = "", asset[256] = "";
        long code = 0;
        bool ok = update_fetch_latest_asset(e.repo, e.asset, tag, sizeof(tag),
                                            url, sizeof(url), asset,
                                            sizeof(asset), NULL, &code);
        if (!ok) {
            st = (code == 403 || code == 429) ? APST_RATELIMIT
                 : (code == 0)               ? APST_OFFLINE
                                             : APST_ERR;
        } else {
            latest = tag;
            rel_url = url;
            rel_asset = asset;
            int cmp = ver.empty() ? -1 : version_cmp(ver.c_str(), tag);
            st = (cmp < 0) ? APST_UPDATE : APST_UPTODATE;
            this->AppMarkChecked(e.id); // stamp "checked just now"
            this->appman_net_cache[this->appman_kind][e.id] = {latest, rel_url,
                                                                rel_asset};
        }
    }
    if (idx < this->appman_state.size()) {
        this->appman_state[idx] = st;
    }
    if (idx < this->appman_ver.size()) {
        this->appman_ver[idx] = ver;
    }
    if (idx < this->appman_ipath.size()) {
        this->appman_ipath[idx] = ipath;
    }
    if (idx < this->appman_latest.size()) {
        this->appman_latest[idx] = latest;
    }
    if (idx < this->appman_url.size()) {
        this->appman_url[idx] = rel_url;
    }
    if (idx < this->appman_asset.size()) {
        this->appman_asset[idx] = rel_asset;
    }
    this->appman_sel = (s32)idx;
    this->AppUpdatesRender();
}

// ---- "Receiving from PC" page for a push over the always-on live link ------
// The transfer server (8080) has its own receive screens; the always-on server
// (8081) is polled on every screen, so a push would otherwise land silently.
// These surface a transient page while one arrives, and it continues in the
// background if the user steps away (see the inv_recv_active branch in input).

// A push started arriving: register a Queue-tab item for it and jump there, the
// same treatment every other transfer now gets. The push continues in the
// background (InvServerPoll runs on every screen), feeding LiveRecvTick.
void MainApplication::LiveRecvBegin() {
    this->inv_recv_active = true;
    // Name/badge the item from what's arriving: an .nro is an app update (the
    // desktop sends its name in X-Filename), anything else lands in the inbox. A
    // nameless buffered collection push still gets a plain placeholder.
    std::string nm = this->inv_srv.recv_name;
    bool is_nro = nm.size() > 4 &&
                  strcasecmp(nm.c_str() + nm.size() - 4, ".nro") == 0;
    const char *target = is_nro ? "HaulNX" : "inbox";
    this->live_xslot = this->BeginXfer(
        nm.empty() ? std::string(tr(S_LIVE_RECV_TITLE)) : nm, target,
        is_nro ? 2 : 1);
}

// Mirror the push's byte progress into its Queue-tab item.
void MainApplication::LiveRecvTick(size_t now, size_t total) {
    queue_ext_progress(this->live_xslot, now, total, 0);
}

// The push finished (applied by InvApply*) or aborted: mark its Queue-tab item
// done/failed. The user stays wherever they are (already on the Queue tab).
void MainApplication::LiveRecvEnd(bool ok) {
    queue_ext_finish(this->live_xslot, ok, ok ? NULL : "failed");
    this->live_xslot = -1;
    this->inv_recv_active = false;
}

// (Legacy) kept only so any stray reference compiles; the receive page is gone.
void MainApplication::LiveRecvHide() {
    this->inv_recv_active = false;
}

// queue_ext_add + jump to the Queue tab, so a starting transfer is immediately
// visible in the one place every transfer now lives. Returns the item's slot.
int MainApplication::BeginXfer(const std::string &name,
                               const std::string &target, uint8_t xkind) {
    int slot = queue_ext_add(name.c_str(), target.c_str(), xkind);
    this->GotoTab(Tab::Queue);
    return slot;
}

// Per-frame (from HandleInput, after InvServerPoll): drive the app-initiated
// pulls that used to own the UI on their own screen. Each mirrors its live
// progress into its Queue-tab item and, when its worker finishes, reaps it via
// the existing Tick (which installs/toasts and marks the item done). A Queue-tab
// cancel on the item sets the worker's cancel flag. Receives are driven from
// their own servers (InvServerPoll / PollXfers Phase 3), not here.
void MainApplication::PollXfers() {
    if (this->upd.running) {
        if (this->upd_xslot >= 0 && queue_ext_cancelled(this->upd_xslot)) {
            this->upd_cancel = true;
        }
        if (!this->upd.done) {
            queue_ext_progress(this->upd_xslot, this->upd_now, this->upd_total, 0);
        } else {
            this->UpdTick(); // joins, installs/stages, finishes the item
        }
    }
    if (this->dat.running) {
        if (this->dat_xslot >= 0 && queue_ext_cancelled(this->dat_xslot)) {
            this->dat_cancel = true;
        }
        if (!this->dat.done) {
            queue_ext_progress(this->dat_xslot, this->dat_idx, this->dat_total, 0);
        } else {
            this->DatSyncTick(); // toasts + finishes the item
        }
    }
    // Tools update-manager installs: same shape as the self-update above, but
    // a small pool (see UmiJob) so several run concurrently instead of one
    // stomping another's in-flight state.
    for (int j = 0; j < UMI_MAX; j++) {
        UmiJob &job = this->umi_jobs[j];
        if (!job.task.running) {
            continue;
        }
        if (job.xslot >= 0 && queue_ext_cancelled(job.xslot)) {
            job.cancel = true;
        }
        if (!job.task.done) {
            queue_ext_progress(job.xslot, job.now, job.total, 0);
        } else {
            this->UmiTick(j); // joins, validates, backs up, swaps, finishes
        }
    }
    // Background unpack for a Wi-Fi push landed in a console folder (see
    // InvApplyFile) -- no Queue-tab item and no live progress to mirror, since
    // the desktop already got its "done" response before this started; just
    // reap it once finished so Join() doesn't pile up.
    if (this->pxt.running && this->pxt.done) {
        this->PushExtractTick();
    }
    // A USB (MTP) session runs on every screen now, so a copy started from the
    // connect screen keeps flowing after we jump to the Queue tab. UsbMtpTick
    // services the pipe, mirrors transfers into queue items, and tears the
    // session down on unplug. Also runs when the responder is up in the
    // background (usb_bg) for the inventory server — no screen needed.
    if (this->usb_open || this->usb_bg) {
        this->UsbMtpTick();
    }
    // The Wi-Fi receiver is likewise serviced on every screen so a ROM push keeps
    // arriving (and updating its Queue-tab item) after we jump to the Queue tab.
    // Non-ROM flavours (collection / .nro / DAT) complete on their own screen.
    if (this->imp_open) {
        this->ImportTick();
    }
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
    this->imp_rom = false;
    this->imp_dat = false;
    this->imp_prog = false;
    this->screen = Screen::Import;

    // The path is this session's one-time code: without it the receiver
    // refuses uploads, so only someone shown this screen can send anything.
    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/%s", ip, HTTPSRV_PORT,
             this->imp_srv.token);

    xfer_log("listening  %s", url);

    this->layout->SetTitle(tr(S_TITLE_IMPORT));
    this->layout->SetSubtitle(tr(S_SUB_IMPORT));
    this->layout->ClearMenu();
    // The console is the instruction sheet until the user reaches the page, so
    // it carries the address and the steps: badge, URL, then what to do. The
    // accent chip points power users at the other way in — pushing straight from
    // the app utility on GitHub.
    this->layout->SetEmptyState(g_header_logo, url, tr(S_IMPORT_STEPS), true,
                                tr(S_IMPORT_REPO_NOTE), this->imp_srv.token);
}

// ---- export this console's collection to a PC on the same LAN -------------
// The mirror of ImportStart: same receiver, opened in export mode so the browser
// (or the app utility's Export tab) can pull the running dl_sources.json back to
// a computer to edit. Nothing is received here — the screen just serves the
// export until the user backs out, so it reuses the whole Import machinery
// (poll/tick/stop/return) with only the mode, title and steps changed.
void MainApplication::ExportStart() {
    char ip[64];
    if (!httpsrv_local_ip(ip, sizeof(ip))) {
        this->ToastErr(tr(S_IMPORT_NO_NET));
        return;
    }
    if (!httpsrv_open(&this->imp_srv)) {
        this->ToastErr(tr(S_IMPORT_SRV_FAIL));
        return;
    }
    this->imp_srv.mode = HTTPSRV_MODE_EXPORT; // serve the export, refuse uploads
    this->imp_open = true;
    this->imp_grace = 0;
    this->imp_onboard = false;
    this->imp_nro = false; // returns to Manage data, like an import
    this->imp_rom = false;
    this->imp_dat = false;
    this->imp_prog = false;
    this->screen = Screen::Import;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/%s", ip, HTTPSRV_PORT,
             this->imp_srv.token);

    xfer_log("listening  %s (export)", url);

    this->layout->SetTitle(tr(S_TITLE_EXPORT));
    this->layout->SetSubtitle(tr(S_SUB_EXPORT));
    this->layout->ClearMenu();
    this->layout->SetEmptyState(g_header_logo, url, tr(S_EXPORT_STEPS), true,
                                tr(S_IMPORT_REPO_NOTE), this->imp_srv.token);
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
    this->imp_srv.mode = HTTPSRV_MODE_NRO; // browser gets the update page
    this->imp_open = true;
    this->imp_grace = 0;
    this->imp_onboard = false;
    this->imp_nro = true;
    this->imp_rom = false;
    this->imp_dat = false;
    this->imp_prog = false;
    this->screen = Screen::Import;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/%s", ip, HTTPSRV_PORT,
             this->imp_srv.token);

    xfer_log("listening  %s (app update)", url);

    this->layout->SetTitle(tr(S_UPDATE_WIFI_TITLE));
    this->layout->SetSubtitle(tr(S_SUB_IMPORT));
    this->layout->ClearMenu();
    this->layout->SetEmptyState(g_header_logo, url, tr(S_UPDATE_WIFI_STEPS),
                                true, tr(S_IMPORT_REPO_NOTE), this->imp_srv.token);
}

// ---- receive a game into a console's install folder ------------------------
// Launched per-console from the Installed tab. Same LAN receiver as the import
// flow, but in ROM mode: the upload streams straight to disk (a game can be
// gigabytes) and lands in this console's folder — either the browser upload
// page or the app utility's "Send a game" tab can push to it. The console is
// fixed by which row opened this screen, so the sender never has to pick one.
void MainApplication::RomRecvStart(int ci, bool fromSettings, bool autosort) {
    if (ci < 0 || ci >= g_cfg.console_count) {
        return;
    }
    ConsoleGroup *g = &g_cfg.consoles[ci];
    // Resolve the install folder exactly as the queue and Installed tab do: the
    // per-console custom folder when one is set (and the feature is on), else
    // <roms_root>/<target>. In auto-sort mode the console is not chosen up front
    // — the upload streams into the inbox and the sorter files it on completion.
    std::string dir;
    if (autosort) {
        dir = INBOX_DIR;
    } else {
        const char *custom = install_folder_for(g->target);
        dir = (custom && custom[0])
                  ? std::string(custom)
                  : std::string(roms_root(&g_tico)) + "/" + g->target;
    }
    fs_mkdir_p(dir.c_str()); // stream target must exist before the first byte

    char ip[64];
    if (!httpsrv_local_ip(ip, sizeof(ip))) {
        this->ToastErr(tr(S_IMPORT_NO_NET));
        return;
    }
    if (!httpsrv_open(&this->imp_srv)) {
        this->ToastErr(tr(S_IMPORT_SRV_FAIL));
        return;
    }
    this->imp_srv.mode = HTTPSRV_MODE_ROM;
    snprintf(this->imp_srv.dest_dir, sizeof(this->imp_srv.dest_dir), "%s",
             dir.c_str());
    this->imp_open = true;
    this->imp_grace = 0;
    this->imp_onboard = false;
    this->imp_nro = false;
    this->imp_rom = true;
    this->imp_dat = false;
    this->imp_prog = false;
    this->imp_rom_from_settings = fromSettings;
    this->imp_autosort = autosort;
    this->imp_rom_ci = ci;
    this->imp_rom_dir = dir;
    this->imp_xslot = -1;        // no file arriving yet
    this->imp_recv.clear();      // fresh multi-file session
    this->imp_recv_draw = 0;
    this->imp_cur_total = 0;
    const char *full = console_full_name(g->target);
    this->imp_rom_label = autosort ? tr(S_INBOX_LABEL) : (full ? full : g->target);
    this->screen = Screen::Import;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/%s", ip, HTTPSRV_PORT,
             this->imp_srv.token);
    xfer_log("listening  %s (rom -> %s)", url, dir.c_str());

    this->layout->SetTitle(tr(S_ROM_RECV_TITLE));
    this->layout->SetSubtitle(tr(S_SUB_IMPORT));
    this->layout->ClearMenu();
    char steps[512];
    snprintf(steps, sizeof(steps), tr(S_ROM_RECV_STEPS),
             this->imp_rom_label.c_str());
    this->layout->SetEmptyState(g_header_logo, url, steps, true,
                                tr(S_IMPORT_REPO_NOTE), this->imp_srv.token);
}

// ---- receive a verification DAT into DATS_DIR ------------------------------
// Launched per-console from the Installed tab, next to Verify. Reuses the LAN
// receiver in buffered mode (a DAT is a small XML catalog, unlike a game), and
// saves the upload as DATS_DIR/<target>.dat so a later Verify finds it with no
// hand-renaming — the one manual step DAT verification used to require.
void MainApplication::DatRecvStart() {
    // When the app utility is already connected (its always-on server sees our
    // inventory), a DAT can be pushed straight from its DAT Files tab with no
    // code to type — the always-on receiver files it (InvApplyDat). Offer that
    // as the default, keeping the browser-URL path for everyone else.
    if (this->CompanionConnected()) {
        int c = this->CreateShowDialog(
            tr(S_DAT_RECV_TITLE), tr(S_DAT_RECV_HOW),
            {tr(S_DAT_RECV_VIA_APP), tr(S_DAT_RECV_VIA_URL), tr(S_CANCEL)}, false,
            {}, style_dialog);
        if (c == 0) {
            // Nothing to open: the always-on inventory server receives the push
            // on whatever screen we're on and toasts on arrival. Send the user
            // back to the DAT list with a nudge to push from the companion.
            this->Toast(tr(S_DAT_RECV_APP_HINT));
            this->GotoDats();
            return;
        }
        if (c != 1) return; // Cancel (or dismissed)
        // c == 1 falls through to the browser-URL receiver below.
    }
    char ip[64];
    if (!httpsrv_local_ip(ip, sizeof(ip))) {
        this->ToastErr(tr(S_IMPORT_NO_NET));
        return;
    }
    if (!httpsrv_open(&this->imp_srv)) {
        this->ToastErr(tr(S_IMPORT_SRV_FAIL));
        return;
    }
    this->imp_srv.mode = HTTPSRV_MODE_DAT; // browser gets the DAT upload page
    this->imp_open = true;
    this->imp_grace = 0;
    this->imp_onboard = false;
    this->imp_nro = false;
    this->imp_rom = false;
    this->imp_dat = true;
    this->imp_prog = false;
    this->screen = Screen::Import;

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/%s", ip, HTTPSRV_PORT,
             this->imp_srv.token);
    xfer_log("listening  %s (dat -> auto-detected console)", url);

    this->layout->SetTitle(tr(S_DAT_RECV_TITLE));
    this->layout->SetSubtitle(tr(S_SUB_IMPORT));
    this->layout->ClearMenu();
    this->layout->SetEmptyState(g_header_logo, url, tr(S_DAT_RECV_STEPS), true,
                                tr(S_IMPORT_REPO_NOTE), this->imp_srv.token);
}

// Every way out of the import flow comes through here. A first-run import is
// launched from the welcome dialog on Home, so landing back in Manage data
// would strand a new user in a settings submenu instead of showing them the
// collections they just imported.
void MainApplication::ImportReturn() {
    bool onboard = this->imp_onboard;
    bool nro = this->imp_nro;
    bool rom = this->imp_rom;
    bool dat = this->imp_dat;
    bool rom_settings = this->imp_rom_from_settings;
    int rom_ci = this->imp_rom_ci;
    this->imp_onboard = false; // one-shot: only the import it was set for
    this->imp_nro = false;
    this->imp_rom = false;
    this->imp_dat = false;
    this->imp_rom_from_settings = false;
    this->imp_rom_ci = -1;
    if (onboard) {
        this->GotoHome();
    } else if (nro) {
        this->GotoUpdates(); // update-over-Wi-Fi lives under Updates now
    } else if (dat) {
        // DAT receive is launched from Settings › DAT files; return there so the
        // freshly received .dat shows in the on-disk list.
        this->GotoDats();
    } else if (rom && rom_settings) {
        // Launched from Settings › Install from PC: return to that console
        // picker so the user can send to another console or step back out.
        this->GotoRecvConsole();
    } else if (rom) {
        // Back to the Installed console list, reselecting the console the
        // receiver was opened for so the user lands where they started.
        std::string nm = (rom_ci >= 0 && rom_ci < g_cfg.console_count)
                             ? std::string(g_cfg.consoles[rom_ci].target)
                             : "";
        this->GotoInstalled(roms_root(&g_tico));
        for (s32 k = 0; !nm.empty() && k < (s32)g_inst.size(); k++) {
            if (g_inst[k].name == nm) {
                this->layout->SetSel(k);
                break;
            }
        }
    } else {
        this->GotoTransfers();
    }
}

void MainApplication::ImportStop() {
    if (this->imp_open) {
        this->layout->ClearEmptyState();
        httpsrv_close(&this->imp_srv);
        this->imp_open = false;
    }
}

// ---- connect to a PC over USB (embedded MTP) ------------------------------
// Unlike the LAN receiver above, this brings the console up as a USB MTP device
// so a PC mounts the library as a drive. Stage 1 only proves the console can
// grab usb:ds in applet mode and be enumerated; the file browsing and PC->Switch
// copy land on top of this bring-up.
static bool inbox_has_files(void); // defined below; used by the tick's auto-exit

// Build one MTP top-level folder per supported console and bring device mode up.
// The PC only sees the console name, but each maps to that console's resolved
// install path — the per-console custom folder when set (and the feature is on),
// else <roms_root>/<target> — so drops land where the download queue would put
// them, custom paths included. Shared by the connect screen and the background
// inventory-server instance. Returns false if usb:ds couldn't be acquired
// (docked, or the host owns USB). Safe to call when already up (idempotent).
bool MainApplication::UsbResponderStart() {
    const char *root = roms_root(&g_tico);
    std::vector<mtp::Folder> folders;
    for (int i = 0; i < g_cfg.supported_count; i++) {
        const char *name = g_cfg.supported[i];
        if (!name[0]) continue;
        mtp::Folder f{};
        snprintf(f.name, sizeof(f.name), "%s", name);
        const char *custom = install_folder_for(name);
        if (custom && custom[0]) snprintf(f.path, sizeof(f.path), "%s", custom);
        else                     snprintf(f.path, sizeof(f.path), "%s/%s", root, name);
        folders.push_back(f);
    }
    // Full SD card access (Prefs.sd_full_access): one extra top-level "SD
    // Card" object over the whole card, generic browse/write/delete parity
    // with mounting it in Windows Explorer over MTP -- see mtp::Start.
    return mtp::Start(root, folders.data(), static_cast<int>(folders.size()),
                      INBOX_DIR, g_prefs.sd_full_access ? "sdmc:/" : nullptr);
}

void MainApplication::GotoUsbMtp(bool fromSettings) {
    if (!g_prefs.mtp_enabled) {
        this->ToastErr(tr(S_MTP_DISABLED_TOAST));
        return;
    }
    this->usb_from_settings = fromSettings;
    // Publish the inventory snapshot up front, before the USB link comes up, so
    // it's present in the host's very first root enumeration. Windows' MTP driver
    // caches that first listing; if inventory.json only appeared later (once
    // UsbMtpTick starts regenerating it) a WPD companion would keep reading the
    // stale cached tree and never see it. UsbMtpTick keeps it fresh thereafter.
    this->WriteInventoryJson();
    // The background instance (inventory server) may already have device mode up;
    // UsbResponderStart is idempotent, so this just adopts it for the screen.
    if (!this->UsbResponderStart()) {
        // usb:ds is typically only ours in handheld mode — docked, the system
        // owns USB. Leave the user where they were with a reason.
        this->ToastErr(tr(S_USB_FAIL));
        return;
    }
    this->usb_open = true;
    this->usb_seen_conn = false;
    this->usb_recvd = false;
    this->screen = Screen::UsbMtp;
    this->layout->SetTitle(tr(S_USB_TITLE));
    this->layout->SetSubtitle(tr(S_USB_WAIT));
    this->layout->ClearMenu();
    this->layout->SetEmptyState(g_header_logo, tr(S_USB_TITLE), tr(S_USB_STEPS),
                                true);
}

void MainApplication::UsbMtpTick() {
    // Watch the link every frame. Once a host has configured it, a drop back to
    // non-connected means the cable was unplugged (or the PC closed the
    // session): tear device mode down so the USB-C port returns to host mode
    // for a controller, then leave exactly as a manual exit would.
    mtp::Status s = mtp::Poll();
    this->usb_status = s; // drives CompanionConnected() for USB companions
    if (s == mtp::Status_Connected) this->usb_seen_conn = true;
    else if (this->usb_seen_conn) {
        // Host went away. The background instance (inventory server) stays up so a
        // reconnect is recognized without reopening the screen — just reset the
        // session and flush any in-flight mirror items. The foreground screen
        // still tears down (freeing usb:ds for a controller) and returns.
        if (this->usb_bg) {
            this->usb_seen_conn = false;
            this->usb_active = false;
            mtp::Xfer dxf[64];
            int dxn = mtp::GetTransfers(dxf, 64);
            for (auto &e : this->usb_xslots) {
                if (e.second < 0) continue;
                queue_ext_finish(e.second, false, "disconnected");
                // Terminal, same as the Xfer_Done/Xfer_Failed cases below --
                // stop mirroring this id so a later re-poll of it (see next
                // comment) treats it as already-handled instead of spawning a
                // duplicate card for it.
                e.second = -1;
                // Remember what just ended so a driver-retry echo of it (see
                // usb_last_end_ns above) doesn't get its own ghost card too.
                for (int i = 0; i < dxn; i++) {
                    if (dxf[i].id != e.first) continue;
                    this->usb_last_end_name = dxf[i].name;
                    this->usb_last_end_console = dxf[i].console[0] ? dxf[i].console : "inbox";
                    this->usb_last_end_ns = armGetSystemTick();
                    break;
                }
            }
            // NOT usb_xslots.clear() here: mtp::Stop()/Start() never run in the
            // background-instance path (the responder and its Xfer ring stay up
            // exactly as the comment above says), so ids already seen this
            // session stay valid and GetTransfers() keeps reporting a finished
            // one every frame until the ring evicts it. Forgetting the mapping
            // (the previous behavior) made every such re-poll look like a brand
            // new transfer -- BeginXfer'd again and immediately shown "done" --
            // which is exactly the ghost second card reported after a USB
            // disconnect. Entries above are already marked handled (second=-1),
            // so leaving them in place makes every later re-poll of the same id
            // a no-op instead. UsbMtpStop() below is the one place that still
            // clears the vector, because that path *does* tear the session down
            // and a subsequent connect legitimately restarts ids from zero.
            if (this->screen == Screen::UsbMtp) {
                this->layout->SetSubtitle(tr(S_USB_WAIT));
            }
            return;
        }
        this->UsbMtpStop();
        this->UsbMtpReturn();
        return;
    }

    mtp::Xfer xf[64];
    int n = mtp::GetTransfers(xf, 64);

    bool busy = false;
    for (int i = 0; i < n; i++) {
        if (xf[i].state == mtp::Xfer_Active || xf[i].state == mtp::Xfer_Extracting)
            busy = true;
        // Latch that at least one file actually completed this session, so exit
        // (on unplug) routes to the inbox sorter only when something was received.
        if (xf[i].state == mtp::Xfer_Done) this->usb_recvd = true;
    }
    this->usb_active = busy;

    // While a USB host is attached, keep inventory.json fresh so a PC companion
    // can pull the same snapshot the Wi-Fi inventory server serves, straight over
    // MTP. The rebuild scans the library on the render thread, so throttle it (as
    // the Wi-Fi inventory poll does) to keep browsing smooth; skipped mid-transfer
    // to avoid SD contention. `busy` only covers an in-flight MTP copy, so also
    // stand down for any other byte-moving transfer (a queue download, a Wi-Fi
    // receive) — same gate InvServerPoll uses.
    if (s == mtp::Status_Connected && !busy && !queue_io_active()) {
        static u64 last_inv = 0;
        u64 tnow = armGetSystemTick();
        if (last_inv == 0 || armTicksToNs(tnow - last_inv) >= 15000000000ULL) {
            this->WriteInventoryJson();
            last_inv = tnow;
        }
    }

    // Mirror each MTP transfer into a Queue-tab item, so a USB copy shows in the
    // same place as everything else. Keyed by the transfer's session-unique id
    // (not its array position): the progress ring drops its oldest entry after
    // 16 files, which shifts the array but not the ids, so a positional map would
    // start driving the wrong Queue items on a 16+ file drop. The first new
    // transfer jumps to the Queue tab (via BeginXfer). No per-file cancel — the
    // MTP responder has none; the whole session ends on unplug.
    for (int i = 0; i < n; i++) {
        const mtp::Xfer &x = xf[i];
        int slot = -2; // -2 = not yet seen this session
        for (auto &e : this->usb_xslots) {
            if (e.first == x.id) { slot = e.second; break; }
        }
        if (slot == -2) {
            const char *tgt = x.console[0] ? x.console : "inbox";
            // Swallow a driver-retry echo of the transfer that just ended (see
            // usb_last_end_ns's comment in MainApplication.hpp) instead of
            // giving it its own ghost "failed" card and jumping to the Queue
            // tab for it.
            bool echo = this->usb_last_end_ns != 0 &&
                        this->usb_last_end_name == x.name &&
                        this->usb_last_end_console == tgt &&
                        armTicksToNs(armGetSystemTick() - this->usb_last_end_ns) < 4000000000ULL;
            if (echo) {
                xfer_log("usb        swallowed retry echo of %s -> %s (id %u, "
                         "%llums after the original ended)",
                         x.name, tgt, (unsigned)x.id,
                         (unsigned long long)(armTicksToNs(
                             armGetSystemTick() - this->usb_last_end_ns) / 1000000ULL));
                this->usb_xslots.push_back(std::make_pair(x.id, -1));
                continue;
            }
            xfer_log("usb        new transfer %s -> %s (id %u)", x.name, tgt,
                     (unsigned)x.id);
            slot = this->BeginXfer(x.name, tgt, 1);
            this->usb_xslots.push_back(std::make_pair(x.id, slot));
        }
        if (slot < 0) {
            continue; // already reaped (terminal), or swallowed as a retry echo
        }
        switch (x.state) {
        case mtp::Xfer_Active:
            queue_ext_progress(slot, x.done, x.total, 0);
            break;
        case mtp::Xfer_Extracting:
            // The archive-auto-extract-on-drop step (mtp/responder.cpp) reuses
            // this same id/card for the whole unpack, resetting done/total to
            // the uncompressed size -- flip the card to "unzip" the moment that
            // starts, or the reset otherwise looks exactly like the transfer
            // restarting from 0% under a "recv" label that never explains why.
            queue_ext_set_kind(slot, 4);
            queue_ext_progress(slot, x.done, x.total, 0);
            break;
        case mtp::Xfer_Done:
            queue_ext_finish(slot, true, NULL);
            this->usb_last_end_name = x.name;
            this->usb_last_end_console = x.console[0] ? x.console : "inbox";
            this->usb_last_end_ns = armGetSystemTick();
            // Terminal: stop mirroring this id. GetTransfers keeps reporting the
            // done file every frame, but the item now belongs to the queue's
            // display lifecycle — so if "clear finished" frees this slot and a
            // later transfer reuses it, we must not keep driving it. -1 makes the
            // per-frame progress/finish/cancel calls above and below no-op.
            for (auto &e : this->usb_xslots)
                if (e.first == x.id) { e.second = -1; break; }
            break;
        case mtp::Xfer_Failed:
            queue_ext_finish(slot, false, "failed");
            this->usb_last_end_name = x.name;
            this->usb_last_end_console = x.console[0] ? x.console : "inbox";
            this->usb_last_end_ns = armGetSystemTick();
            for (auto &e : this->usb_xslots)
                if (e.first == x.id) { e.second = -1; break; }
            break;
        }
    }

    // A USB copy cancelled from its Queue-tab item: ask the responder to abort
    // just that transfer (it drains the remaining bytes in place so the bulk
    // pipe stays in sync, rather than dropping the whole USB session -- see
    // mtp::CancelCurrentTransfer). The Xfer_Failed case above reaps it and
    // clears the slot to -1 once the responder answers, so this stops firing
    // for that item on its own; no UsbMtpStop/return here, the link stays up
    // for whatever comes next.
    for (auto &e : this->usb_xslots) {
        if (e.second >= 0 && queue_ext_cancelled(e.second)) {
            mtp::CancelCurrentTransfer();
        }
    }

    // The connect screen itself is setup-only now (the live list moved to the
    // Queue tab): just reflect the link state in its subtitle while it's up.
    if (this->screen == Screen::UsbMtp) {
        static mtp::Status last = mtp::Status_Down;
        if (s != last) {
            this->layout->SetSubtitle(tr(s == mtp::Status_Connected ? S_USB_CONNECTED
                                                                    : S_USB_WAIT));
            last = s;
        }
    }
}

void MainApplication::UsbMtpStop() {
    if (this->usb_open || this->usb_bg) {
        if (this->usb_open) this->layout->ClearEmptyState();
        mtp::Stop();
        this->usb_open = false;
        this->usb_bg = false;
        this->usb_status = 0;
        this->usb_active = false;
        // Fail any copy still in flight when the session ends (unplug), so its
        // Queue-tab item doesn't hang at "recv". Finished ones are left as-is
        // (queue_ext_finish only acts on an in-progress item).
        for (auto &e : this->usb_xslots) {
            if (e.second >= 0) queue_ext_finish(e.second, false, "disconnected");
        }
        this->usb_xslots.clear(); // next session maps transfers to fresh items
    }
}

void MainApplication::UsbMtpReturn() {
    // Only route to the inbox sorter when a file actually completed this session
    // and landed there — a plain cancel with nothing received (even if the inbox
    // held leftovers from before) returns to the origin instead: Settings
    // (Install from PC) or the Library console list that launched the screen.
    if (this->usb_recvd && inbox_has_files()) this->SortInboxStart();
    else if (this->usb_from_settings) {
        this->GotoTransfers(); // PC Sync's "Install from USB connection" row
        this->layout->SetSel(0);
    }
    else this->GotoInstalled(roms_root(&g_tico));
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
    // A receive cancelled from its Queue-tab item: drop the in-flight upload
    // (the server keeps listening for another file) and mark the item failed.
    if (this->imp_xslot >= 0 && queue_ext_cancelled(this->imp_xslot)) {
        httpsrv_abort(&this->imp_srv);
        queue_ext_finish(this->imp_xslot, false, "cancelled");
        this->imp_xslot = -1;
        this->imp_cur_total = 0;
        this->imp_prog = false;
        if (!this->imp_rom) {
            this->layout->SetSubtitle(tr(S_SUB_IMPORT));
        }
        return;
    }
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
    int r = this->ImportPoll();
    if (r == 4) {
        // A stream broke mid-transfer (card full, or the sender dropped). For a
        // ROM the receiver stays open for more files: record the failure as a
        // row and keep listening rather than leaving the screen.
        xfer_log("rom        transfer aborted: %s", this->imp_srv.last_err);
        if (this->imp_rom) {
            std::string nm = this->imp_srv.recv_name;
            this->imp_recv.push_back({nm.empty() ? std::string("?") : nm, 0,
                                      RECV_FAILED});
            this->imp_cur_total = 0;
            queue_ext_finish(this->imp_xslot, false, "failed");
            this->imp_xslot = -1;
            return; // keep listening for more files
        }
        this->ToastErr(tr(S_ROM_RECV_FAIL));
        this->imp_prog = false;
        this->layout->SetSubtitle(tr(S_SUB_IMPORT));
        return;
    }
    if (r == 1) {
        // A ROM streams straight to disk and its upload gets a plain 200 (no
        // /sent redirect to serve). Save it into place and keep listening so the
        // sender can push more files into the same session.
        if (this->imp_rom) {
            this->RomRecvSaveOne();
            this->imp_cur_total = 0;
            bool ok = !this->imp_recv.empty() &&
                      this->imp_recv.back().status == RECV_DONE;
            queue_ext_finish(this->imp_xslot, ok, ok ? NULL : "skipped");
            this->imp_xslot = -1;
            return;
        }
        // A buffered app build (over-Wi-Fi update) finished arriving: its Queue
        // item is done; ImportApply then stages it (the on-screen restart prompt).
        if (this->imp_nro && this->imp_xslot >= 0) {
            queue_ext_finish(this->imp_xslot, true, NULL);
            this->imp_xslot = -1;
        }
        this->imp_grace = IMPORT_GRACE_FRAMES;
        return;
    }
    // Live progress while an upload is arriving (the receiver reads a slice
    // per frame). Put the waiting text back once the transfer ends however it
    // ends — completion is handled above, but an aborted client just vanishes.
    size_t now = 0, total = 0;
    if (httpsrv_receiving(&this->imp_srv, &now, &total)) {
        if (this->imp_rom) {
            this->imp_cur_total = total;
            this->imp_prog = true;
            if (this->imp_xslot < 0) {
                // First bytes of a new file: register a Queue-tab item and jump
                // there, badged with where it's headed (a console, or the inbox
                // for auto-sort). The receiver keeps listening for more files.
                std::string nm = this->imp_srv.recv_name;
                const char *key =
                    this->imp_autosort ? "inbox"
                    : (this->imp_rom_ci >= 0 &&
                       this->imp_rom_ci < g_cfg.console_count)
                          ? g_cfg.consoles[this->imp_rom_ci].target
                          : "inbox";
                this->imp_xslot = this->BeginXfer(
                    nm.empty() ? std::string("Incoming file") : nm, key, 1);
            }
            queue_ext_progress(this->imp_xslot, now, total, 0);
        } else if (this->imp_nro) {
            // An app build pushed over Wi-Fi (Settings > Update > over Wi-Fi):
            // show it in the Queue tab like every other transfer, not as a footer
            // line. It buffers in RAM and is staged by ImportApply on completion.
            this->imp_prog = true;
            if (this->imp_xslot < 0) {
                std::string nm = this->imp_srv.recv_name;
                this->imp_xslot = this->BeginXfer(
                    nm.empty() ? std::string(tr(S_UPDATE_WIFI_TITLE)) : nm,
                    "HaulNX", 2);
            }
            queue_ext_progress(this->imp_xslot, now, total, 0);
        } else {
            char sub[128];
            snprintf(sub, sizeof(sub), tr(S_RECV_PROGRESS),
                     (int)((now * 100) / total), human_size(now).c_str(),
                     human_size(total).c_str());
            this->layout->SetSubtitle(sub);
            this->imp_prog = true;
        }
    } else if (this->imp_prog) {
        // The upload stopped before completing (peer dropped): fail a buffered
        // update's Queue item so it doesn't hang at "updt".
        if (this->imp_nro && this->imp_xslot >= 0) {
            queue_ext_finish(this->imp_xslot, false, "stopped");
            this->imp_xslot = -1;
        }
        if (!this->imp_rom) {
            this->layout->SetSubtitle(tr(S_SUB_IMPORT));
        }
        this->imp_prog = false;
    }
}

// One finished ROM streamed straight to disk (nothing in s->body). Take the
// finished ".part" and its name, confirm any overwrite, and swap it into place —
// writing to ".part" first means a failed transfer never corrupts a file already
// there. The receiver is left OPEN so the sender can push more files; the row is
// recorded for the live list and the session finishes when the user backs out.
void MainApplication::RomRecvSaveOne() {
    std::string name = this->imp_srv.recv_name;
    std::string part = this->imp_srv.part_path;
    std::string dir = this->imp_rom_dir;
    uint64_t size = this->imp_cur_total;
    if (name.empty() || part.empty() || !fs_exists(part.c_str())) {
        return; // never completed (or the partial was already cleaned up)
    }
    std::string dest = dir + "/" + name;
    if (fs_exists(dest.c_str())) {
        char msg[512];
        snprintf(msg, sizeof(msg), tr(S_ROM_RECV_CONFIRM), name.c_str());
        // The confirm dialog takes over the render loop, so the receiver isn't
        // polled while it's up; the next connection waits in the listen backlog
        // and is picked up when polling resumes.
        if (!this->ConfirmDanger(tr(S_ROM_RECV_TITLE), msg)) {
            remove(part.c_str());
            xfer_log("rom        %s not replaced (declined)", name.c_str());
            this->imp_recv.push_back({name, size, RECV_SKIPPED});
            return;
        }
    }
    if (!fs_move(part.c_str(), dest.c_str())) {
        remove(part.c_str());
        xfer_log("rom        FAILED to save %s into %s", name.c_str(), dir.c_str());
        this->imp_recv.push_back({name, size, RECV_FAILED});
        return;
    }
    // Landed straight in a chosen console folder (not auto-sort -> inbox):
    // unpack on arrival, same as an MTP drop or a download-queue install. An
    // inbox landing stays whole here; SortFileRow unpacks it when the sorter
    // later files it out, so it isn't extracted twice.
    if (dir != INBOX_DIR && is_archive_name(name.c_str())) {
        int ow = 0;
        int n = extract_archive(dest.c_str(), dir.c_str(), NULL, NULL, &ow);
        if (n <= 0) {
            size_t nl = name.size();
            if (nl > 4 && strcasecmp(name.c_str() + nl - 4, ".rar") == 0) {
                ow = 0;
                n = rar3_extract(dest.c_str(), dir.c_str(), NULL, NULL, &ow);
            }
        }
        if (n > 0) {
            remove(dest.c_str());
        }
    }
    xfer_log("rom        saved %s into %s", name.c_str(), dir.c_str());
    this->imp_recv.push_back({name, size, RECV_DONE});
}

// The user backed out of the receive screen. Stop listening, then either hand
// the inbox to the sorter (auto-sort mode) or summarise what landed and return.
void MainApplication::RomRecvFinish() {
    // A file still arriving when the user finishes is interrupted by the socket
    // close below; mark its Queue-tab item failed so it doesn't hang at "recv".
    if (this->imp_xslot >= 0) {
        queue_ext_finish(this->imp_xslot, false, "stopped");
        this->imp_xslot = -1;
    }
    bool autosort = this->imp_autosort;
    int done = 0;
    for (const auto &e : this->imp_recv) {
        if (e.status == RECV_DONE) done++;
    }
    this->ImportStop(); // close the socket; received files are already on disk
    if (autosort && done > 0) {
        // Files streamed into the inbox: reset the one-shot receive flags and let
        // the sorter identify everything that landed and file it (or ask).
        this->imp_autosort = false;
        this->imp_rom = false;
        this->imp_rom_from_settings = false;
        this->imp_rom_ci = -1;
        this->imp_recv.clear();
        this->SortInboxStart();
        return;
    }
    if (done > 0) {
        char t[64];
        snprintf(t, sizeof(t), tr(S_ROM_RECV_MULTI_DONE), done);
        this->Toast(t);
    }
    this->imp_recv.clear();
    this->ImportReturn();
}

// Rebuild the receive list in the USB transfer screen's row style: one row per
// completed file with a full bar, plus a live row for the file arriving now.
void MainApplication::RenderRecvList(size_t now, size_t total, bool receiving) {
    // Throttle rebuilds to ~10 Hz so the volatile size text doesn't re-rasterize
    // every frame (matches the USB screen). A forced rebuild passes draw=0.
    uint64_t tick = armGetSystemTick();
    if (this->imp_recv_draw != 0 &&
        armTicksToNs(tick - this->imp_recv_draw) < 100000000ULL) {
        return;
    }
    this->imp_recv_draw = tick;

    if (this->imp_recv.empty() && !receiving) {
        return; // nothing yet: leave the instruction sheet up
    }

    this->layout->SetSubtitle(
        tr(receiving ? S_RECV_LIST_RECEIVING : S_RECV_LIST_IDLE));
    this->layout->ClearMenu(false); // also clears the empty-state sheet

    // Badge every row with the destination console (or the inbox), like USB.
    pu::sdl2::Texture icon = nullptr;
    std::string prefix;
    const char *key = this->imp_autosort ? "inbox"
                      : (this->imp_rom_ci >= 0 &&
                         this->imp_rom_ci < g_cfg.console_count)
                            ? g_cfg.consoles[this->imp_rom_ci].target
                            : "";
    if (key[0]) {
        icon = console_icon(key);
        char up[64];
        size_t j = 0;
        for (; key[j] && j < sizeof(up) - 1; j++) {
            char ch = key[j];
            up[j] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
        }
        up[j] = '\0';
        prefix = std::string(up) + "  ";
    }

    for (const auto &e : this->imp_recv) {
        char right[64];
        pu::ui::Color rc;
        if (e.status == RECV_DONE) {
            snprintf(right, sizeof(right), "%s", tr(S_RECV_DONE_ROW));
            rc = accent_green();
        } else if (e.status == RECV_SKIPPED) {
            snprintf(right, sizeof(right), "%s", tr(S_RECV_SKIPPED));
            rc = value_color();
        } else {
            snprintf(right, sizeof(right), "%s", tr(S_USB_FAILED));
            rc = value_color();
        }
        this->layout->AddRow2(prefix + e.name, right, g_theme->row_text, rc,
                              1.0f, icon, "", false, false, false, 1);
    }
    if (receiving) {
        float prog = (total > 0) ? (float)((double)now / (double)total) : -1.0f;
        if (prog > 1.0f) prog = 1.0f;
        int pct = (total > 0) ? (int)((now * 100) / total) : 0;
        char right[96];
        snprintf(right, sizeof(right), tr(S_USB_XFER), pct,
                 human_size(now).c_str(), human_size(total).c_str());
        std::string nm = this->imp_srv.recv_name;
        this->layout->AddRow2(prefix + (nm.empty() ? std::string("...") : nm),
                              right, g_theme->row_text, accent_green(), prog,
                              icon, "", true, false, false, 0);
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

    // A DAT pushed from the per-console "Receive DAT" screen: validate and save
    // it for that console rather than treating it as a collection/nro.
    if (this->imp_dat) {
        this->DatApply(body, len); // takes ownership of body
        return;
    }

    // The .nro can also arrive zipped (or in any archive extract_archive/RAR3
    // reads) -- unpack it and fall straight into the NRO0 check below with the
    // found build's bytes instead. A no-op for anything that isn't an archive
    // containing an .nro (a plain collection push included).
    try_unpack_nro_push(&body, &len);

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
    // Yes is the default here: the user just sent this build over to install
    // it, so the expected answer is "install". The red title still flags that
    // it replaces the running app; B (or Cancel) dismisses as "no".
    int cr = this->CreateShowDialog(tr(S_TITLE_UPDATE), msg,
                                    {tr(S_YES), tr(S_CANCEL)}, false, {},
                                    style_dialog_danger);
    if (cr != 0) {
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

// As NroApply, but reached from the always-on server's background poll (app
// utility › Device Transfer › App update, while connected) rather than the LAN
// receive screen. Still confirms on-screen — replacing the running app is never
// silent — but there is no receive screen to return to, so no ImportReturn.
void MainApplication::InvApplyNro(char *body, size_t len) {
    char ver[24];
    if (!nro_buf_version(body, len, ver, sizeof(ver))) {
        snprintf(ver, sizeof(ver), "?");
    }
    xfer_log("received   PC push of app build v%s (%zu bytes)", ver, len);

    char msg[512];
    snprintf(msg, sizeof(msg), tr(S_NRO_CONFIRM), ver, human_size(len).c_str(),
             APP_VERSION_STR);
    int cr = this->CreateShowDialog(tr(S_TITLE_UPDATE), msg,
                                    {tr(S_YES), tr(S_CANCEL)}, false, {},
                                    style_dialog_danger);
    if (cr != 0) {
        xfer_log("cancelled  PC app build v%s not installed", ver);
        free(body);
        return;
    }

    std::string selfp = resolve_self_path();
    std::string stage = selfp + ".new";
    remove(stage.c_str());
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
        remove(stage.c_str());
        xfer_log("FAILED     PC app build could not be staged at %s",
                 stage.c_str());
        this->ToastErr(tr(S_NRO_STAGE_FAIL));
        return;
    }
    xfer_log("staged     PC app build v%s -> %s", ver, stage.c_str());
    char done[256];
    snprintf(done, sizeof(done), tr(S_NRO_STAGED), ver);
    this->StagedRestartPrompt(done);
}

static const char *dat_system_target(const char *system); // defined below

// ---- a DAT pushed over one of the receivers -------------------------------
// The received bytes are staged to a temp file, parsed to prove they are a real
// DAT and to read which system it catalogs, then filed under that console. The
// PC never picks a console — the DAT's own <header> decides — so a mismatched
// upload can't be saved against the wrong one. Staging first means a bad upload
// never clobbers a working DAT already there.
//
// Two receivers land here: the per-console "Receive DAT from PC" screen (8080,
// DatApply, which confirms on-screen) and the always-on inventory server (8081,
// InvApplyDat, no modal). The shared staging + validation lives in this helper
// so both agree on what a valid DAT is and where it files.
namespace {
enum DatStageResult { DAT_STAGE_OK, DAT_STAGE_WRITE, DAT_STAGE_PARSE,
                      DAT_STAGE_UNKNOWN };
// Stage `body` to DATS_DIR/incoming.dat.tmp and validate it. On DAT_STAGE_OK,
// out_target/out_label/out_count describe the DAT and the temp file is left in
// place for the caller to fs_move into DATS_DIR/<target>.dat (or remove). On any
// other result the temp file is already cleaned up. out_sys carries the DAT's
// header name for the "unknown system" message. body is always freed.
static DatStageResult dat_stage(char *body, size_t len, std::string &tmp_out,
                                std::string &out_target, std::string &out_label,
                                int &out_count, std::string &out_sys) {
    fs_mkdir_p(DATS_DIR);
    std::string tmp = std::string(DATS_DIR) + "/incoming.dat.tmp";
    tmp_out = tmp;

    FILE *f = fopen(tmp.c_str(), "wb");
    bool wrote = f && fwrite(body, 1, len, f) == len;
    if (f && fclose(f) != 0) wrote = false;
    free(body);
    if (!wrote) {
        remove(tmp.c_str());
        xfer_log("FAILED     DAT could not be staged at %s", tmp.c_str());
        return DAT_STAGE_WRITE;
    }
    // Prove it parses as a DAT — an unrelated file (or a truncated transfer) is
    // rejected here rather than saved and failing later.
    Dat d;
    if (!dat_load(&d, tmp.c_str())) {
        remove(tmp.c_str());
        xfer_log("rejected   DAT upload of %zu bytes: not a valid DAT", len);
        return DAT_STAGE_PARSE;
    }
    const char *det = dat_system_target(d.system); // header decides the console
    if (!det) {
        out_sys = d.system;
        dat_free(&d);
        remove(tmp.c_str());
        xfer_log("rejected   DAT system '%s': no matching console",
                 out_sys.empty() ? "?" : out_sys.c_str());
        return DAT_STAGE_UNKNOWN;
    }
    out_target = det;
    const char *full = console_full_name(out_target.c_str());
    out_label = full ? full : out_target;
    out_count = d.count;
    dat_free(&d);
    xfer_log("received   DAT for %s: %d entries (%zu bytes)", out_target.c_str(),
             out_count, len);
    return DAT_STAGE_OK;
}
} // namespace

void MainApplication::DatApply(char *body, size_t len) {
    std::string tmp, target, label, sys;
    int count = 0;
    DatStageResult r = dat_stage(body, len, tmp, target, label, count, sys);
    if (r == DAT_STAGE_UNKNOWN) {
        char m[256];
        snprintf(m, sizeof(m), tr(S_DAT_RECV_UNKNOWN), sys.empty() ? "?"
                                                                   : sys.c_str());
        this->ToastErr(m);
        this->ImportReturn();
        return;
    }
    if (r != DAT_STAGE_OK) {
        this->ToastErr(tr(S_DAT_RECV_BAD));
        this->ImportReturn();
        return;
    }
    std::string dest = std::string(DATS_DIR) + "/" + target + ".dat";
    char msg[256];
    snprintf(msg, sizeof(msg), tr(S_DAT_RECV_CONFIRM), count, label.c_str());
    if (this->CreateShowDialog(tr(S_DAT_RECV_TITLE), msg,
                               {tr(S_OK), tr(S_CANCEL)}, false, {},
                               style_dialog) != 0) {
        remove(tmp.c_str());
        xfer_log("cancelled  DAT for %s not saved", target.c_str());
        this->ImportReturn();
        return;
    }
    if (!fs_move(tmp.c_str(), dest.c_str())) { // replaces any existing DAT
        remove(tmp.c_str());
        xfer_log("FAILED     DAT could not be saved to %s", dest.c_str());
        this->ToastErr(tr(S_DAT_RECV_BAD));
        this->ImportReturn();
        return;
    }
    xfer_log("dat        saved %d entries -> %s", count, dest.c_str());
    char done[96];
    snprintf(done, sizeof(done), tr(S_DAT_RECV_DONE), count);
    this->Toast(done);
    this->ImportReturn();
}

// A DAT pushed to the always-on inventory server (companion › DAT Files › push
// while connected). Filed silently by its own header — no confirm modal, since
// InvServerPoll runs on every screen and the DAT can't land on the wrong
// console — mirroring the collection push. Refreshes the inventory so the
// companion sees has_dat flip. body is owned here.
void MainApplication::InvApplyDat(char *body, size_t len) {
    std::string tmp, target, label, sys;
    int count = 0;
    DatStageResult r = dat_stage(body, len, tmp, target, label, count, sys);
    if (r != DAT_STAGE_OK) {
        this->ToastErr(r == DAT_STAGE_UNKNOWN ? tr(S_DAT_RECV_UNKNOWN_SHORT)
                                              : tr(S_DAT_RECV_BAD));
        return;
    }
    std::string dest = std::string(DATS_DIR) + "/" + target + ".dat";
    if (!fs_move(tmp.c_str(), dest.c_str())) {
        remove(tmp.c_str());
        xfer_log("FAILED     DAT could not be saved to %s", dest.c_str());
        this->ToastErr(tr(S_DAT_RECV_BAD));
        return;
    }
    xfer_log("push       PC DAT saved %d entries -> %s", count, dest.c_str());
    this->inv_last_gen_ns = 0; // regen inventory: has_dat/dat_bytes changed
    char done[128];
    snprintf(done, sizeof(done), tr(S_DAT_PUSH_DONE), label.c_str(), count);
    this->Toast(done);
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
    this->GotoTransfers();
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
    this->GotoMetaCache(); // refresh-all is reached from Data Files now
    this->layout->SetSel(2);
}

// ---- DAT auto-download (Storage -> DAT files) -----------------------------
// The verification DATs come from the Fresh1G1R daily 1G1R project (McLean
// variant), which carries the No-Intro (cartridge) and Redump (disc/arcade)
// sets in sibling folders. There is no API, so a refresh reads each folder
// listing off the GitHub contents endpoint and pulls the .dat whose system name
// matches each configured console.
#define DAT_REPO_API_BASE                                                    \
    "https://api.github.com/repos/UnluckyForSome/Fresh1G1R/contents/"        \
    "daily-1g1r-dat/McLean/"
#define DAT_FOLDER_NOINTRO "no-intro"
#define DAT_FOLDER_REDUMP  "redump"

// Every DAT this app knows how to place, as (console target, system name,
// source folder). One table serves both directions:
//   * download — `folder` names the Fresh1G1R subfolder to pull from, and
//     `name` is matched against the live listing's filenames.
//   * identify — a DAT arriving from a PC is filed by matching its own
//     <header><name> against `name` (see dat_system_target).
// Both work off the same string because Fresh1G1R names each file after the
// system it catalogs. Matching is `name` exactly, or `name` followed by " ("
// (see sys_name_match), which absorbs the suffixes both sides add: the listing's
// " (Redump - Fresh1G1R - McLean).dat" and the header's " (Retool)".
//
// folder == NULL marks an *alias*: an alternate spelling of the same system
// that we can identify but never download under. Redump's own DATs and the
// Retool-rewritten ones disagree on several names ("Sega - Mega-CD - Sega CD"
// vs "Sega - Mega CD & Sega CD"), so a DAT fetched by hand from redump.org
// still files itself against the right console.
//
// 3DS is pinned to the "(Decrypted)" No-Intro set — the canonical set emulators
// use; the Encrypted DAT shares the same "Nintendo - Nintendo 3DS" prefix and
// would leave those ROMs unverified.
struct DatSource {
    const char *key;    // console target folder id
    const char *name;   // system name: listing anchor + DAT header
    const char *folder; // Fresh1G1R subfolder, or NULL for an identify-only alias
};
static const DatSource DAT_SOURCES[] = {
    // --- No-Intro: cartridge/handheld systems ---
    {"nes", "Nintendo - Nintendo Entertainment System", DAT_FOLDER_NOINTRO},
        {"fds", "Nintendo - Family Computer Disk System", DAT_FOLDER_NOINTRO},
        {"snes", "Nintendo - Super Nintendo Entertainment System", DAT_FOLDER_NOINTRO},
        {"n64", "Nintendo - Nintendo 64", DAT_FOLDER_NOINTRO},
        {"gb", "Nintendo - Game Boy", DAT_FOLDER_NOINTRO},
        {"gbc", "Nintendo - Game Boy Color", DAT_FOLDER_NOINTRO},
        {"gba", "Nintendo - Game Boy Advance", DAT_FOLDER_NOINTRO},
        {"nds", "Nintendo - Nintendo DS", DAT_FOLDER_NOINTRO},
        {"3ds", "Nintendo - Nintendo 3DS (Decrypted)", DAT_FOLDER_NOINTRO},
        {"virtual-boy", "Nintendo - Virtual Boy", DAT_FOLDER_NOINTRO},
        {"pokemon-mini", "Nintendo - Pokemon Mini", DAT_FOLDER_NOINTRO},
        {"game-and-watch", "Nintendo - Game & Watch", DAT_FOLDER_NOINTRO},
        {"sg-1000", "Sega - SG-1000 - SC-3000", DAT_FOLDER_NOINTRO},
        {"master-system", "Sega - Master System - Mark III", DAT_FOLDER_NOINTRO},
        {"game-gear", "Sega - Game Gear", DAT_FOLDER_NOINTRO},
        {"genesis", "Sega - Mega Drive - Genesis", DAT_FOLDER_NOINTRO},
        {"sega-32x", "Sega - 32X", DAT_FOLDER_NOINTRO},
        {"pc-engine", "NEC - PC Engine - TurboGrafx-16", DAT_FOLDER_NOINTRO},
        {"supergrafx", "NEC - PC Engine SuperGrafx", DAT_FOLDER_NOINTRO},
        {"neo-geo-pocket", "SNK - NeoGeo Pocket", DAT_FOLDER_NOINTRO},
        {"neo-geo-pocket-color", "SNK - NeoGeo Pocket Color", DAT_FOLDER_NOINTRO},
        {"atari-2600", "Atari - Atari 2600", DAT_FOLDER_NOINTRO},
        {"atari-5200", "Atari - Atari 5200", DAT_FOLDER_NOINTRO},
        {"atari-7800", "Atari - Atari 7800", DAT_FOLDER_NOINTRO},
        {"atari-lynx", "Atari - Atari Lynx", DAT_FOLDER_NOINTRO},
        {"atari-jaguar", "Atari - Atari Jaguar", DAT_FOLDER_NOINTRO},
        {"wonderswan", "Bandai - WonderSwan", DAT_FOLDER_NOINTRO},
        {"wonderswan-color", "Bandai - WonderSwan Color", DAT_FOLDER_NOINTRO},
        {"colecovision", "Coleco - ColecoVision", DAT_FOLDER_NOINTRO},
        {"intellivision", "Mattel - Intellivision", DAT_FOLDER_NOINTRO},
        {"odyssey2", "Magnavox - Odyssey 2", DAT_FOLDER_NOINTRO},
        {"vectrex", "GCE - Vectrex", DAT_FOLDER_NOINTRO},
        {"channel-f", "Fairchild - Channel F", DAT_FOLDER_NOINTRO},
        {"supervision", "Watara - Supervision", DAT_FOLDER_NOINTRO},
    // --- Redump: disc-based systems and disc arcade platforms. These are the
    // consoles that used to get no DAT at all; the names are the Retool
    // spellings Fresh1G1R files them under. ---
        {"psx", "Sony - PlayStation", DAT_FOLDER_REDUMP},
        {"ps2", "Sony - PlayStation 2", DAT_FOLDER_REDUMP},
        {"psp", "Sony - PlayStation Portable", DAT_FOLDER_REDUMP},
        {"gc", "Nintendo - GameCube", DAT_FOLDER_REDUMP},
        {"wii", "Nintendo - Wii", DAT_FOLDER_REDUMP},
        {"saturn", "Sega - Saturn", DAT_FOLDER_REDUMP},
        {"dc", "Sega - Dreamcast", DAT_FOLDER_REDUMP},
        {"sega-cd", "Sega - Mega CD & Sega CD", DAT_FOLDER_REDUMP},
        {"pc-engine-cd", "NEC - PC Engine CD & TurboGrafx CD", DAT_FOLDER_REDUMP},
        {"neo-geo-cd", "SNK - Neo Geo CD", DAT_FOLDER_REDUMP},
        {"3do", "Panasonic - 3DO Interactive Multiplayer", DAT_FOLDER_REDUMP},
        {"cd-i", "Philips - CD-i", DAT_FOLDER_REDUMP},
        {"naomi", "Arcade - Sega - Naomi", DAT_FOLDER_REDUMP},
    // --- Identify-only aliases: the spellings redump.org's own DATs use, so a
    // hand-downloaded one still lands on the right console. ---
        {"sega-cd", "Sega - Mega-CD - Sega CD", NULL},
        {"pc-engine-cd", "NEC - PC Engine CD - TurboGrafx-CD", NULL},
        {"naomi", "Sega - NAOMI", NULL},
        {"gc", "Nintendo - GameCube - NKit RVZ", NULL},
        {"wii", "Nintendo - Wii - NKit RVZ", NULL},
    // No Fresh1G1R set exists for these, but a DAT supplied by hand still files
    // itself: Wii U and PC-FX are absent from both folders, and the cartridge
    // arcade sets (MAME/FBNeo) are not a Redump product at all.
        {"wiiu", "Nintendo - Wii U", NULL},
        {"pc-fx", "NEC - PC-FX", NULL},
        {"atomiswave", "Sammy - Atomiswave", NULL},
        {"neo-geo", "SNK - Neo Geo", NULL},
};

// The Fresh1G1R folder + listing anchor for a console target, or NULL when no
// downloadable set covers it (an identify-only alias, or a system nobody
// publishes a DAT for).
static const DatSource *dat_source_for(const char *target) {
    if (!target) return NULL;
    for (size_t i = 0; i < sizeof(DAT_SOURCES) / sizeof(DAT_SOURCES[0]); i++) {
        if (DAT_SOURCES[i].folder &&
            strcasecmp(target, DAT_SOURCES[i].key) == 0)
            return &DAT_SOURCES[i];
    }
    return NULL;
}

// Download-only filename overrides, for systems with more than one format
// variant published under the same base name (NES Headered/Headerless, N64
// BigEndian/ByteSwapped, FDS FDS/QD, NDS Decrypted/Encrypted/Download Play,
// Atari Jaguar J64/ROM). DAT_SOURCES.name stays bare so a hand-supplied DAT of
// *any* variant still self-files via sys_name_match's boundary rule; this
// table only narrows which exact file DatSyncThread requests.
//
// This is NOT the place to fix a system whose Fresh1G1R filename just isn't
// the bare system name at all (that was SG-1000 filed as "... - SC-3000", and
// Odyssey2 missing its space) — an override here only helped the download
// path, while identify (dat_system_target, for a DAT pushed from a PC) kept
// matching against the wrong bare name in DAT_SOURCES and failed with
// "Couldn't tell which console" for every such DAT. Fix DAT_SOURCES.name
// itself for those; only reach for this table for genuine sibling variants.
struct DatDlOverride { const char *key; const char *dl_name; };
static const DatDlOverride DAT_DL_OVERRIDES[] = {
    {"nes",          "Nintendo - Nintendo Entertainment System (Headered)"},
    {"fds",          "Nintendo - Family Computer Disk System (FDS)"},
    {"n64",          "Nintendo - Nintendo 64 (BigEndian)"},
    {"nds",          "Nintendo - Nintendo DS (Decrypted)"},
    {"atari-7800",   "Atari - Atari 7800 (BIN)"},
    {"atari-lynx",   "Atari - Atari Lynx (LYX)"},
    {"atari-jaguar", "Atari - Atari Jaguar (J64)"},
};
static const char *dat_dl_name_for(const char *key, const char *fallback) {
    for (size_t i = 0; i < sizeof(DAT_DL_OVERRIDES) / sizeof(DAT_DL_OVERRIDES[0]); i++)
        if (strcasecmp(key, DAT_DL_OVERRIDES[i].key) == 0)
            return DAT_DL_OVERRIDES[i].dl_name;
    return fallback;
}

// Human label for the checklist source of a console target, for the Settings
// -> DAT files list. Scans every DAT_SOURCES row (not just downloadable
// ones), so a console that's only reachable through an identify-only alias
// still resolves once its real (folder-bearing) entry is hit. "" means no
// published set covers it at all (Wii U, PC-FX, arcade cartridge sets) — a
// DAT there only exists because it was received from a PC or copied by hand.
static const char *dat_source_label_for(const char *target) {
    if (!target || !target[0]) return "";
    for (size_t i = 0; i < sizeof(DAT_SOURCES) / sizeof(DAT_SOURCES[0]); i++) {
        if (strcasecmp(target, DAT_SOURCES[i].key) != 0) continue;
        const char *folder = DAT_SOURCES[i].folder;
        if (!folder) continue; // identify-only alias, not a real source
        if (strcmp(folder, DAT_FOLDER_NOINTRO) == 0) return "No-Intro";
        if (strcmp(folder, DAT_FOLDER_REDUMP) == 0) return "Redump";
    }
    return "";
}

// True if a DAT's <header><name> is this system: an exact (case-insensitive)
// match, or the name followed by a " (" suffix (No-Intro/Redump append things
// like " (Retool)"). The boundary check is what stops "… - Game Boy" from also
// matching "… - Game Boy Color"/"Advance", or "PlayStation" swallowing "… 2".
static bool sys_name_match(const char *system, const char *name) {
    size_t n = strlen(name);
    if (strncasecmp(system, name, n) != 0) return false;
    const char *rest = system + n;
    return rest[0] == '\0' || (rest[0] == ' ' && rest[1] == '(');
}

// Reverse of the name tables: map a DAT's <header><name> to the console target
// folder it verifies, so a DAT pushed from a PC files itself without the user
// picking a console. Returns NULL when the header matches nothing we know.
static const char *dat_system_target(const char *system) {
    if (!system || !system[0]) return NULL;
    // One pass over the whole table: downloadable sets and identify-only
    // aliases are equally valid answers here. The boundary check in
    // sys_name_match already prevents prefix collisions ("… - Game Boy" vs
    // "… - Game Boy Color"), so ordering is cosmetic.
    for (size_t i = 0; i < sizeof(DAT_SOURCES) / sizeof(DAT_SOURCES[0]); i++)
        if (sys_name_match(system, DAT_SOURCES[i].name))
            return DAT_SOURCES[i].key;
    return NULL;
}

// Consoles queued for this refresh: target, its listing anchor, and which
// Fresh1G1R folder to look in.
struct DatSyncItem {
    std::string target;
    std::string match;
    std::string folder;
};
static std::vector<DatSyncItem> g_dat_items;

// Data Files: top-level Settings section hosting the two catalog-maintenance
// screens that used to sit directly under Storage — DAT files (verification
// checklists) and the metadata cache (repo listings). Grouped together since
// both are "data about your library" rather than storage/space concerns.
void MainApplication::GotoDataFiles() {
    this->screen = Screen::DataFiles;
    this->layout->SetTitle(tr(S_SEC_DATA_FILES));
    this->layout->SetSubtitle(tr(S_SUB_DATA_FILES));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    this->layout->AddRow2(tr(S_MANAGE_DAT_FILES), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);          // 0
    this->layout->AddRow2(tr(S_MANAGE_META), CHEVRON, lbl, chevron_color(),
                          -1.0f, nullptr, "", false, false);          // 1
    // 2: push the emulator/app list (with each entry's repo) to a connected
    // companion. Moved here from PC Sync — it's a catalog push, not a
    // transfer-session concern. The right cell reflects whether a companion
    // is reachable now.
    bool conn = this->CompanionConnected();
    this->layout->AddRow2(tr(S_PCSYNC_PUSH_LIST),
                          tr(conn ? S_APPMAN_PC_CONNECTED : S_APPMAN_PC_OFFLINE),
                          lbl, onoff_color(conn), -1.0f, nullptr, "", false,
                          conn);
    // 3: the on-disk box-art cache (SteamGridDB downloads) — count + size, A
    // opens ArtCacheMenu (browse via Manage Box Art, or wipe everything).
    // Also "data about your library" the same way the DAT/metadata rows are,
    // rather than a Storage/space concern.
    {
        auto art_files = list_dir(BOXART_DIR);
        uint64_t total = 0;
        int n = 0;
        for (auto &e : art_files) {
            if (e.is_dir) continue;
            n++;
            total += e.size;
        }
        char val[64];
        if (n > 0) {
            snprintf(val, sizeof(val), tr(S_ART_CACHE_N), n,
                     human_size(total).c_str());
        }
        this->layout->AddRow2(tr(S_ART_CACHE), n > 0 ? val : tr(S_ART_CACHE_NONE),
                              lbl, n > 0 ? value_color() : lbl, -1.0f, nullptr,
                              "", false, false);
    }
}

// Data Files sub-screen: the metadata cache's on/off switch, a browsable list
// of what's cached, and a one-shot refresh-all. Was three of Storage's rows;
// folded into its own screen so Storage stays about space/folders.
void MainApplication::GotoMetaCache() {
    this->screen = Screen::MetaCache;
    this->layout->SetTitle(tr(S_MANAGE_META));
    this->layout->SetSubtitle(tr(S_SUB_META_SECTION));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    {                                               // 0 metadata cache on/off
        bool b = g_prefs.use_cache;
        this->layout->AddRow2(settings_label(tr(S_META_CACHE)),
                              b ? tr(S_ON) : tr(S_OFF), lbl, onoff_color(b));
    }
    this->layout->AddRow(tr(S_VIEW_CACHED_FILES));  // 1 browse/clear the cache
    this->layout->AddRow(tr(S_REFRESH_ALL));        // 2 refetch every repo's list
}

void MainApplication::GotoDats() {
    this->screen = Screen::Dats;
    this->layout->SetTitle(tr(S_TITLE_DATS));
    this->layout->SetSubtitle(tr(S_SUB_DATS));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    this->layout->AddRow(tr(S_DAT_DOWNLOAD)); // 0: fetch/refresh from git
    this->layout->AddRow(tr(S_RECV_DAT));     // 1: receive one from a PC
    {
        // 2: which region 1G1R keeps when a title has more than one copy.
        const char *order =
            g_prefs.region_order[0] ? g_prefs.region_order : "WUEJ";
        auto region_label = [](char c) {
            switch (c) {
            case 'W': return tr(S_REGION_WORLD);
            case 'U': return tr(S_REGION_USA);
            case 'E': return tr(S_REGION_EUROPE);
            case 'J': return tr(S_REGION_JAPAN);
            default:  return "";
            }
        };
        std::string disp;
        for (int i = 0; order[i]; i++) {
            if (i) disp += " > ";
            disp += region_label(order[i]);
        }
        this->layout->AddRow2(settings_label(tr(S_REGION_PRIORITY)), disp,
                              g_theme->row_text, value_color());
    }
    // The rest mirror the metadata-cache viewer: one row per .dat on disk with
    // its size, so the user can see what verification data they actually have.
    // Each is tagged with its checklist source (No-Intro/Redump/neither) and
    // grouped by that source, so the No-Intro (cartridge) and Redump
    // (disc) sets read as two blocks instead of one flat A-Z list.
    fs_mkdir_p(DATS_DIR);
    struct DatRow { std::string name; uint64_t size; const char *src; };
    std::vector<DatRow> rows;
    for (const auto &e : list_dir(DATS_DIR)) {
        if (e.is_dir) continue;
        std::string target = e.name;
        size_t dot = target.find_last_of('.');
        if (dot != std::string::npos) target.erase(dot);
        rows.push_back({e.name, e.size, dat_source_label_for(target.c_str())});
    }
    std::sort(rows.begin(), rows.end(), [](const DatRow &a, const DatRow &b) {
        // No-Intro, then Redump, then anything unclassified last; A-Z within
        // each group. Empty-string source sorts after "Redump" naturally
        // since "" < any non-empty string is false — compare group rank
        // explicitly instead of relying on lexical order of the label.
        auto rank = [](const char *s) {
            if (strcmp(s, "No-Intro") == 0) return 0;
            if (strcmp(s, "Redump") == 0) return 1;
            return 2;
        };
        int ra = rank(a.src), rb = rank(b.src);
        if (ra != rb) return ra < rb;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    for (const auto &r : rows) {
        std::string left = r.src[0] ? ("[" + std::string(r.src) + "] " + r.name)
                                    : r.name;
        this->layout->AddRow2(left, human_size(r.size), lbl, value_color());
    }
    if (rows.empty()) this->layout->AddRow(tr(S_DAT_NONE));
}

// 1G1R region priority: four rows, most preferred first, in the order stored
// in g_prefs.region_order. A promotes the selected row (swaps it with the one
// above) -- see the Screen::RegionOrder input handler -- so any permutation is
// reachable with a handful of presses without a drag/drop gesture.
void MainApplication::GotoRegionOrder() {
    this->screen = Screen::RegionOrder;
    this->layout->SetTitle(tr(S_TITLE_REGION_PRIORITY));
    this->layout->SetSubtitle(tr(S_SUB_REGION_PRIORITY));
    this->layout->ClearMenu();
    pu::ui::Color lbl = g_theme->row_text;
    const char *order = g_prefs.region_order[0] ? g_prefs.region_order : "WUEJ";
    for (int i = 0; order[i]; i++) {
        int str = S_REGION_WORLD;
        switch (order[i]) {
        case 'W': str = S_REGION_WORLD;  break;
        case 'U': str = S_REGION_USA;    break;
        case 'E': str = S_REGION_EUROPE; break;
        case 'J': str = S_REGION_JAPAN;  break;
        }
        char rank[16];
        snprintf(rank, sizeof(rank), "#%d", i + 1);
        this->layout->AddRow2(tr(str), rank, lbl, value_color());
    }
}

void MainApplication::DatSyncStart() {
    g_dat_items.clear();
    for (int c = 0; c < g_cfg.console_count; c++) {
        const char *t = g_cfg.consoles[c].target;
        if (!t || !t[0]) continue;
        const DatSource *src = dat_source_for(t);
        if (!src) continue; // no published set covers this system
        bool dup = false;
        for (auto &it : g_dat_items)
            if (it.target == t) { dup = true; break; }
        if (!dup) g_dat_items.push_back({t, dat_dl_name_for(t, src->name), src->folder});
    }
    if (g_dat_items.empty()) {
        this->ToastErr(tr(S_DAT_NO_MATCH));
        return;
    }
    this->dat_cancel = false;
    this->dat_idx = 0;
    this->dat_total = (int)g_dat_items.size();
    this->dat_ok = 0;
    this->dat_fail = 0;
    this->dat_listing_fail = false;
    this->dat_listing_code = 0;
    if (!this->dat.Start(&MainApplication::DatSyncThread, this)) {
        this->ToastErr(tr(S_META_FAILED));
        return;
    }
    // Show the batch as one Queue-tab item (console count, not bytes; xkind 3),
    // driven by PollXfers, and jump there.
    this->dat_xslot = this->BeginXfer(tr(S_DAT_SYNC), "dats", 3);
}

void MainApplication::DatSyncThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    void *conn = net_conn_new();
    // One listing per folder the queue actually needs, fetched on first use and
    // reused for every console from that set. A folder that fails to list only
    // sinks its own consoles — a Redump outage shouldn't cost the No-Intro half
    // of the refresh — so the "listing failed" toast is reserved for the case
    // where nothing could be listed at all.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>>
        listings;
    int folders_tried = 0, folders_failed = 0;
    auto listing_for = [&](const std::string &folder)
        -> const std::vector<std::pair<std::string, std::string>> & {
        auto found = listings.find(folder);
        if (found != listings.end()) return found->second;
        folders_tried++;
        auto &files = listings[folder]; // empty on any failure below
        long lcode = 0;
        size_t llen = 0;
        std::string url = std::string(DAT_REPO_API_BASE) + folder;
        char *lbody = http_get_on(conn, url.c_str(), &lcode, &llen);
        if (!lbody || lcode != 200) {
            folders_failed++;
            self->dat_listing_code.store(lcode);
            free(lbody);
            return files;
        }
        // Flatten the listing into (name, download_url) pairs.
        int ntok = 0;
        jsmntok_t *tok = json_parse_alloc(lbody, llen, &ntok);
        if (tok && tok[0].type == JSMN_ARRAY) {
            int child = 1;
            for (int i = 0; i < tok[0].size; i++) {
                if (tok[child].type == JSMN_OBJECT) {
                    char nm[512] = "", durl[1024] = "";
                    json_copy(lbody, tok, json_obj_get(lbody, tok, child, "name"),
                              nm, sizeof(nm));
                    json_copy(lbody, tok,
                              json_obj_get(lbody, tok, child, "download_url"),
                              durl, sizeof(durl));
                    if (nm[0] && durl[0]) files.push_back({nm, durl});
                }
                child = json_tok_skip(tok, child);
            }
        }
        free(tok);
        free(lbody);
        return files;
    };

    fs_mkdir_p(DATS_DIR);
    for (auto &it : g_dat_items) {
        if (self->dat_cancel) break;
        // Exact filename match: "<system> (<No-Intro|Redump> - Fresh1G1R -
        // McLean).dat". A prefix/substring match here would collide whenever
        // Fresh1G1R ships more than one variant under the same base name
        // (see DAT_DL_OVERRIDES) — exact match pins each console to one
        // specific file regardless of listing order.
        const char *tag =
            strcmp(it.folder.c_str(), DAT_FOLDER_NOINTRO) == 0 ? "No-Intro" : "Redump";
        std::string want = it.match + " (" + tag + " - Fresh1G1R - McLean).dat";
        const char *url = NULL;
        for (auto &f : listing_for(it.folder)) {
            if (strcasecmp(f.first.c_str(), want.c_str()) == 0) {
                url = f.second.c_str();
                break;
            }
        }
        bool ok = false;
        if (url) {
            std::string dest = std::string(DATS_DIR) + "/" + it.target + ".dat";
            long dc = 0;
            ok = http_download(url, dest.c_str(), NULL, NULL, NULL, NULL, NULL, 0,
                               &dc, NULL) &&
                 dc >= 200 && dc < 300;
        }
        if (ok) self->dat_ok.fetch_add(1);
        else self->dat_fail.fetch_add(1);
        self->dat_idx.fetch_add(1);
    }
    // "listing failed" is the couldn't-even-enumerate case: reserve it for when
    // every folder we tried failed to list (a dead network / rate-limit), so a
    // single folder's outage still lets the other set's DATs through and only
    // shows the per-item "some failed" summary.
    if (folders_tried > 0 && folders_failed == folders_tried) {
        self->dat_listing_fail = true;
        self->dat_idx.store((int)g_dat_items.size());
    }
    net_conn_free(conn);
    self->dat.done = true;
}

void MainApplication::DatSyncTick() {
    // Progress is mirrored into the Queue-tab item by PollXfers; only reap here.
    if (!this->dat.done) {
        return;
    }
    this->dat.Join();
    bool ok = !this->dat_listing_fail && this->dat_fail == 0;
    queue_ext_finish(this->dat_xslot, ok,
                     this->dat_listing_fail ? "listing"
                     : this->dat_fail > 0   ? "some failed"
                                            : NULL);
    this->dat_xslot = -1;

    // The "no DAT yet, download it now?" path from Verify/Have-vs-Missing: this
    // was a single-item sync, so land back in Verify instead of the usual toast
    // — success means the DAT is now on disk and Verify can just run.
    if (this->dat_sync_then_verify) {
        this->dat_sync_then_verify = false;
        if (this->dat_ok > 0) {
            this->VerifyStart(this->dsv_folder, this->dsv_target,
                              this->dsv_label, this->dsv_force,
                              this->dsv_goto_missing);
        } else {
            long lc = this->dat_listing_code.load();
            this->ToastErr(tr(this->dat_listing_fail &&
                                      (lc == 403 || lc == 429)
                                  ? S_DAT_RATELIMIT
                              : this->dat_listing_fail ? S_DAT_LISTING_FAIL
                                                        : S_DAT_FETCH_FAIL));
        }
        return;
    }

    if (this->dat_listing_fail) {
        // GitHub's contents API is the source; an unauthenticated device shares
        // a 60/hr budget, so a 403/429 here is rate-limiting, not a dead link.
        // Point the user at the token that lifts them to 5000/hr (see net.c).
        long lc = this->dat_listing_code.load();
        this->ToastErr(tr((lc == 403 || lc == 429) ? S_DAT_RATELIMIT
                                                    : S_DAT_LISTING_FAIL));
    } else {
        char t[96];
        snprintf(t, sizeof(t), tr(S_DAT_SYNC_DONE), (int)this->dat_ok,
                 (int)this->dat_fail);
        if (this->dat_fail > 0) this->ToastErr(t);
        else this->Toast(t);
    }
}

// ---- app: DAT verification (Library tab) --------------------------------

// Record every item from a completed verify pass into the persistent status
// cache, so the Installed/Library browser can still show a verified/bad badge
// long after this VerifyJob is freed. Called on the worker thread right after
// verify_run, by both the single-console and the all-consoles verify paths —
// one place, so neither can drift out of sync with the other. Best-effort: a
// write failure just means no badge next time, never a wrong one.
static void vfy_persist_status(const VerifyJob &job) {
    VfyStatusCache vc;
    vfystatus_load(&vc, VFYSTATUS_PATH);
    for (int i = 0; i < job.count; i++) {
        const VerifyItem &it = job.items[i];
        std::string full = std::string(job.folder) + "/" + it.name;
        vfystatus_put(&vc, full.c_str(), it.size, it.mtime, it.status);
    }
    vfystatus_save(&vc, VFYSTATUS_PATH);
    vfystatus_free(&vc);
}

void MainApplication::VerifyThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    // Parse the DAT on the worker too, so a multi-MB XML file doesn't hitch the
    // UI, then scan. verify_run polls vfy_job.cancel throughout.
    self->vfy_dat_ok =
        dat_load(&self->vfy_dat, self->vfy_dat_path_str.c_str());
    if (self->vfy_dat_ok) {
        self->vfy_job.dat = &self->vfy_dat;
        verify_run(&self->vfy_job);
        vfy_persist_status(self->vfy_job);
    }
    self->vfy.done = true;
}

void MainApplication::VerifyStart(const std::string &folder,
                                  const std::string &target,
                                  const std::string &label, bool force,
                                  bool goto_missing) {
    this->vfy_goto_missing = goto_missing;
    // Make the folder discoverable and derive the expected DAT path.
    fs_mkdir_p(DATS_DIR);
    std::string dat = std::string(DATS_DIR) + "/" + target + ".dat";
    if (!fs_exists(dat.c_str())) {
        // Most consoles have a downloadable set (see DAT_SOURCES) — offer to
        // fetch it right here instead of dead-ending on "go find one
        // yourself", which is what used to make Verify/Have-vs-Missing look
        // broken for anyone who hadn't separately run Storage > DAT files.
        const DatSource *src = dat_source_for(target.c_str());
        if (src) {
            char body[700];
            snprintf(body, sizeof(body), tr(S_NO_DAT_FETCH_BODY), src->name);
            if (this->CreateShowDialog(tr(S_NO_DAT_TITLE), body,
                                       {tr(S_DAT_FETCH_NOW), tr(S_CANCEL)},
                                       false, {}, style_dialog) != 0)
                return;
            g_dat_items.clear();
            g_dat_items.push_back(
                {target, dat_dl_name_for(target.c_str(), src->name), src->folder});
            this->dat_cancel = false;
            this->dat_idx = 0;
            this->dat_total = 1;
            this->dat_ok = 0;
            this->dat_fail = 0;
            this->dat_listing_fail = false;
            this->dat_listing_code = 0;
            if (!this->dat.Start(&MainApplication::DatSyncThread, this)) {
                this->ToastErr(tr(S_META_FAILED));
                return;
            }
            this->dat_xslot = this->BeginXfer(tr(S_DAT_SYNC), "dats", 3);
            this->dat_sync_then_verify = true;
            this->dsv_folder = folder;
            this->dsv_target = target;
            this->dsv_label = label;
            this->dsv_force = force;
            this->dsv_goto_missing = goto_missing;
            this->Toast(tr(S_DAT_FETCH_QUEUED));
            return;
        }
        char body[700];
        snprintf(body, sizeof(body), tr(S_NO_DAT_BODY), dat.c_str());
        this->CreateShowDialog(tr(S_NO_DAT_TITLE), body, {tr(S_OK)}, true, {},
                               style_dialog);
        return;
    }

    // Reset the job. The DAT is parsed on the worker (VerifyThread).
    verify_free(&this->vfy_job);
    dat_free(&this->vfy_dat);
    memset(&this->vfy_job, 0, sizeof(this->vfy_job));
    snprintf(this->vfy_job.folder, sizeof(this->vfy_job.folder), "%s",
             folder.c_str());
    fs_mkdir_p(CACHE_DIR);
    snprintf(this->vfy_job.cache_path, sizeof(this->vfy_job.cache_path), "%s",
             HASH_CACHE_PATH);
    this->vfy_job.force_rehash = force;
    this->vfy_folder = folder;
    this->vfy_target = target;
    this->vfy_label = label;
    this->vfy_dat_ok = false;
    this->vfy_dat_path_str = dat;

    if (!this->vfy.Start(&MainApplication::VerifyThread, this)) {
        this->ToastErr(tr(S_META_FAILED));
        return;
    }
    this->screen = Screen::Verify;
    this->layout->SetTitle(label);
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_VERIFYING));
}

void MainApplication::VerifyTick() {
    if (!this->vfy.done) {
        int fd = this->vfy_job.files_done, ft = this->vfy_job.files_total;
        char s[160];
        snprintf(s, sizeof(s), "(%d/%d)   B %s", fd, ft, tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    this->vfy.Join();
    this->layout->HideSpinner();

    if (!this->vfy_dat_ok) {
        this->ToastErr(tr(S_DAT_LOAD_FAIL));
        verify_free(&this->vfy_job);
        dat_free(&this->vfy_dat);
        this->GotoInstalled(roms_root(&g_tico));
        return;
    }
    if (this->vfy_job.cancel) {
        this->Toast(tr(S_VERIFY_CANCELLED));
        verify_free(&this->vfy_job);
        dat_free(&this->vfy_dat);
        this->GotoInstalled(roms_root(&g_tico));
        return;
    }
    if (this->vfy_goto_missing) {
        // "Have vs Missing" entry point: skip the per-file results and open the
        // missing-titles checklist directly (same list VerifyMenu's "show missing"
        // builds). The verify pass is what produces the have/missing split.
        this->vfy_goto_missing = false;
        this->vfy_missing = this->VerifyMissingList();
        this->vfy_missing_filter.clear();
        this->vfy_missing_direct = true;
        this->VerifyMissingResults();
        return;
    }
    this->VerifyResults();
}

// The base name of a verify item's (possibly subfolder-relative) name.
static const char *vfy_base(const char *name) {
    const char *slash = strrchr(name, '/');
    return slash ? slash + 1 : name;
}

// The directory prefix of a verify item's name, with a trailing '/', or "" when
// the file is at the scan root. A rename keeps the file in this same subfolder.
static std::string vfy_dir_prefix(const char *name) {
    const char *slash = strrchr(name, '/');
    return slash ? std::string(name, slash - name + 1) : std::string();
}

// A verified file whose data matches the DAT but whose on-disk name isn't the
// canonical one — the only class of file the rename actions act on. Compares the
// base name so a correctly-named file in a subfolder is not flagged.
static bool vfy_misnamed(const VerifyItem &it) {
    return it.status == DAT_VERIFIED && it.canonical[0] &&
           strcmp(vfy_base(it.name), it.canonical) != 0;
}

// A fully-clean multi-file set (every member DAT_VERIFIED and correctly
// named) that VerifyResults collapses into one row, the same way the
// Installed browser collapses a cue/bin or multi-disc set into one game. Kept
// separate from InstGroup because a group here also needs each member's item
// index to resolve back into vfy_job.items.
struct VfyCleanGroup {
    int repr = -1;         // representative item index (the row this becomes)
    std::vector<int> members; // every item index the row stands for, sorted
    std::string name;
    uint64_t size = 0;
};

// Detect the clean sets in one verify pass, reusing inst_detect_groups (the
// exact cue/gdi/m3u + "(Track N)"/"(Disc N)" detection the Library uses) so a
// set is defined identically everywhere. A set with any bad/unknown/misnamed
// member is left ungrouped — it stays as individual, individually-actionable
// rows, so grouping never hides a problem behind a clean-looking summary row.
static std::vector<VfyCleanGroup> vfy_detect_clean_groups(
        const std::string &folder, const VerifyJob &job) {
    std::vector<VfyCleanGroup> out;
    if (!g_prefs.group_sets) return out;

    // A set's pieces always sit beside each other, so bucket by subfolder
    // (verify_run recurses; item names may carry a "sub/dir/" prefix).
    std::map<std::string, std::vector<int>> buckets;
    for (int i = 0; i < job.count; i++)
        buckets[vfy_dir_prefix(job.items[i].name)].push_back(i);

    for (auto &kv : buckets) {
        const std::string &prefix = kv.first;
        std::vector<int> &idxs = kv.second;
        if (idxs.size() < 2) continue;
        std::vector<DirEnt> ents;
        ents.reserve(idxs.size());
        for (int i : idxs) {
            DirEnt de;
            de.name = vfy_base(job.items[i].name);
            de.is_dir = false;
            de.size = job.items[i].size;
            ents.push_back(de);
        }
        std::string dir = prefix.empty()
                               ? folder
                               : folder + "/" + prefix.substr(0, prefix.size() - 1);
        for (auto &g : inst_detect_groups(dir, ents)) {
            if (g.members.size() < 2) continue;
            VfyCleanGroup vg;
            vg.name = g.name;
            vg.size = g.size;
            bool clean = true;
            for (auto &mname : g.members) {
                int found = -1;
                for (int i : idxs) {
                    if (strcasecmp(vfy_base(job.items[i].name), mname.c_str()) == 0) {
                        found = i;
                        break;
                    }
                }
                const VerifyItem *it = found >= 0 ? &job.items[found] : nullptr;
                if (!it || it->status != DAT_VERIFIED || vfy_misnamed(*it)) {
                    clean = false;
                    break;
                }
                vg.members.push_back(found);
            }
            if (!clean || vg.members.size() < 2) continue;
            std::sort(vg.members.begin(), vg.members.end());
            vg.repr = vg.members.front();
            out.push_back(std::move(vg));
        }
    }
    return out;
}

void MainApplication::VerifyResults() {
    // Collapse each fully-clean multi-file set (cue/bin, multi-disc, a bare
    // "(Track N)" run — every member DAT_VERIFIED and correctly named) into
    // one row, the same way the Installed browser collapses one into a
    // single game. A set with any bad/unknown/misnamed member is left
    // ungrouped so the problem file stays its own, individually-actionable
    // row instead of hiding behind a summary.
    auto groups = vfy_detect_clean_groups(this->vfy_folder, this->vfy_job);
    std::map<int, const VfyCleanGroup *> group_by_repr;
    std::set<int> hidden; // non-representative members: no row of their own
    for (auto &g : groups) {
        group_by_repr[g.repr] = &g;
        for (int m : g.members)
            if (m != g.repr) hidden.insert(m);
    }

    // Problems first, then actionable renames, then clean-verified (including
    // clean group rows), each block A→Z by display name. Sorting an index
    // (kept as vfy_order so input can map a row back to its item) leaves the
    // underlying results intact for anyone who wants them; a group row's
    // index is its representative member, which — being clean by
    // construction — is a silent no-op for the rename/reacquire actions.
    this->vfy_order.clear();
    this->vfy_order.reserve(this->vfy_job.count);
    for (int i = 0; i < this->vfy_job.count; i++)
        if (!hidden.count(i)) this->vfy_order.push_back(i);
    auto rank = [](const VerifyItem &it) {
        return it.status == DAT_BAD       ? 0
               : it.status == DAT_UNKNOWN ? 1
               : vfy_misnamed(it)         ? 2
                                          : 3;
    };
    auto disp_name = [&](int idx) -> const char * {
        auto git = group_by_repr.find(idx);
        return git != group_by_repr.end() ? git->second->name.c_str()
                                          : this->vfy_job.items[idx].name;
    };
    std::sort(this->vfy_order.begin(), this->vfy_order.end(),
              [&](int a, int b) {
                  int rx = rank(this->vfy_job.items[a]),
                      ry = rank(this->vfy_job.items[b]);
                  if (rx != ry) return rx < ry;
                  return strcasecmp(disp_name(a), disp_name(b)) < 0;
              });

    int n_misnamed = 0;
    for (int i = 0; i < this->vfy_job.count; i++) {
        if (vfy_misnamed(this->vfy_job.items[i])) n_misnamed++;
    }

    char summary[128];
    snprintf(summary, sizeof(summary), tr(S_VERIFY_SUMMARY), this->vfy_job.n_ok,
             this->vfy_job.n_bad, this->vfy_job.n_unknown);
    this->layout->SetTitle(this->vfy_label + " > " + summary);
    // A is contextual: rename when there are misnamed files (takes priority in
    // the hint since X rename-all pairs with it), else re-download when there are
    // bad dumps, else nothing actionable.
    const char *sub = n_misnamed             ? tr(S_SUB_VERIFY_RENAME)
                      : this->vfy_job.n_bad   ? tr(S_SUB_VERIFY_BAD)
                                              : tr(S_SUB_VERIFY);
    this->layout->SetSubtitle(sub);
    this->layout->ClearMenu();

    const pu::ui::Color green = accent_green();
    const pu::ui::Color red(224, 78, 78, 255);
    const pu::ui::Color amber = is_light_theme() ? pu::ui::Color(180, 110, 30, 255)
                                                  : pu::ui::Color(245, 175, 95, 255);
    const pu::ui::Color grey = g_theme->rom_info_clr;
    const pu::ui::Color lbl = g_theme->row_text;
    for (int idx : this->vfy_order) {
        auto git = group_by_repr.find(idx);
        if (git != group_by_repr.end()) {
            // "N files · OK": the same group_sets row style the Library uses,
            // with the status word standing in for a size.
            char gsub[64];
            snprintf(gsub, sizeof(gsub), tr(S_GROUP_SUBTITLE),
                     (int)git->second->members.size(), tr(S_VERIFY_OK));
            this->layout->AddRow2(git->second->name, gsub, lbl, green);
            continue;
        }
        const VerifyItem &it = this->vfy_job.items[idx];
        bool misnamed = vfy_misnamed(it);
        const char *st = misnamed                   ? tr(S_VERIFY_MISNAMED)
                         : it.status == DAT_VERIFIED ? tr(S_VERIFY_OK)
                         : it.status == DAT_BAD      ? tr(S_VERIFY_BAD)
                                                     : tr(S_VERIFY_UNKNOWN);
        pu::ui::Color sc = misnamed                   ? amber
                           : it.status == DAT_VERIFIED ? green
                           : it.status == DAT_BAD      ? red
                                                       : grey;
        this->layout->AddRow2(it.name, st, lbl, sc);
    }
    if (this->vfy_order.empty()) {
        this->layout->AddRow(tr(S_EMPTY));
    }
}

// Rename the file under the cursor to its DAT canonical name. Only verified-
// but-misnamed rows are actionable; anything else is a no-op. Mirrors the
// manual rename in the Library file view: refuse to clobber a *different*
// existing file, but allow a case-only rename (FAT is case-insensitive).
void MainApplication::VerifyRenameSel() {
    s32 row = this->layout->Sel();
    if (row < 0 || row >= (s32)this->vfy_order.size()) return;
    VerifyItem &it = this->vfy_job.items[this->vfy_order[row]];
    if (!vfy_misnamed(it)) return;

    char body[512];
    snprintf(body, sizeof(body), tr(S_RENAME_ONE_BODY), it.canonical);
    if (this->CreateShowDialog(tr(S_RENAME_TO_DAT), body,
                               {tr(S_RENAME_TO_DAT), tr(S_CANCEL)}, false, {},
                               style_dialog) != 0) {
        return;
    }
    std::string pfx = vfy_dir_prefix(it.name); // keep the file in its subfolder
    std::string from = this->vfy_folder + "/" + it.name;
    std::string to = this->vfy_folder + "/" + pfx + it.canonical;
    if (fs_exists(to.c_str()) && strcasecmp(it.canonical, vfy_base(it.name)) != 0) {
        this->ToastErr(tr(S_RENAME_FAILED));
        return;
    }
    if (rename(from.c_str(), to.c_str()) == 0) {
        snprintf(it.name, sizeof(it.name), "%s%s", pfx.c_str(), it.canonical);
        this->Toast(tr(S_RENAMED));
    } else {
        this->ToastErr(tr(S_RENAME_FAILED));
    }
    this->VerifyResults(); // the row is no longer misnamed; rebuild + re-sort
}

// Rename every verified-but-misnamed file to its canonical name in one pass.
void MainApplication::VerifyRenameAll() {
    int total = 0;
    for (int i = 0; i < this->vfy_job.count; i++) {
        if (vfy_misnamed(this->vfy_job.items[i])) total++;
    }
    if (total == 0) return;

    char body[96];
    snprintf(body, sizeof(body), tr(S_RENAME_ALL_BODY), total);
    if (this->CreateShowDialog(tr(S_RENAME_ALL), body,
                               {tr(S_RENAME_ALL), tr(S_CANCEL)}, false, {},
                               style_dialog) != 0) {
        return;
    }
    int done = 0, skip = 0;
    for (int i = 0; i < this->vfy_job.count; i++) {
        VerifyItem &it = this->vfy_job.items[i];
        if (!vfy_misnamed(it)) continue;
        std::string pfx = vfy_dir_prefix(it.name);
        std::string from = this->vfy_folder + "/" + it.name;
        std::string to = this->vfy_folder + "/" + pfx + it.canonical;
        if (fs_exists(to.c_str()) && strcasecmp(it.canonical, vfy_base(it.name)) != 0) {
            skip++;
            continue;
        }
        if (rename(from.c_str(), to.c_str()) == 0) {
            snprintf(it.name, sizeof(it.name), "%s%s", pfx.c_str(), it.canonical);
            done++;
        } else {
            skip++;
        }
    }
    char msg[96];
    snprintf(msg, sizeof(msg), tr(S_RENAME_ALL_DONE), done, skip);
    this->Toast(msg);
    this->VerifyResults();
}

// A on a BAD (corrupt) row: the file is named like a known dump but its data is
// wrong, so offer to fetch a replacement. A DAT name can't be mapped to an exact
// archive.org URL reliably, so hand off to the existing search — scoped to this
// console's repos and seeded with the game's name — where the user picks the
// match and downloads it with the normal A-to-queue flow.
void MainApplication::VerifyReacquireSel() {
    s32 row = this->layout->Sel();
    if (row < 0 || row >= (s32)this->vfy_order.size()) return;
    const VerifyItem &it = this->vfy_job.items[this->vfy_order[row]];
    if (it.status != DAT_BAD) return;

    char body[512];
    snprintf(body, sizeof(body), tr(S_REACQUIRE_BODY), vfy_base(it.name));
    if (this->CreateShowDialog(tr(S_REACQUIRE_TITLE), body,
                               {tr(S_REACQUIRE), tr(S_CANCEL)}, false, {},
                               style_dialog) != 0) {
        return;
    }
    // Query the base name without its extension: the metadata cache indexes
    // archive.org file names, so the bare title matches more broadly than a name
    // carrying our extension.
    std::string q = vfy_base(it.name);
    size_t dot = q.find_last_of('.');
    if (dot != std::string::npos && dot > 0) q.erase(dot);
    // Scope to this console's repos when it resolves; otherwise search all.
    ConsoleGroup *g = config_find_console(&g_cfg, this->vfy_target.c_str());
    int ci = g ? (int)(g - g_cfg.consoles) : -1;
    this->GotoSearch(q, ci, -1);
}

// Build the sorted, de-duplicated list of DAT titles that no scanned file
// verified to — the "missing from your library" set. Both the present names
// (item canonical) and DAT names are canonical base names, so they compare
// directly. Empty when no DAT was loaded.
std::vector<std::string> MainApplication::VerifyMissingList() {
    std::vector<std::string> missing;
    if (!this->vfy_dat_ok) return missing;
    std::vector<std::string> present;
    for (int i = 0; i < this->vfy_job.count; i++) {
        const VerifyItem &it = this->vfy_job.items[i];
        if (it.status == DAT_VERIFIED && it.canonical[0])
            present.emplace_back(it.canonical);
    }
    std::sort(present.begin(), present.end());
    for (int i = 0; i < this->vfy_dat.count; i++) {
        std::string nm = this->vfy_dat.roms[i].name;
        if (!std::binary_search(present.begin(), present.end(), nm))
            missing.push_back(std::move(nm));
    }
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    return missing;
}

// The missing-titles checklist: every DAT game the library doesn't have. A on a
// row hands off to archive.org search (scoped to this console, seeded with the
// title) so it can be found and downloaded, exactly like re-acquiring a bad dump.
// Y opens a text filter (large sets like PS2/GBA can be thousands of entries);
// vfy_missing_order maps the filtered rows shown back to vfy_missing indices, the
// same pattern vfy_order uses for the per-file results.
void MainApplication::VerifyMissingResults() {
    this->vfy_missing_order.clear();
    for (int i = 0; i < (int)this->vfy_missing.size(); i++) {
        if (ci_contains(this->vfy_missing[i].c_str(),
                        this->vfy_missing_filter.c_str())) {
            this->vfy_missing_order.push_back(i);
        }
    }
    char title[80];
    if (this->vfy_missing_filter.empty()) {
        snprintf(title, sizeof(title), tr(S_MISSING_TITLE),
                 (int)this->vfy_missing.size());
    } else {
        snprintf(title, sizeof(title), tr(S_MISSING_TITLE_FILTERED),
                 (int)this->vfy_missing_order.size(), (int)this->vfy_missing.size());
    }
    this->layout->SetTitle(this->vfy_label + " > " + title);
    this->layout->SetSubtitle(this->vfy_missing_filter.empty()
                                  ? tr(S_SUB_MISSING)
                                  : tr(S_SUB_MISSING_FILTERED));
    this->layout->ClearMenu();
    for (int idx : this->vfy_missing_order) this->layout->AddRow(this->vfy_missing[idx]);
    if (this->vfy_missing_order.empty())
        this->layout->AddRow(this->vfy_missing.empty() ? tr(S_EMPTY)
                                                        : tr(S_MISSING_NO_MATCH));
    this->screen = Screen::VerifyMissing;
}

// Y on the verify results screen: actions that don't fit a single button —
// browse the missing titles, export a full text report, force a fresh re-hash.
void MainApplication::VerifyMenu() {
    std::vector<std::string> miss = this->VerifyMissingList();
    const bool have_miss = !miss.empty();
    // Parallel opts/action vectors so the conditional missing-only entries don't
    // turn the result into fragile index arithmetic.
    enum { ACT_SHOW_MISS, ACT_FIXDAT, ACT_REPORT, ACT_REVERIFY, ACT_CANCEL };
    std::vector<std::string> opts;
    std::vector<int> acts;
    char showmiss[48];
    if (have_miss) {
        snprintf(showmiss, sizeof(showmiss), tr(S_SHOW_MISSING), (int)miss.size());
        opts.push_back(showmiss);           acts.push_back(ACT_SHOW_MISS);
        opts.push_back(tr(S_EXPORT_FIXDAT)); acts.push_back(ACT_FIXDAT);
    }
    opts.push_back(tr(S_EXPORT_REPORT));  acts.push_back(ACT_REPORT);
    opts.push_back(tr(S_REVERIFY_FRESH)); acts.push_back(ACT_REVERIFY);
    opts.push_back(tr(S_CANCEL));         acts.push_back(ACT_CANCEL);
    int r = this->CreateShowDialog(tr(S_VERIFY_ACTIONS), "", opts, false, {},
                                   style_dialog);
    if (r < 0 || r >= (int)acts.size()) return;
    switch (acts[r]) {
    case ACT_SHOW_MISS:
        this->vfy_missing = std::move(miss);
        this->vfy_missing_filter.clear();
        this->vfy_missing_direct = false;
        this->VerifyMissingResults();
        break;
    case ACT_FIXDAT:
        this->VerifyExportFixdat();
        break;
    case ACT_REPORT:
        this->VerifyExportReport();
        break;
    case ACT_REVERIFY:
        // The cache auto-invalidates on a size/mtime change, so a forced re-hash
        // only matters for silent corruption; make the user opt in, it re-reads
        // every byte. VerifyStart resets the job, so re-run it with force set.
        if (this->CreateShowDialog(tr(S_REVERIFY_TITLE), tr(S_REVERIFY_BODY),
                                   {tr(S_REVERIFY_TITLE), tr(S_CANCEL)}, false, {},
                                   style_dialog) == 0) {
            this->VerifyStart(this->vfy_folder, this->vfy_target, this->vfy_label,
                              true);
        }
        break;
    default:
        break;
    }
}

// Minimal XML text/attribute escaping for the fixdat writer: DAT names can carry
// & < > " ' (e.g. "Tom & Jerry"), which would otherwise break the output.
static std::string xml_escape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&apos;"; break;
        default: o += c; break;
        }
    }
    return o;
}

// Write a Logiqx "fixdat" — a DAT holding only the entries missing from the
// library — to REPORTS_DIR, so it can be fed to a PC rebuilder/downloader to
// fetch exactly what's absent. One <game>/<rom> per missing DAT entry; the same
// present-vs-DAT basis the text report and the missing list use.
void MainApplication::VerifyExportFixdat() {
    if (!this->vfy_dat_ok) {
        this->ToastErr(tr(S_REPORT_FAIL));
        return;
    }
    std::vector<std::string> present;
    for (int i = 0; i < this->vfy_job.count; i++) {
        const VerifyItem &it = this->vfy_job.items[i];
        if (it.status == DAT_VERIFIED && it.canonical[0])
            present.emplace_back(it.canonical);
    }
    std::sort(present.begin(), present.end());

    fs_mkdir_p(REPORTS_DIR);
    std::string path =
        std::string(REPORTS_DIR) + "/" + this->vfy_target + "-fixdat.dat";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        this->ToastErr(tr(S_REPORT_FAIL));
        return;
    }

    char ts[32] = "";
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *tm = localtime_r(&t, &tmv);
    if (tm) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    std::string sys = xml_escape(this->vfy_dat.system[0] ? this->vfy_dat.system
                                                         : this->vfy_label);
    fprintf(f, "<?xml version=\"1.0\"?>\n<datafile>\n\t<header>\n");
    fprintf(f, "\t\t<name>%s (fixdat)</name>\n", sys.c_str());
    fprintf(f, "\t\t<description>Missing from library - %s</description>\n",
            sys.c_str());
    fprintf(f, "\t\t<version>%s</version>\n", ts);
    fprintf(f, "\t\t<author>HaulNX</author>\n\t</header>\n");

    int n = 0;
    for (int i = 0; i < this->vfy_dat.count; i++) {
        const DatRom &rom = this->vfy_dat.roms[i];
        if (std::binary_search(present.begin(), present.end(),
                               std::string(rom.name)))
            continue;
        // Game name = rom name without its final extension (Logiqx convention).
        std::string gname = rom.name;
        size_t dot = gname.rfind('.');
        if (dot != std::string::npos && dot > 0) gname.resize(dot);
        std::string egame = xml_escape(gname);
        std::string erom = xml_escape(rom.name);
        fprintf(f, "\t<game name=\"%s\">\n\t\t<description>%s</description>\n",
                egame.c_str(), egame.c_str());
        fprintf(f, "\t\t<rom name=\"%s\" size=\"%llu\" crc=\"%08x\"",
                erom.c_str(), (unsigned long long)rom.size, (unsigned)rom.crc);
        if (rom.have_sha1) fprintf(f, " sha1=\"%s\"", rom.sha1);
        fprintf(f, "/>\n\t</game>\n");
        n++;
    }
    fprintf(f, "</datafile>\n");
    fclose(f);

    char done[600];
    snprintf(done, sizeof(done), tr(S_FIXDAT_DONE), n, path.c_str());
    this->Toast(done);
}

// Write a full text report for the finished pass to REPORTS_DIR, grouped by
// outcome, so it can be read on a PC. Includes the DAT-missing games (#3): every
// canonical DAT name that no scanned file verified to.
void MainApplication::VerifyExportReport() {
    fs_mkdir_p(REPORTS_DIR);
    std::string path =
        std::string(REPORTS_DIR) + "/" + this->vfy_target + "-verify.txt";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        this->ToastErr(tr(S_REPORT_FAIL));
        return;
    }

    char ts[32] = "";
    time_t t = time(NULL);
    struct tm tmv;
    struct tm *tm = localtime_r(&t, &tmv);
    if (tm) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);

    int n_misnamed = 0;
    for (int i = 0; i < this->vfy_job.count; i++)
        if (vfy_misnamed(this->vfy_job.items[i])) n_misnamed++;

    fprintf(f, "HaulNX verify report\n");
    fprintf(f, "Console : %s\n", this->vfy_label.c_str());
    fprintf(f, "Folder  : %s\n", this->vfy_folder.c_str());
    if (this->vfy_dat_ok)
        fprintf(f, "DAT     : %s (%d known dumps)\n", this->vfy_dat.system,
                this->vfy_dat.count);
    fprintf(f, "Date    : %s\n\n", ts);
    fprintf(f,
            "Summary : %d verified   %d bad   %d unknown   %d to-rename   "
            "%d read-error\n",
            this->vfy_job.n_ok, this->vfy_job.n_bad, this->vfy_job.n_unknown,
            n_misnamed, this->vfy_job.n_err);

    fprintf(f, "\n== BAD DUMPS (%d) ==\n", this->vfy_job.n_bad);
    for (int i = 0; i < this->vfy_job.count; i++)
        if (this->vfy_job.items[i].status == DAT_BAD)
            fprintf(f, "%s\n", this->vfy_job.items[i].name);

    fprintf(f, "\n== UNKNOWN (%d) ==\n", this->vfy_job.n_unknown);
    for (int i = 0; i < this->vfy_job.count; i++)
        if (this->vfy_job.items[i].status == DAT_UNKNOWN)
            fprintf(f, "%s\n", this->vfy_job.items[i].name);

    fprintf(f, "\n== TO RENAME (%d) ==\n", n_misnamed);
    for (int i = 0; i < this->vfy_job.count; i++) {
        const VerifyItem &it = this->vfy_job.items[i];
        if (vfy_misnamed(it)) fprintf(f, "%s  ->  %s\n", it.name, it.canonical);
    }

    // Missing: DAT entries no scanned file verified to. Both present and DAT
    // names are the canonical base name, so they compare directly.
    if (this->vfy_dat_ok) {
        std::vector<std::string> present;
        for (int i = 0; i < this->vfy_job.count; i++) {
            const VerifyItem &it = this->vfy_job.items[i];
            if (it.status == DAT_VERIFIED && it.canonical[0])
                present.emplace_back(it.canonical);
        }
        std::sort(present.begin(), present.end());
        std::vector<std::string> missing;
        for (int i = 0; i < this->vfy_dat.count; i++) {
            std::string nm = this->vfy_dat.roms[i].name;
            if (!std::binary_search(present.begin(), present.end(), nm))
                missing.push_back(std::move(nm));
        }
        std::sort(missing.begin(), missing.end());
        missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
        fprintf(f, "\n== MISSING FROM LIBRARY (%d) ==\n", (int)missing.size());
        for (const std::string &m : missing) fprintf(f, "%s\n", m.c_str());
    }

    fprintf(f, "\n== VERIFIED (%d) ==\n", this->vfy_job.n_ok - n_misnamed);
    for (int i = 0; i < this->vfy_job.count; i++) {
        const VerifyItem &it = this->vfy_job.items[i];
        if (it.status == DAT_VERIFIED && !vfy_misnamed(it))
            fprintf(f, "%s\n", it.name);
    }

    fclose(f);
    char done[600];
    snprintf(done, sizeof(done), tr(S_REPORT_DONE), path.c_str());
    this->Toast(done);
}

// Verify every console that has a DAT, one after another, on a single worker.
void MainApplication::VerifyAllStart() {
    // Gather the consoles worth checking: those with a DAT on disk. The folder
    // is resolved the same way a single verify does (custom folder or default).
    this->vfy_all.clear();
    for (int i = 0; i < g_cfg.console_count; i++) {
        ConsoleGroup *g = &g_cfg.consoles[i];
        std::string dat = std::string(DATS_DIR) + "/" + g->target + ".dat";
        if (!fs_exists(dat.c_str())) continue;
        const char *custom = install_folder_for(g->target);
        std::string dir = (custom && custom[0])
                              ? std::string(custom)
                              : std::string(roms_root(&g_tico)) + "/" + g->target;
        const char *full = console_full_name(g->target);
        VfyAllRow row;
        row.target = g->target;
        row.label = full ? full : g->console;
        row.folder = std::move(dir);
        this->vfy_all.push_back(std::move(row));
    }
    if (this->vfy_all.empty()) {
        this->ToastErr(tr(S_NO_DATS));
        return;
    }

    // Reset the shared scratch job/DAT the worker reuses per console, then run
    // the whole batch on one worker thread (VerifyAllThread loops the rows).
    verify_free(&this->vfy_job);
    dat_free(&this->vfy_dat);
    memset(&this->vfy_job, 0, sizeof(this->vfy_job));
    this->vfy_all_idx = 0;
    this->vfy_all_cancel = false;
    fs_mkdir_p(CACHE_DIR);

    if (!this->vfy.Start(&MainApplication::VerifyAllThread, this)) {
        this->ToastErr(tr(S_META_FAILED));
        return;
    }
    this->screen = Screen::VerifyAll;
    this->layout->SetTitle(tr(S_VERIFY_ALL));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_VERIFYING));
}

void MainApplication::VerifyAllThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    for (int k = 0; k < (int)self->vfy_all.size(); k++) {
        if (self->vfy_all_cancel) break;
        self->vfy_all_idx = k;
        VfyAllRow &row = self->vfy_all[k];

        verify_free(&self->vfy_job);
        dat_free(&self->vfy_dat);
        memset(&self->vfy_job, 0, sizeof(self->vfy_job));
        snprintf(self->vfy_job.folder, sizeof(self->vfy_job.folder), "%s",
                 row.folder.c_str());
        snprintf(self->vfy_job.cache_path, sizeof(self->vfy_job.cache_path), "%s",
                 HASH_CACHE_PATH);

        std::string dat = std::string(DATS_DIR) + "/" + row.target + ".dat";
        if (!dat_load(&self->vfy_dat, dat.c_str())) {
            row.completed = false; // DAT unreadable — leave the row's tallies at 0
            continue;
        }
        self->vfy_job.dat = &self->vfy_dat;
        verify_run(&self->vfy_job);
        vfy_persist_status(self->vfy_job);

        row.n_ok = self->vfy_job.n_ok;
        row.n_bad = self->vfy_job.n_bad;
        row.n_unknown = self->vfy_job.n_unknown;
        row.n_err = self->vfy_job.n_err;
        int mis = 0;
        for (int j = 0; j < self->vfy_job.count; j++)
            if (vfy_misnamed(self->vfy_job.items[j])) mis++;
        row.n_misnamed = mis;

        // Missing: DAT entries no scanned file verified to. Counts unique DAT
        // names not present; duplicate names in a DAT would over-count, but that
        // is rare and this is only a summary figure (the per-console report has
        // the exact list).
        std::vector<std::string> present;
        for (int j = 0; j < self->vfy_job.count; j++) {
            const VerifyItem &it = self->vfy_job.items[j];
            if (it.status == DAT_VERIFIED && it.canonical[0])
                present.emplace_back(it.canonical);
        }
        std::sort(present.begin(), present.end());
        present.erase(std::unique(present.begin(), present.end()), present.end());
        int missing = 0;
        for (int j = 0; j < self->vfy_dat.count; j++)
            if (!std::binary_search(present.begin(), present.end(),
                                    std::string(self->vfy_dat.roms[j].name)))
                missing++;
        row.n_missing = missing;
        row.completed = self->vfy_job.completed;

        if (self->vfy_job.cancel) break;
    }
    self->vfy.done = true;
}

void MainApplication::VerifyAllTick() {
    if (!this->vfy.done) {
        int k = this->vfy_all_idx, total = (int)this->vfy_all.size();
        int fd = this->vfy_job.files_done, ft = this->vfy_job.files_total;
        std::string lbl = (k >= 0 && k < total) ? this->vfy_all[k].label : "";
        char s[192];
        snprintf(s, sizeof(s), "%s  (%d/%d)   %d/%d   B %s", lbl.c_str(), k + 1,
                 total, fd, ft, tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    this->vfy.Join();
    this->layout->HideSpinner();
    // The summary lives in vfy_all; the last console's scratch can go now.
    verify_free(&this->vfy_job);
    dat_free(&this->vfy_dat);
    this->VerifyAllResults();
}

void MainApplication::VerifyAllResults() {
    char title[64];
    snprintf(title, sizeof(title), tr(S_VERIFY_ALL_DONE),
             (int)this->vfy_all.size());
    this->layout->SetTitle(title);
    // Library-wide audit roll-up: sum the problem tallies across every console so
    // the header answers "any bad/unknown/missing anywhere?" at a glance, without
    // reading each row. (Per-console detail is still one row down, A to drill in.)
    int t_bad = 0, t_unknown = 0, t_missing = 0;
    for (const VfyAllRow &row : this->vfy_all) {
        t_bad += row.n_bad;
        t_unknown += row.n_unknown;
        t_missing += row.n_missing;
    }
    char sub[160];
    snprintf(sub, sizeof(sub), tr(S_AUDIT_SUMMARY), t_bad, t_unknown, t_missing);
    this->layout->SetSubtitle(sub);
    this->layout->ClearMenu();

    const pu::ui::Color green = accent_green();
    const pu::ui::Color red(224, 78, 78, 255);
    const pu::ui::Color amber = is_light_theme()
                                    ? pu::ui::Color(180, 110, 30, 255)
                                    : pu::ui::Color(245, 175, 95, 255);
    const pu::ui::Color lbl = g_theme->row_text;
    for (const VfyAllRow &row : this->vfy_all) {
        char val[128];
        snprintf(val, sizeof(val), tr(S_VERIFY_ALL_ROW), row.n_ok, row.n_bad,
                 row.n_unknown, row.n_missing);
        pu::ui::Color sc =
            !row.completed        ? amber
            : row.n_bad > 0       ? red
            : (row.n_unknown || row.n_missing || row.n_misnamed) ? amber
                                                                 : green;
        this->layout->AddRow2(row.label, val, lbl, sc);
    }
    if (this->vfy_all.empty()) this->layout->AddRow(tr(S_EMPTY));
}

// ---- inbox sorter ----------------------------------------------------------
// Games arriving from a PC (USB, or the LAN receiver in auto-sort mode) land in
// INBOX_DIR undifferentiated. This identifies each one (idgame.h) and files the
// confident, known-console ones into <roms_root>/<target>; anything ambiguous
// or for a console not in the collection is left for the user to place by hand.

// True if the inbox holds at least one non-directory entry — used to decide
// whether disconnecting a USB/MTP session should drop into the sorter.
static bool inbox_has_files(void) {
    DIR *d = opendir(INBOX_DIR);
    if (!d) return false;
    bool any = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t nl = strlen(e->d_name);            // skip in-flight receiver temp
        if (nl >= 5 && strcasecmp(e->d_name + nl - 5, ".part") == 0) continue;
        std::string full = std::string(INBOX_DIR) + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) { any = true; break; }
    }
    closedir(d);
    return any;
}

// Move one staged file into the install folder for `target`. Moves from the
// file's current location (r.path is kept pointing there), so this also handles
// re-filing an already-sorted file when the user corrects an auto-detected
// console. Leaves the file where it is (returning false) on a name clash — a
// matching name in the target is almost certainly the same game, not something
// to clobber. Re-picking the same console is a no-op success.
bool MainApplication::SortFileRow(SortRow &r, const std::string &target) {
    ConsoleGroup *g = config_find_console(&g_cfg, target.c_str());
    if (!g) return false;
    const char *custom = install_folder_for(g->target);
    std::string dir = (custom && custom[0])
                          ? std::string(custom)
                          : std::string(roms_root(&g_tico)) + "/" + g->target;
    fs_mkdir_p(dir.c_str());
    std::string dest = dir + "/" + r.name;
    if (dest != r.path) {                            // actually moving folders
        if (fs_exists(dest.c_str())) return false;   // already in the library
        if (!fs_move(r.path.c_str(), dest.c_str())) return false;
    }
    // Every inbox item funnels through here to reach a console folder --
    // Wi-Fi/companion pushes and an MTP/USB drop into the inbox all land raw
    // and stay whole (direct-to-console-folder pushes already extract on
    // arrival, see queue.c install_item / mtp/responder.cpp) -- so this is the
    // one place that needs to unpack a staged archive. Same fallback as
    // everywhere else: libarchive first, then the native RAR3 decoder for the
    // "programmable filter" entries libarchive can't read; a total failure
    // just leaves the raw archive sitting in the console folder, same as today.
    if (is_archive_name(r.name.c_str())) {
        int ow = 0;
        int n = extract_archive(dest.c_str(), dir.c_str(), NULL, NULL, &ow);
        if (n <= 0) {
            size_t nl = r.name.size();
            if (nl > 4 && strcasecmp(r.name.c_str() + nl - 4, ".rar") == 0) {
                ow = 0;
                n = rar3_extract(dest.c_str(), dir.c_str(), NULL, NULL, &ow);
            }
        }
        if (n > 0) {
            remove(dest.c_str());
        }
    }
    const char *full = console_full_name(g->target);
    r.target = g->target;
    r.label = full ? full : g->console;
    r.path = dest;   // current location, so a later re-file moves from here
    r.dest = dest;
    r.filed = true;
    r.needs_pick = false;
    xfer_log("sort       %s -> %s", r.name.c_str(), dest.c_str());
    return true;
}

void MainApplication::SortInboxStart() {
    fs_mkdir_p(INBOX_DIR);
    this->sort_rows.clear();
    this->sort_pick_idx = -1;

    DIR *d = opendir(INBOX_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            std::string full = std::string(INBOX_DIR) + "/" + e->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
            size_t nl = strlen(e->d_name); // skip an in-flight receiver temp file
            if (nl >= 5 && strcasecmp(e->d_name + nl - 5, ".part") == 0) continue;
            SortRow r;
            r.name = e->d_name;
            r.path = std::move(full);
            this->sort_rows.push_back(std::move(r));
        }
        closedir(d);
    }
    if (this->sort_rows.empty()) {
        this->ToastErr(tr(S_INBOX_EMPTY));
        this->GotoInstalled(roms_root(&g_tico));
        return;
    }

    // Identify and auto-file every confident guess that maps to a console the
    // user actually has. A confident guess for a missing console, or anything
    // ambiguous/unknown, keeps its best-guess label but still needs a hand-pick.
    for (SortRow &r : this->sort_rows) {
        IdResult id = idgame_identify(r.path.c_str());
        if (idgame_is_confident(&id) && config_find_console(&g_cfg, id.target)) {
            if (this->SortFileRow(r, id.target)) continue;
        }
        if (id.target[0]) {
            ConsoleGroup *g = config_find_console(&g_cfg, id.target);
            const char *full = g ? console_full_name(g->target) : nullptr;
            r.target = id.target;
            r.label = full ? full : id.target;
        }
        r.needs_pick = true;
    }
    this->SortInboxResults();
}

void MainApplication::SortInboxResults() {
    int filed = 0, pend = 0;
    for (const SortRow &r : this->sort_rows) {
        if (r.filed) filed++; else pend++;
    }
    char title[96];
    snprintf(title, sizeof(title), tr(S_SORT_DONE), filed, pend);
    this->screen = Screen::SortInbox;
    this->layout->SetTitle(title);
    // A is always live now (even filed rows can be re-assigned), so always show
    // the choose-console hint.
    this->layout->SetSubtitle(tr(S_SUB_SORT_PICK));
    this->layout->ClearMenu();

    const pu::ui::Color green = accent_green();
    const pu::ui::Color amber = is_light_theme()
                                    ? pu::ui::Color(180, 110, 30, 255)
                                    : pu::ui::Color(245, 175, 95, 255);
    const pu::ui::Color lbl = g_theme->row_text;
    for (const SortRow &r : this->sort_rows) {
        std::string right;
        if (r.filed) {
            right = r.label;
        } else if (r.label.empty()) {
            right = tr(S_SORT_UNKNOWN);
        } else {
            right = std::string(tr(S_SORT_GUESS)) + " " + r.label;
        }
        this->layout->AddRow2(r.name, right, lbl, r.filed ? green : amber);
    }
}

void MainApplication::SortAssignPicked(const std::string &target) {
    int i = this->sort_pick_idx;
    this->sort_pick_idx = -1;
    if (i < 0 || i >= (int)this->sort_rows.size()) {
        this->SortInboxResults();
        return;
    }
    if (this->SortFileRow(this->sort_rows[i], target)) {
        this->Toast(tr(S_SORTED));
    } else {
        this->ToastErr(tr(S_SORT_FAIL));
    }
    this->SortInboxResults();
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
    this->layout->AddRow(tr(S_SPEEDTEST_LOG)); // 4: speedtest.log
    this->layout->AddRow(tr(S_EXPORT_BUNDLE)); // 5: fold every log into one file
    // The exported bundle lives under logs/; once written, offer to view it.
    if (fs_exists(DIAG_BUNDLE_PATH))
        this->layout->AddRow(tr(S_VIEW_BUNDLE));   // 6 (only when it exists)
    this->layout->AddRow(tr(S_CLEAR_ALL_LOGS));    // last: wipe every log file
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

    // For a scoped search (a whole console, or one repo within it) make sure
    // every in-scope repo's metadata is actually on the card before we walk the
    // cache. Otherwise searching a console straight from its repo list — before
    // you've opened any of its repos — finds nothing, because the on-disk cache
    // is only populated when a repo is browsed. Cache-first, so this is instant
    // for repos you've already opened and only hits the network for the gaps;
    // the "Searching…" spinner is already showing and B still cancels between
    // fetches. Global search (scope_ci < 0) would mean fetching every repo you
    // own, so it keeps scanning whatever is already cached.
    if (scope_ci >= 0) {
        for (const auto &rr : repos) {
            if (g_search_cancel) return;
            ArchiveItem tmp;
            if (ia_fetch(rr.id.c_str(), &tmp, true, CACHE_DIR)) {
                ia_free(&tmp);
            }
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
                            h.size = json_u64_size(body, tok,
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
    // Reset on every fresh search; the one call site that needs Missing Games
    // return-routing (VerifyMissing's A handler) sets this back to true right
    // after the call, so it never leaks into an unrelated search.
    this->search_from_missing = false;
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
    // "A download" only makes sense once there's a row to act on; an empty
    // result list drops it instead of hinting at a no-op.
    this->layout->SetSubtitle(
        tr(g_search_results.empty() ? S_SUB_SEARCH_EMPTY : S_SUB_SEARCH));
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
            accent_blue(),
            -1.0f, nullptr, "", false, false);
    }
}

// Colour of the Shown/Hidden state label on the Manage consoles rows. Shared
// by the initial build and the in-place toggle so the two never drift.
// Per-console visibility spans the two tabs it can appear on. It is stored as
// two independent bools (shown = Browse, shown_installed = Installed); a single
// A-press on the Manage screen cycles through the four combinations. Kept as one
// control so both tabs are managed in one place.
enum {
    VIS_BOTH = 0,      // Browse + Installed
    VIS_BROWSE = 1,    // Browse only
    VIS_INSTALLED = 2, // Installed only
    VIS_HIDDEN = 3     // neither
};

static int console_vis_state(const ConsoleGroup &g) {
    if (g.shown && g.shown_installed) return VIS_BOTH;
    if (g.shown) return VIS_BROWSE;
    if (g.shown_installed) return VIS_INSTALLED;
    return VIS_HIDDEN;
}

static void console_vis_apply(ConsoleGroup &g, int st) {
    g.shown = (st == VIS_BOTH || st == VIS_BROWSE);
    g.shown_installed = (st == VIS_BOTH || st == VIS_INSTALLED);
}

static const char *console_vis_label(int st) {
    switch (st) {
    case VIS_BOTH: return tr(S_VIS_BOTH);
    case VIS_BROWSE: return tr(S_VIS_BROWSE);
    case VIS_INSTALLED: return tr(S_VIS_INSTALLED);
    default: return tr(S_VIS_HIDDEN);
    }
}

static pu::ui::Color console_vis_color(int st) {
    bool light = is_light_theme();
    switch (st) {
    case VIS_BOTH: return accent_green();
    case VIS_BROWSE: return light ? pu::ui::Color(40, 120, 200, 255)
                                  : pu::ui::Color(150, 205, 255, 255);
    case VIS_INSTALLED: return light ? pu::ui::Color(180, 110, 30, 255)
                                     : pu::ui::Color(245, 175, 95, 255);
    default: return light ? pu::ui::Color(95, 95, 105, 255)
                          : pu::ui::Color(150, 150, 162, 255);
    }
}

void MainApplication::GotoManage() {
    this->screen = Screen::Manage;
    this->layout->SetTitle(tr(S_TITLE_MANAGE));
    this->layout->SetSubtitle(tr(S_SUB_MANAGE));
    this->layout->ClearMenu();
    for (int i = 0; i < g_cfg.console_count; i++) {
        int st = console_vis_state(g_cfg.consoles[i]);
        char clabel[160];
        console_label(g_cfg.consoles[i].console, clabel, sizeof(clabel));
        // Stock icons only here too, same reasoning as the queue card view:
        // this is a uniform show/hide toggle list, not a library browse
        // screen, so it stays on the built-in badge set regardless of any
        // console's custom box art.
        this->layout->AddRow2(
            clabel, console_vis_label(st),
            g_theme->row_text, console_vis_color(st),
            -1.0f, console_display_icon(g_cfg.consoles[i].console, nullptr, false));
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

// The console whose custom install folder is exactly `path` (its own top
// level), or nullptr. Used to recognise a custom folder as a console root.
static ConsoleGroup *console_by_custom_folder(const std::string &path) {
    if (!g_prefs.custom_folders) return nullptr; // master switch off: ignore folders
    for (int i = 0; i < g_cfg.console_count; i++)
        if (g_cfg.consoles[i].folder[0] && path == g_cfg.consoles[i].folder)
            return &g_cfg.consoles[i];
    return nullptr;
}

// The console owning `path`: its custom install folder or anything beneath it.
// Lets the Installed header name the console even inside a custom subtree.
static ConsoleGroup *console_owning_path(const std::string &path) {
    if (!g_prefs.custom_folders) return nullptr; // master switch off: ignore folders
    for (int i = 0; i < g_cfg.console_count; i++) {
        const char *f = g_cfg.consoles[i].folder;
        if (!f[0]) continue;
        std::string fs = f;
        if (path == fs || path.rfind(fs + "/", 0) == 0)
            return &g_cfg.consoles[i];
    }
    return nullptr;
}

// Absolute path an Installed-tab entry points at: its explicit `path` override
// (a custom-folder console), else the usual <listed folder>/<name>.
static std::string inst_entry_path(const std::string &base, const DirEnt &e) {
    return e.path.empty() ? base + "/" + e.name : e.path;
}

// True when `path` is a top-level console folder — either directly under the
// ROM root (roms/<console>) or a console's exact custom folder. Such a folder
// holds roms (so delete/rename apply) but files there can't "move up", and B
// steps back to the console list rather than the filesystem parent.
static bool inst_is_console_root(const std::string &path) {
    std::string root = roms_root(&g_tico);
    if (path == root) return false;
    auto pp = path.find_last_of('/');
    std::string parent = (pp == std::string::npos) ? path : path.substr(0, pp);
    if (parent == root) return true;
    return console_by_custom_folder(path) != nullptr;
}

// Open the Installed browser's sort picker and apply the choice, keeping the
// cursor where it was. Shared by the root console list's options menu (X) and
// the in-folder ◀ shortcut so both offer the same sort regardless of view.
void MainApplication::InstSortDialog() {
    int s = this->SideMenu(
        tr(g_sort_keys[g_inst_sort]),
        {tr(S_SORT_DEFAULT), tr(S_SORT_NAME_AZ), tr(S_SORT_NAME_ZA),
         tr(S_SORT_SIZE_DESC), tr(S_SORT_SIZE_ASC), tr(S_CANCEL)},
        g_inst_sort);
    if (s >= 0 && s < SORT__COUNT) {
        g_inst_sort = s;
        s32 keep = this->layout->Sel();
        this->GotoInstalled(this->inst_path);
        if (keep >= 0 && keep < this->layout->RowCount())
            this->layout->SetSel(keep);
    }
}

// Rename the file under the cursor. A rename stays a rename: reject separators
// or ".." (which would move it elsewhere) and never silently clobber a different
// file (a case-only change is fine — FAT is case-insensitive).
void MainApplication::InstRenameSel() {
    s32 i = this->layout->Sel();
    if (i < 0 || i >= (s32)g_inst.size()) return;
    // A grouped row has no single file to rename, and its .cue names its .bin
    // tracks internally — renaming them would break the game. The menu doesn't
    // offer it for a group; this guards against that drifting apart.
    if (!g_inst[i].group_members.empty()) return;
    char nm[256] = {0};
    if (!prompt(tr(S_RENAME_PROMPT), g_inst[i].name.c_str(), nm, sizeof(nm)))
        return;
    if (strchr(nm, '/') || strchr(nm, '\\') || strstr(nm, "..")) {
        this->ToastErr(tr(S_RENAME_FAILED));
        return;
    }
    std::string from = this->inst_path + "/" + g_inst[i].name;
    std::string to = this->inst_path + "/" + nm;
    if (fs_exists(to.c_str()) && strcasecmp(nm, g_inst[i].name.c_str()) != 0) {
        this->ToastErr(tr(S_RENAME_FAILED));
        return;
    }
    if (rename(from.c_str(), to.c_str()) == 0) {
        this->Toast(tr(S_RENAMED));
    } else {
        this->ToastErr(tr(S_RENAME_FAILED));
    }
    this->GotoInstalled(this->inst_path);
}

// Delete the marked set (Y), or the single file under the cursor when nothing is
// marked. A confirm guards it either way. Shared by the ▶ button (list view
// only — the card grid needs Right for navigation) and the X options menu
// (both views).
void MainApplication::InstDeleteSel() {
    s32 i = this->layout->Sel();
    int mc = this->layout->MarkedCount();
    // A grouped row stands for a whole game, so it deletes as a whole game:
    // the counts and the confirm text below all speak in real files.
    auto files_of = [](const DirEnt &e) {
        return e.group_members.empty() ? std::vector<std::string>{e.name}
                                       : e.group_members;
    };
    if (mc > 0) {
        auto marks = this->layout->Marked();
        int n = 0;
        for (s32 idx : marks)
            if (idx >= 0 && idx < (s32)g_inst.size())
                n += (int)files_of(g_inst[idx]).size();
        char msg[64];
        snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), n);
        if (!this->ConfirmDanger(tr(S_DELETE), msg, true)) return;
        // Delete in reverse order so indices stay valid.
        for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
            s32 idx = *it;
            if (idx < 0 || idx >= (s32)g_inst.size()) continue;
            for (const std::string &f : files_of(g_inst[idx]))
                fs_rm_rf((this->inst_path + "/" + f).c_str());
        }
        char t[32];
        snprintf(t, sizeof(t), tr(S_DELETED_N), n);
        this->Toast(t);
        this->GotoInstalled(this->inst_path);
    } else if (i >= 0 && i < (s32)g_inst.size()) {
        std::vector<std::string> files = files_of(g_inst[i]);
        std::string msg;
        char hdr[300];
        if (g_inst[i].group_members.empty()) {
            snprintf(hdr, sizeof(hdr), tr(S_DELETE_ONE), g_inst[i].name.c_str());
            msg = hdr;
        } else {
            // Never delete an unnamed batch: name the game, then list its
            // pieces the way the move dialog lists a selection.
            snprintf(hdr, sizeof(hdr), tr(S_DELETE_GROUP_ONE),
                     g_inst[i].name.c_str(), (int)files.size());
            msg = hdr;
            int shown = 0;
            for (const std::string &f : files) {
                if (shown < 12)       msg += "\n• " + f;
                else if (shown == 12) msg += "\n…";
                shown++;
            }
        }
        if (!this->ConfirmDanger(tr(S_DELETE), msg, true)) return;
        for (const std::string &f : files)
            fs_rm_rf((this->inst_path + "/" + f).c_str());
        char t[32];
        snprintf(t, sizeof(t), tr(S_DELETED_N), (int)files.size());
        this->Toast(t);
        this->GotoInstalled(this->inst_path);
    }
}

// Move the marked set (Y), or the single file under the cursor, into another
// console's install folder. The ONLY destinations are console folders — the
// whole point of the manager is that ROMs stay inside our install-folder concept,
// so this never offers an arbitrary path.
void MainApplication::InstMoveDialog() {
    s32 i = this->layout->Sel();
    if (i < 0 || i >= (s32)g_inst.size()) return;
    // A grouped row moves as a whole game — expand it into its real files, so
    // MvStartTo (which takes a flat filename list) needs no notion of groups.
    std::vector<std::string> names;
    auto add = [&](s32 idx) {
        if (idx < 0 || idx >= (s32)g_inst.size()) return;
        const DirEnt &e = g_inst[idx];
        if (e.group_members.empty()) names.push_back(e.name);
        else names.insert(names.end(), e.group_members.begin(),
                          e.group_members.end());
    };
    auto marks = this->layout->Marked();
    if (!marks.empty()) {
        for (s32 idx : marks) add(idx);
    } else {
        add(i);
    }
    if (names.empty()) return;

    // Build the picker: every configured console, minus the folder these files
    // already live in. `dests` runs parallel to the option labels; the list is
    // sorted A-Z by display name so destinations are easy to scan.
    std::vector<std::pair<std::string, std::string>> picks; // (label, dir)
    for (int c = 0; c < g_cfg.console_count; c++) {
        ConsoleGroup *g = &g_cfg.consoles[c];
        const char *custom = install_folder_for(g->target);
        std::string dir = (custom && custom[0])
                              ? std::string(custom)
                              : std::string(roms_root(&g_tico)) + "/" + g->target;
        if (dir == this->inst_path) continue; // already here
        const char *full = console_full_name(g->target);
        picks.emplace_back(full ? full : g->target, dir);
    }
    std::sort(picks.begin(), picks.end(),
              [](const std::pair<std::string, std::string> &a,
                 const std::pair<std::string, std::string> &b) {
                  return strcasecmp(a.first.c_str(), b.first.c_str()) < 0;
              });
    std::vector<std::string> opts, dests;
    for (auto &p : picks) { opts.push_back(p.first); dests.push_back(p.second); }
    if (opts.empty()) {
        this->ToastErr(tr(S_NO_MOVE_TARGET));
        return;
    }
    opts.push_back(tr(S_CANCEL));
    char title[64];
    snprintf(title, sizeof(title), tr(S_MOVE_PICK), (int)names.size());
    int r = this->SideMenu(title, opts, 0, "", false, false,
                           console_icon("default"));
    if (r < 0 || r >= (int)dests.size()) return; // cancelled
    this->MvStartTo(names, dests[r]);
}

// X inside a console folder: gather the file actions in one panel. Move and
// Delete act on the marked set when there is one; Rename is single-file only, so
// it drops off the menu during a multi-select.
void MainApplication::InstFileMenu() {
    s32 i = this->layout->Sel();
    if (i < 0 || i >= (s32)g_inst.size()) return;
    bool multi = this->layout->MarkedCount() > 0;
    // Renaming a grouped row is meaningless (many files) and unsafe (a .cue
    // names its tracks), so it drops off for a group the same way it does
    // during a multi-select.
    bool group = !g_inst[i].group_members.empty();
    std::vector<std::string> opts;
    int i_move = (int)opts.size();   opts.push_back(tr(S_MOVE_TO_CONSOLE));
    int i_rename = -1;
    if (!multi && !group) { i_rename = (int)opts.size(); opts.push_back(tr(S_RENAME)); }
    int i_sort = (int)opts.size();   opts.push_back(tr(S_SORT_MENU));
    int i_delete = (int)opts.size(); opts.push_back(tr(S_DELETE));
    opts.push_back(tr(S_CANCEL));
    // Hero backdrop from this game's cover. Unlike the list/grid builds,
    // this is a one-off menu open (X press on a single row), not a per-frame
    // rebuild, so a synchronous decode here is cheap enough to not stall --
    // boxart_row_icon's "only if already warm" split (meant to avoid
    // decoding 40+ rows at once) was the wrong tool here and left the panel
    // showing no art almost every time, since a row is rarely already
    // decoded unless it happened to scroll through BoxArtIconsPoll first.
    pu::sdl2::Texture backdrop = boxart_icon_for(g_inst[i].name);
    int r = this->SideMenu(tr(S_OPTIONS), opts, 0, "", false, false,
                           console_icon("default"), nullptr, 0, backdrop);
    if (r == i_move)        this->InstMoveDialog();
    else if (r == i_rename) this->InstRenameSel();
    else if (r == i_sort)   this->InstSortDialog();
    else if (r == i_delete) this->InstDeleteSel();
}

// Global "Tools" panel (left slide). These operate on the whole library / SD, so
// they're reachable the same way from both the Library and Browse tabs; none of
// them care which console is under the cursor.
bool MainApplication::ToolsMenu() {
    // The inventory-server row toggles in place (SideMenuLive): the panel stays
    // out, its ON/OFF badge flips, and the live address shows along the bottom.
    SideMenuLive live;
    live.row = 8;
    live.state = g_prefs.inv_server;
    live.on_toggle = [this]() -> bool {
        g_prefs.inv_server = !g_prefs.inv_server;
        prefs_save(&g_prefs);
        if (g_prefs.inv_server) { this->InvServerStart(); }
        else                    { this->InvServerStop(); }
        return g_prefs.inv_server; // InvServerStart clears it on bind failure
    };
    live.footer = [this]() -> std::string {
        if (!g_prefs.inv_server) return "";
        char ip[64];
        if (!httpsrv_local_ip(ip, sizeof(ip))) return "";
        // Second line reflects whether a companion is actually polling us: the
        // inventory server stamps every inventory.json GET, and the companion
        // polls every few seconds, so a hit within ~15s means it's connected.
        // (Refreshed ~1s by SideMenu so this stays current while the panel is out.)
        unsigned long long last = this->inv_srv.last_inv_ns;
        const char *state = "Waiting for a companion to connect";
        if (last != 0 &&
            armTicksToNs(armGetSystemTick()) - last <= 15000000000ULL) {
            state = "Companion connected";
        }
        char buf[192];
        snprintf(buf, sizeof(buf), "%s:%d/%s\n%s", ip, HTTPSRV_INV_PORT,
                 g_prefs.inv_code, state);
        return std::string(buf);
    };
    // The modal suspends the main loop, so pump the inventory server here or a
    // companion couldn't connect (or be shown as connected) while this is open.
    live.tick = [this]() { this->InvServerPoll(); };
    // The inventory toggle must stay at the row index `live.row` points to, so
    // the update-manager entry goes before it and live.row is set to match.
    // Emulator/app update management itself lives under Settings -> Updates now;
    // this row is just a shortcut into that screen.
    // PC Sync and Scan Art were two rows apiece (USB / Wi-Fi, game art /
    // console art) — collapsed into one row each that opens a small chooser
    // dialog, so the panel doesn't keep growing as more transfer/scan
    // methods land. live.row tracks the inventory toggle's new index.
    live.row = 9;
    int r = this->SideMenu(tr(S_TOOLS),
                           {tr(S_SORT_INBOX), tr(S_PC_SYNC),
                            tr(S_TIDY_LIBRARY), tr(S_VERIFY_ALL), tr(S_SORT_MENU),
                            tr(S_STORAGE_OVERVIEW), tr(S_ONEGR_SCAN),
                            tr(S_SCAN_ART),
                            tr(S_APPMAN_MENU),
                            tr(S_INV_SERVER)},
                           0, "", false, /*from_left=*/true,
                           console_icon("default"), &live, // generic joystick icon
                           HidNpadButton_X);               // X → per-console Options
    switch (r) {
    case 0: this->SortInboxStart(); return false; // View inbox
    case 1: { // PC Sync: USB (Connect to PC) or Wi-Fi (auto-sort receive) —
             // Wi-Fi's sorter is console-agnostic (it picks the console from
             // each file), so it belongs here rather than on a single
             // console's Options.
        int cr = this->CreateShowDialog(tr(S_PC_SYNC), tr(S_PC_SYNC_BODY),
                                        {tr(S_USB_MENU), tr(S_RECV_SORT),
                                         tr(S_CANCEL)},
                                        true, {}, style_dialog);
        if (cr == 0) this->GotoUsbMtp();
        else if (cr == 1) this->RomRecvStart(0, false, true);
        return false;
    }
    case 2: this->TidyStart(); return false;      // Tidy library
    case 3: this->VerifyAllStart(); return false; // Verify all consoles
    case 4: this->InstSortDialog(); return false; // Sort the console list
    case 5: this->StorageOverview(); return false; // Library storage summary
    case 6: this->OneGRStart(); return false;     // Remove duplicate copies (1G1R)
    case 7: { // Scan Art: game box art or console icons, each with its own
             // fill/rescan/reset flow below (needs an API key either way)
        int cr = this->CreateShowDialog(tr(S_SCAN_ART), tr(S_SCAN_ART_BODY),
                                        {tr(S_SCAN_BOX_ART),
                                         tr(S_SCAN_CONSOLE_ART), tr(S_CANCEL)},
                                        true, {}, style_dialog);
        if (cr == 0) this->ToolsScanGameArt();
        else if (cr == 1) this->ToolsScanConsoleArt();
        return false;
    }
    case 8: this->GotoUpdates(); return false;    // Emulator & app updates (Settings)
    // row 9 (inventory toggle) is handled in-place by SideMenu; never returns here
    case SIDEMENU_SWITCH: return true;            // flip to per-console Options
    default: return false;                        // dismissed (B)
    }
}

// Fetch box art for every game in the library (needs an API key). Split out
// of ToolsMenu's Scan Art chooser so each scan kind is a self-contained call.
void MainApplication::ToolsScanGameArt() {
    if (g_creds.steamgriddb_key[0]) {
        // Same three-way choice as ToolsScanConsoleArt: "Fill Missing" is
        // the safe, fast default; "Rescan All" force-requeries everything,
        // including a bad automatic pick an earlier scan couldn't be
        // revisited otherwise (see boxart_game_force); "Reset All to
        // Default" wipes every cached game cover (disk + runtime texture)
        // and starts over from scratch.
        int cr = this->CreateShowDialog(
            tr(S_SCAN_BOX_ART), tr(S_SCAN_BOX_ART_BODY),
            {tr(S_SCAN_BOX_ART_FILL), tr(S_SCAN_BOX_ART_FORCE),
             tr(S_SCAN_BOX_ART_RESET), tr(S_CANCEL)},
            true, {}, style_dialog);
        if (cr == 0) {
            this->BoxArtScanStart("", "", false, false);
        } else if (cr == 1) {
            this->BoxArtScanStart("", "", false, true);
        } else if (cr == 2) {
            if (this->ConfirmDanger(tr(S_SCAN_BOX_ART_RESET),
                                    tr(S_SCAN_BOX_ART_RESET_CONFIRM), true)) {
                boxart_clear_games();
                boxart_cache_forget_games();
                this->Toast(tr(S_SCAN_BOX_ART_RESET_DONE));
            }
        }
    } else {
        this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
    }
}

// Bulk console-icon scan: fills in every console's SteamGridDB art in one
// pass, instead of visiting each one's own Console Art > Find Box Art
// individually (needs an API key). Split out of ToolsMenu's Scan Art chooser.
void MainApplication::ToolsScanConsoleArt() {
    if (g_creds.steamgriddb_key[0]) {
        // "Fill Missing" is the safe, fast default (skips consoles already
        // resolved); "Rescan All" force-requeries everything, including a
        // bad automatic pick from an earlier scan that fill-missing alone
        // could never revisit -- see boxart_console_force. "Reset All to
        // Default" is the way out of a scan that made things worse: wipe
        // every console's cached art (disk + runtime texture) and drop back
        // to the built-in icons, rather than searching again.
        int cr = this->CreateShowDialog(
            tr(S_SCAN_CONSOLE_ART), tr(S_SCAN_CONSOLE_ART_BODY),
            {tr(S_SCAN_CONSOLE_ART_FILL), tr(S_SCAN_CONSOLE_ART_FORCE),
             tr(S_SCAN_CONSOLE_ART_RESET), tr(S_CANCEL)},
            true, {}, style_dialog);
        if (cr == 0) {
            this->BoxArtScanStart("", "", true, false);
        } else if (cr == 1) {
            this->BoxArtScanStart("", "", true, true);
        } else if (cr == 2) {
            if (this->ConfirmDanger(tr(S_SCAN_CONSOLE_ART_RESET),
                                    tr(S_SCAN_CONSOLE_ART_RESET_CONFIRM),
                                    true)) {
                for (int c = 0; c < g_cfg.console_count; c++) {
                    g_cfg.consoles[c].use_boxart = false;
                }
                config_save(&g_cfg);
                boxart_clear_consoles();
                boxart_cache_forget_consoles();
                this->Toast(tr(S_SCAN_CONSOLE_ART_RESET_DONE));
            }
        }
    } else {
        this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
    }
}

// Per-console Options panel (X on the root console list): slides in from the
// right with the actions tied to THIS console — receive from a PC, verify it,
// pin it, tidy just its folder, and where it installs. Library-wide actions
// live on the Tools panel (Y). Available in list and card views alike (the card
// grid needs the D-pad, so these can't hang off ◀/▶). Returns true if the user
// pressed Y to flip to the Tools panel instead.
bool MainApplication::ConsoleOptionsMenu(s32 i) {
    if (i < 0 || i >= (s32)g_inst.size() || !g_inst[i].is_dir) return false;
    ConsoleGroup *g = config_find_console(&g_cfg, g_inst[i].name.c_str());
    const char *full = g ? console_full_name(g->target) : nullptr;
    std::string title = full ? full : g_inst[i].name;
    bool pinned = prefs_dir_pinned(&g_prefs, g_inst[i].name.c_str());
    int r = this->SideMenu(
        title,
        {tr(S_INSTALL_FOLDER), tr(S_CONSOLE_INFO), tr(S_RECEIVE_FROM_PC),
         tr(S_TIDY_CONSOLE), pinned ? tr(S_UNPIN) : tr(S_PIN),
         tr(S_VERIFY_DAT), tr(S_HAVE_MISSING), tr(S_SCAN_BOX_ART_CONSOLE),
         tr(S_CONSOLE_ART)},
        0, "", false, /*from_left=*/false,
        console_display_icon(g_inst[i].name.c_str()), nullptr,
        HidNpadButton_Y); // Y → global Tools panel
    if (r == 0) { // Install folder (where this console lands)
        this->InstFolderDialog(i);
    } else if (r == 1) { // Console info (read-only stats)
        this->ConsoleInfoDialog(i);
    } else if (r == 2) { // Receive from PC (into this console)
        if (g) {
            this->RomRecvStart((int)(g - g_cfg.consoles));
        }
    } else if (r == 3) { // Tidy just this console's folder
        this->TidyStart(g ? g->target : g_inst[i].name,
                        inst_entry_path(this->inst_path, g_inst[i]));
    } else if (r == 4) { // Pin / Unpin
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
    } else if (r == 5) { // Verify Files (against the console's DAT)
        // Verify the folder the console actually installs into: a custom
        // per-console folder (when on) or the default <root>/<target>.
        // g_inst[i].path already resolves this — custom-folder rows are
        // synthesised pointing at it.
        this->VerifyStart(inst_entry_path(this->inst_path, g_inst[i]),
                          g ? g->target : g_inst[i].name, title);
    } else if (r == 6) { // Missing Games (verify, then open the missing list)
        this->VerifyStart(inst_entry_path(this->inst_path, g_inst[i]),
                          g ? g->target : g_inst[i].name, title, false,
                          /*goto_missing=*/true);
    } else if (r == 7) { // Scan for Box Art, limited to this console
        if (g_creds.steamgriddb_key[0]) {
            this->BoxArtScanStart(g ? g->target : g_inst[i].name,
                                  inst_entry_path(this->inst_path, g_inst[i]));
        } else {
            this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
        }
    } else if (r == 8) { // Console Art: default icon vs. SteamGridDB cover
        this->ConsoleArtMenu(i);
    } else if (r == SIDEMENU_SWITCH) {
        return true; // flip to the global Tools panel
    }
    return false;
}

// Read-only stats for one console (its Options "Console info" row): where it
// installs, how much is there, DAT status, active repos, and tab visibility.
void MainApplication::ConsoleInfoDialog(s32 i) {
    if (i < 0 || i >= (s32)g_inst.size() || !g_inst[i].is_dir) return;
    ConsoleGroup *g = config_find_console(&g_cfg, g_inst[i].name.c_str());
    const char *full = g ? console_full_name(g->target) : nullptr;
    std::string title = full ? full : g_inst[i].name;

    std::string folder = inst_entry_path(this->inst_path, g_inst[i]);
    int count = 0;
    uint64_t bytes = 0;
    inst_dir_stats(folder, &count, &bytes);

    std::string dat = "None";
    if (g) {
        std::string datp = std::string(DATS_DIR) + "/" + g->target + ".dat";
        struct stat ds;
        if (stat(datp.c_str(), &ds) == 0) {
            dat = "Present  ·  " + human_size((uint64_t)ds.st_size);
        } else {
            // Matches VerifyStart's own check: Verify offers to fetch one on the
            // spot only when a downloadable set actually covers this console.
            dat = dat_source_for(g->target)
                      ? "None — Verify offers to download it"
                      : "None — receive one from a PC (Options)";
        }
    }

    int repo_active = 0, repo_total = g ? g->repo_count : 0;
    if (g)
        for (int r = 0; r < g->repo_count; r++)
            if (g->repos[r].enabled) repo_active++;

    const char *vis = "Hidden";
    if (g && g->shown && g->shown_installed) vis = "Browse and Installed tabs";
    else if (g && g->shown)                  vis = "Browse tab only";
    else if (g && g->shown_installed)        vis = "Installed tab only";

    char buf[900];
    snprintf(buf, sizeof(buf),
             "Install folder\n%s\n\nInstalled\n%d games  ·  %s\n\n"
             "DAT\n%s\n\nRepos\n%d of %d active\n\nVisible in\n%s",
             folder.c_str(), count, human_size(bytes).c_str(), dat.c_str(),
             repo_active, repo_total, vis);
    this->CreateShowDialog(title, buf, {tr(S_OK)}, true, {}, style_dialog);
}

// Library-wide storage summary (Tools "Storage overview"): SD usage, total
// installed games/size, DAT coverage, and inbox backlog. Read-only.
void MainApplication::StorageOverview() {
    uint64_t freeb = fs_free_bytes("sdmc:/");
    uint64_t totalb = fs_total_bytes("sdmc:/");
    std::string free_s = (freeb == UINT64_MAX) ? "?" : human_size(freeb);
    std::string total_s = (totalb == UINT64_MAX) ? "?" : human_size(totalb);
    std::string used_s =
        (freeb == UINT64_MAX || totalb == UINT64_MAX || totalb < freeb)
            ? std::string("?")
            : human_size(totalb - freeb);

    int games = 0, consoles_with = 0, dat_count = 0;
    uint64_t lib_bytes = 0;
    for (int i = 0; i < g_cfg.console_count; i++) {
        ConsoleGroup &c = g_cfg.consoles[i];
        const char *custom = install_folder_for(c.target);
        std::string dir = (custom && custom[0])
                              ? std::string(custom)
                              : std::string(roms_root(&g_tico)) + "/" + c.target;
        int count = 0;
        uint64_t bytes = 0;
        inst_dir_stats(dir, &count, &bytes);
        if (count > 0) {
            games += count;
            consoles_with++;
            lib_bytes += bytes;
        }
        std::string datp = std::string(DATS_DIR) + "/" + c.target + ".dat";
        if (fs_exists(datp.c_str())) dat_count++;
    }
    int inbox = 0;
    for (const auto &e : list_dir(INBOX_DIR))
        if (!e.is_dir) inbox++;

    char buf[700];
    snprintf(buf, sizeof(buf),
             "SD card\n%s free  ·  %s of %s used\n\n"
             "Library\n%d games in %d consoles  ·  %s\n\n"
             "DATs\n%d of %d consoles have a DAT\n\n"
             "Inbox\n%d waiting to sort",
             free_s.c_str(), used_s.c_str(), total_s.c_str(), games,
             consoles_with, human_size(lib_bytes).c_str(), dat_count,
             g_cfg.console_count, inbox);
    this->CreateShowDialog(tr(S_STORAGE_OVERVIEW), buf, {tr(S_OK)}, true, {},
                           style_dialog);
}

// Install-folder info / change dialog for the console at g_inst[i]. Extracted
// from the old Y handler so it can hang off the per-console Options menu (Y is
// now the Tools panel). No-op unless the row is a console directory.
void MainApplication::InstFolderDialog(s32 i) {
    if (i < 0 || i >= (s32)g_inst.size() || !g_inst[i].is_dir) return;
    ConsoleGroup *g = config_find_console(&g_cfg, g_inst[i].name.c_str());
    if (!g) return;
    const char *full = console_full_name(g->target);
    bool cf = g_prefs.custom_folders;
    bool hascustom = cf && g->folder[0];
    std::string path = hascustom
                           ? std::string(g->folder)
                           : std::string(roms_root(&g_tico)) + "/" + g->target;
    char line[700];
    snprintf(line, sizeof(line), tr(S_ROM_FOLDER_INSTALLS_TO), path.c_str());
    std::string body = std::string(full ? full : g->target) + "\n\n" + line +
                       "\n" +
                       (hascustom ? tr(S_ROM_FOLDER_CUSTOM_TAG)
                                  : tr(S_ROM_FOLDER_DEFAULT_TAG));
    if (!cf) {
        body += "\n\n";
        body += tr(S_ROM_FOLDER_LOCKED_NOTE);
    }
    // With per-console folders off there is nothing to change — the dialog is
    // purely informational (where games land). Otherwise it sets, changes, or
    // resets the custom folder. Either way it offers "Open settings" (→ Storage,
    // where the per-console folders toggle lives), then a trailing Cancel.
    std::vector<std::string> btns;
    int change_idx = -1, reset_idx = -1, set_idx = -1;
    if (cf && hascustom) {
        change_idx = (int)btns.size(); btns.push_back(tr(S_CHANGE_FOLDER));
        reset_idx = (int)btns.size();  btns.push_back(tr(S_RESET_DEFAULT));
    } else if (cf) {
        set_idx = (int)btns.size();    btns.push_back(tr(S_SET_FOLDER));
    }
    int settings_idx = (int)btns.size(); btns.push_back(tr(S_OPEN_SETTINGS));
    btns.push_back(tr(S_CANCEL)); // last: B / cancel returns this, a no-op
    int r = this->CreateShowDialog(tr(S_INSTALL_FOLDER), body, btns, true, {},
                                   style_dialog);
    bool pick = (change_idx >= 0 && r == change_idx) ||
                (set_idx >= 0 && r == set_idx);   // Set/Change → open the picker
    bool reset = reset_idx >= 0 && r == reset_idx; // Reset to default
    if (r == settings_idx) {
        this->GotoStorage(); // ROM / per-console folders live under Storage
    } else if (pick) {
        this->picker_console = (int)(g - g_cfg.consoles);
        this->picker_from_installed = true;
        std::string start = "sdmc:/";
        if (g->folder[0] && fs_exists(g->folder)) {
            start = g->folder;
        }
        this->GotoRomPicker(start);
    } else if (reset) {
        g->folder[0] = '\0';
        config_save(&g_cfg);
        this->Toast(tr(S_INSTALL_FOLDER_CLEARED));
        std::string nm = g->target;
        this->GotoInstalled(this->inst_path);
        for (s32 k = 0; k < (s32)g_inst.size(); k++) {
            if (g_inst[k].name == nm) {
                this->layout->SetSel(k);
                break;
            }
        }
    }
}

// "Console Art" dialog for the console at g_inst[i] (Options menu). Default
// icon vs. SteamGridDB box art -- the same search+picker flow a game's cover
// uses, just keyed under "console:<target>" (see boxart.h) instead of a game
// title, and landing back on ConsoleGroup::use_boxart instead of a row's
// resolved-cover state. Gated on a SteamGridDB key exactly like every other
// box-art entry point.
void MainApplication::ConsoleArtMenu(s32 i) {
    if (i < 0 || i >= (s32)g_inst.size() || !g_inst[i].is_dir) return;
    ConsoleGroup *g = config_find_console(&g_cfg, g_inst[i].name.c_str());
    if (!g) return;
    if (!g_creds.steamgriddb_key[0]) {
        this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
        return;
    }
    const char *full = console_full_name(g->target);
    std::string title = full ? full : g->console;
    std::string key = std::string("console:") + g->target;
    bool has_art = boxart_lookup(key.c_str(), nullptr, 0);

    // Buttons are built in display order, tracking which slot (if any) each
    // dynamic option landed in -- same pattern as InstFolderDialog just above.
    std::vector<std::string> btns;
    int default_idx = -1, boxart_idx = -1;
    if (g->use_boxart) {
        default_idx = (int)btns.size();
        btns.push_back(tr(S_CONSOLE_ART_USE_DEFAULT));
    } else if (has_art) {
        boxart_idx = (int)btns.size();
        btns.push_back(tr(S_CONSOLE_ART_USE_BOXART));
    }
    int find_idx = (int)btns.size();
    btns.push_back(tr(has_art ? S_CONSOLE_ART_CHANGE : S_CONSOLE_ART_FIND));
    btns.push_back(tr(S_CANCEL)); // last: B / cancel returns this, a no-op

    int r = this->CreateShowDialog(title, tr(S_CONSOLE_ART_BODY), btns, true,
                                   {}, style_dialog);
    if (default_idx >= 0 && r == default_idx) {
        g->use_boxart = false;
        config_save(&g_cfg);
        std::string nm = g_inst[i].name;
        this->GotoInstalled(this->inst_path);
        for (s32 k = 0; k < (s32)g_inst.size(); k++) {
            if (g_inst[k].name == nm) {
                this->layout->SetSel(k);
                break;
            }
        }
    } else if (boxart_idx >= 0 && r == boxart_idx) {
        g->use_boxart = true;
        config_save(&g_cfg);
        std::string nm = g_inst[i].name;
        this->GotoInstalled(this->inst_path);
        for (s32 k = 0; k < (s32)g_inst.size(); k++) {
            if (g_inst[k].name == nm) {
                this->layout->SetSel(k);
                break;
            }
        }
    } else if (r == find_idx) {
        char initial[256];
        snprintf(initial, sizeof(initial), "%s", title.c_str());
        char q[256] = {0};
        if (!prompt(tr(S_BOXART_SEARCH_GUIDE), initial, q, sizeof(q))) return;
        this->BoxArtPickStart(key, q, Screen::Installed, i, g->target);
    }
}

// ---- multi-file game grouping (Installed browser) -------------------------
//
// A disc dump is usually several files that are collectively one game: a .cue
// beside 25 .bin tracks, or an .m3u naming one .cue per disc. Emulators load
// the set fine, but listing every piece buries the library, so the Installed
// browser collapses each set into a single row. Nothing on disk is touched —
// this only decides what a row stands for.

// InstGroup itself lives up near inst_group_row_count now, so
// WriteInventoryJson (defined earlier in the file) can also emit set
// membership for the desktop companion. See there for the struct.

// Case-insensitive "ends with", for a lowercase extension including the dot.
static bool inst_has_ext(const std::string &n, const char *ext) {
    size_t el = strlen(ext);
    return n.size() > el && strcasecmp(n.c_str() + n.size() - el, ext) == 0;
}

// A name minus its final extension: "Game (USA).cue" -> "Game (USA)".
static std::string inst_stem(const std::string &n) {
    size_t d = n.find_last_of('.');
    return d == std::string::npos ? n : n.substr(0, d);
}

// True when `name` reads as a piece of the set titled `stem`: the stem, then
// either the extension straight away ("X.cue" / "X.bin") or a bracketed tag
// ("X (Track 01).bin"). That anchor is the whole point — without it the stem
// "Game" would swallow the unrelated "Game 2 (USA).bin" sitting next to it.
static bool inst_stem_member(const std::string &name, const std::string &stem) {
    if (name.size() <= stem.size()) return false;
    if (strncasecmp(name.c_str(), stem.c_str(), stem.size()) != 0) return false;
    const char *rest = name.c_str() + stem.size();
    return rest[0] == '.' || (rest[0] == ' ' && rest[1] == '(');
}

// The filenames an index file names. Only read for the uncommon set whose
// pieces aren't named after their index file, so an ordinary folder of
// cue/bin games costs no file reads at all. Bounded on purpose: these are
// small text files, and something huge or binary isn't one of ours.
static std::vector<std::string> inst_index_refs(const std::string &full,
                                                const char *ext) {
    std::vector<std::string> out;
    std::ifstream f(full.c_str());
    if (!f) return out;
    bool m3u = strcmp(ext, ".m3u") == 0, cue = strcmp(ext, ".cue") == 0;
    std::string line;
    for (int n = 0; n < 4096 && std::getline(f, line); n++) {
        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        std::string s = line.substr(b, e - b + 1);
        std::string ref;
        if (m3u) {
            if (s[0] == '#') continue; // comment / #EXTINF
            ref = s;
        } else if (cue) {
            // FILE "Game (Track 01).bin" BINARY
            if (strncasecmp(s.c_str(), "FILE", 4) != 0) continue;
            size_t q = s.find('"');
            if (q == std::string::npos) continue;
            size_t q2 = s.find('"', q + 1);
            if (q2 == std::string::npos) continue;
            ref = s.substr(q + 1, q2 - q - 1);
        } else {
            // .gdi: "track lba type sectorsize name offset". Writers differ on
            // quoting, so take the quoted name when there is one and otherwise
            // the fifth field. Best effort by design — a misread just leaves
            // the set ungrouped, never mis-grouped, because every reference
            // still has to resolve to a real sibling below.
            size_t q = s.find('"');
            if (q != std::string::npos) {
                size_t q2 = s.find('"', q + 1);
                if (q2 == std::string::npos) continue;
                ref = s.substr(q + 1, q2 - q - 1);
            } else {
                size_t p = 0;
                for (int t = 0; t < 4 && p != std::string::npos; t++) {
                    p = s.find_first_of(" \t", p);
                    if (p != std::string::npos) p = s.find_first_not_of(" \t", p);
                }
                if (p == std::string::npos) continue;
                size_t pe = s.find_first_of(" \t", p);
                ref = (pe == std::string::npos) ? s.substr(p) : s.substr(p, pe - p);
            }
        }
        // Install folders are flat. A reference reaching into a subdirectory
        // is either not ours or trying to leave the folder — drop it.
        if (ref.empty() || ref.find('/') != std::string::npos ||
            ref.find('\\') != std::string::npos)
            continue;
        out.push_back(ref);
    }
    return out;
}

// Find the sets in one folder's listing. Every filename in a returned group is
// spoken for and must drop out of the flat rows in favour of that group's row.
static std::vector<InstGroup> inst_detect_groups(const std::string &dir,
                                                 const std::vector<DirEnt> &ents) {
    std::vector<InstGroup> groups;
    // FAT is case-insensitive, so claims are too — and a claim is what keeps
    // one file out of two groups and lets the passes run cheapest-first.
    struct CaseLess {
        bool operator()(const std::string &a, const std::string &b) const {
            return strcasecmp(a.c_str(), b.c_str()) < 0;
        }
    };
    std::set<std::string, CaseLess> claimed;

    // Index files, widest container first: an .m3u names the discs and each
    // disc's .cue names its tracks, so claiming m3u sets first makes a whole
    // multi-disc game one row rather than one row per disc.
    static const char *const kinds[] = {".m3u", ".cue", ".gdi"};
    for (const char *ext : kinds) {
        for (const DirEnt &idx : ents) {
            if (idx.is_dir || !inst_has_ext(idx.name, ext)) continue;
            if (claimed.count(idx.name)) continue;
            std::string stem = inst_stem(idx.name);
            if (stem.empty()) continue;
            InstGroup g;
            g.name = stem;
            g.size = idx.size;
            g.members.push_back(idx.name);
            // Pass 1 — siblings named after the index file. No reads.
            for (const DirEnt &e : ents) {
                if (e.is_dir || claimed.count(e.name)) continue;
                if (e.name == idx.name || !inst_stem_member(e.name, stem)) continue;
                g.members.push_back(e.name);
                g.size += e.size;
            }
            // Pass 2 — the set isn't named after its index file, so ask the
            // index file itself who belongs to it.
            if (g.members.size() < 2) {
                for (const std::string &ref :
                     inst_index_refs(dir + "/" + idx.name, ext)) {
                    for (const DirEnt &e : ents) {
                        if (e.is_dir || claimed.count(e.name)) continue;
                        if (strcasecmp(e.name.c_str(), ref.c_str()) != 0) continue;
                        if (std::find(g.members.begin(), g.members.end(),
                                      e.name) != g.members.end())
                            break; // a cue may name one file for many tracks
                        g.members.push_back(e.name);
                        g.size += e.size;
                        break;
                    }
                }
            }
            if (g.members.size() < 2) continue; // an index file on its own
            for (const std::string &m : g.members) claimed.insert(m);
            groups.push_back(g);
        }
    }

    // Pass 3 — sets with no index file at all, recognised by the part tag in
    // their names. Keyed on the title in front of the tag, so every piece of
    // one dump lands in one group. A lone "(Disc 1)" with no sibling has
    // nothing to collapse and stays an ordinary row.
    std::map<std::string, InstGroup> by_title;
    for (const DirEnt &e : ents) {
        if (e.is_dir || claimed.count(e.name)) continue;
        size_t t = rom_multipart_tag(e.name);
        if (t == std::string::npos || t == 0) continue;
        std::string title = e.name.substr(0, t);
        while (!title.empty() && title.back() == ' ') title.pop_back();
        if (title.empty()) continue;
        InstGroup &g = by_title[title];
        g.name = title;
        g.members.push_back(e.name);
        g.size += e.size;
    }
    for (auto &kv : by_title)
        if (kv.second.members.size() >= 2) groups.push_back(kv.second);

    return groups;
}

// Rows the browser shows inside `path`: subfolders, loose files, and one row
// per multi-file game. Groups partition the files they claim, so subtracting
// the claimed pieces and adding one per group is exact. This is the number
// the console chips want — games, not files.
static int inst_group_row_count(const std::string &path) {
    std::vector<DirEnt> ents = list_dir(path);
    auto groups = inst_detect_groups(path, ents);
    size_t claimed = 0;
    for (const auto &g : groups) claimed += g.members.size();
    return (int)(ents.size() - claimed + groups.size());
}

void MainApplication::GotoInstalled(const std::string &path) {
    this->screen = Screen::Installed;
    this->inst_path = path;
    // Stale row indices from whatever folder was open before — BoxArtIconsPoll
    // must not resolve against the list being rebuilt below.
    this->boxart_pending.clear();
    inst_stat_load(); // warm the folder-size cache from disk (once per session)
    g_inst = list_dir(path);
    bool is_root = (path == roms_root(&g_tico));
    // Load the persisted verify-status cache once for this folder (never at
    // the root, which lists consoles, not files) rather than per row — a
    // console folder can hold hundreds of entries, and this is a single TSV
    // read regardless of how many of them get looked up.
    VfyStatusCache vfy_cache;
    bool have_vfy_cache = false;
    if (!is_root) {
        vfystatus_load(&vfy_cache, VFYSTATUS_PATH);
        have_vfy_cache = true;
    }
    // At the roms root, honor per-console Installed visibility: a console set to
    // hide from the Installed tab drops off here even if its folder holds files.
    // Folders with no matching console entry are always kept. Removing them from
    // g_inst (not just skipping in the row loop) keeps row indices aligned with
    // the vector the input handler opens from.
    if (is_root) {
        g_inst.erase(std::remove_if(g_inst.begin(), g_inst.end(),
                                    [](const DirEnt &e) {
                                        if (!e.is_dir) return false;
                                        ConsoleGroup *g = config_find_console(
                                            &g_cfg, e.name.c_str());
                                        if (!g) return false;
                                        // Hidden from Installed, or (when custom
                                        // folders are on) redirected to a custom
                                        // folder — re-added below as a synthetic
                                        // row pointing at that folder.
                                        return !g->shown_installed ||
                                               (g_prefs.custom_folders &&
                                                g->folder[0]);
                                    }),
                     g_inst.end());
        // Consoles with a custom install folder live outside the ROM root, so
        // list_dir() never sees them. Surface each as a row keyed by the console
        // name (for its icon/label/pin) but pointing at the real custom path.
        for (int i = 0; g_prefs.custom_folders && i < g_cfg.console_count; i++) {
            ConsoleGroup &c = g_cfg.consoles[i];
            if (!c.folder[0] || !c.shown_installed) continue;
            DirEnt de;
            de.name = c.target;
            de.is_dir = true;
            de.size = 0;
            de.path = c.folder;
            g_inst.push_back(de);
        }
    }
    // Collapse each multi-file game (cue/bin set, multi-disc set) into one row
    // standing for all its pieces. Only inside a console folder — the root is
    // the console list, which has no game files to group. Done before the sort
    // below so a group row orders exactly like any other file row.
    if (!is_root && g_prefs.group_sets) {
        auto groups = inst_detect_groups(path, g_inst);
        if (!groups.empty()) {
            std::set<std::string> taken;
            for (auto &g : groups)
                for (auto &m : g.members) taken.insert(m);
            std::vector<DirEnt> rebuilt;
            rebuilt.reserve(g_inst.size());
            for (auto &e : g_inst)
                if (e.is_dir || !taken.count(e.name)) rebuilt.push_back(e);
            for (auto &g : groups) {
                DirEnt row;
                row.name = g.name;
                row.is_dir = false;
                row.size = g.size;
                row.group_members = g.members;
                rebuilt.push_back(row);
            }
            g_inst.swap(rebuilt);
        }
    }
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
        } else if (ConsoleGroup *g = console_owning_path(path)) {
            // A console's custom install folder (or a subfolder of one): it lives
            // outside the ROM root, so name it from the owning console instead.
            cons = g->target;
        }
    }
    if (cons.empty()) {
        this->layout->SetTitle(tr(S_TITLE_INSTALLED));
    } else {
        const char *cfull = console_full_name(cons.c_str());
        this->layout->SetTitle(std::string(cfull ? cfull : cons.c_str()) +
                               " > " + tr(S_TITLE_INSTALLED));
        this->layout->SetTitleIcon(console_display_icon(cons.c_str()));
    }
    // Card view: the roms root keeps its existing folder cards; inside a
    // console folder it switches to the narrower "poster" style instead of
    // falling back to the plain file table (see CardGrid's SetPoster). Y
    // still marks (blue border, same selection set as list view) — but ▶ is
    // D-pad navigation in the card grid, not the delete shortcut, so the
    // folder hint swaps that line for X > Options while cards are showing.
    bool cards = g_prefs.card_view && !g_inst.empty();
    this->layout->SetSubtitle(
        cards && is_root ? tr(S_SUB_INSTALLED_CARDS)
        : is_root         ? tr(S_SUB_INSTALLED)
        : cards           ? tr(S_SUB_INSTALLED_FOLDER_CARDS)
                          : tr(S_SUB_INSTALLED_FOLDER));
    this->layout->ClearMenu();
    if (cards) {
        // Root folder cards are plain console-logo icons (never real art),
        // same as Home/Repos/Settings -- 6-wide/2-row poster geometry with
        // poster's "no cover" fallback path, not the old narrower 4-wide
        // plain layout.
        this->layout->SetCardCols(6);
        this->layout->SetCardPoster(true);
        // Set now (rather than after the loop, the older pattern) so
        // RowCount() below already reads the grid - poster cards need their
        // future index for the same lazy box-art queue the list rows use.
        this->layout->SetCardsMode(true);
    }
    // A file's persisted verify result, if any (fresh by size+mtime), tints
    // its size text the same green/red VerifyResults uses — verified and bad
    // are the two states worth a glance in the dense list; unknown-to-the-DAT
    // (homebrew, or simply never verified) falls back to the usual
    // magnitude-based color rather than implying a problem that isn't one. A
    // group row shows red if any member is bad, else green only if every
    // member is a confirmed-fresh verified hit.
    const pu::ui::Color vfy_green = accent_green();
    const pu::ui::Color vfy_red(224, 78, 78, 255);
    auto badge_lookup = [&](const std::string &full, uint64_t size,
                            uint64_t mtime, DatMatch *out) {
        return have_vfy_cache &&
               vfystatus_get(&vfy_cache, full.c_str(), size, mtime, out);
    };
    auto row_color = [&](const DirEnt &e) -> pu::ui::Color {
        if (!have_vfy_cache) return size_color(e.size);
        if (!e.group_members.empty()) {
            bool any_bad = false, all_verified = true;
            for (const auto &m : e.group_members) {
                struct stat st;
                std::string full = path + "/" + m;
                DatMatch st_match;
                if (stat(full.c_str(), &st) == 0 &&
                    badge_lookup(full, (uint64_t)st.st_size,
                                (uint64_t)st.st_mtime, &st_match)) {
                    if (st_match == DAT_BAD) any_bad = true;
                    if (st_match != DAT_VERIFIED) all_verified = false;
                } else {
                    all_verified = false;
                }
            }
            if (any_bad) return vfy_red;
            if (all_verified) return vfy_green;
            return size_color(e.size);
        }
        DatMatch st_match;
        if (badge_lookup(inst_entry_path(path, e), e.size, e.mtime, &st_match)) {
            if (st_match == DAT_BAD) return vfy_red;
            if (st_match == DAT_VERIFIED) return vfy_green;
        }
        return size_color(e.size);
    };
    for (int i = 0; i < (int)g_inst.size(); i++) {
        DirEnt &e = g_inst[i];
        if (e.is_dir) {
            int n = 0;
            uint64_t bytes = 0;
            inst_dir_stats(inst_entry_path(path, e), &n, &bytes);
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
            bool ic_is_art = false;
            pu::sdl2::Texture ic = (path == roms_root(&g_tico))
                                       ? console_display_icon(e.name.c_str(),
                                                              &ic_is_art)
                                       : nullptr;
            if (cards) {
                // Card: full name title (wrappable) + size/app count beneath.
                this->layout->AddCard(full ? full : e.name.c_str(), card_sub,
                                      ic, pinned, false, ic_is_art);
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
            continue;
        }
        if (cards && is_root) {
            // Stray file at the roms root: still a card so indices match.
            this->layout->AddCard(e.name, human_size(e.size),
                                  console_icon(e.name.c_str()));
            continue;
        }
        // A whole game: either a multi-file set (one row/card standing for
        // the group, piece count carrying the size) or a single file (plain
        // size, tinted by magnitude or by a persisted verify result). Box
        // art is looked up the same way whichever view is showing - a cover
        // already decoded shows immediately, one not yet decoded falls back
        // to the console icon here and is queued for BoxArtIconsPoll rather
        // than decoded right in the middle of the list build.
        std::string sub;
        if (!e.group_members.empty()) {
            char sbuf[64];
            snprintf(sbuf, sizeof(sbuf), tr(S_GROUP_SUBTITLE),
                     (int)e.group_members.size(), human_size(e.size).c_str());
            sub = sbuf;
        } else {
            sub = human_size(e.size);
        }
        std::string t = boxart_query_title(e.name);
        pu::sdl2::Texture ic2 = nullptr;
        bool has_cover = boxart_row_icon(t, &ic2);
        if (cards) {
            // Poster card: the real cover if it's already decoded, else the
            // console icon centred as a placeholder (CardGrid's Card::art
            // picks stretch-fill vs centred-natural-size between the two).
            s32 idx = this->layout->RowCount();
            this->layout->AddCard(e.name, sub,
                                  ic2 ? ic2 : console_display_icon(cons.c_str()),
                                  false, false, ic2 != nullptr);
            if (has_cover && !ic2) this->boxart_pending.push_back({idx, t});
        } else {
            s32 row_idx = this->layout->RowCount();
            this->layout->AddRow2(e.name, sub, g_theme->row_text, row_color(e),
                                  -1.0f, ic2);
            if (has_cover && !ic2) this->boxart_pending.push_back({row_idx, t});
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
    // Restore the cursor on the root console list so leaving the tab and coming
    // back keeps your place (matches the Browse tab's home_sel).
    if (is_root && !g_inst.empty()) {
        this->layout->SetSel(this->inst_sel < (s32)g_inst.size() ? this->inst_sel
                                                                 : 0);
    }
    inst_stat_save(); // persist any folder sizes (re)computed this visit
    if (have_vfy_cache) vfystatus_free(&vfy_cache);
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
    // Stale row indices from whatever search result was showing before —
    // BoxArtIconsPoll must not resolve against the list being rebuilt below.
    this->boxart_pending.clear();
    std::sort(g_inst_hits.begin(), g_inst_hits.end(),
              [](const InstHit &a, const InstHit &b) {
                  return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
              });
    bool capped = (int)g_inst_hits.size() >= INST_SEARCH_MAX;
    for (const auto &h : g_inst_hits) {
        std::string label =
            h.console.empty() ? h.name : "[" + h.console + "] " + h.name;
        // Same cheap cache-check / defer-to-BoxArtIconsPoll split as
        // GotoInstalled's row build — a search hit is still a library game,
        // just reached a different way.
        std::string t = boxart_query_title(h.name);
        pu::sdl2::Texture ic = nullptr;
        bool has_cover = boxart_row_icon(t, &ic);
        s32 row_idx = this->layout->RowCount();
        this->layout->AddRow2(label, human_size(h.size), g_theme->row_text,
                              size_color(h.size), -1.0f,
                              ic ? ic : console_icon(h.console.c_str()));
        if (has_cover && !ic) this->boxart_pending.push_back({row_idx, t});
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

// ---- archive.org catalogue search -> add a source --------------------------
// Discover items on archive.org and add a chosen one as a repo under a console,
// so a user who found a good set doesn't have to hand-copy its item id.

#define ARCH_SEARCH_MAX 50

void MainApplication::ArchSearchThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->arch_hits.assign(ARCH_SEARCH_MAX, ArchiveSearchItem{});
    int n = ia_search(nullptr, self->arch_query.c_str(), self->arch_hits.data(),
                      ARCH_SEARCH_MAX);
    if (n < 0) {
        self->arch_hits.clear();
    } else {
        self->arch_hits.resize(n);
    }
    self->arch_count = n;
    self->arch.done = true;
}

void MainApplication::GotoArchSearch(const std::string &query,
                                     const std::string &console) {
    if (this->arch.running) this->arch.Join();
    this->screen = Screen::ArchiveSearch;
    this->arch_query = query;
    this->arch_console = console;
    this->arch_hits.clear();
    this->arch_count = 0;
    this->layout->SetTitle(tr(S_TITLE_IA_SEARCH));
    this->layout->SetSubtitle(query); // the query, while the spinner runs
    this->layout->ClearMenu();
    this->arch_discard = false;
    this->layout->ShowSpinner(tr(S_SEARCHING));
    if (this->arch.Start(&MainApplication::ArchSearchThread, this)) return;
    // Couldn't spawn a thread: search inline so the feature still works.
    this->layout->HideSpinner();
    ArchSearchThread(this);
    this->FinishArchSearch();
}

void MainApplication::ArchSearchTick() {
    if (!this->arch.done) return; // the spinner overlay animates itself
    this->layout->HideSpinner();
    this->arch.Join();
    this->FinishArchSearch();
}

// Build the results list on the UI thread once the search lands (Plutonium calls
// must not run off-thread). The item id is shown under each title — it's what
// gets stored as the repo id, so seeing it helps spot the right dump.
void MainApplication::FinishArchSearch() {
    this->layout->SetSubtitle(tr(S_SUB_IA_SEARCH));
    this->layout->ClearMenu();
    if (this->arch_count < 0) {
        this->layout->SetEmptyState(console_icon("default"),
                                    tr(S_IA_SEARCH_FAIL));
        return;
    }
    const pu::ui::Color lbl = g_theme->row_text;
    const pu::ui::Color grey = g_theme->rom_info_clr;
    for (const ArchiveSearchItem &h : this->arch_hits) {
        std::string label = h.title[0] ? h.title : h.identifier;
        this->layout->AddRow2(label, h.identifier, lbl, grey);
    }
    if (this->arch_hits.empty()) {
        this->layout->SetEmptyState(console_icon("default"), tr(S_IA_NO_RESULTS));
    } else {
        char info[64];
        snprintf(info, sizeof(info), tr(S_IA_RESULTS),
                 (int)this->arch_hits.size());
        this->layout->SetRomInfo(info);
    }
}

// A on a result: confirm, then add it as a repo under arch_console. Stays on the
// results list afterwards so several items can be added from one search.
void MainApplication::ArchAddSel() {
    s32 i = this->layout->Sel();
    if (i < 0 || i >= (s32)this->arch_hits.size()) return;
    const ArchiveSearchItem &h = this->arch_hits[i];
    std::string title = h.title[0] ? h.title : h.identifier;

    char body[700];
    snprintf(body, sizeof(body), tr(S_IA_ADD_BODY), this->arch_console.c_str(),
             title.c_str());
    if (this->CreateShowDialog(tr(S_IA_ADD_TITLE), wrap_for_dialog(body),
                               {tr(S_IA_ADD_TITLE), tr(S_CANCEL)}, false, {},
                               style_dialog) != 0) {
        return;
    }
    ConsoleGroup *g = config_add_console(&g_cfg, this->arch_console.c_str());
    char label[64];
    snprintf(label, sizeof(label), "%s", title.c_str()); // repo label caps at 64
    if (g && config_add_repo(g, label, h.identifier)) {
        config_sort(&g_cfg);
        config_save(&g_cfg);
        this->Toast(tr(S_ADDED));
    } else {
        this->ToastErr(tr(S_SAVE_FAILED));
    }
}

// After the user picks a console to add to (AddRepo picker), ask how: search
// archive.org for the item, or type an item id in by hand (the original flow).
void MainApplication::AddRepoChoose(const std::string &console) {
    int r = this->CreateShowDialog(
        tr(S_ADD_SOURCE_HOW), "",
        {tr(S_SRC_SEARCH_IA), tr(S_SRC_ENTER_ID), tr(S_CANCEL)}, false, {},
        style_dialog);
    if (r == 0) {
        char q[256] = {0};
        if (prompt(tr(S_IA_QUERY_GUIDE), nullptr, q, sizeof(q))) {
            this->GotoArchSearch(q, console);
        } else {
            this->GotoHome();
        }
    } else if (r == 1) {
        char nm[64] = {0}, id[256] = {0};
        if (prompt(tr(S_HINT_NAME), nullptr, nm, sizeof(nm)) &&
            prompt(tr(S_HINT_ARCHIVE_ID), nullptr, id, sizeof(id))) {
            ConsoleGroup *g = config_add_console(&g_cfg, console.c_str());
            if (g && config_add_repo(g, nm, id)) {
                config_sort(&g_cfg);
                config_save(&g_cfg);
                this->Toast(tr(S_ADDED));
            }
        }
        this->GotoHome();
    } else {
        this->GotoHome();
    }
}

// ---- tidy library: find misfiled files + exact duplicates ------------------
// A non-destructive scan of the whole ROM library. It reports two kinds of
// issue and fixes them one at a time, only ever on an explicit confirmation:
//   - misfiled: a file that idgame confidently pins to a *different* configured
//     console than the folder it sits in -> offer to move it there.
//   - duplicate: two files with identical content (same size + SHA-1) -> offer
//     to delete the extra copy, keeping the first by path.

namespace {
struct TidyFile {
    std::string path, name, console;
    uint64_t size, mtime;
};

// Recursively collect every regular file under `dir`, tagging each with the
// top-level console folder it belongs to.
void tidy_collect(const std::string &dir, const std::string &console,
                  std::vector<TidyFile> &out, volatile bool *cancel) {
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && !*cancel) {
        if (e->d_name[0] == '.') continue;
        std::string full = dir + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            tidy_collect(full, console, out, cancel);
        } else if (S_ISREG(st.st_mode)) {
            out.push_back({full, e->d_name, console, (uint64_t)st.st_size,
                           (uint64_t)st.st_mtime});
        }
    }
    closedir(d);
}

// Collect every file under the ROM root, tagged with its top-level console
// folder. Shared by the tidy and 1G1R scans, which both walk the whole library.
void tidy_gather(std::vector<TidyFile> &files, volatile bool *cancel) {
    const std::string root = roms_root(&g_tico);
    DIR *rd = opendir(root.c_str());
    if (!rd) return;
    struct dirent *e;
    while ((e = readdir(rd)) != NULL && !*cancel) {
        if (e->d_name[0] == '.') continue;
        std::string full = root + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            tidy_collect(full, e->d_name, files, cancel);
    }
    closedir(rd);
}

// --- 1G1R clone grouping ----------------------------------------------------
// Group files the user actually has by their No-Intro base title and keep the
// single most-preferred regional/revision copy, flagging the rest. Purely
// name-based (No-Intro naming: "Title (Region) (Rev X).ext"), so it needs no
// parent/clone DAT metadata and reflects the library on disk.

// The base title: everything before the first " (" region/version tag, lowered
// so case never splits a group. With no tag, the name minus its extension.
std::string onegr_base_title(const std::string &name) {
    size_t paren = name.find(" (");
    std::string t = (paren != std::string::npos)
                        ? name.substr(0, paren)
                        : name.substr(0, name.find_last_of('.'));
    for (char &c : t) c = (char)tolower((unsigned char)c);
    return t;
}

// True for one piece of a multi-part dump: "Title (USA) (Track 01).bin",
// "Title (USA) (Disc 2).bin", etc. These all collapse to the same base title
// and region score, so without this check 1G1R would see a 25-track redump
// or a multi-disc game as 25 "duplicate copies" and offer to delete all but
// one. A part is never itself a duplicate, so it's excluded from 1G1R
// entirely rather than guessing which whole set to prefer.
bool onegr_is_multipart(const std::string &n) {
    return rom_multipart_tag(n) != std::string::npos;
}

// Keep-preference score: region weight dominates, then revision; a beta/proto/
// demo/sample tag is heavily penalised so a final release always wins. The
// four named regions rank by their position in g_prefs.region_order (earlier
// = more preferred); anything else -- an untagged file, or a region not in
// that set -- always ranks below all four, same as the old fixed ranking.
int onegr_score(const std::string &n) {
    static const struct { char code; const char *tag; } kRegions[] = {
        {'W', "(World"}, {'U', "(USA"}, {'E', "(Europe"}, {'J', "(Japan"},
    };
    char found = 0;
    for (const auto &r : kRegions) {
        if (n.find(r.tag) != std::string::npos) {
            found = r.code;
            break;
        }
    }
    int region = 1;
    if (found) {
        const char *order =
            g_prefs.region_order[0] ? g_prefs.region_order : "WUEJ";
        const char *pos = strchr(order, found);
        int rank = pos ? (int)(pos - order) : 3; // not in the saved order: least preferred of the four
        region = 5 - rank; // position 0 (most preferred) -> 5, ... position 3 -> 2
    }
    int rev = 0;
    size_t p = n.find("(Rev ");
    if (p != std::string::npos) {
        char c = n[p + 5];
        if (isdigit((unsigned char)c))
            rev = atoi(n.c_str() + p + 5);
        else if (isalpha((unsigned char)c))
            rev = 1 + (toupper((unsigned char)c) - 'A');
    }
    bool prerelease = n.find("(Beta") != std::string::npos ||
                      n.find("(Proto") != std::string::npos ||
                      n.find("(Demo") != std::string::npos ||
                      n.find("(Sample") != std::string::npos ||
                      n.find("(Pirate") != std::string::npos;
    return region * 10 + rev - (prerelease ? 1000 : 0);
}
} // namespace

void MainApplication::TidyThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->tidy_issues.clear();
    self->tidy_pruned_cache = 0;

    // Walk each console folder directly under the ROM root — or, for a
    // per-console tidy, just that one console's install folder.
    std::vector<TidyFile> files;
    if (self->tidy_only.empty())
        tidy_gather(files, &self->tidy_cancel);
    else
        tidy_collect(self->tidy_only_path, self->tidy_only, files,
                     &self->tidy_cancel);
    self->tidy_files_total = (int)files.size();
    self->tidy_files_done = 0;

    // Phase 1 — misfiled. Identify each file by its content and flag any that
    // belong to a different console that the collection actually has a folder
    // for (moving into a console the user doesn't use would be unhelpful).
    // Parse the DATs once up front; the deep pass reuses them across every file.
    // The persistent hash cache is loaded once and shared by both phases: the
    // deep-identify hash (phase 1) and the dedup hash (phase 2) of the same file
    // are identical (both hash_rom under path+size+mtime), so a file is hashed at
    // most once per scan and not at all when nothing changed since last time.
    HashCache cache;
    hashcache_load(&cache, HASH_CACHE_PATH);
    DatCache *dats = idgame_dat_cache_new(DATS_DIR);
    // Path -> "sits in its correctly-identified console folder", recorded for
    // every file phase 1 could confidently identify (whether misfiled or
    // not). Phase 2's duplicate grouping reads this so it never recommends
    // keeping a misfiled copy over a correctly-filed one just because the
    // misfiled copy's full path happens to sort first alphabetically -- see
    // the sort comparator below.
    std::map<std::string, bool> filed_ok;
    for (size_t i = 0; i < files.size() && !self->tidy_cancel; i++) {
        const TidyFile &f = files[i];
        IdResult id = idgame_identify(f.path.c_str());
        // Tier 3: the cheap tiers place cartridges and most disc headers, but a
        // raw .bin / .chd / generic ISO leaves them unsure. Here on the worker
        // thread we can afford to hash such a file and look it up across the
        // installed DATs — the way to catch a misfiled disc image. Only for the
        // ones idgame_identify couldn't already pin down.
        if (!idgame_is_confident(&id)) {
            IdResult deep = idgame_deep_identify(dats, f.path.c_str(), &cache,
                                                 &self->tidy_cancel);
            if (idgame_is_confident(&deep)) id = deep;
        }
        if (idgame_is_confident(&id)) {
            filed_ok[f.path] = (f.console == id.target);
        }
        if (idgame_is_confident(&id) && f.console != id.target &&
            config_find_console(&g_cfg, id.target) != nullptr) {
            self->tidy_issues.push_back(
                {f.path, f.name, f.console, id.target, 1});
        }
        self->tidy_files_done = (int)i + 1;
    }
    idgame_dat_cache_free(dats);

    // Phase 2 — duplicates. Only files of equal size can be identical, so sort
    // by size and hash within each size-run (reusing the cache loaded above).
    std::vector<int> order(files.size());
    for (size_t i = 0; i < files.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return files[a].size < files[b].size; });

    struct HF {
        std::string sha1, path, name, console;
    };
    size_t i = 0;
    while (i < order.size() && !self->tidy_cancel) {
        size_t j = i + 1;
        while (j < order.size() && files[order[j]].size == files[order[i]].size)
            j++;
        if (j - i >= 2 && files[order[i]].size > 0) {
            std::vector<HF> hv;
            for (size_t k = i; k < j && !self->tidy_cancel; k++) {
                const TidyFile &f = files[order[k]];
                HashSet hs;
                if (!hashcache_get(&cache, f.path.c_str(), f.size, f.mtime,
                                   &hs)) {
                    // hash_rom (not hash_file) so a zip's cached digest is its
                    // inner ROM's — the same thing the verify pass stores under
                    // this path, keeping the shared cache coherent.
                    if (!hash_rom(f.path.c_str(), &hs, &self->tidy_cancel, NULL,
                                  NULL))
                        continue;
                    hashcache_put(&cache, f.path.c_str(), f.size, f.mtime, &hs);
                }
                hv.push_back({hs.sha1_hex, f.path, f.name, f.console});
            }
            // Group equal SHA-1s; keep whichever copy phase 1 confirmed is in
            // its correct console folder over one it flagged as misfiled (or
            // couldn't identify), falling back to alphabetical path only when
            // that signal doesn't distinguish them -- see filed_ok above.
            std::sort(hv.begin(), hv.end(), [&](const HF &a, const HF &b) {
                if (a.sha1 != b.sha1) return a.sha1 < b.sha1;
                auto ita = filed_ok.find(a.path), itb = filed_ok.find(b.path);
                bool a_ok = ita != filed_ok.end() && ita->second;
                bool b_ok = itb != filed_ok.end() && itb->second;
                if (a_ok != b_ok) return a_ok; // correctly-filed copy kept first
                return a.path < b.path;
            });
            size_t g = 0;
            while (g < hv.size()) {
                size_t h = g + 1;
                while (h < hv.size() && hv[h].sha1 == hv[g].sha1) h++;
                for (size_t d = g + 1; d < h; d++)
                    self->tidy_issues.push_back({hv[d].path, hv[d].name,
                                                 hv[d].console, hv[g].path, 0});
                g = h;
            }
        }
        i = j;
    }

    // Phase 3 — orphans. A zero-byte file can never be a working game (it's
    // definitionally broken), and a stray ".part" sitting inside a console
    // folder -- as opposed to DL_TMP_DIR, which the Downloads screen already
    // manages -- is the destination-side leftover of a Wi-Fi/MTP receive that
    // never got renamed into place (httpsrv.c's upload path writes to
    // "<dest>/<name>.part" and renames on success; an interrupted transfer
    // that missed the normal abort cleanup leaves the .part behind). Both are
    // unambiguous junk: unlike a file idgame just doesn't recognize (which may
    // be legitimate homebrew content with no DAT entry), neither of these
    // could ever be a real game, so it's safe to flag without risking
    // something the user actually wants.
    for (const TidyFile &f : files) {
        if (self->tidy_cancel) break;
        bool is_part = f.name.size() > 5 &&
                       strcasecmp(f.name.c_str() + f.name.size() - 5,
                                  ".part") == 0;
        if (f.size == 0) {
            self->tidy_issues.push_back({f.path, f.name, f.console, "empty", 3});
        } else if (is_part) {
            self->tidy_issues.push_back({f.path, f.name, f.console, "part", 3});
        }
    }

    // Stale-cache maintenance: only on a whole-library pass (tidy_only empty),
    // since it isn't console-scoped work and adds a stat() per cached entry.
    // Neither cache ever drops a row on its own -- a file moved, renamed, or
    // deleted just leaves its old path unlookupable, so the row sits in the
    // TSV forever. This is pure bookkeeping cleanup (no game file touched),
    // so unlike the issues above it's applied immediately rather than queued
    // for per-item confirmation; TidyTick reports the combined count once.
    if (self->tidy_only.empty() && !self->tidy_cancel) {
        self->tidy_pruned_cache += hashcache_prune(&cache);
        VfyStatusCache vfy;
        vfystatus_load(&vfy, VFYSTATUS_PATH);
        self->tidy_pruned_cache += vfystatus_prune(&vfy);
        vfystatus_save(&vfy, VFYSTATUS_PATH);
        vfystatus_free(&vfy);
    }

    hashcache_save(&cache, HASH_CACHE_PATH);
    hashcache_free(&cache);
    self->tidy.done = true;
}

void MainApplication::TidyStart(const std::string &only,
                                const std::string &only_path) {
    this->tidy_onegr = false;
    this->tidy_only = only;
    this->tidy_only_path = only_path;
    this->tidy_cancel = false;
    this->tidy_files_done = 0;
    this->tidy_files_total = 0;
    this->tidy_issues.clear();
    this->tidy_pruned_cache = 0;
    this->tidy_kind_filter = -1;
    fs_mkdir_p(CACHE_DIR);
    this->screen = Screen::Tidy;
    this->layout->SetTitle(tr(only.empty() ? S_TIDY_LIBRARY : S_TIDY_CONSOLE));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_TIDY_SCANNING));
    if (this->tidy.Start(&MainApplication::TidyThread, this)) return;
    // No worker: scan inline so the feature still works.
    this->layout->HideSpinner();
    TidyThread(this);
    this->TidyReportPruned();
    this->TidyResults();
}

// 1G1R: group No-Intro-named files by title within each console and flag every
// copy but the most-preferred region/revision. Report-only; each removal is
// still an explicit A-press (TidyActSel), matching the tidy contract.
void MainApplication::OneGRThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    self->tidy_issues.clear();

    std::vector<TidyFile> files;
    tidy_gather(files, &self->tidy_cancel);
    self->tidy_files_total = (int)files.size();
    self->tidy_files_done = 0;

    // A file that inst_detect_groups would collapse into one Library row (a
    // plain X.cue + X.bin, an m3u's per-disc set) is one game in pieces, same
    // as a tagged multi-part file -- 1G1R must never see its .bin as a
    // "duplicate" of its .cue just because both carry the same region tag.
    // Detected once per folder, not per file: every candidate in a directory
    // shares the same list_dir + inst_detect_groups call.
    std::map<std::string, std::vector<std::string>> grouped_names;
    auto is_grouped = [&](const std::string &path, const std::string &name) {
        size_t slash = path.find_last_of('/');
        std::string dir = slash == std::string::npos ? "" : path.substr(0, slash);
        auto it = grouped_names.find(dir);
        if (it == grouped_names.end()) {
            std::vector<std::string> names;
            for (auto &grp : inst_detect_groups(dir, list_dir(dir)))
                for (auto &m : grp.members) names.push_back(m);
            it = grouped_names.emplace(dir, std::move(names)).first;
        }
        for (auto &m : it->second)
            if (strcasecmp(m.c_str(), name.c_str()) == 0) return true;
        return false;
    };

    // Consider only No-Intro-tagged files ("Title (Region)..."); a file with no
    // "(...)" tag isn't part of a regional clone set and is left alone. Key each
    // by console + base title so clones group only within one console's folder.
    // A multi-part file (a track or disc of one larger dump) is skipped too --
    // see onegr_is_multipart and is_grouped above.
    struct Cand {
        int fi;
        std::string key;
        int score;
    };
    std::vector<Cand> cands;
    for (size_t i = 0; i < files.size() && !self->tidy_cancel; i++) {
        const std::string &nm = files[i].name;
        self->tidy_files_done = (int)i + 1;
        if (nm.find(" (") == std::string::npos) continue;
        if (onegr_is_multipart(nm)) continue;
        if (is_grouped(files[i].path, nm)) continue;
        cands.push_back({(int)i,
                         files[i].console + "\x1f" + onegr_base_title(nm),
                         onegr_score(nm)});
    }
    // Sort into groups (by key), keeper first within each group (highest score,
    // name as a stable tie-break so the kept copy is deterministic).
    std::sort(cands.begin(), cands.end(), [&](const Cand &a, const Cand &b) {
        if (a.key != b.key) return a.key < b.key;
        if (a.score != b.score) return a.score > b.score;
        return files[a.fi].name < files[b.fi].name;
    });
    size_t g = 0;
    while (g < cands.size()) {
        size_t h = g + 1;
        while (h < cands.size() && cands[h].key == cands[g].key) h++;
        if (h - g >= 2) {
            const TidyFile &keep = files[cands[g].fi];
            for (size_t d = g + 1; d < h; d++) {
                const TidyFile &f = files[cands[d].fi];
                self->tidy_issues.push_back(
                    {f.path, f.name, f.console, keep.name, 2});
            }
        }
        g = h;
    }
    self->tidy.done = true;
}

void MainApplication::OneGRStart() {
    this->tidy_onegr = true;
    this->tidy_cancel = false;
    this->tidy_files_done = 0;
    this->tidy_files_total = 0;
    this->tidy_issues.clear();
    this->tidy_pruned_cache = 0; // 1G1R is name-based; never touches the caches
    this->tidy_kind_filter = -1;
    this->screen = Screen::Tidy;
    this->layout->SetTitle(tr(S_ONEGR_SCAN));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_TIDY_SCANNING));
    if (this->tidy.Start(&MainApplication::OneGRThread, this)) return;
    // No worker: scan inline so the feature still works.
    this->layout->HideSpinner();
    OneGRThread(this);
    this->TidyReportPruned();
    this->TidyResults();
}

void MainApplication::TidyTick() {
    if (!this->tidy.done) {
        char s[128];
        snprintf(s, sizeof(s), "%s  %d/%d   Ⓑ %s", tr(S_TIDY_SCANNING),
                 this->tidy_files_done, this->tidy_files_total, tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    this->layout->HideSpinner();
    this->tidy.Join();
    this->TidyReportPruned();
    this->TidyResults();
}

void MainApplication::TidyReportPruned() {
    if (this->tidy_pruned_cache <= 0) return;
    char t[64];
    snprintf(t, sizeof(t), tr(S_TIDY_PRUNED_CACHE), this->tidy_pruned_cache);
    this->Toast(t);
    this->tidy_pruned_cache = 0;
}

void MainApplication::TidyResults() {
    char title[64];
    snprintf(title, sizeof(title),
             tr(this->tidy_onegr ? S_ONEGR_TITLE : S_TIDY_TITLE),
             (int)this->tidy_issues.size());
    this->layout->SetTitle(title);
    this->layout->ClearMenu();
    const pu::ui::Color amber = is_light_theme()
                                    ? pu::ui::Color(180, 110, 30, 255)
                                    : pu::ui::Color(245, 175, 95, 255);
    const pu::ui::Color lbl = g_theme->row_text;
    // tidy_kind_filter restricts which issues are shown (and what Ⓧ Fix all
    // touches) to one TidyIssue::kind; -1 shows everything. tidy_results_order
    // maps the row actually displayed back to tidy_issues, same convention as
    // boxart_results_order/vfy_missing_order, since the filter can hide rows.
    this->tidy_results_order.clear();
    for (int i = 0; i < (int)this->tidy_issues.size(); i++) {
        const TidyIssue &is = this->tidy_issues[i];
        if (this->tidy_kind_filter >= 0 && is.kind != this->tidy_kind_filter)
            continue;
        this->tidy_results_order.push_back(i);
        char val[96];
        if (is.kind == 1) {
            const char *full = console_full_name(is.target.c_str());
            snprintf(val, sizeof(val), tr(S_TIDY_MISFILED),
                     full ? full : is.target.c_str());
        } else if (is.kind == 2) {
            snprintf(val, sizeof(val), tr(S_TIDY_CLONE), is.target.c_str());
        } else if (is.kind == 3) {
            snprintf(val, sizeof(val), "%s",
                     tr(is.target == "part" ? S_TIDY_ORPHAN_PART
                                             : S_TIDY_ORPHAN_EMPTY));
        } else {
            snprintf(val, sizeof(val), "%s", tr(S_TIDY_DUP));
        }
        std::string name = "[" + is.console + "] " + is.name;
        this->layout->AddRow2(name, val, lbl, amber, -1.0f,
                              console_icon(is.console.c_str()));
    }
    // Three subtitle states: truly nothing found, a filter hiding everything
    // that WAS found, or the normal "here's what you can do" hint. The Ⓐ Fix
    // hint would point at a no-op in either empty case (see TidyActSel's
    // empty guard).
    if (this->tidy_issues.empty()) {
        this->layout->SetSubtitle(tr(S_SUB_TIDY_EMPTY));
        this->layout->SetEmptyState(console_icon("default"),
                                    tr(this->tidy_onegr ? S_ONEGR_CLEAN
                                                        : S_TIDY_CLEAN));
    } else if (this->tidy_results_order.empty()) {
        this->layout->SetSubtitle(tr(S_SUB_TIDY_FILTER_EMPTY));
        this->layout->SetEmptyState(console_icon("default"),
                                    tr(S_TIDY_FILTER_EMPTY));
    } else {
        this->layout->SetSubtitle(tr(S_SUB_TIDY));
    }
    this->screen = Screen::Tidy;
}

// A on an issue: confirm, then perform just that one fix (move or delete) and
// drop the row. Never clobbers an existing file on a move.
void MainApplication::TidyActSel() {
    s32 row = this->layout->Sel();
    if (row < 0 || row >= (s32)this->tidy_results_order.size()) return;
    s32 idx = this->tidy_results_order[row]; // tidy_issues index behind this row
    const TidyIssue is = this->tidy_issues[idx]; // copy; the vector is rebuilt

    bool ok = false;
    if (is.kind == 1) { // misfiled -> move to the target console's folder
        const char *full = console_full_name(is.target.c_str());
        char body[600];
        snprintf(body, sizeof(body), tr(S_TIDY_MOVE_BODY), is.name.c_str(),
                 full ? full : is.target.c_str());
        if (this->CreateShowDialog(tr(S_TIDY_MOVE_TITLE), wrap_for_dialog(body),
                                   {tr(S_TIDY_MOVE_TITLE), tr(S_CANCEL)}, false,
                                   {}, style_dialog) != 0)
            return;
        const char *custom = install_folder_for(is.target.c_str());
        std::string dir = (custom && custom[0])
                              ? std::string(custom)
                              : std::string(roms_root(&g_tico)) + "/" +
                                    is.target;
        fs_mkdir_p(dir.c_str());
        std::string dest = dir + "/" + is.name;
        if (fs_exists(dest.c_str())) {
            this->ToastErr(tr(S_TIDY_MOVE_FAIL));
            return;
        }
        ok = fs_move(is.path.c_str(), dest.c_str());
        this->Toast(ok ? tr(S_MOVED) : tr(S_TIDY_MOVE_FAIL));
    } else if (is.kind == 2) { // 1G1R clone -> delete, keeping the preferred copy
        char body[700];
        snprintf(body, sizeof(body), tr(S_ONEGR_BODY), is.name.c_str(),
                 is.target.c_str());
        if (this->CreateShowDialog(tr(S_ONEGR_DELETE_TITLE),
                                   wrap_for_dialog(body),
                                   {tr(S_DELETE), tr(S_CANCEL)}, true, {},
                                   style_dialog) != 0)
            return;
        ok = (remove(is.path.c_str()) == 0);
        this->Toast(ok ? tr(S_DELETED) : tr(S_RENAME_FAILED));
    } else if (is.kind == 3) { // orphan -> delete (empty file or stray .part)
        char body[700];
        snprintf(body, sizeof(body), tr(S_TIDY_ORPHAN_BODY), is.name.c_str(),
                 tr(is.target == "part" ? S_TIDY_ORPHAN_PART
                                        : S_TIDY_ORPHAN_EMPTY));
        if (this->CreateShowDialog(tr(S_TIDY_ORPHAN_TITLE), wrap_for_dialog(body),
                                   {tr(S_DELETE), tr(S_CANCEL)}, true, {},
                                   style_dialog) != 0)
            return;
        ok = (remove(is.path.c_str()) == 0);
        this->Toast(ok ? tr(S_DELETED) : tr(S_RENAME_FAILED));
    } else { // duplicate -> delete the extra copy
        char body[700];
        snprintf(body, sizeof(body), tr(S_TIDY_DUP_BODY), is.path.c_str(),
                 is.target.c_str());
        if (this->CreateShowDialog(tr(S_TIDY_DUP_TITLE), wrap_for_dialog(body),
                                   {tr(S_DELETE), tr(S_CANCEL)}, true, {},
                                   style_dialog) != 0)
            return;
        ok = (remove(is.path.c_str()) == 0);
        this->Toast(ok ? tr(S_DELETED) : tr(S_RENAME_FAILED));
    }
    if (ok) {
        this->tidy_issues.erase(this->tidy_issues.begin() + idx);
        this->TidyResults();
        if (row < (s32)this->tidy_results_order.size()) this->layout->SetSel(row);
    }
}

// X: apply every currently-filtered/visible issue's native fix (move for
// misfiled, delete for duplicate/clone/orphan) behind one confirm, instead of
// requiring 30 individual Ⓐ presses on a big result set. Scoped to
// tidy_results_order, so filtering first (Ⓨ -> just orphans) narrows what
// this touches -- same semantics as TidyActSel, just batched.
void MainApplication::TidyFixAll() {
    if (this->tidy_results_order.empty()) return;
    int n = (int)this->tidy_results_order.size();
    char msg[160];
    snprintf(msg, sizeof(msg), tr(S_TIDY_FIX_ALL_CONFIRM), n);
    if (this->CreateShowDialog(tr(S_TIDY_FIX_ALL_TITLE), wrap_for_dialog(msg),
                               {tr(S_TIDY_FIX_ALL_TITLE), tr(S_CANCEL)}, true,
                               {}, style_dialog) != 0)
        return;
    // Snapshot the target indices before mutating tidy_issues, then apply
    // highest index first: erasing index i only shifts entries AFTER i, so
    // every not-yet-processed (smaller) snapshotted index stays valid.
    std::vector<int> idxs = this->tidy_results_order;
    std::sort(idxs.begin(), idxs.end(), [](int a, int b) { return a > b; });
    int done = 0, failed = 0;
    for (int idx : idxs) {
        if (idx < 0 || idx >= (int)this->tidy_issues.size()) continue;
        const TidyIssue is = this->tidy_issues[idx];
        bool ok;
        if (is.kind == 1) { // misfiled -> move to the target console's folder
            const char *custom = install_folder_for(is.target.c_str());
            std::string dir = (custom && custom[0])
                                  ? std::string(custom)
                                  : std::string(roms_root(&g_tico)) + "/" +
                                        is.target;
            fs_mkdir_p(dir.c_str());
            std::string dest = dir + "/" + is.name;
            ok = !fs_exists(dest.c_str()) &&
                 fs_move(is.path.c_str(), dest.c_str());
        } else { // duplicate / 1G1R clone / orphan -> delete
            ok = (remove(is.path.c_str()) == 0);
        }
        if (ok) {
            this->tidy_issues.erase(this->tidy_issues.begin() + idx);
            done++;
        } else {
            failed++;
        }
    }
    char t[64];
    if (failed > 0) {
        snprintf(t, sizeof(t), tr(S_TIDY_FIX_ALL_PARTIAL), done, failed);
        this->ToastErr(t);
    } else {
        snprintf(t, sizeof(t), tr(S_TIDY_FIX_ALL_DONE), done);
        this->Toast(t);
    }
    this->TidyResults();
}

// Y: pick which issue kind(s) to show. Options are built from whichever kinds
// are actually present, so a plain Tidy scan (kinds 0/1/3) never offers the
// 1G1R clone option and vice versa -- the two scans never mix in one pass.
void MainApplication::TidyFilterDialog() {
    static const int kOrder[4] = {0, 1, 3, 2}; // dup, misfiled, orphan, clone
    static const int kLabel[4] = {S_TIDY_DUP, S_TIDY_FILTER_MISFILED,
                                  S_TIDY_FILTER_ORPHAN, S_TIDY_FILTER_CLONE};
    std::vector<std::string> opts = {tr(S_TIDY_FILTER_ALL)};
    std::vector<int> kinds = {-1};
    for (int k = 0; k < 4; k++) {
        bool present = false;
        for (const TidyIssue &is : this->tidy_issues)
            if (is.kind == kOrder[k]) { present = true; break; }
        if (!present) continue;
        opts.push_back(tr(kLabel[k]));
        kinds.push_back(kOrder[k]);
    }
    int sel = 0;
    for (size_t i = 0; i < kinds.size(); i++)
        if (kinds[i] == this->tidy_kind_filter) { sel = (int)i; break; }
    int r = this->SideMenu(tr(S_FILTER), opts, sel);
    if (r < 0 || r >= (int)kinds.size() || kinds[r] == this->tidy_kind_filter)
        return;
    this->tidy_kind_filter = kinds[r];
    this->TidyResults();
}

// ---- largest files: what to delete to free space --------------------------
// Storage's direct answer to "what's eating my SD card": the whole library's
// biggest files, sorted descending. Complements Tidy (which only reports
// *problems*) and StorageOverview (which only totals). Reuses tidy_gather's
// walk, but does no hashing or classification — a plain stat pass — so unlike
// Tidy there's no meaningful per-file progress to report while it runs.
static const int kLargeFilesCap = 100; // bound render cost + memory on a huge library

void MainApplication::LargeFilesThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    std::vector<TidyFile> files;
    tidy_gather(files, &self->lgf_cancel);
    std::sort(files.begin(), files.end(),
              [](const TidyFile &a, const TidyFile &b) { return a.size > b.size; });
    if (files.size() > (size_t)kLargeFilesCap) files.resize(kLargeFilesCap);
    self->large_files.clear();
    self->large_files.reserve(files.size());
    for (const TidyFile &f : files)
        self->large_files.push_back({f.path, f.name, f.console, f.size});
    self->lgf.done = true;
}

void MainApplication::LargeFilesStart() {
    this->lgf_cancel = false;
    this->large_files.clear();
    this->screen = Screen::LargestFiles;
    this->layout->SetTitle(tr(S_LARGE_FILES));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_LARGE_FILES_SCANNING));
    if (this->lgf.Start(&MainApplication::LargeFilesThread, this)) return;
    // No worker: scan inline so the feature still works.
    this->layout->HideSpinner();
    LargeFilesThread(this);
    this->GotoLargestFiles();
}

void MainApplication::LargeFilesTick() {
    if (!this->lgf.done) return; // nothing incremental to show; just wait
    this->layout->HideSpinner();
    this->lgf.Join();
    this->GotoLargestFiles();
}

void MainApplication::GotoLargestFiles() {
    char title[64];
    snprintf(title, sizeof(title), tr(S_LARGE_FILES_TITLE_N),
             (int)this->large_files.size());
    this->layout->SetTitle(title);
    this->layout->SetSubtitle(
        tr(this->large_files.empty() ? S_SUB_LARGE_FILES_EMPTY : S_SUB_LARGE_FILES));
    this->layout->ClearMenu();
    const pu::ui::Color lbl = g_theme->row_text;
    for (const LargeFile &f : this->large_files) {
        std::string name = "[" + f.console + "] " + f.name;
        this->layout->AddRow2(name, human_size(f.size), lbl, value_color(), -1.0f,
                              console_icon(f.console.c_str()));
    }
    if (this->large_files.empty())
        this->layout->SetEmptyState(console_icon("default"), tr(S_LARGE_FILES_NONE));
    this->screen = Screen::LargestFiles;
}

// A on a row: jump to the file's console folder in the Library, cursor on the
// file itself — same "open where this lives" pattern used elsewhere.
void MainApplication::LargeFileOpenSel() {
    s32 row = this->layout->Sel();
    if (row < 0 || row >= (s32)this->large_files.size()) return;
    const LargeFile f = this->large_files[row]; // copy; GotoInstalled rebuilds g_inst
    auto p = f.path.find_last_of('/');
    std::string dir = (p == std::string::npos) ? f.path : f.path.substr(0, p);
    this->GotoInstalled(dir);
    for (s32 k = 0; k < (s32)g_inst.size(); k++) {
        if (g_inst[k].name == f.name) {
            this->layout->SetSel(k);
            break;
        }
    }
}

// X on a row: delete the file directly (danger confirm), then drop the row.
void MainApplication::LargeFileDeleteSel() {
    s32 row = this->layout->Sel();
    if (row < 0 || row >= (s32)this->large_files.size()) return;
    const LargeFile f = this->large_files[row];
    char body[600];
    snprintf(body, sizeof(body), tr(S_LARGE_FILES_DEL_BODY), f.name.c_str(),
             human_size(f.size).c_str());
    if (!this->ConfirmDanger(tr(S_DELETE), body, true)) return;
    bool ok = (remove(f.path.c_str()) == 0);
    this->Toast(ok ? tr(S_DELETED) : tr(S_RENAME_FAILED));
    if (ok) {
        this->large_files.erase(this->large_files.begin() + row);
        this->GotoLargestFiles();
        if (row < (s32)this->large_files.size()) this->layout->SetSel(row);
    }
}

// ---- box art scan: SteamGridDB cover fetch for the local library ----------
// "Scan for Box Art" (Tools panel, gated on Settings -> Account holding a
// SteamGridDB key): resolve + cache a cover for every distinct game title in
// the library. See boxart.h for the fetch/cache contract.

void MainApplication::BoxArtScanThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    if (self->boxart_console_mode) {
        // Bulk console-icon scan: one row per configured console instead of
        // per game title, resolved under its "console:<target>" cache key
        // (see ConsoleGroup::use_boxart / console_display_icon). A hit flips
        // that console over to showing it automatically -- same auto-enable
        // rule BoxArtPickConsoleCommit applies to an interactive pick -- so a
        // scan alone is enough to fix a console stuck on its 1x1 placeholder,
        // with no extra "now use it" step per console afterwards.
        self->boxart_total = g_cfg.console_count;
        self->boxart_done = 0;
        self->boxart_rows.clear();
        self->boxart_rows.reserve((size_t)g_cfg.console_count);
        bool cfg_dirty = false;
        char path[768];
        int done = 0;
        for (int i = 0; i < g_cfg.console_count; i++) {
            if (self->boxart_cancel) break;
            ConsoleGroup *g = &g_cfg.consoles[i];
            const char *full = console_full_name(g->target);
            BoxArtRow row;
            row.title = full ? full : g->console;
            row.console = g->target;
            row.key = std::string("console:") + g->target;
            row.console_target = g->target;
            // Force mode always re-queries, even a console already marked
            // "found" -- otherwise a bad automatic pick from an earlier scan
            // was a dead end, since boxart_fetch_titled's normal "already
            // cached" short circuit means re-running the scan silently
            // touched nothing. See BoxArtScanStart's Tools-panel dialog.
            row.found = self->boxart_console_force
                          ? boxart_fetch_query(g_creds.steamgriddb_key,
                                              row.key.c_str(),
                                              row.title.c_str(), path,
                                              sizeof(path), &row.score)
                          : boxart_fetch_titled(g_creds.steamgriddb_key,
                                                row.key.c_str(),
                                                row.title.c_str(), path,
                                                sizeof(path), &row.score);
            if (row.found && !g->use_boxart) {
                g->use_boxart = true;
                cfg_dirty = true;
            }
            self->boxart_rows.push_back(row);
            self->boxart_done = ++done;
        }
        boxart_index_save();
        if (cfg_dirty) {
            config_save(&g_cfg);
        }
        self->boxart.done = true;
        return;
    }
    std::vector<TidyFile> files;
    // Whole-library scan (Tools panel) or just one console's folder (that
    // console's Options panel) -- same scoping split as TidyThread.
    if (self->boxart_only.empty())
        tidy_gather(files, &self->boxart_cancel);
    else
        tidy_collect(self->boxart_only_path, self->boxart_only, files,
                     &self->boxart_cancel);

    // Dedupe by console + base title: a multi-disc/multi-track piece or a
    // regional clone of the same game would otherwise burn a separate
    // SteamGridDB query (and cache slot) per copy for art that's identical.
    std::map<std::string, BoxArtRow> uniq;
    for (const TidyFile &f : files) {
        if (onegr_is_multipart(f.name)) continue; // one query per whole game
        std::string title = boxart_query_title(f.name);
        if (title.empty()) continue;
        uniq.emplace(f.console + "\x1f" + title, BoxArtRow{title, f.console, false});
    }

    self->boxart_total = (int)uniq.size();
    self->boxart_done = 0;
    self->boxart_rows.clear();
    self->boxart_rows.reserve(uniq.size());
    char path[768];
    int done = 0;
    for (auto &kv : uniq) {
        if (self->boxart_cancel) break;
        BoxArtRow row = kv.second;
        // Force mode mirrors the console branch above: re-query every game,
        // even one already marked "found", via boxart_fetch_query's always-
        // overwrite contract -- otherwise a bad pick from an earlier scan was
        // a dead end (see boxart_game_force / the Tools-panel dialog).
        row.found = self->boxart_game_force
                      ? boxart_fetch_query(g_creds.steamgriddb_key,
                                          row.title.c_str(), row.title.c_str(),
                                          path, sizeof(path), &row.score)
                      : boxart_fetch(g_creds.steamgriddb_key,
                                     row.title.c_str(), path, sizeof(path),
                                     &row.score);
        self->boxart_rows.push_back(row);
        self->boxart_done = ++done; // plain int++, then a single volatile store
    }
    // One write for the whole pass (not per-title) -- same reasoning as
    // hashcache_save, and boxart_fetch already only marks the index dirty.
    boxart_index_save();
    self->boxart.done = true;
}

void MainApplication::BoxArtScanStart(const std::string &only,
                                      const std::string &only_path,
                                      bool console_mode, bool force_all) {
    // The auto-fetch drain (BoxArtAutoPoll) and a manual scan both walk
    // boxart.c's unsynchronized static index -- never let them run at once.
    // A pending drain is at most a handful of single-title fetches, so this
    // is a short, bounded wait, not an open-ended stall.
    if (this->boxart_auto.running) {
        this->boxart_auto_cancel = true;
        this->boxart_auto.Join();
    }
    this->boxart_only = only;
    this->boxart_only_path = only_path;
    this->boxart_console_mode = console_mode;
    this->boxart_console_force = console_mode && force_all;
    this->boxart_game_force = !console_mode && force_all;
    this->boxart_cancel = false;
    this->boxart_done = 0;
    this->boxart_total = 0;
    this->boxart_rows.clear();
    this->boxart_result_filter = 0; // fresh scan: never start on a stale filter
    this->screen = Screen::BoxArtResults;
    this->layout->SetTitle(tr(console_mode ? S_SCAN_CONSOLE_ART
                              : (only.empty() ? S_SCAN_BOX_ART
                                              : S_SCAN_BOX_ART_CONSOLE)));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_BOXART_SCANNING));
    if (this->boxart.Start(&MainApplication::BoxArtScanThread, this)) return;
    // No worker: scan inline so the feature still works.
    this->layout->HideSpinner();
    BoxArtScanThread(this);
    this->GotoBoxArtResults();
}

void MainApplication::BoxArtScanTick() {
    if (!this->boxart.done) {
        char s[128];
        snprintf(s, sizeof(s), "%s  %d/%d   Ⓑ %s", tr(S_BOXART_SCANNING),
                 this->boxart_done, this->boxart_total, tr(S_CANCEL));
        this->layout->SetSubtitle(s);
        return;
    }
    this->layout->HideSpinner();
    this->boxart.Join();
    this->GotoBoxArtResults();
}

void MainApplication::GotoBoxArtResults() {
    int found = 0;
    for (const BoxArtRow &r : this->boxart_rows) found += r.found ? 1 : 0;
    char title[64];
    snprintf(title, sizeof(title), tr(S_BOXART_RESULTS_TITLE_N),
             (int)this->boxart_rows.size());
    this->layout->SetTitle(title);
    char sub[128];
    snprintf(sub, sizeof(sub), tr(S_BOXART_RESULTS_SUB_N), found,
             (int)this->boxart_rows.size());
    this->layout->SetSubtitle(sub);
    this->layout->ClearMenu();
    const pu::ui::Color lbl = g_theme->row_text;
    const pu::ui::Color green = accent_green();
    const pu::ui::Color amber(245, 175, 95, 255); // same warning tone used elsewhere in this file
    const pu::ui::Color grey(150, 160, 185, 255);
    // boxart_result_filter: 0 = all, 1 = found only, 2 = not found only, 3 =
    // low-confidence hits only (see kLowConfidenceScore). boxart_results_order
    // maps the row actually shown at a given index back to boxart_rows, since
    // a filter can skip entries — same convention as vfy_missing_order.
    this->boxart_results_order.clear();
    // Stale entries from whatever screen was open before this (Installed,
    // BoxArtManageList, ...) must not get resolved against these brand new
    // row indices — same discipline GotoBoxArtManageList's own rebuild
    // follows for the same shared queue.
    this->boxart_pending.clear();
    for (int i = 0; i < (int)this->boxart_rows.size(); i++) {
        const BoxArtRow &r = this->boxart_rows[i];
        bool low_conf = r.found && r.score >= 0 && r.score < kLowConfidenceScore;
        if (this->boxart_result_filter == 1 && !r.found) continue;
        if (this->boxart_result_filter == 2 && r.found) continue;
        if (this->boxart_result_filter == 3 && !low_conf) continue;
        this->boxart_results_order.push_back(i);
        std::string name = "[" + r.console + "] " + r.title;
        pu::ui::Color status_c = !r.found ? grey : (low_conf ? amber : green);
        int status_s = !r.found ? S_BOXART_NOT_FOUND
                                : (low_conf ? S_BOXART_FOUND_LOW : S_BOXART_FOUND);
        if (r.found) {
            // Real cover thumbnail instead of the generic console badge, so a
            // bad auto-pick is visible right here instead of only after
            // drilling into Data Files > Art Cache > Browse separately.
            // boxart_row_icon's "decode only if already warm" split is the
            // same async pattern GotoInstalled/GotoBoxArtManageList use.
            const std::string &key = r.key.empty() ? r.title : r.key;
            pu::sdl2::Texture ic = nullptr;
            bool has_cover = boxart_row_icon(key, &ic);
            s32 row_idx = this->layout->RowCount();
            this->layout->AddRow2(name, tr(status_s), lbl, status_c, -1.0f,
                                  ic ? ic : console_icon(r.console.c_str()));
            if (has_cover && !ic) this->boxart_pending.push_back({row_idx, key});
        } else {
            this->layout->AddRow2(name, tr(status_s), lbl, status_c, -1.0f,
                                  console_icon(r.console.c_str()));
        }
    }
    if (this->boxart_results_order.empty())
        this->layout->SetEmptyState(console_icon("default"), tr(S_EMPTY));
    this->screen = Screen::BoxArtResults;
}

// X on the results list: which subset to show. Same "row index maps through
// boxart_results_order" contract GotoBoxArtResults sets up, rebuilt on any
// change; a straight -1 (B/cancel) or picking the already-active filter is a
// no-op so this never causes a pointless rebuild.
void MainApplication::BoxArtResultsFilterDialog() {
    int r = this->SideMenu(tr(S_FILTER),
                           {tr(S_BOXART_FILTER_ALL), tr(S_BOXART_FOUND),
                            tr(S_BOXART_NOT_FOUND), tr(S_BOXART_FILTER_LOW)},
                           this->boxart_result_filter);
    if (r < 0 || r == this->boxart_result_filter) return;
    this->boxart_result_filter = r;
    this->GotoBoxArtResults();
}

// Shared by BoxArtResultsRowMenu and the Library "A on a game" File dialog
// (Screen::Installed's A handler) — prompts for a search string seeded with
// `title`, then hands off to the art picker (BoxArtPickStart) instead of
// blindly saving SteamGridDB's top-ranked image the way this used to call
// boxart_fetch_query directly. `return_screen`/`return_idx` are just carried
// through to BoxArtPickStart — see BoxArtPickCtx.
void MainApplication::BoxArtCustomSearch(const std::string &title,
                                         Screen return_screen,
                                         int return_idx,
                                         const std::string &console_target,
                                         const std::string &seed) {
    if (title.empty()) return;
    if (!g_creds.steamgriddb_key[0]) {
        this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
        return;
    }
    char initial[256];
    snprintf(initial, sizeof(initial), "%s",
             (seed.empty() ? title : seed).c_str());
    char q[256] = {0};
    if (!prompt(tr(S_BOXART_SEARCH_GUIDE), initial, q, sizeof(q))) return;
    this->BoxArtPickStart(title, q, return_screen, return_idx, console_target);
}

// ---- art picker: browse SteamGridDB's cover options for a matched game ----

void MainApplication::BoxArtPickListThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    BoxArtCandidate cands[BOXART_MAX_CANDIDATES];
    int n = boxart_list_candidates(g_creds.steamgriddb_key,
                                   self->boxart_pick.query.c_str(), cands,
                                   BOXART_MAX_CANDIDATES);
    self->boxart_candidates.assign(cands, cands + n);
    self->boxart_thumb_paths.assign((size_t)n, std::string());
    // Best-effort per slot: a thumb that fails to download just falls back to
    // a placeholder icon in GotoBoxArtPicker, same as a poster card with no
    // cover anywhere else in the app — never worth failing the whole picker.
    for (int i = 0; i < n; i++) {
        char path[768];
        if (boxart_fetch_thumb(&self->boxart_candidates[(size_t)i], i, path,
                               sizeof(path))) {
            self->boxart_thumb_paths[(size_t)i] = path;
        }
    }
    self->boxpick.done = true;
}

void MainApplication::BoxArtPickConfirmThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    int idx = self->boxpick_confirm_idx;
    bool ok = false;
    if (idx >= 0 && idx < (int)self->boxart_candidates.size()) {
        char path[768];
        ok = boxart_fetch_candidate(g_creds.steamgriddb_key,
                                    self->boxart_pick.title.c_str(),
                                    &self->boxart_candidates[(size_t)idx],
                                    path, sizeof(path));
    }
    self->boxpick_result = ok;
    self->boxpick.done = true;
}

// Kicks off the listing step (search + thumbs) and switches to the picker
// screen once results land — GotoBoxArtPicker's row/subtitle already fold in
// "0 found" (nothing to browse), so callers never need to check the count.
void MainApplication::BoxArtPickStart(const std::string &title,
                                      const std::string &query,
                                      Screen return_screen, int return_idx,
                                      const std::string &console_target) {
    this->BoxArtPickFreeTex();
    this->boxart_candidates.clear();
    this->boxart_thumb_paths.clear();
    this->boxart_pick.title = title;
    this->boxart_pick.query = query;
    this->boxart_pick.return_screen = return_screen;
    this->boxart_pick.return_idx = return_idx;
    this->boxart_pick.console_target = console_target;
    this->boxpick_confirm = false;
    this->screen = Screen::BoxArtPicker;
    this->layout->SetTitle(tr(S_BOXART_PICKER_TITLE));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_BOXART_PICKER_SEARCHING));
    if (this->boxpick.Start(&MainApplication::BoxArtPickListThread, this)) return;
    // No worker: run inline, same fallback every other scan here uses.
    // Start() never having set .running means the HandleInput gate below
    // never fires for this, so there's nothing to Join here.
    this->layout->HideSpinner();
    BoxArtPickListThread(this);
    if (this->boxart_candidates.empty()) {
        this->Toast(tr(S_BOXART_CUSTOM_NOT_FOUND));
        this->BoxArtPickReturn();
    } else {
        this->GotoBoxArtPicker();
    }
}

// Called every frame from HandleInput while boxpick.running (listing or
// confirming — both share the one BgTask, never concurrently: the user can't
// reach a confirm until a listing has already finished and put up the
// picker screen). No per-item progress worth showing either way — one search
// plus a handful of small thumb downloads, or one single-image download.
void MainApplication::BoxArtPickTick() {
    if (!this->boxpick.done) {
        return;
    }
    this->layout->HideSpinner();
    this->boxpick.Join();
    if (this->boxpick_confirm) {
        this->boxpick_confirm = false;
        bool ok = this->boxpick_result;
        boxart_cache_forget(this->boxart_pick.title); // drop any stale
                                                       // texture — a hit
                                                       // just replaced it
        this->BoxArtPickConsoleCommit(ok);
        this->Toast(tr(ok ? S_BOXART_CUSTOM_FOUND : S_BOXART_CUSTOM_NOT_FOUND));
        this->BoxArtPickReturn();
        return;
    }
    if (this->boxart_candidates.empty()) {
        this->Toast(tr(S_BOXART_CUSTOM_NOT_FOUND));
        this->BoxArtPickReturn();
        return;
    }
    this->GotoBoxArtPicker();
}

void MainApplication::GotoBoxArtPicker() {
    this->screen = Screen::BoxArtPicker;
    this->layout->SetTitle(tr(S_BOXART_PICKER_TITLE));
    char sub[128];
    snprintf(sub, sizeof(sub), tr(S_BOXART_PICKER_SUB_N),
             (int)this->boxart_candidates.size());
    this->layout->SetSubtitle(sub);
    this->layout->ClearMenu();
    // 4-wide: these cards carry a short "Option N" label instead of a real
    // game title, so they don't need the Installed poster view's narrower
    // 6-wide layout to leave room for wrapped text.
    this->layout->SetCardCols(4);
    this->layout->SetCardPoster(true);
    this->layout->SetCardsMode(true);
    this->BoxArtPickFreeTex();
    for (size_t i = 0; i < this->boxart_candidates.size(); i++) {
        pu::sdl2::Texture tex = nullptr;
        if (!this->boxart_thumb_paths[i].empty()) {
            tex = pu::ui::render::LoadImageFromFile(this->boxart_thumb_paths[i]);
        }
        this->boxart_pick_tex.push_back(tex);
        char label[32];
        snprintf(label, sizeof(label), tr(S_BOXART_PICKER_OPTION_N), (int)(i + 1));
        std::string dims;
        const BoxArtCandidate &c = this->boxart_candidates[i];
        if (c.width > 0 && c.height > 0) {
            char d[32];
            snprintf(d, sizeof(d), "%d\xc3\x97%d", c.width, c.height); // U+00D7 ×
            dims = d;
        }
        this->layout->AddCard(label, dims,
                              tex ? tex : console_icon("default"), false,
                              false, tex != nullptr);
    }
}

void MainApplication::BoxArtPickFreeTex() {
    for (auto t : this->boxart_pick_tex) {
        if (t) pu::ui::render::DeleteTexture(t);
    }
    this->boxart_pick_tex.clear();
}

void MainApplication::BoxArtPickConfirm(int idx) {
    if (idx < 0 || idx >= (s32)this->boxart_candidates.size()) return;
    this->boxpick_confirm_idx = idx;
    this->boxpick_confirm = true;
    this->layout->ShowSpinner(tr(S_BOXART_PICKER_DOWNLOADING));
    if (this->boxpick.Start(&MainApplication::BoxArtPickConfirmThread, this)) return;
    // No worker: run inline (Start() never set .running, so there's no
    // thread for the HandleInput gate below to see or Join).
    this->layout->HideSpinner();
    BoxArtPickConfirmThread(this);
    bool ok = this->boxpick_result;
    this->boxpick_confirm = false;
    boxart_cache_forget(this->boxart_pick.title);
    this->BoxArtPickConsoleCommit(ok);
    this->Toast(tr(ok ? S_BOXART_CUSTOM_FOUND : S_BOXART_CUSTOM_NOT_FOUND));
    this->BoxArtPickReturn();
}

// Shared tail of a landed confirm (worker or inline path): when this pick was
// a console-art search (boxart_pick.console_target set -- see ConsoleArtMenu),
// flip ConsoleGroup::use_boxart on for a successful download and persist it,
// so the console actually starts showing the cover it just fetched instead of
// requiring a separate "now use it" step. A miss leaves the flag untouched --
// nothing was saved to show. No-op for a game pick (console_target empty).
void MainApplication::BoxArtPickConsoleCommit(bool ok) {
    if (!ok || this->boxart_pick.console_target.empty()) return;
    ConsoleGroup *g =
        config_find_console(&g_cfg, this->boxart_pick.console_target.c_str());
    if (g && !g->use_boxart) {
        g->use_boxart = true;
        config_save(&g_cfg);
    }
}

// Leaves the picker: B with nothing picked, or after a confirm's outcome was
// already toasted by BoxArtPickTick/BoxArtPickConfirm above. Re-derives
// whatever the caller needs fresh from boxart_lookup rather than trusting a
// passed-through "did it change" flag, so a plain cancel and a real pick both
// land in a correct state through the same path.
void MainApplication::BoxArtPickReturn() {
    this->BoxArtPickFreeTex();
    this->boxart_candidates.clear();
    this->boxart_thumb_paths.clear();
    if (this->boxart_pick.return_screen == Screen::BoxArtResults) {
        int i = this->boxart_pick.return_idx;
        if (i >= 0 && i < (s32)this->boxart_rows.size()) {
            BoxArtRow &row = this->boxart_rows[(size_t)i];
            // A console row's cache key differs from its display title (see
            // BoxArtRow::key) -- same lookup-key rule BoxArtResultsRowMenu
            // and GotoBoxArtResults already follow.
            const std::string &key = row.key.empty() ? row.title : row.key;
            row.found = boxart_lookup(key.c_str(), nullptr, 0);
            // A deliberate manual pick is never "low confidence" -- that
            // label only means "an automatic search guessed this," which no
            // longer applies once the user picked it themselves.
            row.score = -1;
        }
        this->GotoBoxArtResults();
    } else {
        // Every other caller (Installed's "A on a game" File dialog, and
        // ConsoleArtMenu's console-art search) lands here — both pass a
        // g_inst row to reselect, same refresh a custom search always did.
        this->GotoInstalled(this->inst_path);
        if (this->boxart_pick.return_idx >= 0) {
            this->layout->SetSel(this->boxart_pick.return_idx);
        }
    }
}

// A on a results row: search with a custom term (any row — a wrong automatic
// match is as worth fixing as a miss), or delete the cached cover outright
// when this row already has one.
void MainApplication::BoxArtResultsRowMenu(int idx) {
    if (idx < 0 || idx >= (s32)this->boxart_rows.size()) return;
    BoxArtRow &row = this->boxart_rows[idx];
    // A console-art scan row's cache key differs from its display title (see
    // BoxArtRow::key) -- every other row (a game) has an empty `key` and
    // uses `title` as its own key, same as always.
    const std::string &key = row.key.empty() ? row.title : row.key;
    std::vector<std::string> opts = {tr(S_BOXART_SEARCH_CUSTOM)};
    int del_idx = -1;
    if (row.found) {
        del_idx = (int)opts.size();
        opts.push_back(tr(S_BOXART_DELETE_COVER));
    }
    opts.push_back(tr(S_CANCEL));
    int r = this->SideMenu(row.title, opts);
    if (r == 0) {
        this->BoxArtCustomSearch(key, Screen::BoxArtResults, idx,
                                 row.console_target, row.title);
    } else if (del_idx >= 0 && r == del_idx) {
        char body[300];
        snprintf(body, sizeof(body), tr(S_BOXART_DELETE_BODY), row.title.c_str());
        if (this->ConfirmDanger(tr(S_DELETE), body, true)) {
            if (boxart_forget(key.c_str())) {
                boxart_cache_forget(key);
                row.found = false;
                // A console row with nothing left cached has nothing to
                // show -- fall back to its built-in icon, same invariant
                // BoxArtPickConsoleCommit maintains for an interactive pick.
                if (!row.console_target.empty()) {
                    ConsoleGroup *g =
                        config_find_console(&g_cfg, row.console_target.c_str());
                    if (g && g->use_boxart) {
                        g->use_boxart = false;
                        config_save(&g_cfg);
                    }
                }
                this->Toast(tr(S_DELETED));
                this->GotoBoxArtResults();
            }
        }
    }
}

// Called every frame from HandleInput. Decodes a small, bounded number of
// still-pending row/card icons per frame -- only the ones currently scrolled
// into view -- so a big scroll jump (or the initial list build) never spends
// more than a couple of PNG decodes in one frame. Entries outside the
// visible window stay pending; they get resolved once scrolling brings them
// on screen, or dropped for good on the next GotoInstalled/GotoBoxArtManageList
// rebuild (see there).
// `this->boxart_pending`'s indices are row indices in list view, card
// indices in poster view -- either way it's whatever RowCount() returned
// when the entry was queued, which already tracks the active one (see
// MainLayout::RowCount).
void MainApplication::BoxArtIconsPoll() {
    // Installed (list/card library view), BoxArtManageList, and InstSearch
    // (row-only, "cards" is always false on either of the latter two — see
    // GotoBoxArtManageList) plus BoxArtResults (also row-only) are the
    // screens that queue into boxart_pending.
    if (this->boxart_pending.empty() ||
        (this->screen != Screen::Installed &&
         this->screen != Screen::BoxArtManageList &&
         this->screen != Screen::InstSearch &&
         this->screen != Screen::BoxArtResults)) {
        return;
    }
    static const int kMaxPerFrame = 2; // ~1 decode every other frame at worst
    const bool cards = this->layout->InCards();
    s32 top = cards ? this->layout->CardFirstVisible() : this->layout->ScrollTop();
    s32 bottom = top + (cards ? this->layout->CardVisibleCount()
                              : this->layout->RowsVisible());
    int resolved = 0;
    for (size_t i = 0; i < this->boxart_pending.size() && resolved < kMaxPerFrame;) {
        s32 idx = this->boxart_pending[i].first;
        if (idx < top || idx >= bottom) {
            i++; // not on screen yet -- leave queued, check the next one
            continue;
        }
        pu::sdl2::Texture tex = boxart_icon_for(this->boxart_pending[i].second);
        if (tex) {
            if (cards) {
                this->layout->SetCardIcon(idx, tex);
            } else {
                this->layout->SetRowIcon(idx, tex);
            }
        }
        this->boxart_pending.erase(this->boxart_pending.begin() + i);
        resolved++;
        // don't advance i -- the erase shifted the next element into place
    }
}

// ---- auto-fetch box art for newly landed games -----------------------------
// Appearance -> "Auto-Fetch New Art" (g_prefs.box_art_auto_fetch, default on).
// Drains g_boxart_auto_pending (fed by boxart_auto_on_landed, a worker-thread
// callback registered on queue_on_landed at OnLoad) one title at a time,
// entirely off-thread and off-screen: nothing here ever touches this->screen
// or shows progress, unlike a manual Scan. Reads g_creds.steamgriddb_key on
// its own thread the same way BoxArtScanThread already does -- an existing,
// accepted cross-thread read in this file, not a new one.
void MainApplication::BoxArtAutoThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    for (;;) {
        if (self->boxart_auto_cancel) {
            break;
        }
        mutexLock(&g_boxart_auto_mtx);
        if (g_boxart_auto_pending.empty()) {
            mutexUnlock(&g_boxart_auto_mtx);
            break;
        }
        std::string title = g_boxart_auto_pending.front();
        g_boxart_auto_pending.erase(g_boxart_auto_pending.begin());
        mutexUnlock(&g_boxart_auto_mtx);
        boxart_fetch(g_creds.steamgriddb_key, title.c_str(), NULL, 0, NULL); // cache
                                                                       // hit/miss is
                                                                       // enough; no
                                                                       // screen needs
                                                                       // the path
    }
    // One save at the end of the drain rather than one per title -- same
    // "batch, don't hammer the SD card" rule boxart_index_save documents for
    // BoxArtScanThread's full scans, just for a much smaller batch.
    boxart_index_save();
    self->boxart_auto.done = true;
}

// Called every frame from HandleInput (see BoxArtIconsPoll just above for the
// sibling "decode what's already found" half of this feature). Starting a
// BgTask always happens here, on the UI thread, never from the worker thread
// that queued the title -- queue_on_landed can fire concurrently from more
// than one download slot (max_downloads > 1), and two threads racing to
// start the same BgTask would be a real bug, not just a style choice.
void MainApplication::BoxArtAutoPoll() {
    if (this->boxart_auto.running) {
        if (!this->boxart_auto.done) {
            return; // still draining -- nothing to do this frame
        }
        this->boxart_auto.Join();
        // Fall through: more titles may have queued while that pass ran: a
        // manual scan below.
    }
    if (this->boxart.running) {
        // A manual "Scan for Box Art" owns boxart.c's index right now (see
        // BoxArtScanStart's own boxart_auto.Join() for the reverse order).
        // Leave everything queued and try again once it's done -- no data
        // loss, just deferred.
        return;
    }
    if (!g_prefs.box_art_auto_fetch || !g_creds.steamgriddb_key[0]) {
        // Feature off, or no key configured to fetch with: drop whatever
        // queued rather than let it grow for an entire play session. Any
        // title that lands while this is true just re-queues itself the next
        // time it's imported, and a future manual scan covers it regardless.
        mutexLock(&g_boxart_auto_mtx);
        g_boxart_auto_pending.clear();
        mutexUnlock(&g_boxart_auto_mtx);
        return;
    }
    mutexLock(&g_boxart_auto_mtx);
    bool has_work = !g_boxart_auto_pending.empty();
    mutexUnlock(&g_boxart_auto_mtx);
    if (!has_work) {
        return;
    }
    this->boxart_auto_cancel = false;
    this->boxart_auto.Start(&MainApplication::BoxArtAutoThread, this);
    // If Start() failed (thread creation), the pending titles simply stay
    // queued and this is retried next frame -- no inline fallback here,
    // unlike BoxArtScanStart's, since there is no screen to fall back onto.
}

// ---- manage box art: browse/delete what's actually cached on disk ---------
// Storage sub-screen. Distinct from the scan results above (a one-off report
// of a single run): this reflects boxart.c's persistent index at any time,
// whether or not a scan has run this session, grouped by console since that's
// how a user thinks about their library even though the index itself is
// title-only (see boxart.c's header comment). A console-list screen (counts),
// A drills into that console's titles, X deletes a cover.

void MainApplication::BoxArtManageThread(void *arg) {
    auto self = static_cast<MainApplication *>(arg);
    std::vector<TidyFile> files;
    tidy_gather(files, &self->boxman_cancel); // always whole-library: this is a browser, not a scan

    // Same console+title dedupe key as the scan, so one row per game — then
    // keep only the ones that actually have a cached cover; nothing to manage
    // for a miss or an unresolved title.
    std::map<std::string, BoxArtManageRow> uniq;
    char path[768];
    for (const TidyFile &f : files) {
        if (self->boxman_cancel) break;
        if (onegr_is_multipart(f.name)) continue;
        std::string title = boxart_query_title(f.name);
        if (title.empty()) continue;
        std::string key = f.console + "\x1f" + title;
        if (uniq.count(key)) continue;
        if (!boxart_lookup(title.c_str(), path, sizeof(path))) continue;
        uniq.emplace(key, BoxArtManageRow{f.console, title});
    }
    self->boxart_manage_rows.clear();
    self->boxart_manage_rows.reserve(uniq.size());
    for (auto &kv : uniq) self->boxart_manage_rows.push_back(kv.second);
    std::sort(self->boxart_manage_rows.begin(), self->boxart_manage_rows.end(),
              [](const BoxArtManageRow &a, const BoxArtManageRow &b) {
                  return a.console != b.console ? a.console < b.console
                                                : a.title < b.title;
              });
    self->boxman.done = true;
}

void MainApplication::BoxArtManageStart() {
    this->boxman_cancel = false;
    this->boxart_manage_rows.clear();
    this->boxart_manage_console.clear();
    this->screen = Screen::BoxArtManageConsoles;
    this->layout->SetTitle(tr(S_MANAGE_BOX_ART));
    this->layout->ClearMenu();
    this->layout->ShowSpinner(tr(S_BOXART_MANAGE_SCANNING));
    if (this->boxman.Start(&MainApplication::BoxArtManageThread, this)) return;
    this->layout->HideSpinner();
    BoxArtManageThread(this);
    this->GotoBoxArtManageConsoles();
}

void MainApplication::BoxArtManageTick() {
    if (!this->boxman.done) return; // no meaningful per-file progress to show
    this->layout->HideSpinner();
    this->boxman.Join();
    this->GotoBoxArtManageConsoles();
}

// Console list: one row per console that has at least one cached cover, with
// a count. Built fresh from boxart_manage_rows every time so a delete on the
// per-console screen is reflected the moment you back out.
void MainApplication::GotoBoxArtManageConsoles() {
    this->layout->SetTitle(tr(S_MANAGE_BOX_ART));
    this->layout->ClearMenu();
    const pu::ui::Color lbl = g_theme->row_text;
    std::string cur;
    int count = 0;
    auto flush = [&]() {
        if (!cur.empty()) {
            char v[32];
            snprintf(v, sizeof(v), "%d", count);
            this->layout->AddRow2(console_full_name(cur.c_str()) ? console_full_name(cur.c_str())
                                                                  : cur,
                                  v, lbl, value_color(), -1.0f,
                                  console_icon(cur.c_str()));
        }
    };
    for (const BoxArtManageRow &r : this->boxart_manage_rows) {
        if (r.console != cur) {
            flush();
            cur = r.console;
            count = 0;
        }
        count++;
    }
    flush();
    if (this->boxart_manage_rows.empty())
        this->layout->SetEmptyState(console_icon("default"), tr(S_BOXART_MANAGE_EMPTY));
    this->screen = Screen::BoxArtManageConsoles;
}

// One console's cached covers. X deletes the highlighted one (disk + index +
// the runtime texture cache, so the Library list stops showing it too) and
// drops the row in place, same immediate-feedback pattern as LargeFileDeleteSel.
void MainApplication::GotoBoxArtManageList(const std::string &console) {
    this->boxart_manage_console = console;
    const char *full = console_full_name(console.c_str());
    this->layout->SetTitle(full ? full : console);
    this->layout->SetSubtitle(tr(S_BOXART_MANAGE_LIST_SUB));
    this->layout->ClearMenu();
    // Stale row indices from whatever console's list was open before --
    // BoxArtIconsPoll must not resolve against the list being rebuilt below.
    // Every row here is a confirmed hit (BoxArtManageThread only includes
    // titles boxart_lookup already found), but the texture itself may not be
    // decoded yet -- same "cache hit is free, miss is deferred" split as
    // GotoInstalled's boxart_row_icon/boxart_pending, so opening a console
    // with a few hundred cached covers doesn't decode all of them in one
    // frame.
    this->boxart_pending.clear();
    const pu::ui::Color lbl = g_theme->row_text;
    for (const BoxArtManageRow &r : this->boxart_manage_rows) {
        if (r.console != console) continue;
        pu::sdl2::Texture ic = nullptr;
        bool has_cover = boxart_row_icon(r.title, &ic);
        s32 row_idx = this->layout->RowCount();
        this->layout->AddRow2(r.title, tr(S_BOXART_FOUND), lbl, accent_green(),
                              -1.0f, ic);
        if (has_cover && !ic) this->boxart_pending.push_back({row_idx, r.title});
    }
    this->screen = Screen::BoxArtManageList;
}

void MainApplication::BoxArtManageDeleteSel() {
    // Row index -> title by walking the same console-filtered order the list
    // was built in (indices into boxart_manage_rows aren't the same as the
    // filtered rows shown on screen).
    auto title_at = [&](s32 row) -> std::string {
        int i = -1;
        for (const BoxArtManageRow &r : this->boxart_manage_rows) {
            if (r.console != this->boxart_manage_console) continue;
            i++;
            if (i == row) return r.title;
        }
        return std::string();
    };

    // Marked set (Y), or just the row under the cursor when nothing is
    // marked — same split as InstDeleteSel.
    std::vector<std::string> titles;
    auto marks = this->layout->Marked();
    if (!marks.empty()) {
        for (s32 idx : marks) {
            std::string t = title_at(idx);
            if (!t.empty()) titles.push_back(t);
        }
    } else {
        std::string t = title_at(this->layout->Sel());
        if (!t.empty()) titles.push_back(t);
    }
    if (titles.empty()) return;

    if (titles.size() == 1) {
        char body[300];
        snprintf(body, sizeof(body), tr(S_BOXART_DELETE_BODY), titles[0].c_str());
        if (!this->Confirm(tr(S_DELETE), body, true)) return;
    } else {
        char body[64];
        snprintf(body, sizeof(body), tr(S_DELETE_SELECTED), (int)titles.size());
        if (!this->Confirm(tr(S_DELETE), body, true)) return;
    }

    s32 row = this->layout->Sel();
    int removed = 0;
    for (const std::string &title : titles) {
        if (!boxart_forget(title.c_str())) continue;
        boxart_cache_forget(title); // drop the runtime texture too, or the
                                    // Library list keeps showing it
        for (size_t k = 0; k < this->boxart_manage_rows.size(); k++) {
            if (this->boxart_manage_rows[k].console == this->boxart_manage_console &&
                this->boxart_manage_rows[k].title == title) {
                this->boxart_manage_rows.erase(this->boxart_manage_rows.begin() + k);
                break;
            }
        }
        removed++;
    }
    if (removed == 0) return;
    char t[32];
    snprintf(t, sizeof(t), tr(S_DELETED_N), removed);
    this->Toast(t);
    // Still any covers left for this console? Stay here (marks cleared by the
    // rebuild below); otherwise the console itself just dropped out of the
    // list, so back out to it.
    bool any = false;
    for (const BoxArtManageRow &r : this->boxart_manage_rows)
        if (r.console == this->boxart_manage_console) { any = true; break; }
    if (any) {
        this->GotoBoxArtManageList(this->boxart_manage_console);
        this->layout->SetSel(row < (s32)this->layout->RowCount() ? row : row - 1);
    } else {
        this->GotoBoxArtManageConsoles();
    }
}

// Data Files > Art Cache: report the on-disk cache's size, then either drill
// into the per-title browser above (same screen Storage's Manage Box Art
// uses) or wipe everything in one confirmed pass via boxart_clear_all.
void MainApplication::ArtCacheMenu() {
    auto art_files = list_dir(BOXART_DIR);
    uint64_t total = 0;
    int n = 0;
    for (auto &e : art_files) {
        if (e.is_dir) continue;
        n++;
        total += e.size;
    }
    std::vector<std::string> opts = {tr(S_ART_CACHE_BROWSE)};
    int clear_idx = -1;
    if (n > 0) {
        clear_idx = (int)opts.size();
        opts.push_back(tr(S_ART_CACHE_CLEAR));
    }
    opts.push_back(tr(S_CANCEL));
    char body[96];
    if (n > 0) {
        snprintf(body, sizeof(body), tr(S_ART_CACHE_N), n,
                 human_size(total).c_str());
    } else {
        snprintf(body, sizeof(body), "%s", tr(S_ART_CACHE_NONE));
    }
    int r = this->SideMenu(tr(S_ART_CACHE), opts, 0, body);
    if (r == 0) {
        this->BoxArtManageStart();
    } else if (clear_idx >= 0 && r == clear_idx) {
        char msg[128];
        snprintf(msg, sizeof(msg), tr(S_ART_CACHE_CLEAR_CONFIRM), n);
        if (this->ConfirmDanger(tr(S_ART_CACHE_CLEAR), msg, true)) {
            boxart_clear_all();
            boxart_cache_forget_all();
            this->Toast(tr(S_ART_CACHE_CLEARED));
        }
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
    auto p = this->inst_path.find_last_of('/');
    this->MvStartTo(names, (p == std::string::npos) ? this->inst_path
                                                    : this->inst_path.substr(0, p));
}

void MainApplication::MvStartTo(const std::vector<std::string> &names,
                                const std::string &dest) {
    if (names.empty() || dest.empty() || dest == this->inst_path) {
        return;
    }
    this->mv_from = this->inst_path;
    this->mv_to = dest;
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
    if (ok > 0 && folder != roms_root(&g_tico) &&
        !inst_is_console_root(folder) && list_dir(folder).empty()) {
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
        if (si >= 0) e.size = json_u64_size(js, tok, si);
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
    // Read-only inventory server for the desktop companion: polled every frame,
    // on every screen, independent of the transfer receiver below.
    this->InvServerPoll();
    // Drive the app-initiated transfers (self-update, DAT sync) that now live as
    // Queue-tab items instead of owning their own screen.
    this->PollXfers();
    // Lazily decode box art for rows that just scrolled into view — see
    // BoxArtIconsPoll for why this isn't done at list-build time.
    this->BoxArtIconsPoll();
    // Silently fetch art for anything that landed since the last frame — see
    // BoxArtAutoPoll.
    this->BoxArtAutoPoll();

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
        //
        // NOT console_count == 0: the bundled dl_sources.json seeds every
        // supported console (with zero repos each) on first launch, so
        // console_count is never 0 on a fresh install and this never fired.
        // "Nothing to browse yet" actually means every console has 0 repos.
        bool any_repos = false;
        for (int c = 0; c < g_cfg.console_count && !any_repos; c++) {
            any_repos = g_cfg.consoles[c].repo_count > 0;
        }
        if (!any_repos) {
            this->GuidedTour();
        }
        // Bring the companion inventory server up if it was left enabled.
        if (g_prefs.inv_server) {
            this->InvServerStart();
        }
        return;
    }
    // Reap the silent startup update check (if any) and light the Settings dot.
    this->BgChkPoll();

    // (Self-update now runs as a Queue-tab item, driven by PollXfers above; it no
    // longer owns the UI — cancel it with A on its Queue item.)

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

    // (DAT auto-download now runs as a Queue-tab item, driven by PollXfers above;
    // it no longer owns the UI — cancel it with A on its Queue item.)

    // A DAT verification is running: hash progress owns the UI; B cancels
    // between/within files. VerifyTick builds the results list when it lands.
    // The all-consoles batch reuses the same worker/job; B must also stop the
    // batch loop from starting the next console (vfy_all_cancel).
    if (this->vfy.running) {
        if (down & HidNpadButton_B) {
            this->vfy_job.cancel = true;
            this->vfy_all_cancel = true;
        }
        if (this->screen == Screen::VerifyAll) {
            this->VerifyAllTick();
        } else {
            this->VerifyTick();
        }
        return;
    }

    // A tidy-library scan is running: same spinner+cancel model as verify. B
    // stops the scan; TidyTick then shows whatever issues were found so far.
    if (this->tidy.running) {
        if (down & HidNpadButton_B) this->tidy_cancel = true;
        this->TidyTick();
        return;
    }

    // A largest-files scan is running: no per-file progress to show (it's a
    // walk + sort, not a hash), so just hold the spinner. B stops the walk.
    if (this->lgf.running) {
        if (down & HidNpadButton_B) this->lgf_cancel = true;
        this->LargeFilesTick();
        return;
    }

    // A box art scan is running: same spinner+cancel model as tidy/verify (one
    // SteamGridDB round trip per distinct title, so per-item progress is worth
    // showing). B stops the scan; BoxArtScanTick then shows whatever was
    // resolved so far.
    if (this->boxart.running) {
        if (down & HidNpadButton_B) this->boxart_cancel = true;
        this->BoxArtScanTick();
        return;
    }

    // "Manage Box Art" is walking the library to find what's actually cached
    // on disk: a stat walk + index lookups, same no-per-item-progress model as
    // the largest-files scan above. B stops the walk.
    if (this->boxman.running) {
        if (down & HidNpadButton_B) this->boxman_cancel = true;
        this->BoxArtManageTick();
        return;
    }

    // The art picker is listing candidates + thumbs, or downloading a picked
    // cover for real: both are one search plus a handful of small transfers,
    // over well before a cancel would be worth offering.
    if (this->boxpick.running) {
        this->BoxArtPickTick();
        return;
    }

    // The emulator/app update list is checking each installed entry against its
    // GitHub release: hold the spinner with (n/total) progress; B stops the
    // remaining checks (rows checked so far still show). AppChkTick builds the
    // list once the worker lands.
    if (this->appchk.running) {
        if (down & HidNpadButton_B) this->appchk_cancel = true;
        this->AppChkTick();
        return;
    }

    // A background diagnostic is running: the speed test shows live meters,
    // the self-test just holds a spinner, but both let B cancel mid-flight —
    // B sets the relevant curl handle's cancel flag and DiagTick reaps the
    // (cancelled) result once the worker lands.
    if (this->diag.running) {
        if (down & HidNpadButton_B) {
            if (this->diag_speed) {
                this->sp_prog.cancel = 1;
            } else {
                this->diag_cancel = 1;
            }
        }
        this->DiagTick();
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
            g_sel.clear(); // its indices pointed into the item just freed
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

    // An archive.org search is in flight: same threaded/cancellable model. The
    // network request can't be aborted mid-way, so B just drops its result and
    // returns to the Add tab; the worker finishes in the background.
    if (this->arch.running) {
        (void)held;
        if (!this->arch_discard) {
            if (down & HidNpadButton_B) {
                this->arch_discard = true;
                this->layout->HideSpinner();
                this->GotoHome();
                return;
            }
            this->ArchSearchTick();
            return;
        }
        if (this->arch.done) {
            this->arch.Join();
            this->arch_discard = false;
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

    // (A live-link push now shows as a Queue-tab item, fed by InvServerPoll on
    // every screen; it no longer takes over the UI, so input flows normally.)

    // Waiting for a dl_sources.json upload: serve the LAN receiver a frame at a
    // time and swallow input except B, which gives up and closes the socket.
    // On the Wi-Fi connect screen: the receiver is serviced globally (PollXfers)
    // and a ROM push shows in the Queue tab, so this only handles leaving the
    // setup screen. Gated to the connect screen so it never captures Queue-tab
    // input once a push has jumped there.
    if (this->imp_open && this->screen == Screen::Import) {
        if (down & HidNpadButton_B) {
            // A ROM receiver stays open across files; B means "done" — save what
            // arrived and (in auto-sort) hand the inbox to the sorter.
            if (this->imp_rom) {
                this->RomRecvFinish();
                return;
            }
            this->ImportStop();
            this->ImportReturn();
            return;
        }
        if (down & (HidNpadButton_L | HidNpadButton_R)) {
            // A ROM receiver keeps listening when you leave via a tab (files land
            // in the Queue tab; finish it there with B). Other flavours have
            // nothing in flight, so leaving closes the socket.
            if (!this->imp_rom) {
                this->ImportStop();
            }
            this->SwitchTab((down & HidNpadButton_R) ? +1 : -1);
            return;
        }
        if (!touch.IsEmpty() && touch.y >= 80 && touch.y < 150) {
            s32 seg = (s32)pu::ui::render::ScreenWidth / 4;
            s32 idx = touch.x / (seg > 0 ? seg : 1);
            if (idx >= 0 && idx < 4) {
                if (!this->imp_rom) {
                    this->ImportStop();
                }
                this->GotoTab((Tab)idx);
                return;
            }
        }
        return;
    }

    // On the USB connect screen: the session is serviced globally (PollXfers) and
    // any copy shows in the Queue tab, so this only handles leaving the setup
    // screen. Each exit tears device mode down first (leaving usb:ds held would
    // block a later reconnect). Navigating away with a copy in flight is fine now
    // — it keeps running in the background and B here (or unplug) ends it. This
    // block is gated to the connect screen so it never captures Queue-tab input
    // while a USB copy is mirrored there.
    if (this->usb_open && this->screen == Screen::UsbMtp) {
        if (down & HidNpadButton_B) {
            // Keep the responder up if it's the background inventory-server
            // instance (just leave the screen); otherwise tear device mode down.
            if (this->usb_bg) this->usb_open = false;
            else              this->UsbMtpStop();
            this->UsbMtpReturn();
            return;
        }
        if (down & (HidNpadButton_L | HidNpadButton_R)) {
            // L/R leaves the setup screen but KEEPS the session alive (unlike the
            // old teardown): a copy started later still lands in the Queue tab.
            this->SwitchTab((down & HidNpadButton_R) ? +1 : -1);
            return;
        }
        if (!touch.IsEmpty() && touch.y >= 80 && touch.y < 150) {
            s32 seg = (s32)pu::ui::render::ScreenWidth / 4;
            s32 idx = touch.x / (seg > 0 ? seg : 1);
            if (idx >= 0 && idx < 4) {
                this->GotoTab((Tab)idx);
                return;
            }
        }
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
    // Touch: tapping a footer hint chip fires that button, so the on-screen
    // controls are usable without the physical buttons. Edge-triggered so one
    // tap is one press.
    {
        static bool ftr_prev = false;
        bool ftr_now = !touch.IsEmpty();
        if (ftr_now && !ftr_prev) {
            down |= this->layout->FooterButtonAt(touch.x, touch.y);
        }
        ftr_prev = ftr_now;
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
    // Ambient sliver under the tab bar: the active item's byte progress,
    // visible from any tab so a running transfer never needs a Queue check
    // just to see it's alive. Hidden when nothing's actually moving bytes
    // (queued/paused-only doesn't count) or total is unknown yet.
    if (qac > 0) {
        uint64_t now = 0, total = 0;
        if (queue_active_info(NULL, 0, NULL, &now, &total, NULL, NULL, NULL) &&
            total > 0) {
            this->layout->SetQueueProgress((float)((double)now / (double)total));
        } else {
            this->layout->SetQueueProgress(-1.0f);
        }
    } else {
        this->layout->SetQueueProgress(-1.0f);
    }

    // Remember the current selection per browseable list so backing out and
    // returning keeps your place.
    switch (this->screen) {
    case Screen::Home:
        this->home_sel = this->layout->Sel();
        break;
    case Screen::Installed:
        // Only the root console list restores its place; inside a folder the
        // cursor is transient (you drilled in), so don't clobber the saved row.
        if (this->inst_path == roms_root(&g_tico))
            this->inst_sel = this->layout->Sel();
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
            if (it->external && it->xkind == 3 && it->status == Q_DOWNLOADING) {
                // DAT batch: now/total are console counts, not bytes.
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                    snprintf(c0, sizeof(c0), "%d / %d", (int)it->now,
                             (int)it->total);
                }
            } else if (it->external && it->xkind == 4 && it->status == Q_DOWNLOADING) {
                // Unzip phase of a USB receive (stays Q_DOWNLOADING, verb flips to
                // "unzip"): show only the byte-based bar, never speed/ETA -- those
                // are transfer stats and read as nonsense while unpacking.
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                    snprintf(c0, sizeof(c0), "%s", human_size(it->total).c_str());
                }
            } else if (it->status == Q_DOWNLOADING && it->total) {
                prog = (float)it->now / (float)it->total;
                snprintf(c0, sizeof(c0), "%s", human_size(it->total).c_str());
                if (it->stalled) {
                    // A failed attempt is being retried (backoff or an
                    // immediate credentialed re-attempt) -- no bytes are
                    // moving yet, but this isn't dead either, so say so
                    // instead of leaving the field blank like a hung download.
                    snprintf(c1, sizeof(c1), "%s", tr(S_RECONNECTING));
                } else if (it->speed) {
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
            } else if (it->status == Q_VERIFYING) {
                // A big uncompressed ISO has no unzip step to follow, so its
                // md5 pass is the only "processing" the user waits on. Show the
                // same byte-based bar (hashed / total) so it can't look hung.
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                    snprintf(c0, sizeof(c0), "%s",
                             human_size(it->total).c_str());
                }
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
            pu::ui::Color sc = xfer_color(it);
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
            snprintf(st, sizeof(st), "%s", xfer_verb(it));
            // Hero (tint + ring shimmer) covers the actively-worked item:
            // downloading or unzipping.
            // Stock icons only on queue cards -- a queue card is a small,
            // uniform-grid status tile, not a library browse card, so custom
            // box art (portrait covers, inconsistent shapes/crops) is left
            // out here even for a console that opted into it elsewhere;
            // allow_boxart=false forces the built-in square badge every time
            // except the one non-console entry, HaulNX self-update, which
            // console_display_icon already special-cases regardless of this
            // flag. is_art comes back false unconditionally as a result, so
            // the queue card grid's aspect-fit path (Card::art) simply never
            // triggers here -- kept rather than removed since a future case
            // may still want it.
            bool is_art = false;
            pu::sdl2::Texture qicon =
                console_display_icon(it->target, &is_art, false);
            this->layout->SetQueueCard(i, it->target, qicon, st, sc, c0, c1,
                                       c2, it->name, prog,
                                       it->status == Q_DOWNLOADING ||
                                           it->status == Q_VERIFYING ||
                                           it->status == Q_EXTRACTING,
                                       ring, qpos, qrefresh,
                                       !strcmp(it->target, "HaulNX"), is_art);
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
            if (it->external && it->xkind == 3 && it->status == Q_DOWNLOADING) {
                // DAT batch: now/total are console counts, not bytes.
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                    snprintf(info, sizeof(info), "%d / %d", (int)it->now,
                             (int)it->total);
                }
            } else if (it->external && it->xkind == 4 && it->status == Q_DOWNLOADING) {
                // Unzip phase of a USB receive: byte-based bar only, no speed/ETA
                // (those are transfer stats and read as nonsense while unpacking).
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                    snprintf(info, sizeof(info), "%s",
                             human_size(it->total).c_str());
                }
            } else if (it->status == Q_DOWNLOADING && it->total) {
                prog = (float)it->now / (float)it->total;
                // The bottom bar shows progress; the text shows size (+ speed
                // and ETA), no percent — matching the card view.
                if (it->stalled) {
                    // See the card view's identical check: a failed attempt
                    // is being retried, not a dead download.
                    snprintf(info, sizeof(info), "%s  ·  %s",
                             human_size(it->total).c_str(), tr(S_RECONNECTING));
                } else if (it->speed) {
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
            } else if (it->status == Q_VERIFYING) {
                // Uncompressed files skip the unzip phase, so verify is the
                // whole wait: give it the same moving bar (bytes hashed / size)
                // rather than a static "vrfy" that reads as a hang.
                if (it->total) {
                    prog = (float)it->now / (float)it->total;
                    snprintf(info, sizeof(info), "%s",
                             human_size(it->total).c_str());
                }
            } else if (it->status == Q_EXTRACTING) {
                // Bar shows progress from archive bytes consumed; text shows just
                // the count of entries finished so far (no percent). This also
                // keeps the text static between file steps, so it stops
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
            const char *pfx = xfer_verb(it);
            char left[560];
            snprintf(left, sizeof(left), "[%s] %s", it->target, it->name);
            pu::ui::Color c = xfer_color(it);
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
            // The actively-worked item (downloading, verifying or unzipping)
            // is the "hero" row: accent background, thicker bar, shimmer.
            bool accent = (it->status == Q_DOWNLOADING ||
                           it->status == Q_VERIFYING ||
                           it->status == Q_EXTRACTING);
            // The hero row shows that game's box art instead of the console
            // icon when one's already warm in the cache (never decodes on
            // demand here -- see boxart_row_icon -- so a cold cache just
            // falls back to the console icon rather than stalling this
            // every-frame rebuild on a PNG decode).
            pu::sdl2::Texture row_icon = console_display_icon(it->target);
            if (accent) {
                pu::sdl2::Texture art = nullptr;
                if (boxart_row_icon(it->name, &art) && art) {
                    row_icon = art;
                }
            }
            this->layout->AddRow2(left, info, c, rc, prog,
                                  row_icon, pfx, accent,
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
    // Reuses qac (one active-count scan per frame, computed above) instead of
    // taking the queue mutex and rescanning QUEUE_MAX a second time.
    {
        static bool cur = false;
        bool want = g_prefs.prevent_sleep && (qac > 0);
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
    {
        // Hold-to-autopage: same shape as the D-pad hold-repeat above, but with
        // a longer initial delay and slower repeat -- a page jump moves a whole
        // screen at once, so firing at the D-pad's ~20/frame-3 rate would blow
        // past the target list in a fraction of a second. ~0.5s to first repeat,
        // then one page every ~8 frames (~7.5/sec at 60fps) until released.
        static int hold = 0;
        const bool pageable = this->screen != Screen::Queue;
        int dir = (pageable && (held & HidNpadButton_ZL))   ? -1
                  : (pageable && (held & HidNpadButton_ZR)) ? 1
                                                              : 0;
        if (dir == 0) {
            hold = 0;
        } else {
            hold++;
            if (hold > 30 && ((hold - 30) % 8) == 0) {
                if (dir < 0) {
                    this->layout->PageUp();
                } else {
                    this->layout->PageDown();
                }
            }
        }
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
            } else if ((down & HidNpadButton_X) && valid) {
                // Per-console Options (right slide): the actions tied to THIS
                // console — add a repo to it, pin/unpin it. Same menu in list and
                // card views (the card grid needs the D-pad, so pin can't stay on
                // ▶). Library-wide actions are on the Tools panel (Y).
                int ci = g_home_map[sel];
                ConsoleGroup *g = &g_cfg.consoles[ci];
                const char *full = console_full_name(g->target);
                int r = this->SideMenu(
                    full ? full : g->target,
                    {tr(S_ADD_REPO), g->pinned ? tr(S_UNPIN) : tr(S_PIN),
                     tr(S_CANCEL)});
                if (r == 0) { // Add repo
                    this->GotoPicker(Pending::AddRepo);
                } else if (r == 1) { // Pin / Unpin the console
                    g->pinned = !g->pinned;
                    config_save(&g_cfg);
                    this->GotoHome();
                    this->layout->SetSel(0);
                }
            } else if (down & HidNpadButton_Y) {
                this->ToolsMenu();
            } else if (down & HidNpadButton_Minus) {
                char q[256] = {0};
                if (prompt_raw(tr(S_SEARCH_PROMPT), nullptr, q, sizeof(q)) &&
                    q[0]) {
                    this->GotoSearch(q);
                    return;
                }
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
            if (!g_have_item) {
                break;
            }
            // With files marked, A acts on the whole selection; with none, it
            // queues the highlighted row exactly as it always has.
            if (!g_sel.empty()) {
                this->QueueSelection();
                break;
            }
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
                                    f->md5,
                                    install_folder_for(g_files_target));
                if (ok) {
                    this->Toast(std::string(tr(S_QUEUED)) + ": " + f->name);
                } else {
                    this->ToastErr(tr(S_QUEUE_FULL));
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
            // Toggle this row's selection. Only the widget mark and the info
            // line change — rebuilding the list on every press would re-add
            // thousands of rows for one keystroke.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_files.size()) {
                int fi = g_files[i];
                bool on = g_sel.count(fi) == 0;
                if (on) {
                    g_sel.insert(fi);
                } else {
                    g_sel.erase(fi);
                }
                this->layout->SetMark(i, on);
                files_info_line(this->layout.get());
            }
        } else if (down & HidNpadButton_X) {
            this->FilesViewMenu();
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
        // A Wi-Fi ROM receive that auto-jumped here is finished with B: stop
        // listening and (in auto-sort) hand the inbox to the sorter. This is the
        // one case B does something on the Queue tab.
        if (this->imp_open && this->imp_rom && (down & HidNpadButton_B)) {
            this->RomRecvFinish();
            return;
        }
        // Otherwise no B handler: the queue is a top-level tab, so B (which would
        // jump to the Browse tab) makes no sense here — tabs are on L/R.
        if (down & HidNpadButton_Minus) {
            this->GotoLog();
            return;
        } else if (down & HidNpadButton_Y) {
            // Y opens the global Tools panel here too (same as the Library tab),
            // so library/SD actions are reachable without leaving the Queue.
            this->ToolsMenu();
        } else if (down & HidNpadButton_X) {
            // The queue's options menu (X; Y opens the global Tools panel). When a
            // specific row is highlighted, its own actions come first — this is
            // where per-item Retry / Cancel now live, rather than on the A button.
            // The queue-wide batch actions follow; Pause/Cancel all get a
            // secondary confirmation (they touch the whole queue).
            static QueueView qv[QUEUE_MAX];
            int n = queue_snapshot(qv, QUEUE_MAX);
            s32 i = this->layout->Sel();
            bool have = (i >= 0 && i < n);
            QStatus is = have ? qv[i].item.status : Q_FREE;
            bool ext = have && qv[i].item.external;
            // Retry only applies to real (worker-driven) items; external pushes
            // can't be re-run. Cancel applies to anything still in flight.
            bool it_retry = have && !ext &&
                            (is == Q_FAILED || is == Q_CANCELLED || is == Q_PAUSED);
            bool it_cancel = have && (is == Q_QUEUED || is == Q_PAUSED ||
                                      is == Q_DOWNLOADING);
            std::vector<std::string> opts;
            std::vector<int> acts; // 0 retry item, 1 cancel item, 10.. batch
            if (it_retry) { opts.push_back(tr(S_RETRY)); acts.push_back(0); }
            if (it_cancel) { opts.push_back(tr(S_CANCEL_DOWNLOAD)); acts.push_back(1); }
            opts.push_back(tr(S_PAUSE_ALL));           acts.push_back(10);
            opts.push_back(tr(S_CANCEL_ALL));          acts.push_back(11);
            opts.push_back(tr(S_RETRY_ALL));           acts.push_back(12);
            opts.push_back(tr(S_RETRY_ALL_PAUSED));    acts.push_back(13);
            opts.push_back(tr(S_RETRY_ALL_CANCELLED)); acts.push_back(14);
            opts.push_back(tr(S_CLEAR_FINISHED));      acts.push_back(15);
            opts.push_back(tr(S_CANCEL));              acts.push_back(-1);
            std::string title = have ? std::string(qv[i].item.name)
                                     : std::string(tr(S_TITLE_QUEUE));
            int r = this->CreateShowDialog(title, "", opts, false, {},
                                           style_dialog);
            int act = (r >= 0 && r < (int)acts.size()) ? acts[r] : -1;
            char t[48];
            if (act == 0) { // retry this item: a paused one resumes in place from
                // its .part, a failed/cancelled one re-queues from zero.
                queue_retry(qv[i].slot);
                this->Toast(tr(S_RETRYING));
            } else if (act == 1) { // cancel this item
                if (ext) {
                    queue_ext_cancel(qv[i].slot);
                } else {
                    queue_cancel(qv[i].slot);
                }
                this->Toast(tr(S_CANCELLED));
            } else if (act == 10) {
                if (this->ConfirmDanger(tr(S_PAUSE_ALL),
                                        tr(S_PAUSE_ALL_CONFIRM))) {
                    queue_pause_all();
                    this->Toast(tr(S_PAUSED_ALL));
                }
            } else if (act == 11) {
                if (this->ConfirmDanger(tr(S_CANCEL_ALL),
                                        tr(S_CANCEL_ALL_CONFIRM))) {
                    queue_cancel_all();
                    this->Toast(tr(S_CANCELLED_ALL));
                }
            } else if (act == 12) {
                int c = queue_retry_status(Q_FAILED);
                snprintf(t, sizeof(t), tr(S_RETRIED_N), c);
                this->Toast(t);
            } else if (act == 13) {
                int c = queue_retry_status(Q_PAUSED);
                snprintf(t, sizeof(t), tr(S_RESUMED_N), c);
                this->Toast(t);
            } else if (act == 14) {
                int c = queue_retry_status(Q_CANCELLED);
                snprintf(t, sizeof(t), tr(S_RETRIED_N), c);
                this->Toast(t);
            } else if (act == 15) {
                queue_clear_finished();
                this->Toast(tr(S_CLEARED));
            }
        } else {
            static QueueView qv[QUEUE_MAX];
            int n = queue_snapshot(qv, QUEUE_MAX);
            s32 i = this->layout->Sel();
            if (i >= 0 && i < n && qv[i].item.external) {
                // External transfers are driven from outside the queue: they
                // can't be reordered or retried, but any in-flight one can be
                // cancelled. Cancel just sets the item's flag; each owner polls
                // queue_ext_cancelled and tears its own transfer down — the app
                // pulls (self-update, DAT) stop the download, a Wi-Fi/live push
                // aborts the receive (httpsrv_abort), and a USB copy ends the MTP
                // session (its only lever — the responder has no per-file abort).
                // A finished item just waits for "clear finished".
                if ((down & HidNpadButton_A) &&
                    qv[i].item.status == Q_DOWNLOADING &&
                    this->Confirm(tr(S_CANCEL),
                                  std::string(qv[i].item.name) + "?", true)) {
                    queue_ext_cancel(qv[i].slot);
                    this->Toast(tr(S_CANCELLED));
                }
            } else if (i >= 0 && i < n) {
                if (down & HidNpadButton_A) {
                    QStatus s = qv[i].item.status;
                    bool processing = (s == Q_VERIFYING ||
                                       s == Q_AWAIT_EXTRACT ||
                                       s == Q_EXTRACTING);
                    if (processing) {
                        // Verify/unzip can appear to hang; rather than force a
                        // quit, offer the three ways forward. Retry keeps the
                        // downloaded file and re-runs the check/unpack;
                        // Redownload drops it and pulls the file again.
                        int r = this->SideMenu(
                            std::string(qv[i].item.name),
                            {tr(S_RETRY), tr(S_REDOWNLOAD), tr(S_CANCEL_DOWNLOAD),
                             tr(S_CANCEL)},
                            0, tr(S_QUEUE_BUSY_PROMPT));
                        if (r == 0) {
                            queue_requeue(qv[i].slot, false);
                            this->Toast(tr(S_RETRYING));
                        } else if (r == 1) {
                            queue_requeue(qv[i].slot, true);
                            this->Toast(tr(S_RETRYING));
                        } else if (r == 2) {
                            queue_cancel(qv[i].slot);
                            this->Toast(tr(S_CANCELLED));
                        }
                    } else {
                        // A cancels an in-flight item (with a confirm). Per-item
                        // Retry now lives in the X options menu, not here.
                        bool cancellable = (s == Q_QUEUED || s == Q_PAUSED ||
                                            s == Q_DOWNLOADING);
                        if (cancellable &&
                            this->Confirm(tr(S_CANCEL),
                                          std::string(qv[i].item.name) + "?",
                                          true)) {
                            queue_cancel(qv[i].slot);
                            this->Toast(tr(S_CANCELLED));
                        }
                    }
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
        // Settings is a top-level tab, not a sub-screen: leaving it is L/R (or a
        // tab tap), so B does nothing here. (B still returns from every Settings
        // sub-screen below, where it is a real Back.)
        if (down & HidNpadButton_Y) {
            // Y opens the global Tools panel (same as Library/Queue).
            this->ToolsMenu();
        } else if (down & HidNpadButton_A) {
            // Every top-level row opens a single-concern sub-screen; the row
            // order here is the contract with GotoSettings' kEntries.
            switch (this->layout->Sel()) {
            case 0: this->GotoAppearance();  return;
            case 1: this->GotoDlPrefs();     return;
            case 2: this->GotoStorage();     return;
            case 3: this->GotoDataFiles();   return; // DAT files + metadata cache
            case 4: this->GotoTransfers();   return; // PC Sync (hosts Install from PC)
            case 5: this->GotoAccount();     return;
            case 6: this->GotoUpdates();     return;
            case 7: this->GotoViewLogs();    return;
            case 8: this->GotoDiagnostics(); return;
            case 9: this->GotoHelp();        return; // Getting Started/How-To/Troubleshooting
            case 10: this->GotoAbout();      return; // About = credits page
            default: break;
            }
        }
        break;
    }

    case Screen::DlPrefs: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
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
                g_prefs.ex_prealloc = !g_prefs.ex_prealloc;
                apply_extract_tunables();
                prefs_save(&g_prefs);
                break;
            case 5:
                g_prefs.ex_chunk_mb = ex_chunk_step(g_prefs.ex_chunk_mb, +1);
                apply_extract_tunables();
                prefs_save(&g_prefs);
                break;
            case 6:
                g_prefs.keep_archives = !g_prefs.keep_archives;
                queue_set_keep_archives(g_prefs.keep_archives);
                prefs_save(&g_prefs);
                break;
            // Row 7 is the post-import converter when present (hook registered),
            // else it's skip-installed (the last row shifts up to fill the gap).
            case 7:
                if (queue_post_import) {
                    g_prefs.convert_import = !g_prefs.convert_import;
                    queue_set_post_import_enabled(g_prefs.convert_import);
                } else {
                    g_prefs.skip_installed = !g_prefs.skip_installed;
                }
                prefs_save(&g_prefs);
                break;
            case 8: // skip-installed, only a distinct row when row 7 is the converter
                if (queue_post_import) {
                    g_prefs.skip_installed = !g_prefs.skip_installed;
                    prefs_save(&g_prefs);
                }
                break;
            default:
                break;
            }
            if (this->screen == Screen::DlPrefs) {
                s32 sel = this->layout->Sel();
                this->GotoDlPrefs();
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
            } else if (i == 6) {
                g_prefs.ex_chunk_mb = ex_chunk_step(g_prefs.ex_chunk_mb, dir);
                apply_extract_tunables();
            } else {
                changed = false;
            }
            if (changed) {
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoDlPrefs();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::RomPicker: {
        // Apply a chosen ROM root (empty string = reset to auto), then return
        // to Manage data. queue.c holds a pointer into g_tico.roms_path, so
        // rewriting that buffer takes effect without restarting the queue.
        bool per_console = (this->picker_console >= 0 &&
                            this->picker_console < g_cfg.console_count);
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
            this->GotoStorage(); // ROM folder lives under Storage now
        };
        // Where to land after finishing (or backing out of) a per-console pick:
        // the Installed tab when the picker was opened from there, otherwise the
        // Storage per-console folder list. Reselects the edited console either
        // way. Clears the from-Installed flag so it can't leak into a later pick.
        auto return_from_console = [&](int ci) {
            bool from_inst = this->picker_from_installed;
            std::string nm = g_cfg.consoles[ci].target;
            this->picker_console = -1;
            this->picker_from_installed = false;
            if (from_inst) {
                this->GotoInstalled(roms_root(&g_tico));
                for (s32 k = 0; k < (s32)g_inst.size(); k++) {
                    if (g_inst[k].name == nm) {
                        this->layout->SetSel(k);
                        break;
                    }
                }
            } else {
                this->GotoInstallFolders();
                this->layout->SetSel(ci); // keep the cursor on the edited console
            }
        };
        // Set (or, with chosen=="", clear) this console's custom install folder,
        // then return to wherever the pick was launched from.
        auto apply_console = [&](const char *chosen) {
            char norm[512];
            roms_normalize_path(chosen, norm, sizeof(norm));
            int ci = this->picker_console;
            snprintf(g_cfg.consoles[ci].folder,
                     sizeof(g_cfg.consoles[ci].folder), "%s", norm);
            config_save(&g_cfg);
            this->Toast(norm[0] ? tr(S_INSTALL_FOLDER_SET)
                                : tr(S_INSTALL_FOLDER_CLEARED));
            return_from_console(ci);
        };
        bool at_root = (this->picker_path == "sdmc:/");
        if (down & HidNpadButton_B) {
            if (at_root) {
                if (per_console) {
                    return_from_console(this->picker_console);
                } else {
                    this->GotoStorage();
                }
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
            // Use the folder currently being browsed.
            if (at_root) {
                this->ToastErr(tr(S_ROMS_USE_ROOT_WARN));
            } else if (per_console) {
                if (this->Confirm(tr(S_INSTALL_FOLDER),
                                  wrap_for_dialog(this->picker_path) + "\n\n" +
                                      tr(S_INSTALL_FOLDER_WARN))) {
                    apply_console(this->picker_path.c_str());
                }
            } else if (this->Confirm(tr(S_ROMS_OVERRIDE_TITLE),
                                     wrap_for_dialog(this->picker_path) + "\n\n" +
                                         tr(S_ROMS_OVERRIDE_WARN))) {
                apply_roms(this->picker_path.c_str());
            }
        } else if (down & HidNpadButton_Y) {
            // Reset to the default: the ROM root, or this console's default
            // <ROM root>/<console> folder.
            if (per_console) {
                apply_console("");
            } else {
                apply_roms("");
            }
        }
        break;
    }

    case Screen::Appearance: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
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
                this->GotoAccent();
                return;
            case 3:
                this->GotoLanguage();
                return;
            case 4:
                g_prefs.group_consoles = !g_prefs.group_consoles;
                prefs_save(&g_prefs);
                break;
            case 5: this->GotoExtFilter(); return; // file-type filter editor
            case 6: // collapse a disc set into one Library row
                g_prefs.group_sets = !g_prefs.group_sets;
                prefs_save(&g_prefs);
                break;
            case 7: // show/fetch box art in the list
                g_prefs.box_art_enabled = !g_prefs.box_art_enabled;
                prefs_save(&g_prefs);
                if (g_prefs.box_art_enabled && !g_creds.steamgriddb_key[0]) {
                    this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
                }
                break;
            case 8: // auto-fetch art for newly landed games
                g_prefs.box_art_auto_fetch = !g_prefs.box_art_auto_fetch;
                prefs_save(&g_prefs);
                if (g_prefs.box_art_auto_fetch && !g_creds.steamgriddb_key[0]) {
                    this->ToastErr(tr(S_SCAN_BOX_ART_NEED_KEY));
                }
                break;
            case 9: this->GotoManage();    return; // consoles: show/hide
            default:
                break;
            }
            if (this->screen == Screen::Appearance) {
                s32 sel = this->layout->Sel();
                this->GotoAppearance();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::ExtFilter: {
        int n = g_prefs.exclude_ext_count;
        // Prompt for and append a new extension. Shared by A (on the "Add
        // extension" row) and Y (from anywhere, so the user needn't scroll to
        // the bottom of the list to reach it).
        auto add_ext = [this]() {
            char ext[16] = {0};
            if (prompt_raw(tr(S_ADD_EXT_PROMPT), nullptr, ext, sizeof(ext)) &&
                ext[0]) {
                if (prefs_ext_add(&g_prefs, ext)) {
                    prefs_save(&g_prefs);
                } else {
                    this->ToastErr(tr(S_EXT_ADD_FAILED));
                }
            }
        };
        if (down & HidNpadButton_B) {
            this->GotoAppearance(); // extension filter lives under Appearance now
        } else if (down & HidNpadButton_Y) { // add from any row
            add_ext();
            if (this->screen == Screen::ExtFilter) {
                // Land the cursor on the freshly added extension (last one).
                this->GotoExtFilter();
                this->layout->SetSel(g_prefs.exclude_ext_count);
            }
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
                add_ext();
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
            this->GotoAppearance();
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

    case Screen::Accent: {
        if (down & HidNpadButton_B) {
            this->GotoAppearance();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < g_accent_count) {
                snprintf(g_prefs.accent, sizeof(g_prefs.accent), "%s",
                         g_accents[i].key);
                prefs_save(&g_prefs);
                // Same refresh path as the Theme toggle: recolors every
                // ring/glow/progress bar/pulse dot already on screen.
                this->layout->ApplyTheme();
                this->SyncTab();
                this->GotoAccent();
                this->layout->SetSel(i);
            }
        }
        break;
    }

    case Screen::Transfers: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->GotoUsbMtp(true);  return; // install from USB connection
            case 1: this->GotoRecvConsole(); return; // install from Wi-Fi
            case 2: this->ImportStart();    return; // receive dl_sources.json
            case 3: this->ExportStart();    return; // serve dl_sources.json
            case 4: this->RestoreBackup();  return; // restore previous
            case 5: // toggle the read-only inventory server
                g_prefs.inv_server = !g_prefs.inv_server;
                prefs_save(&g_prefs);
                if (g_prefs.inv_server) {
                    this->InvServerStart();
                } else {
                    this->InvServerStop();
                }
                this->GotoTransfers(); // re-render toggle + address
                return;
            case 6: return; // read-only address line
            case 7: { // toggle full SD card access
                if (!g_prefs.sd_full_access) {
                    // Only confirm on the way IN: this is real, unscoped
                    // filesystem access (read/write/delete anywhere on the
                    // card, over Wi-Fi and USB), not a routine preference.
                    int cr = this->CreateShowDialog(
                        tr(S_SD_FULL_ACCESS), tr(S_SD_FULL_ACCESS_CONFIRM),
                        {tr(S_YES), tr(S_CANCEL)}, true, {}, style_dialog);
                    if (cr != 0) return;
                }
                g_prefs.sd_full_access = !g_prefs.sd_full_access;
                prefs_save(&g_prefs);
                // Live for the Wi-Fi inventory server (its client struct just
                // reads this flag per-request); a USB session already in
                // progress keeps the folder set it started with and picks
                // this up on the next connect, same as mtp_enabled.
                if (this->inv_open) {
                    this->inv_srv.sd_access = g_prefs.sd_full_access;
                }
                this->GotoTransfers(); // re-render toggle
                return;
            }
            default: break;
            }
        }
        break;
    }

    case Screen::RecvConsole: {
        if (down & HidNpadButton_B) {
            this->GotoTransfers();
            this->layout->SetSel(1);
        } else if (down & HidNpadButton_A) {
            // Row 0 is "Inbox (auto-sort)"; rows 1.. are g_cfg.consoles in order,
            // so a console selection is (row - 1). (An empty list shows a single
            // "no consoles" row that maps to no console and does nothing.)
            s32 i = this->layout->Sel();
            if (g_cfg.console_count > 0 && i == 0) {
                this->RomRecvStart(0, true, true); // any console -> inbox + sort
            } else if (g_cfg.console_count > 0 && i >= 1 &&
                       i - 1 < g_cfg.console_count) {
                this->RomRecvStart((int)i - 1, true);
            }
        }
        break;
    }

    case Screen::Sources: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->GotoManage();    return; // consoles/repos
            case 1: this->GotoExtFilter(); return; // file-type filter editor
            default: break;
            }
        }
        break;
    }

    case Screen::Storage: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->StorageDetail(); return; // SD used/free breakdown
            case 1: { // ROM folder — browse the SD card and pick a folder
                std::string start = "sdmc:/";
                if (g_prefs.roms_override[0] &&
                    fs_exists(g_prefs.roms_override)) {
                    start = g_prefs.roms_override;
                }
                this->picker_console = -1; // picking the ROM root, not a console
                this->picker_from_installed = false;
                this->GotoRomPicker(start);
                return;
            }
            case 2: // Install-folder mode: single ROM folder vs per-console
                g_prefs.custom_folders = !g_prefs.custom_folders;
                prefs_save(&g_prefs);
                break;
            case 3: // Per-console folders (only when custom mode is on)
                if (g_prefs.custom_folders) {
                    this->GotoInstallFolders();
                    return;
                }
                this->Toast(tr(S_CONSOLE_FOLDERS_LOCKED));
                return;
            case 4: this->GotoDownloads(); return; // download scratch folder
            case 5: this->GotoInboxFiles(); return; // view/select/delete Inbox files
            case 6: this->GotoBackups(); return;   // emulator/app rollback backups
            case 7: this->LargeFilesStart(); return; // whole-library biggest files
            case 8: this->BoxArtManageStart(); return; // view/delete cached covers
            default: break;
            }
            if (this->screen == Screen::Storage) {
                s32 sel = this->layout->Sel();
                this->GotoStorage();
                this->layout->SetSel(sel);
            }
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            s32 sel = this->layout->Sel();
            if (sel == 2) { // Install-folder mode
                g_prefs.custom_folders = !g_prefs.custom_folders;
                prefs_save(&g_prefs);
            } else {
                break;
            }
            this->GotoStorage();
            this->layout->SetSel(sel);
        }
        break;
    }

    case Screen::Dats: {
        if (down & HidNpadButton_B) {
            this->GotoDataFiles(); // DAT files nests under Data Files now
            this->layout->SetSel(0);
        } else if (down & HidNpadButton_A) {
            if (this->layout->Sel() == 0) { // download/refresh from git
                this->DatSyncStart();
                return;
            }
            if (this->layout->Sel() == 1) { // receive one from a PC
                // No console picker: the receiver reads the DAT's own header and
                // files it under the console it catalogs (see DatApply).
                this->DatRecvStart();
                return;
            }
            if (this->layout->Sel() == 2) { // 1G1R region priority
                this->GotoRegionOrder();
                return;
            }
        }
        break;
    }

    case Screen::DataFiles: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
            this->layout->SetSel(3);
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->GotoDats();      return; // DAT files manager
            case 1: this->GotoMetaCache(); return; // metadata cache manager
            case 2: this->PushListToPc();  return; // push emulator/app list to PC
            case 3: // box-art cache: browse or clear
                this->ArtCacheMenu();
                // "Browse" navigates to Manage Box Art itself (screen already
                // changed, leave it be); "Clear"/cancel stay right here, so
                // refresh in place — otherwise the row's count/size goes
                // stale until the screen is re-entered.
                if (this->screen == Screen::DataFiles) {
                    this->GotoDataFiles();
                    this->layout->SetSel(3);
                }
                return;
            default: break;
            }
        }
        break;
    }

    case Screen::MetaCache: {
        if (down & HidNpadButton_B) {
            this->GotoDataFiles();
            this->layout->SetSel(1);
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: // Metadata cache on/off
                g_prefs.use_cache = !g_prefs.use_cache;
                prefs_save(&g_prefs);
                break;
            case 1: this->GotoCache(); return; // browse/clear the cache
            case 2: this->RaStart();   return; // refresh all metadata
            default: break;
            }
            if (this->screen == Screen::MetaCache) {
                s32 sel = this->layout->Sel();
                this->GotoMetaCache();
                this->layout->SetSel(sel);
            }
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            if (this->layout->Sel() == 0) {
                g_prefs.use_cache = !g_prefs.use_cache;
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoMetaCache();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::RegionOrder: {
        if (down & HidNpadButton_B) {
            this->GotoDats();
            this->layout->SetSel(2);
        } else if (down & HidNpadButton_A) {
            // Promote the selected region: swap it with the one above. The top
            // row is already most-preferred, so pressing A there is a no-op.
            s32 i = this->layout->Sel();
            char order[5];
            strncpy(order, g_prefs.region_order[0] ? g_prefs.region_order : "WUEJ",
                    4);
            order[4] = '\0';
            if (i > 0 && i < 4) {
                char t = order[i];
                order[i] = order[i - 1];
                order[i - 1] = t;
                strcpy(g_prefs.region_order, order);
                prefs_save(&g_prefs);
                s32 keep = i - 1;
                this->GotoRegionOrder();
                this->layout->SetSel(keep);
            }
        }
        break;
    }

    case Screen::InstallFolders: {
        if (down & HidNpadButton_B) {
            this->GotoStorage();
            this->layout->SetSel(3); // land back on the "Console folders" row
        } else if ((down & HidNpadButton_A) &&
                   this->layout->Sel() < g_cfg.console_count) {
            // Choose this console's install folder. Start browsing at its current
            // custom folder if it still exists, else the SD root.
            int ci = this->layout->Sel();
            this->picker_console = ci;
            this->picker_from_installed = false;
            std::string start = "sdmc:/";
            const char *f = g_cfg.consoles[ci].folder;
            if (f[0] && fs_exists(f)) start = f;
            this->GotoRomPicker(start);
        }
        break;
    }

    case Screen::Backups: {
        // Rollback builds the update manager kept: A deletes the highlighted one,
        // X clears them all, Y marks a set for Minus to delete together. B
        // returns to Storage on the backups row.
        if (down & HidNpadButton_B) {
            this->GotoStorage();
            this->layout->SetSel(6);
        } else if (down & HidNpadButton_Y) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)this->backup_rows.size()) {
                this->layout->ToggleMark(i);
            }
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)this->backup_rows.size() &&
                this->Confirm(tr(S_DELETE), this->backup_rows[i].second, true)) {
                remove(this->backup_rows[i].first.c_str());
                this->Toast(tr(S_DELETED));
                s32 keep = i;
                this->GotoBackups();
                if (keep >= (s32)this->backup_rows.size()) {
                    keep = (s32)this->backup_rows.size() - 1;
                }
                if (keep >= 0) this->layout->SetSel(keep);
            }
        } else if (down & HidNpadButton_X) {
            if (!this->backup_rows.empty() &&
                this->ConfirmDanger(tr(S_CLEAR_BACKUPS),
                                    tr(S_CLEAR_BACKUPS_CONFIRM))) {
                for (const auto &b : this->backup_rows) {
                    remove(b.first.c_str());
                }
                this->Toast(tr(S_CLEARED));
                this->GotoBackups();
            }
        } else if (down & HidNpadButton_Minus) {
            // Multi-select delete: only the marked set (Ⓨ). Nothing marked ->
            // no-op here, since Ⓐ already covers a single delete under cursor.
            int mc = this->layout->MarkedCount();
            if (mc == 0) break;
            auto marks = this->layout->Marked();
            char msg[128];
            snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), mc);
            if (this->ConfirmDanger(tr(S_DELETE), msg)) {
                for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
                    s32 idx = *it;
                    if (idx >= 0 && idx < (s32)this->backup_rows.size()) {
                        remove(this->backup_rows[idx].first.c_str());
                    }
                }
                char t[32];
                snprintf(t, sizeof(t), tr(S_DELETED_N), mc);
                this->Toast(t);
                this->GotoBackups();
            }
        }
        break;
    }

    case Screen::Account: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->GotoCreds(); return; // archive.org credentials
            case 1: {                          // GitHub API token (raises the
                                               // update-check rate limit)
                char v[1024] = {0};
                if (prompt(tr(S_GITHUB_TOKEN), g_creds.github_token, v,
                           sizeof(v))) {
                    snprintf(g_creds.github_token, sizeof(g_creds.github_token),
                             "%s", v);
                    net_set_github_token(g_creds.github_token);
                    if (creds_save(&g_creds)) {
                        this->Toast(tr(S_SAVED));
                    } else {
                        this->ToastErr(tr(S_SAVE_FAILED));
                    }
                }
                break;
            }
            case 2: {                          // SteamGridDB API key (box art)
                char v[1024] = {0};
                if (prompt(tr(S_STEAMGRIDDB_KEY), g_creds.steamgriddb_key, v,
                           sizeof(v))) {
                    snprintf(g_creds.steamgriddb_key,
                             sizeof(g_creds.steamgriddb_key), "%s", v);
                    net_set_steamgriddb_key(g_creds.steamgriddb_key);
                    if (creds_save(&g_creds)) {
                        this->Toast(tr(S_SAVED));
                    } else {
                        this->ToastErr(tr(S_SAVE_FAILED));
                    }
                }
                break;
            }
            default: break;
            }
            if (this->screen == Screen::Account) {
                s32 sel = this->layout->Sel();
                this->GotoAccount();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::Updates: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->UpdateWifiStart(); return; // receive a pushed .nro
            case 1: // Check GitHub for a new HaulNX build, straight away.
                this->ChkStart(); // background thread; retries won't freeze UI
                return;
            case 2: // emulator updates
                this->appman_sel = 0;
                this->GotoAppUpdates(UPD_KIND_EMU);
                return;
            case 3: // app updates
                this->appman_sel = 0;
                this->GotoAppUpdates(UPD_KIND_APP);
                return;
            case 4: this->GotoReleaseNotes(); return; // version history
            case 5: // Check for updates on startup
                g_prefs.chk_updates = !g_prefs.chk_updates;
                prefs_save(&g_prefs);
                break;
            default: break;
            }
            if (this->screen == Screen::Updates) {
                s32 sel = this->layout->Sel();
                this->GotoUpdates();
                this->layout->SetSel(sel);
            }
        } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            if (this->layout->Sel() == 5) {
                g_prefs.chk_updates = !g_prefs.chk_updates;
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoUpdates();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::AppUpdates: {
        // Emulator/app list. It loads with versions only (no auto-scan); the user
        // checks for updates explicitly: X checks every entry, Y checks the
        // selected one, A opens that entry's action menu (which also has a "Check
        // for updates" option). B goes back to Updates.
        if (down & HidNpadButton_B) {
            this->layout->ClearEmptyState();
            this->GotoUpdates();
        } else if (down & HidNpadButton_X) {
            this->AppScanAll(); // check them all against GitHub
        } else if (down & HidNpadButton_Y) {
            s32 sel = this->layout->Sel();
            if (sel >= 0 && sel < (s32)this->appman_list.size()) {
                this->AppRecheckOne(sel); // check just this one
            }
        } else if (down & HidNpadButton_A) {
            s32 sel = this->layout->Sel();
            if (sel >= 0 && sel < (s32)this->appman_list.size()) {
                this->appman_sel = sel; // restored by the render below
                bool changed = this->AppEntryMenu((size_t)sel);
                // "Check for updates", a source edit, or a revert warrants a fresh
                // network check, and only for that one entry. A plain cancel keeps
                // the cached list (no re-pull). An install/update jumps to the
                // Queue tab, so the screen check below skips the rebuild entirely.
                if (this->screen == Screen::AppUpdates) {
                    if (changed) {
                        this->AppRecheckOne(sel);
                    } else {
                        this->AppUpdatesRender();
                    }
                }
            }
        }
        break;
    }

    case Screen::About: {
        // Credits page: B returns to Settings, A opens the release-notes history.
        if (down & HidNpadButton_B) {
            this->layout->ClearEmptyState();
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            this->layout->ClearEmptyState();
            this->GotoReleaseNotes();
        }
        break;
    }

    case Screen::Help: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            s32 sel = this->layout->Sel();
            if (sel == 3) {
                this->GotoHelpSearch();
            } else {
                this->ShowHelpCategory(sel);
            }
        }
        break;
    }

    case Screen::HelpTopics: {
        if (down & HidNpadButton_B) {
            this->GotoHelp();
        } else if (down & HidNpadButton_A) {
            s32 sel = this->layout->Sel();
            if (this->help_cat == 0) {
                if (sel == 0) {
                    this->GuidedTour();
                    return; // GuidedTour leaves the Help screens entirely
                }
                sel--; // row 0 was the "replay tour" action, not an article
            }
            this->ShowHelpArticle(this->help_cat, sel);
        }
        break;
    }

    case Screen::HelpArticle: {
        if (down & HidNpadButton_B) {
            this->layout->ClearEmptyState();
            this->ShowHelpCategory(this->help_cat);
        }
        break;
    }

    case Screen::HelpSearch: {
        if (down & HidNpadButton_B) {
            this->GotoHelp();
        } else if (down & HidNpadButton_A) {
            s32 sel = this->layout->Sel();
            if (sel >= 0 && sel < (s32)this->help_hits.size()) {
                const auto &h = this->help_hits[sel];
                this->ShowHelpArticle(h.first, h.second);
            }
        } else if (down & HidNpadButton_Y) {
            char q[128] = {0};
            if (prompt_raw(tr(S_HELP_SEARCH_GUIDE), this->help_query.c_str(), q,
                           sizeof(q)) &&
                q[0]) {
                this->RunHelpSearch(q);
            }
        }
        break;
    }

    case Screen::Diagnostics: {
        if (down & HidNpadButton_B) {
            this->GotoSettings();
        } else if (down & HidNpadButton_A) {
            switch (this->layout->Sel()) {
            case 0: this->SpeedTest();     return; // background download test
            case 1: this->NetSelfTest();   return; // background LAN + net check
            case 2:                                // warn on startup if offline
                g_prefs.net_check = !g_prefs.net_check;
                prefs_save(&g_prefs);
                break;
            case 3:                                // benchmark extraction toggle
                g_prefs.ex_bench = !g_prefs.ex_bench;
                apply_extract_tunables();
                prefs_save(&g_prefs);
                break;
            case 4:                                // USB file transfer (MTP) toggle
                g_prefs.mtp_enabled = !g_prefs.mtp_enabled;
                prefs_save(&g_prefs);
                // Turning it off tears down a link in progress immediately,
                // same as an unplug -- otherwise a transfer that started before
                // the toggle would keep running until the cable came out.
                if (!g_prefs.mtp_enabled && (this->usb_open || this->usb_bg)) {
                    this->UsbMtpStop();
                    if (this->screen == Screen::UsbMtp) this->UsbMtpReturn();
                }
                break;
            case 5: return;                        // USB 3.0 status: read-only
            case 6: this->ResetDefaults(); return; // factory-reset settings — bottom
            default: break;
            }
            if (this->screen == Screen::Diagnostics) {
                s32 sel = this->layout->Sel();
                this->GotoDiagnostics();
                this->layout->SetSel(sel);
            }
        }
        break;
    }

    case Screen::ViewLogs: {
        if (down & HidNpadButton_B) {
            this->GotoSettings(); // Logs is a top-level Settings section now
        } else if (down & HidNpadButton_A) {
            // Rows 6+ are dynamic: the "View debug bundle" row only exists once a
            // bundle has been exported, so compute where the tail rows landed.
            bool has_bundle = fs_exists(DIAG_BUNDLE_PATH);
            s32 sel = this->layout->Sel();
            s32 view_bundle_row = has_bundle ? 6 : -1;
            s32 clear_row = has_bundle ? 7 : 6;
            switch (sel) {
            case 0: this->GotoLog(); return;          // download history
            case 1: this->GotoDebugLog(); return;     // debug.log
            case 2: this->GotoQueueState(); return;   // queue.json
            case 3: this->GotoXferLog(); return;      // transfers.log
            case 4:                                    // speedtest.log
                this->GotoTextLog(SPEEDLOG_PATH, tr(S_TITLE_SPEEDTEST_LOG),
                                  S_CLEAR_SPEEDTEST_CONFIRM);
                return;
            case 5:                                    // export debug bundle
                this->ExportBundle();
                this->GotoViewLogs(); // re-render so the "view" row appears
                this->layout->SetSel(5);
                return;
            default: break;
            }
            if (sel == view_bundle_row) {
                this->GotoTextLog(DIAG_BUNDLE_PATH, tr(S_VIEW_BUNDLE),
                                  S_CLEAR_ALL_LOGS_CONFIRM);
                return;
            }
            if (sel == clear_row) {
                if (this->ConfirmDanger(tr(S_CLEAR_ALL_LOGS),
                                        tr(S_CLEAR_ALL_LOGS_CONFIRM))) {
                    // Every log file (queue.json is live app state, not a log,
                    // so it's left alone).
                    remove(LOG_PATH);
                    remove(XFERLOG_PATH);
                    remove(SPEEDLOG_PATH);
                    remove(DLLOG_PATH);
                    remove(DLLOG_JSON);
                    remove(EXBENCH_PATH);
                    remove(DIAG_BUNDLE_PATH);
                    this->Toast(tr(S_LOG_CLEARED));
                    this->GotoViewLogs();
                }
                return;
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
            // Return to wherever the notes were opened from: the About/credits
            // page (A there), else Settings › Updates.
            if (this->notes_origin == Screen::About) {
                this->GotoAbout();
            } else {
                this->GotoUpdates();
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
            this->GotoStorage();
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
        } else if (down & HidNpadButton_Right) {
            // ▶ deletes -- same direct-button shortcut as the Library's file
            // list (InstDeleteSel), so a marked set or the single row under
            // the cursor deletes the same way in every "view/manage files"
            // screen under Storage, not just Installed.
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

    case Screen::InboxFiles: {
        if (down & HidNpadButton_B) {
            this->GotoStorage();
            this->layout->SetSel(5);
        } else if (down & HidNpadButton_Y) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inbox_mfiles.size()) {
                this->layout->ToggleMark(i);
            }
        } else if (down & HidNpadButton_A) {
            // File info (A = open/inspect everywhere; never destructive).
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inbox_mfiles.size()) {
                this->CreateShowDialog(
                    tr(S_FILE),
                    g_inbox_mfiles[i].name + "\n" +
                        human_size(g_inbox_mfiles[i].size),
                    {tr(S_OK)}, true, {}, style_dialog);
            }
        } else if (down & HidNpadButton_X) {
            // Delete all (X = bulk destructive action, same as Log/Cache/Downloads)
            if (g_inbox_mfiles.empty()) break;
            char dmsg[256];
            snprintf(dmsg, sizeof(dmsg), tr(S_DELETE_ALL_INBOX_CONFIRM),
                     (int)g_inbox_mfiles.size());
            if (this->ConfirmDanger(tr(S_DELETE_ALL), dmsg)) {
                for (auto &e : g_inbox_mfiles) {
                    std::string fp = std::string(INBOX_DIR) + "/" + e.name;
                    remove(fp.c_str());
                }
                this->inv_last_gen_ns = 0; // Inbox count changed; refresh
                this->Toast(tr(S_INBOX_CLEARED));
                this->GotoInboxFiles();
            }
        } else if (down & HidNpadButton_Right) {
            // ▶ deletes -- see the matching comment in Screen::Downloads.
            int mc = this->layout->MarkedCount();
            if (mc > 0) {
                // Multi-select delete
                auto marks = this->layout->Marked();
                char msg[128];
                snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), mc);
                if (this->ConfirmDanger(tr(S_DELETE), msg)) {
                    for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
                        s32 idx = *it;
                        if (idx >= 0 && idx < (s32)g_inbox_mfiles.size()) {
                            std::string fp = std::string(INBOX_DIR) + "/" +
                                            g_inbox_mfiles[idx].name;
                            remove(fp.c_str());
                        }
                    }
                    this->inv_last_gen_ns = 0;
                    char t[32];
                    snprintf(t, sizeof(t), tr(S_DELETED_N), mc);
                    this->Toast(t);
                    this->GotoInboxFiles();
                }
            } else {
                // Single delete (nothing marked): the row under the cursor
                s32 i = this->layout->Sel();
                if (i >= 0 && i < (s32)g_inbox_mfiles.size()) {
                    std::string msg = std::string(tr(S_DELETE)) + " '" +
                                      g_inbox_mfiles[i].name + "'?";
                    if (this->ConfirmDanger(tr(S_DELETE), msg)) {
                        std::string fp = std::string(INBOX_DIR) + "/" +
                                        g_inbox_mfiles[i].name;
                        remove(fp.c_str());
                        this->inv_last_gen_ns = 0;
                        this->Toast(tr(S_DELETED));
                        s32 keep = i;
                        this->GotoInboxFiles();
                        if (keep >= (s32)g_inbox_mfiles.size())
                            keep = (s32)g_inbox_mfiles.size() - 1;
                        if (keep >= 0) this->layout->SetSel(keep);
                    }
                }
            }
        }
        break;
    }

    case Screen::Cache: {
        if (down & HidNpadButton_B) {
            this->GotoMetaCache(); // cache browser nests under Manage metadata cache now
            this->layout->SetSel(1);
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
        } else if (down & HidNpadButton_Right) {
            // ▶ deletes -- see the matching comment in Screen::Downloads.
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
        if ((down & HidNpadButton_B) &&
            this->inst_path != roms_root(&g_tico)) {
            // B backs out of a folder toward the console list. At the top level
            // (the console list itself) B does nothing — there's no sensible
            // "back" from a tab root, and the tabs are on L/R.
            if (inst_is_console_root(this->inst_path)) {
                // A console folder (incl. a custom one outside the ROM root):
                // step back to the console list, not the filesystem parent.
                this->GotoInstalled(roms_root(&g_tico));
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
                    this->GotoInstalled(inst_entry_path(this->inst_path,
                                                        g_inst[i]));
                    break;
                }
                // "Move up one folder" is offered only from a *sub*folder
                // (roms/<console>/<sub>/…), never from a console folder itself:
                // files belong in the console folder, so we don't let them
                // escape upward into the roms root.
                std::string root = roms_root(&g_tico);
                // Only a real subfolder can move up: never the ROM root and
                // never a console folder itself (a custom-folder console counts
                // as a console folder even though it sits outside the root).
                bool can_move = this->inst_path != root &&
                                !inst_is_console_root(this->inst_path);
                // With files marked (Y) in a movable subfolder, the dialog lists
                // the whole selection and moves it as a batch; otherwise it acts
                // on the single file under the cursor.
                bool multi = can_move && this->layout->MarkedCount() > 0;
                // Box art is a per-title thing, so it only makes sense for
                // the single game under the cursor — never the batch/marked
                // selection above, which can span several different games.
                std::string art_title =
                    multi ? std::string() : boxart_query_title(g_inst[i].name);
                bool has_art = !art_title.empty() &&
                              boxart_lookup(art_title.c_str(), nullptr, 0);

                std::string content;
                std::vector<std::string> targets;
                if (multi) {
                    // Expand any grouped row first, so the header counts — and
                    // the move acts on — real files rather than rows.
                    for (s32 idx : this->layout->Marked()) {
                        if (idx < 0 || idx >= (s32)g_inst.size()) continue;
                        const DirEnt &e = g_inst[idx];
                        if (e.group_members.empty()) targets.push_back(e.name);
                        else targets.insert(targets.end(),
                                            e.group_members.begin(),
                                            e.group_members.end());
                    }
                    char hdr[96];
                    snprintf(hdr, sizeof(hdr), tr(S_MOVE_UP_MULTI),
                             (int)targets.size());
                    content = hdr;
                    for (size_t n = 0; n < targets.size(); n++) {
                        if (n < 12)       content += "\n• " + targets[n];
                        else if (n == 12) content += "\n…";
                        else break;
                    }
                } else if (!g_inst[i].group_members.empty()) {
                    // A whole game: there is no single path to show, so the
                    // count and total stand in for it, then the pieces.
                    char cnt[64], szline[64];
                    snprintf(cnt, sizeof(cnt), tr(S_GROUP_FILE_COUNT),
                             (int)g_inst[i].group_members.size());
                    snprintf(szline, sizeof(szline), tr(S_SIZE_LABEL),
                             human_size(g_inst[i].size).c_str());
                    content = "Name: " + g_inst[i].name + "\n" + cnt + "\n" +
                              szline;
                    for (const std::string &m : g_inst[i].group_members)
                        content += "\n• " + m;
                    targets = g_inst[i].group_members;
                } else {
                    // Console this file sits under: the root-relative folder
                    // name, or the owning console for a custom install folder.
                    std::string cons;
                    std::string root = roms_root(&g_tico);
                    if (this->inst_path.rfind(root, 0) == 0) {
                        std::string rel = this->inst_path.substr(root.size());
                        while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
                        cons = rel.substr(0, rel.find('/'));
                    } else if (ConsoleGroup *cg =
                                   console_owning_path(this->inst_path)) {
                        cons = cg->target;
                    }
                    const char *cfull =
                        cons.empty() ? nullptr : console_full_name(cons.c_str());
                    char szline[64];
                    snprintf(szline, sizeof(szline), tr(S_SIZE_LABEL),
                             human_size(g_inst[i].size).c_str());
                    // Detail order: console, name, path, size.
                    if (!cons.empty()) {
                        char cline[160];
                        snprintf(cline, sizeof(cline), tr(S_CONSOLE_PREFIX),
                                 cfull ? cfull : cons.c_str());
                        content = std::string(cline) + "\n";
                    }
                    content += "Name: " + g_inst[i].name +
                               "\nPath: " + this->inst_path + "\n" + szline;
                    targets.push_back(g_inst[i].name);
                }
                // OK stays the highlighted default (index 0); "Move up one
                // folder" (only from a movable subfolder) and "Open settings"
                // follow it. B cancels and does nothing.
                std::vector<std::string> opts = {tr(S_OK)};
                int mv_idx = -1;
                if (can_move) {
                    mv_idx = (int)opts.size();
                    opts.push_back(tr(S_MOVE_UP));
                }
                int art_idx = -1, del_art_idx = -1;
                if (!art_title.empty()) {
                    art_idx = (int)opts.size();
                    opts.push_back(tr(S_BOXART_SEARCH_CUSTOM));
                    if (has_art) {
                        del_art_idx = (int)opts.size();
                        opts.push_back(tr(S_BOXART_DELETE_COVER));
                    }
                }
                int set_idx = (int)opts.size();
                opts.push_back(tr(S_OPEN_SETTINGS));
                int r = this->CreateShowDialog(tr(S_FILE), content, opts, false,
                                               {}, style_dialog);
                if (mv_idx >= 0 && r == mv_idx && !targets.empty()) {
                    this->MvStart(targets);
                } else if (art_idx >= 0 && r == art_idx) {
                    // The picker (and the Installed refresh + reselect once
                    // it's done) takes over from here — see BoxArtPickReturn.
                    this->BoxArtCustomSearch(art_title, Screen::Installed, i);
                } else if (del_art_idx >= 0 && r == del_art_idx) {
                    char body[300];
                    snprintf(body, sizeof(body), tr(S_BOXART_DELETE_BODY),
                             art_title.c_str());
                    if (this->ConfirmDanger(tr(S_DELETE), body, true) &&
                        boxart_forget(art_title.c_str())) {
                        boxart_cache_forget(art_title);
                        this->Toast(tr(S_DELETED));
                        this->GotoInstalled(this->inst_path);
                        this->layout->SetSel(i);
                    }
                } else if (r == set_idx) {
                    this->GotoStorage(); // ROM folders live under Storage
                }
            }
        } else if ((down & (HidNpadButton_X | HidNpadButton_Y)) &&
                   this->inst_path == roms_root(&g_tico)) {
            // Root console list: X slides out the per-console Options panel
            // (right), Y the global Tools panel (left). Once either is open the
            // sibling button flips to the other (X in Tools, Y in Options) so the
            // two can be switched between without backing out first. (Inside a
            // console folder Y instead marks files, below.)
            s32 i = this->layout->Sel();
            bool have_console =
                i >= 0 && i < (s32)g_inst.size() && g_inst[i].is_dir;
            bool show_tools = (down & HidNpadButton_Y) != 0;
            while (true) {
                bool flip;
                if (show_tools) {
                    flip = this->ToolsMenu();          // true: X → Options
                    if (flip && !have_console) break;  // nothing to switch to
                } else {
                    if (!have_console) break;          // X with no console: no-op
                    flip = this->ConsoleOptionsMenu(i); // true: Y → Tools
                }
                if (!flip) break;
                show_tools = !show_tools;
            }
        } else if ((down & HidNpadButton_Y) &&
                   this->inst_path != roms_root(&g_tico)) {
            // Y marks roms for deletion — only inside a console folder, never
            // on the console list where selecting/deleting makes no sense.
            // Works the same in the poster card grid (blue border) as the
            // list (green tag bar); the card grid can't be at the console
            // root when this fires, so no in_cards gate is needed here.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                this->layout->ToggleMark(i);
            }
        } else if (!in_cards && (down & HidNpadButton_Right) &&
                   this->inst_path != roms_root(&g_tico)) {
            // Inside a console folder: ▶ deletes roms (kept as a direct
            // button) — list view only. The card grid needs D-pad Right for
            // navigation (multiple columns per row), so there delete is
            // reachable only through the X options menu.
            this->InstDeleteSel();
        } else if ((down & HidNpadButton_Left) && !in_cards &&
                   this->inst_path != roms_root(&g_tico)) {
            // ◀ is a list-view shortcut to the sort picker; it also lives in the
            // X options menu (the only way to reach it in the card grid, where ◀
            // navigates).
            this->InstSortDialog();
        } else if ((down & HidNpadButton_X) &&
                   this->inst_path != roms_root(&g_tico)) {
            // X inside a console folder opens the file Options menu (Move to
            // console, Rename, Sort, Delete). Available in list and card views
            // alike — the card grid can't hang these off ◀/▶.
            this->InstFileMenu();
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
                    // Grouping may have folded the hit's own file into a set's
                    // row (a track or disc of a larger dump) instead of listing
                    // it under its own name -- select that row for it.
                    bool in_group = false;
                    for (const auto &m : g_inst[k].group_members) {
                        if (strcasecmp(m.c_str(), name.c_str()) == 0) {
                            in_group = true;
                            break;
                        }
                    }
                    if (in_group) {
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
            // A console pick for the inbox sorter returns to the sort results,
            // not Home — the rest of the staged files are still listed there.
            if (this->pending == Pending::SortAssign) {
                this->pending = Pending::None;
                this->sort_pick_idx = -1;
                this->SortInboxResults();
            } else {
                this->GotoHome();
            }
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_picker.size()) {
                const char *cname = g_picker[i].c_str();
                if (this->pending == Pending::SortAssign) {
                    this->pending = Pending::None;
                    this->SortAssignPicked(cname);
                } else if (this->pending == Pending::AddRepo) {
                    // Add a source: search archive.org for it, or type an id in.
                    this->pending = Pending::None;
                    this->AddRepoChoose(cname);
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
                                            e.is_archive, e.md5.c_str(),
                                            install_folder_for(e.target.c_str()));
                        if (ok) {
                            this->Toast(std::string(tr(S_QUEUED)) + ": " +
                                        e.name);
                            // Take the user to the queue to watch it run. The
                            // worker resumes from any matching .part on disk and
                            // only re-downloads from scratch if that fails, so
                            // "resume first, else start new" needs nothing extra.
                            this->GotoQueue();
                        } else {
                            this->ToastErr(tr(S_QUEUE_FULL));
                        }
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
            this->GotoAppearance(); // manage consoles lives under Appearance now
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < g_cfg.console_count) {
                // Cycle Both -> Browse only -> Installed only -> Hidden -> Both.
                int st = (console_vis_state(g_cfg.consoles[i]) + 1) % 4;
                console_vis_apply(g_cfg.consoles[i], st);
                config_save(&g_cfg);
                // Update just this row's state label in place — a full
                // GotoManage() rebuild resets scroll_top, which then re-scrolls
                // the selected row to the bottom of the viewport.
                this->layout->SetRowRight(i, console_vis_label(st),
                                          console_vis_color(st));
            }
        }
        break;
    }

    case Screen::Creds: {
        if (down & HidNpadButton_B) {
            this->GotoAccount();
        } else if (down & HidNpadButton_A) {
            s32 i = this->layout->Sel();
            char v[1024] = {0};
            if (i == 0) {
                if (prompt(tr(S_ACCESS_KEY), g_creds.access_key, v, sizeof(v))) {
                    snprintf(g_creds.access_key, sizeof(g_creds.access_key), "%s",
                             v);
                    // These are keys the user typed in by hand off a web page,
                    // so a save that didn't land has to say so rather than
                    // toast "Saved" and lose them.
                    if (creds_save(&g_creds)) {
                        this->Toast(tr(S_SAVED));
                    } else {
                        this->ToastErr(tr(S_SAVE_FAILED));
                    }
                }
            } else if (i == 1) {
                // Pre-filled with the current secret so it's easy to edit.
                if (prompt(tr(S_SECRET_KEY), g_creds.secret, v, sizeof(v))) {
                    snprintf(g_creds.secret, sizeof(g_creds.secret), "%s", v);
                    if (creds_save(&g_creds)) {
                        this->Toast(tr(S_SAVED));
                    } else {
                        this->ToastErr(tr(S_SAVE_FAILED));
                    }
                }
            } else if (i == 2) {
                if (this->ConfirmDanger(tr(S_CLEAR_CREDS),
                                        tr(S_CLEAR_CREDS_CONFIRM))) {
                    g_creds.access_key[0] = '\0';
                    g_creds.secret[0] = '\0';
                    if (creds_save(&g_creds)) {
                        this->Toast(tr(S_CLEARED));
                    } else {
                        this->ToastErr(tr(S_SAVE_FAILED));
                    }
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
            // Return to wherever the search was launched from. A Missing
            // Games "find & download" search didn't come from browsing a
            // repo, so search_ci is only a match scope there, not an origin —
            // route back to the Library tab instead of the Add tab's repo
            // list for that console.
            if (this->search_from_missing) {
                this->search_from_missing = false;
                this->GotoInstalled(roms_root(&g_tico));
            } else if (this->search_ri >= 0) {
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
                                    h.is_archive, h.md5.c_str(),
                                    install_folder_for(h.target.c_str()));
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
                bool from_missing = this->search_from_missing; // GotoSearch resets it
                this->GotoSearch(q, this->search_ci, this->search_ri);
                this->search_from_missing = from_missing; // preserve across re-search
            }
        }
        break;
    }

    case Screen::Verify: {
        if (down & HidNpadButton_B) {
            verify_free(&this->vfy_job);
            dat_free(&this->vfy_dat);
            this->GotoInstalled(roms_root(&g_tico));
        } else if (down & HidNpadButton_A) {
            // A is context-sensitive per row: fix a misnamed file, or fetch a
            // replacement for a corrupt one. These states never overlap.
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->vfy_order.size()) {
                const VerifyItem &it = this->vfy_job.items[this->vfy_order[row]];
                if (it.status == DAT_BAD) {
                    this->VerifyReacquireSel();
                } else {
                    this->VerifyRenameSel();
                }
            }
        } else if (down & HidNpadButton_X) {
            this->VerifyRenameAll();
        } else if (down & HidNpadButton_Y) {
            this->VerifyMenu();
        }
        break;
    }

    case Screen::VerifyMissing: {
        if (down & HidNpadButton_B) {
            if (!this->vfy_missing_filter.empty()) {
                // First B clears an active filter and stays on this screen;
                // only a second B (nothing left to clear) backs out, matching
                // how a filtered list elsewhere in the app un-narrows first.
                this->vfy_missing_filter.clear();
                this->VerifyMissingResults();
            } else if (this->vfy_missing_direct) {
                // Reached straight from console Options -> Missing Games: the
                // per-file Verify results screen was never shown, so back out
                // to the Library tab instead of a screen the user never saw.
                verify_free(&this->vfy_job);
                dat_free(&this->vfy_dat);
                this->GotoInstalled(roms_root(&g_tico));
            } else {
                this->VerifyResults(); // rebuild the per-file results (job live)
                this->screen = Screen::Verify;
            }
        } else if (down & HidNpadButton_A) {
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->vfy_missing_order.size()) {
                // Seed search with just the base title — DAT titles carry
                // region/revision tags ("Yoshi (USA).nes") that rarely match a
                // repo's file name verbatim (different tagging convention,
                // different dump), so matching the exact DAT name almost never
                // found anything. Cutting at the first "(" casts a much wider
                // net and still lets the user pick the right region/dump from
                // the result list. Falls back to stripping just the extension
                // when the title has no parenthesised tag at all.
                std::string q = this->vfy_missing[this->vfy_missing_order[row]];
                size_t paren = q.find('(');
                if (paren != std::string::npos && paren > 0) {
                    q.erase(paren);
                } else {
                    size_t dot = q.find_last_of('.');
                    if (dot != std::string::npos && dot > 0) q.erase(dot);
                }
                while (!q.empty() && q.back() == ' ') q.pop_back();
                ConsoleGroup *g =
                    config_find_console(&g_cfg, this->vfy_target.c_str());
                int ci = g ? (int)(g - g_cfg.consoles) : -1;
                this->GotoSearch(q, ci, -1);
                this->search_from_missing = true; // set after: GotoSearch resets it
            }
        } else if (down & HidNpadButton_Y) {
            char q[128];
            if (prompt_raw(tr(S_FILTER_MISSING_PROMPT),
                           this->vfy_missing_filter.c_str(), q, sizeof(q))) {
                this->vfy_missing_filter = q;
                this->VerifyMissingResults();
            }
        }
        break;
    }

    case Screen::Tidy: {
        if (down & HidNpadButton_B) {
            // First B clears an active filter and stays here -- matches how a
            // filtered list elsewhere in the app (Box Art results, Verify's
            // Missing Games) un-narrows before it backs out. Only a second B
            // (nothing left to clear) actually leaves the screen.
            if (this->tidy_kind_filter != -1) {
                this->tidy_kind_filter = -1;
                this->TidyResults();
            } else {
                this->tidy_issues.clear();
                this->tidy_results_order.clear();
                this->GotoInstalled(roms_root(&g_tico));
            }
        } else if (down & HidNpadButton_A) {
            this->TidyActSel();
        } else if (down & HidNpadButton_X) {
            this->TidyFixAll();
        } else if (down & HidNpadButton_Y) {
            this->TidyFilterDialog();
        }
        break;
    }

    case Screen::LargestFiles: {
        if (down & HidNpadButton_B) {
            this->large_files.clear();
            this->GotoStorage();
            this->layout->SetSel(7);
        } else if (down & HidNpadButton_A) {
            this->LargeFileOpenSel();
        } else if (down & HidNpadButton_X) {
            this->LargeFileDeleteSel();
        }
        break;
    }

    case Screen::BoxArtManageConsoles: {
        if (down & HidNpadButton_B) {
            this->boxart_manage_rows.clear();
            this->GotoStorage();
            this->layout->SetSel(8);
        } else if (down & HidNpadButton_A) {
            // Rebuild the same "one row per console, in appearance order"
            // grouping GotoBoxArtManageConsoles used, so Sel() maps to the
            // right console key.
            s32 sel = this->layout->Sel();
            std::string cur;
            s32 idx = -1;
            for (const BoxArtManageRow &r : this->boxart_manage_rows) {
                if (r.console != cur) {
                    cur = r.console;
                    idx++;
                    if (idx == sel) {
                        this->GotoBoxArtManageList(cur);
                        break;
                    }
                }
            }
        }
        break;
    }

    case Screen::BoxArtManageList: {
        if (down & HidNpadButton_B) {
            this->GotoBoxArtManageConsoles();
        } else if (down & HidNpadButton_Y) {
            // Mark for a batch delete — same mechanism/blue-tag as everywhere
            // else in the app that marks rows.
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)this->layout->RowCount()) {
                this->layout->ToggleMark(i);
            }
        } else if (down & HidNpadButton_X) {
            this->BoxArtManageDeleteSel();
        }
        break;
    }

    case Screen::BoxArtResults: {
        if (down & HidNpadButton_B) {
            // First B clears an active filter and stays here — matches how a
            // filtered list elsewhere in the app (Verify's Missing Games)
            // un-narrows before it backs out. Only a second B (nothing left
            // to clear) actually leaves the screen.
            if (this->boxart_result_filter != 0) {
                this->boxart_result_filter = 0;
                this->GotoBoxArtResults();
            } else {
                this->boxart_rows.clear();
                this->boxart_results_order.clear();
                this->GotoInstalled(roms_root(&g_tico));
            }
        } else if (down & HidNpadButton_A) {
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->boxart_results_order.size()) {
                this->BoxArtResultsRowMenu(this->boxart_results_order[row]);
            }
        } else if (down & HidNpadButton_X) {
            this->BoxArtResultsFilterDialog();
        }
        break;
    }

    case Screen::BoxArtPicker: {
        if (down & HidNpadButton_B) {
            this->BoxArtPickReturn();
        } else if (down & HidNpadButton_A) {
            this->BoxArtPickConfirm(this->layout->Sel());
        }
        break;
    }

    case Screen::ArchiveSearch: {
        if (down & HidNpadButton_B) {
            this->GotoHome();
        } else if (down & HidNpadButton_A) {
            this->ArchAddSel();
        } else if (down & HidNpadButton_X) {
            char q[256] = {0};
            if (prompt(tr(S_IA_QUERY_GUIDE), this->arch_query.c_str(), q,
                       sizeof(q))) {
                this->GotoArchSearch(q, this->arch_console);
            }
        }
        break;
    }

    case Screen::VerifyAll: {
        if (down & HidNpadButton_B) {
            this->vfy_all.clear();
            this->GotoInstalled(roms_root(&g_tico));
        } else if (down & HidNpadButton_A) {
            // Drill into the selected console's full results (cache-hot, so the
            // re-verify is quick). B from there returns to the Installed list.
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->vfy_all.size()) {
                VfyAllRow r = this->vfy_all[row]; // copy; VerifyStart resets state
                this->VerifyStart(r.folder, r.target, r.label);
            }
        }
        break;
    }

    case Screen::SortInbox: {
        if (down & HidNpadButton_B) {
            this->sort_rows.clear();
            this->GotoInstalled(roms_root(&g_tico));
        } else if (down & HidNpadButton_Y) {
            // Y marks/unmarks the row under the cursor for a Minus multi-delete
            // — for clearing out several unwanted files at once, without
            // console-picking or single-deleting them one at a time.
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->sort_rows.size()) {
                this->layout->ToggleMark(row);
            }
        } else if (down & HidNpadButton_A) {
            // A opens the console picker for the row under the cursor — to place
            // a pending file by hand, or to correct an auto-filed one (its guess
            // may be wrong), re-filing it into the chosen console.
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->sort_rows.size()) {
                this->sort_pick_idx = row;
                this->GotoPicker(Pending::SortAssign);
            }
        } else if (down & HidNpadButton_X) {
            // X deletes the file under the cursor from its current location
            // (r.path) — unwanted junk received into the inbox, or a wrongly
            // auto-filed game. Permanent, so guard with a danger confirm.
            s32 row = this->layout->Sel();
            if (row >= 0 && row < (s32)this->sort_rows.size()) {
                SortRow &r = this->sort_rows[row];
                if (this->ConfirmDanger(tr(S_DELETE), r.name, true)) {
                    if (remove(r.path.c_str()) == 0) {
                        xfer_log("delete     %s", r.path.c_str());
                        this->sort_rows.erase(this->sort_rows.begin() + row);
                        this->inv_last_gen_ns = 0; // count changed; refresh
                        this->Toast(tr(S_DELETED));
                        if (this->sort_rows.empty()) {
                            this->GotoInstalled(roms_root(&g_tico));
                        } else {
                            this->SortInboxResults();
                        }
                    } else {
                        this->ToastErr(tr(S_SORT_DELETE_FAIL));
                    }
                }
            }
        } else if (down & HidNpadButton_Minus) {
            // Delete every marked row (Ⓨ) together. Nothing marked -> no-op,
            // since Ⓧ already covers a single delete under the cursor.
            int mc = this->layout->MarkedCount();
            if (mc == 0) break;
            auto marks = this->layout->Marked();
            char msg[128];
            snprintf(msg, sizeof(msg), tr(S_DELETE_SELECTED), mc);
            if (this->ConfirmDanger(tr(S_DELETE), msg)) {
                int failed = 0;
                for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
                    s32 idx = *it;
                    if (idx < 0 || idx >= (s32)this->sort_rows.size()) continue;
                    SortRow &r = this->sort_rows[idx];
                    if (remove(r.path.c_str()) == 0) {
                        xfer_log("delete     %s", r.path.c_str());
                        this->sort_rows.erase(this->sort_rows.begin() + idx);
                    } else {
                        failed++;
                    }
                }
                this->inv_last_gen_ns = 0; // count changed; refresh
                if (failed > 0) {
                    this->ToastErr(tr(S_SORT_DELETE_FAIL));
                } else {
                    char t[32];
                    snprintf(t, sizeof(t), tr(S_DELETED_N), mc);
                    this->Toast(t);
                }
                if (this->sort_rows.empty()) {
                    this->GotoInstalled(roms_root(&g_tico));
                } else {
                    this->SortInboxResults();
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

void MainApplication::OnLoad() {
    // Splash first: the renderer is already initialized at this point (see
    // draw_splash), so this appears at once instead of after the several
    // seconds of init below.
    draw_splash(this->renderer);

    romfsInit();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    net_init();
    tico_init(&g_tico);
    config_load(&g_cfg);
    config_sort(&g_cfg);
    creds_load(&g_creds);
    net_set_github_token(g_creds.github_token); // authenticate update checks if set
    net_set_steamgriddb_key(g_creds.steamgriddb_key); // box art scans, if set
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
    apply_extract_tunables(); /* seed the extraction knobs from saved prefs */
    queue_set_keep_archives(g_prefs.keep_archives); /* keep archives compressed */
    queue_set_post_import_enabled(g_prefs.convert_import); /* post-import converter */
    mutexInit(&g_boxart_auto_mtx);
    queue_on_landed = boxart_auto_on_landed; /* box art auto-fetch, gated at drain time on box_art_auto_fetch */
    cleanup_stale_parts(); // drop unresumable old-format .part leftovers
    load_console_icons();  // romfs:/icons/<key>.png, shared into list rows

    this->screen = Screen::Installed;
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

    // Land on the library (the manager's front door), not the Add/Browse tab.
    this->GotoInstalled(roms_root(&g_tico));
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
    this->InvServerStop(); // same: close the inventory listener before net_exit()
    this->UsbMtpStop(); // release usb:ds if the connect screen was still up
    // Ask every worker that polls a cancel flag to stop, so the joins below
    // return promptly instead of blocking on an in-flight network retry or a
    // long hash scan. Flags whose worker doesn't check them are harmless to set.
    this->ra_cancel = true;
    this->vfy_job.cancel = true;
    this->vfy_all_cancel = true;
    this->upd_cancel = true;
    for (auto &job : this->umi_jobs) job.cancel = true;
    this->appchk_cancel = true;
    this->dat_cancel = true;
    this->sp_prog.cancel = true;
    this->tidy_cancel = true;
    this->lgf_cancel = true;
    this->boxart_cancel = true;
    this->boxart_auto_cancel = true;
    this->boxman_cancel = true;
    this->meta_discard = true;
    this->search_discard = true;
    this->isearch_discard = true;
    this->arch_discard = true;
    this->pxt_cancel = true;
    // EVERY background worker must be joined before the process exits, or libnx
    // faults on the still-running thread — the intermittent "an error occurred"
    // seen on exit and, more often, on update→restart (the app-update / emulator
    // update screens leave `appchk`/`umi`/`dat` fetching in the background). This
    // is the full BgTask set from the header; Join() is a no-op on one that never
    // ran, so listing them all is safe and keeps this honest as tasks are added.
    for (BgTask *t : {&this->upd, &this->chk, &this->bgchk,
                      &this->appchk, &this->diag, &this->ra, &this->dat,
                      &this->meta, &this->search, &this->isearch, &this->arch,
                      &this->mv, &this->notes, &this->vfy, &this->tidy,
                      &this->lgf, &this->pxt, &this->boxart, &this->boxart_auto,
                      &this->boxman}) {
        t->Join();
    }
    for (auto &job : this->umi_jobs) job.task.Join();
    verify_free(&this->vfy_job);
    dat_free(&this->vfy_dat);
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
        // Yes(0) first so it's the default (leftmost) focus — the user asked to
        // check for updates, so accepting is the expected action. "Release notes"
        // lets them see what's in the new version first; Cancel is the last option
        // and doubles as B (use_last_opt_as_cancel), returning to Settings.
        int r = this->CreateShowDialog(tr(S_TITLE_UPDATE), umsg,
                                       {tr(S_YES), tr(S_RELEASE_NOTES), tr(S_CANCEL)},
                                       true, {}, style_dialog);
        if (r == 1) {
            this->GotoReleaseNotes();
            return;
        }
        if (r != 0) {
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
        this->GotoUpdates();
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
                            &code, NULL);
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

void MainApplication::UpdStart(const std::string &url, const std::string &dl,
                               const std::string &tag) {
    this->upd_url = url;
    this->upd_dl = dl;
    this->upd_tag = tag;
    this->upd_now = 0;
    this->upd_total = 0;
    this->upd_ok = false;
    this->upd_cancel = false;

    if (!this->upd.Start(&MainApplication::UpdThread, this)) {
        this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_START_FAIL),
                               {tr(S_OK)}, true, {}, style_dialog);
        this->GotoSettings();
        return;
    }
    // The download now shows as an ordinary Queue-tab item, driven by PollXfers;
    // jump there so it's immediately visible (cancel = A on the item).
    this->upd_xslot = this->BeginXfer(this->upd_tag, "HaulNX", 2);
}

void MainApplication::UpdTick() {
    // Progress is mirrored into the Queue-tab item by PollXfers; this only runs
    // the install/finish once the download thread reports done.
    if (!this->upd.done) {
        return;
    }

    // Download finished: join the worker and install on the main thread.
    this->upd.Join();

    if (this->upd_cancel) {
        remove(this->upd_dl.c_str());
        queue_ext_finish(this->upd_xslot, false, "cxl");
        this->upd_xslot = -1;
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
            inst = fs_copy_file(dl.c_str(), stage) && looks_like_nro(stage);
        }
        upd_log("upd: staged '%s' %s", stage, inst ? "ok" : "FAILED");
        if (inst) {
            remove(dl.c_str());
            queue_ext_finish(this->upd_xslot, true, NULL);
            this->upd_xslot = -1;
            // The new build is staged for the next launch; flip the chips and
            // offer to relaunch in place (same epilogue as a LAN-pushed .nro).
            char umsg[512];
            snprintf(umsg, sizeof(umsg), tr(S_UPDATE_OK), tag.c_str());
            if (this->StagedRestartPrompt(umsg)) {
                return; // closing to restart
            }
        } else {
            remove(stage); // don't leave a half-written stage behind
            queue_ext_finish(this->upd_xslot, false, "stage");
            this->upd_xslot = -1;
            this->CreateShowDialog(
                tr(S_TITLE_UPDATE),
                std::string("Could not stage the update (details in debug "
                            "log).\nDownloaded build kept at:\n") + dl,
                {tr(S_OK)}, true, {}, style_dialog);
        }
    } else {
        remove(dl.c_str());
        queue_ext_finish(this->upd_xslot, false, "failed");
        this->upd_xslot = -1;
        this->CreateShowDialog(tr(S_TITLE_UPDATE), tr(S_UPDATE_FAIL), {tr(S_OK)}, true, {}, style_dialog);
    }
    this->GotoSettings();
}
