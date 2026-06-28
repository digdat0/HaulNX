#include <MainApplication.hpp>
#include <pu/ui/extras/extras_Toast.hpp>
#include "version.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "config.h"
#include "iarchive.h"
#include "net.h"
#include "queue.h"
#include "extract.h"
#include "fsutil.h"
#include "update.h"
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

#define FILES_SUBTITLE \
    "A get  - all  Y filter  X refresh  Dpad< >repo  L/R tabs  B back"

struct DirEnt {
    std::string name;
    bool is_dir;
    uint64_t size;
};
static std::vector<DirEnt> g_inst;
static std::vector<std::string> g_picker; // sorted supported consoles for the picker
static std::vector<int> g_home_map; // grouped Browse: visible row -> console index
static std::string g_launch_path;   // argv[0] from main(), for self-update

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
    if (b >= (1ull << 30)) {
        return pu::ui::Color(245, 175, 95, 255); // >= 1 GB : orange
    }
    if (b >= (1ull << 20)) {
        return pu::ui::Color(130, 225, 150, 255); // >= 1 MB : green
    }
    return pu::ui::Color(150, 205, 255, 255); // KB : light blue
}

// Soft accent for count columns (repo count, file/app count).
static pu::ui::Color count_color() {
    return pu::ui::Color(150, 200, 210, 255); // muted cyan
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
    switch (s) {
    case Q_DOWNLOADING: return pu::ui::Color(245, 246, 250, 255); // white (active)
    case Q_VERIFYING:
    case Q_AWAIT_EXTRACT:
    case Q_EXTRACTING:  return pu::ui::Color(210, 185, 120, 255); // amber
    case Q_DONE:        return pu::ui::Color(130, 225, 150, 255); // green
    case Q_SAVED:       return pu::ui::Color(190, 205, 130, 255); // olive
    case Q_FAILED:      return pu::ui::Color(240, 110, 110, 255); // red
    case Q_CANCELLED:   return pu::ui::Color(150, 150, 162, 255); // gray
    case Q_QUEUED:
    default:            return pu::ui::Color(205, 212, 225, 255); // light
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

static void rebuild_files(MainLayout *lay, const char *target) {
    lay->ClearMenu();
    g_files.clear();
    g_marks.clear();
    if (!g_have_item) {
        lay->AddRow("(metadata fetch failed - B back)");
        return;
    }
    for (int i = 0; i < g_item.file_count; i++) {
        if (g_filter.empty() ||
            ci_contains(g_item.files[i].name, g_filter.c_str())) {
            g_files.push_back(i);
        }
    }
    for (int k = 0; k < (int)g_files.size(); k++) {
        ArchiveFile *f = &g_item.files[g_files[k]];
        bool inst = file_installed(target, f->name);
        g_marks.push_back(inst ? 1 : 0);
        char name[540];
        snprintf(name, sizeof(name), "%s%s", inst ? "* " : "", f->name);
        // Name column white; size column right-aligned and tinted by magnitude.
        lay->AddRow2(name, human_size(f->size),
                     pu::ui::Color(232, 234, 240, 255), size_color(f->size));
    }
    if (g_files.empty()) {
        lay->AddRow("(no files match)");
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

    this->layout->SetSubtitle("Loading metadata - please wait");
    this->layout->ClearMenu();
    this->layout->AddRow("Loading metadata ...");

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
        this->layout->AddRow(std::string("Loading metadata") +
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
    // Dark theme: near-black canvas with light text.
    this->SetBackgroundColor(pu::ui::Color(12, 12, 14, 255));
    const s32 sw = (s32)pu::ui::render::ScreenWidth;
    const s32 sh = (s32)pu::ui::render::ScreenHeight;

    // Header: a darker blue title row sitting above a lighter blue tab strip,
    // so the tabs read as a distinct band.
    this->header = pu::ui::elm::Rectangle::New(0, 0, sw, 150,
                                               pu::ui::Color(28, 54, 104, 255));
    this->Add(this->header);

    this->title = pu::ui::elm::TextBlock::New(45, 24, "TicoDL+");
    this->title->SetColor(pu::ui::Color(255, 255, 255, 255));
    this->Add(this->title);

    this->status = pu::ui::elm::TextBlock::New(sw - 580, 30, "");
    this->status->SetColor(pu::ui::Color(210, 222, 245, 255));
    this->Add(this->status);

    // Lighter-blue tab strip band.
    const s32 strip_y = 80;
    const s32 strip_h = 70;
    this->tab_bar = pu::ui::elm::Rectangle::New(
        0, strip_y, sw, strip_h, pu::ui::Color(58, 104, 178, 255));
    this->Add(this->tab_bar);

    // Tabs (L/R cycles): Browse | Installed | Queue | Settings, evenly spaced
    // by centering each label within its quarter of the screen.
    const char *labels[] = {"Browse", "Installed", "Queue", "Settings"};
    const s32 tab_y = strip_y + 16;
    const s32 seg = sw / 4;
    for (int i = 0; i < 4; i++) {
        auto tb = pu::ui::elm::TextBlock::New(0, tab_y, labels[i]);
        tb->SetColor(pu::ui::Color(196, 212, 240, 255));
        tb->SetX(seg * i + (seg - tb->GetWidth()) / 2);
        this->Add(tb);
        this->tabs.push_back(tb);
    }
    this->tab_underline = pu::ui::elm::Rectangle::New(
        0, strip_y + strip_h - 5, 120, 5, pu::ui::Color(150, 205, 255, 255));
    this->Add(this->tab_underline);

    // List fills the middle; a footer bar at the bottom carries the controls.
    const s32 footer_h = 64;
    const s32 list_y = 158;
    const s32 row_h = 84;
    const s32 avail = sh - list_y - footer_h;
    const s32 rows_visible = avail / row_h; // 10 on 1080, fewer if scaled
    this->list = TableList::New(0, list_y, sw, row_h, rows_visible);
    this->Add(this->list);

    this->footer = pu::ui::elm::Rectangle::New(0, sh - footer_h, sw, footer_h,
                                               pu::ui::Color(22, 42, 80, 255));
    this->Add(this->footer);
    // Footer button hints, split into segments and spread evenly across the row
    // by SetSubtitle. Pre-create a fixed pool of labels.
    for (int i = 0; i < 8; i++) {
        auto seg = pu::ui::elm::TextBlock::New(0, sh - footer_h + 14, "");
        seg->SetColor(pu::ui::Color(206, 216, 238, 255));
        this->Add(seg);
        this->footer_segs.push_back(seg);
    }

    this->SetActiveTab(0);
}

void MainLayout::SetActiveTab(int idx) {
    if (idx < 0 || idx >= (int)this->tabs.size()) {
        return;
    }
    for (int i = 0; i < (int)this->tabs.size(); i++) {
        this->tabs[i]->SetColor(i == idx
                                    ? pu::ui::Color(255, 255, 255, 255)
                                    : pu::ui::Color(196, 212, 240, 255));
    }
    this->tab_underline->SetX(this->tabs[idx]->GetX());
    this->tab_underline->SetWidth(this->tabs[idx]->GetWidth());
}

void MainLayout::SetTitle(const std::string &t) {
    // Keep the app name visible alongside the per-screen breadcrumb.
    this->title->SetText(t.empty() ? std::string("TicoDL+")
                                   : std::string("TicoDL+     ") + t);
}
void MainLayout::SetStatus(const std::string &t) { this->status->SetText(t); }
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
void MainLayout::ClearMenu() { this->list->Clear(); }
void MainLayout::AddRow(const std::string &name) {
    this->AddRow(name, pu::ui::Color(232, 234, 240, 255)); // default: white text
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
    int r = this->CreateShowDialog(title, msg, {"Cancel", "Yes"}, false);
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
    const char *plug = (charger != PsmChargerType_Unconnected) ? "+" : "";
    char s[160];
    uint64_t speed = 0;
    queue_active_info(NULL, 0, NULL, NULL, NULL, &speed, NULL, NULL);
    if (speed > 0) {
        std::string sp = human_size(speed);
        snprintf(s, sizeof(s), "SD %s/%s  DL %s/s  BAT %u%%%s",
                 sf.c_str(), st.c_str(), sp.c_str(), (unsigned)bat, plug);
    } else {
        snprintf(s, sizeof(s), "SD %s/%s  BAT %u%%%s",
                 sf.c_str(), st.c_str(), (unsigned)bat, plug);
    }
    this->layout->SetStatus(s);
}

// ---- screens --------------------------------------------------------------
void MainApplication::GotoHome() {
    this->screen = Screen::Home;
    this->layout->ClearMenu();
    if (g_prefs.group_consoles) {
        this->layout->SetTitle("Consoles");
        this->layout->SetSubtitle(
            "A open  Y add  X del console  L/R tabs  ZL/ZR page");
        // Build the shown consoles, sorted A-Z by their displayed label (the
        // full name), since the stored order is by folder key. g_home_map maps
        // each visible row back to its real console index (for open / delete).
        struct HomeRow {
            std::string label;
            int idx;
        };
        std::vector<HomeRow> rows;
        for (int i = 0; i < g_cfg.console_count; i++) {
            if (!g_cfg.consoles[i].shown) {
                continue;
            }
            char label[160];
            console_label(g_cfg.consoles[i].console, label, sizeof(label));
            rows.push_back({label, i});
        }
        std::sort(rows.begin(), rows.end(),
                  [](const HomeRow &a, const HomeRow &b) {
                      return strcasecmp(a.label.c_str(), b.label.c_str()) < 0;
                  });
        g_home_map.clear();
        for (const auto &row : rows) {
            int rc = g_cfg.consoles[row.idx].repo_count;
            char cnt[32];
            snprintf(cnt, sizeof(cnt), "%d %s", rc, rc == 1 ? "repo" : "repos");
            this->layout->AddRow2(row.label, cnt,
                                  pu::ui::Color(232, 234, 240, 255),
                                  count_color());
            g_home_map.push_back(row.idx);
        }
        if (g_home_map.empty()) {
            this->layout->AddRow("(no collections - press Y to add)");
        }
    } else {
        this->layout->SetTitle("Repos");
        this->layout->SetSubtitle(
            "A browse  X edit  Y add  - delete  L/R tabs  ZL/ZR page");
        for (int c = 0; c < g_cfg.console_count; c++) {
            for (int r = 0; r < g_cfg.consoles[c].repo_count; r++) {
                Repo *rp = &g_cfg.consoles[c].repos[r];
                char row[180];
                snprintf(row, sizeof(row), "[%s] %s - %s",
                         rp->enabled ? "on" : "off", g_cfg.consoles[c].target,
                         rp->label);
                this->layout->AddRow(row);
            }
        }
        if (flat_count() == 0) {
            this->layout->AddRow("(no repos - press Y to add)");
        }
    }
    this->layout->SetSel(this->home_sel); // restore place
}

void MainApplication::GotoRepos(int ci) {
    this->screen = Screen::Repos;
    this->sel_ci = ci;
    ConsoleGroup *g = &g_cfg.consoles[ci];
    this->layout->SetTitle(std::string("Console: ") + g->console);
    this->layout->SetSubtitle("A browse  X edit  Y add  - delete  L/R tabs  B back");
    this->layout->ClearMenu();
    for (int i = 0; i < g->repo_count; i++) {
        char row[180];
        snprintf(row, sizeof(row), "[%s] %s", g->repos[i].enabled ? "on" : "off",
                 g->repos[i].label);
        this->layout->AddRow(row);
    }
    if (g->repo_count == 0) {
        this->layout->AddRow("(no repos - press Y to add)");
    }
    this->layout->SetSel(ci == this->repos_sel_ci ? this->repos_sel : 0);
}

void MainApplication::GotoFiles(int ci, int ri) {
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
    case Screen::Advanced:  return Tab::Settings;
    default:                return Tab::Browse; // Home/Repos/Files/RepoEdit/Picker
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
    this->layout->SetTitle("Download Queue");
    this->layout->SetSubtitle("A cancel  X retry  ZL/ZR move  Y clear  - log  B back");
    this->layout->ClearMenu();
}

void MainApplication::GotoSettings() {
    this->screen = Screen::Settings;
    this->layout->SetTitle(std::string("Settings   (v") + APP_VERSION_STR + ")");
    this->layout->SetSubtitle("A select  L/R tabs  ZL/ZR page");
    this->layout->ClearMenu();
    char r[600];
    snprintf(r, sizeof(r), "ROM folder: %s", roms_root(&g_tico));
    this->layout->AddRow(r);                      // 0
    this->layout->AddRow("Check for updates");    // 1
    this->layout->AddRow("View download log");    // 2
    this->layout->AddRow("Download from URL");    // 3
    this->layout->AddRow("Manage consoles (show/hide)"); // 4
    this->layout->AddRow("Advanced");             // 5
    this->layout->AddRow("Controls / Help");      // 6
    this->layout->AddRow("Credits");              // 7
}

void MainApplication::GotoAdvanced() {
    this->screen = Screen::Advanced;
    this->layout->SetTitle("Advanced");
    this->layout->SetSubtitle("A toggle/edit  B back");
    this->layout->ClearMenu();
    char r[96];
    snprintf(r, sizeof(r), "Stay awake while downloading: %s",
             g_prefs.prevent_sleep ? "ON" : "OFF");
    this->layout->AddRow(r);                      // 0
    snprintf(r, sizeof(r), "Group consoles: %s",
             g_prefs.group_consoles ? "ON" : "OFF");
    this->layout->AddRow(r);                      // 1
    snprintf(r, sizeof(r), "Archive.org credentials: %s",
             g_creds.access_key[0] ? "set" : "unset");
    this->layout->AddRow(r);                      // 2
    snprintf(r, sizeof(r), "Metadata cache: %s", g_prefs.use_cache ? "ON" : "OFF");
    this->layout->AddRow(r);                      // 3
    snprintf(r, sizeof(r), "Max simultaneous downloads: %d", g_prefs.max_downloads);
    this->layout->AddRow(r);                      // 4
}

void MainApplication::GotoManage() {
    this->screen = Screen::Manage;
    this->layout->SetTitle("Manage consoles");
    this->layout->SetSubtitle("A show/hide  L/R tabs  B back");
    this->layout->ClearMenu();
    for (int i = 0; i < g_cfg.console_count; i++) {
        bool sh = g_cfg.consoles[i].shown;
        this->layout->AddRow2(
            g_cfg.consoles[i].console, sh ? "shown" : "hidden",
            pu::ui::Color(232, 234, 240, 255),
            sh ? pu::ui::Color(130, 225, 150, 255)   // green = shown
               : pu::ui::Color(150, 150, 162, 255)); // gray = hidden
    }
    if (g_cfg.console_count == 0) {
        this->layout->AddRow("(no consoles configured)");
    }
}

void MainApplication::GotoCreds() {
    this->screen = Screen::Creds;
    this->layout->SetTitle("Archive.org credentials");
    this->layout->SetSubtitle("A edit  B back");
    this->layout->ClearMenu();
    char r[200];
    snprintf(r, sizeof(r), "Access key: %.50s",
             g_creds.access_key[0] ? g_creds.access_key : "<unset>");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Secret: %s", g_creds.secret[0] ? "<set>" : "<unset>");
    this->layout->AddRow(r);
    this->layout->AddRow("Clear credentials");
}

void MainApplication::GotoInstalled(const std::string &path) {
    this->screen = Screen::Installed;
    this->inst_path = path;
    g_inst = list_dir(path);
    std::string shown = path;
    if (shown.rfind(roms_root(&g_tico), 0) == 0) {
        shown = "roms" + shown.substr(strlen(roms_root(&g_tico)));
    }
    this->layout->SetTitle(std::string("Installed: ") + shown);
    this->layout->SetSubtitle("A open  X rename  - delete  L/R tabs  B back/up");
    this->layout->ClearMenu();
    for (int i = 0; i < (int)g_inst.size(); i++) {
        DirEnt &e = g_inst[i];
        if (e.is_dir) {
            int n = count_dir_entries(path + "/" + e.name);
            char cnt[32];
            snprintf(cnt, sizeof(cnt), "%d %s", n, n == 1 ? "app" : "apps");
            std::string label = "[DIR] ";
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
                                  pu::ui::Color(245, 246, 250, 255),
                                  count_color());
        } else {
            // File: right column is the size, tinted by magnitude.
            this->layout->AddRow2(e.name, human_size(e.size),
                                  pu::ui::Color(232, 234, 240, 255),
                                  size_color(e.size));
        }
    }
    if (g_inst.empty()) {
        this->layout->AddRow("(empty)");
    }
}

void MainApplication::GotoRepoEdit(int ci, int ri) {
    this->screen = Screen::RepoEdit;
    this->sel_ci = ci;
    this->sel_ri = ri;
    Repo *rp = &g_cfg.consoles[ci].repos[ri];
    this->layout->SetTitle(std::string("Edit repo: ") + g_cfg.consoles[ci].console);
    this->layout->SetSubtitle("A edit/toggle  B back");
    this->layout->ClearMenu();
    char r[600];
    snprintf(r, sizeof(r), "Name: %.80s", rp->label[0] ? rp->label : "<unset>");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Archive.org ID: %.120s", rp->id[0] ? rp->id : "<unset>");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Download URL: %.200s",
             rp->download_base[0] ? rp->download_base : "<auto>");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Enabled: %s", rp->enabled ? "yes" : "no");
    this->layout->AddRow(r);
    this->layout->AddRow("Delete this repo");
}

void MainApplication::GotoPicker(Pending what) {
    this->screen = Screen::Picker;
    this->pending = what;
    this->layout->SetTitle("Select console");
    this->layout->SetSubtitle("A select  B cancel");
    this->layout->ClearMenu();

    // Build a sorted (A-Z) copy of the supported list so the picker is ordered;
    // the input handler reads back from g_picker by index.
    g_picker.clear();
    for (int i = 0; i < g_cfg.supported_count; i++) {
        g_picker.push_back(g_cfg.supported[i]);
    }
    std::sort(g_picker.begin(), g_picker.end(),
              [](const std::string &a, const std::string &b) {
                  return strcasecmp(a.c_str(), b.c_str()) < 0;
              });

    for (const auto &name : g_picker) {
        ConsoleGroup *g = config_find_console(&g_cfg, name.c_str());
        int rc = g ? g->repo_count : 0;
        char cnt[32];
        snprintf(cnt, sizeof(cnt), "%d %s", rc, rc == 1 ? "repo" : "repos");
        this->layout->AddRow2(name, cnt, pu::ui::Color(232, 234, 240, 255),
                              count_color());
    }
    if (g_picker.empty()) {
        this->layout->AddRow("(no supported consoles)");
    }
}

void MainApplication::GotoLog() {
    // Remember where we came from (Settings or Queue) so B returns there.
    if (this->screen != Screen::Log) {
        this->log_origin = this->screen;
    }
    this->screen = Screen::Log;
    this->layout->SetTitle("Download Log");
    this->layout->SetSubtitle("X clear log  B back");
    this->layout->ClearMenu();
    std::vector<std::string> lines;
    std::ifstream f(DLLOG_PATH);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    for (int i = (int)lines.size() - 1; i >= 0; i--) {
        this->layout->AddRow(lines[i]);
    }
    if (lines.empty()) {
        this->layout->AddRow("(no downloads logged yet)");
    }
}

// ---- input ----------------------------------------------------------------
void MainApplication::HandleInput(u64 down, u64 held) {
    // A self-update download owns the UI while it runs: drive its progress /
    // finish and swallow all other input until it completes.
    if (this->upd_running) {
        (void)down;
        (void)held;
        this->UpdTick();
        return;
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
                            this->ToastErr(std::string("Failed: ") + nm);
                        } else {
                            this->Toast(std::string(s == Q_SAVED ? "Saved: "
                                                                 : "Done: ") +
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
            snprintf(left, sizeof(left), "%-6s %s", qstatus(it->status),
                     it->name);
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
            this->layout->AddRow("(queue empty)");
        }
        this->layout->SetSel(keep);
    }

    // SD/battery refresh at most ~every 30s (psm/statvfs aren't free, and the
    // input callback runs every frame — uncapped fps would spam them).
    {
        static u64 last = 0;
        u64 now = armGetSystemTick();
        if (last == 0 || armTicksToNs(now - last) >= 30000000000ULL) {
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
            snprintf(msg, sizeof(msg),
                     "%d download(s) still in progress.\nExit anyway?", active);
            if (!this->Confirm("Exit", msg)) {
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
            } else if (down & HidNpadButton_Y) {
                this->GotoPicker(Pending::AddRepo);
            } else if ((down & HidNpadButton_X) && valid) {
                int ci = g_home_map[sel];
                if (this->Confirm("Delete console",
                                  std::string("Delete '") +
                                      g_cfg.consoles[ci].console +
                                      "' and its repos?")) {
                    config_remove_console(&g_cfg, ci);
                    config_save(&g_cfg);
                    this->Toast("Deleted");
                    this->GotoHome();
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
            } else if ((down & HidNpadButton_Minus) &&
                       flat_ref(this->layout->Sel(), &ci, &ri)) {
                if (this->Confirm("Delete repo", "Delete this repo?")) {
                    config_remove_repo(&g_cfg.consoles[ci], ri);
                    config_save(&g_cfg);
                    this->Toast("Deleted");
                    this->GotoHome();
                }
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
            if (prompt("Repo name", nullptr, nm, sizeof(nm)) &&
                prompt("archive.org item id", nullptr, id, sizeof(id))) {
                if (config_add_repo(g, nm, id)) {
                    config_save(&g_cfg);
                    this->Toast("Added");
                }
            }
            this->GotoRepos(this->sel_ci);
        } else if ((down & HidNpadButton_Minus) && g->repo_count > 0) {
            if (this->Confirm("Delete repo", "Delete this repo?")) {
                config_remove_repo(g, this->layout->Sel());
                config_save(&g_cfg);
                this->Toast("Deleted");
                this->GotoRepos(this->sel_ci);
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
                        this->Toast(std::string("Queued: ") + f->name);
                    } else {
                        this->ToastErr("Queue is full");
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
                char msg[200];
                if (avail != UINT64_MAX && total_sz > avail) {
                    snprintf(msg, sizeof(msg),
                             "Queue all %d file(s)?\n\n"
                             "Total: %s   Free: %s\n"
                             "WARNING: not enough free space!",
                             (int)g_files.size(),
                             human_size(total_sz).c_str(),
                             human_size(avail).c_str());
                } else {
                    snprintf(msg, sizeof(msg),
                             "Queue all %d file(s)?\n\nTotal: %s   Free: %s",
                             (int)g_files.size(),
                             human_size(total_sz).c_str(),
                             avail != UINT64_MAX
                                 ? human_size(avail).c_str() : "?");
                }
                if (this->Confirm("Download all", msg)) {
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
                        snprintf(t, sizeof(t), "Queued %d (queue full)", ok);
                        this->ToastErr(t);
                    } else {
                        snprintf(t, sizeof(t), "Queued %d file(s)", ok);
                        this->Toast(t);
                    }
                }
            }
        } else if (down & HidNpadButton_Y) {
            char fb[64] = {0};
            if (prompt_raw("Filter (blank = all)", g_filter.c_str(), fb,
                           sizeof(fb))) {
                g_filter = fb;
                rebuild_files(this->layout.get(), g_files_target);
            }
        } else if (down & HidNpadButton_X) {
            this->StartMetaLoad(g_files_id, g_files_base, g_files_target, true,
                                FILES_SUBTITLE);
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
            this->Toast("Cleared");
        } else {
            static QueueView qv[QUEUE_MAX];
            int n = queue_snapshot(qv, QUEUE_MAX);
            s32 i = this->layout->Sel();
            if (i >= 0 && i < n) {
                if (down & HidNpadButton_A) {
                    queue_cancel(qv[i].slot);
                    this->Toast("Cancelled");
                } else if (down & HidNpadButton_X) {
                    queue_retry(qv[i].slot);
                    this->Toast("Retrying");
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
            case 0: // ROM folder (read-only)
                this->CreateShowDialog(
                    "ROM folder",
                    std::string("Current: ") + roms_root(&g_tico) +
                        "\n\nThis is read from TICO's config.\n"
                        "To change it, open TICO and set the\n"
                        "ROMs path in its Settings.",
                    {"OK"}, true);
                break;
            case 1: { // Check for updates
                char tag[64], url[1024];
                if (!update_fetch_latest(UPDATE_REPO, tag, sizeof(tag), url,
                                         sizeof(url))) {
                    this->CreateShowDialog("Update",
                                           "Could not fetch release info.",
                                           {"OK"}, true);
                    break;
                }
                if (version_cmp(APP_VERSION_STR, tag) >= 0) {
                    this->CreateShowDialog(
                        "Update",
                        std::string("You are up to date (v") + APP_VERSION_STR +
                            ").",
                        {"OK"}, true);
                    break;
                }
                if (!this->Confirm("Update", std::string("Update to ") + tag +
                                                 "?  Replaces the app.")) {
                    break;
                }
                char dl[1024];
                snprintf(dl, sizeof(dl), "%s/downloads/update.nro", CONFIG_DIR);
                fs_ensure_parent(dl);
                this->UpdStart(url, dl, tag);
                return;
            }
            case 2: // View download log
                this->GotoLog();
                return;
            case 3: { // Download from URL
                char inp[1024] = {0};
                if (prompt("archive.org URL or item id", nullptr, inp,
                           sizeof(inp))) {
                    char id[256];
                    if (ia_extract_id(inp, id, sizeof(id))) {
                        this->pending_id = id;
                        this->GotoPicker(Pending::Manual);
                        return;
                    }
                    this->CreateShowDialog("Manual",
                                           "Could not parse an item id.",
                                           {"OK"}, true);
                }
                break;
            }
            case 4: // Manage consoles
                this->GotoManage();
                return;
            case 5: // Advanced
                this->GotoAdvanced();
                return;
            case 6: // Controls / Help
                this->CreateShowDialog(
                    "Controls",
                    "Tabs: L / R  (Browse | Installed | Queue | Settings)\n"
                    "Navigate: D-pad (hold to repeat)   ZL/ZR: page\n"
                    "+: exit   B: back\n"
                    "Browse: A open  Y add  X edit/del  - delete\n"
                    "Files: A get  - all  Y filter  X refresh  Dpad L/R: repo\n"
                    "Queue: A cancel  X retry  ZL/ZR move  Y clear  - log",
                    {"OK"}, true);
                break;
            case 7: // Credits
                this->CreateShowDialog(
                    "Credits",
                    "TicoDL+\ncreated by digdat0\n\n"
                    "Plutonium UI library provided by XorTroll\n\n"
                    "TICO emulator - https://ticoverse.com/",
                    {"OK"}, true);
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
                prefs_save(&g_prefs);
                s32 sel = this->layout->Sel();
                this->GotoAdvanced();
                this->layout->SetSel(sel);
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
                        "File",
                        g_inst[i].name + "\n" + human_size(g_inst[i].size),
                        {"OK"}, true);
                }
            }
        } else if (down & HidNpadButton_X) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                char nm[256] = {0};
                if (prompt("Rename to", g_inst[i].name.c_str(), nm, sizeof(nm))) {
                    std::string from = this->inst_path + "/" + g_inst[i].name;
                    std::string to = this->inst_path + "/" + nm;
                    if (rename(from.c_str(), to.c_str()) == 0) {
                        this->Toast("Renamed");
                    } else {
                        this->ToastErr("Rename failed");
                    }
                    this->GotoInstalled(this->inst_path);
                }
            }
        } else if (down & HidNpadButton_Minus) {
            s32 i = this->layout->Sel();
            if (i >= 0 && i < (s32)g_inst.size()) {
                if (this->Confirm("Delete", std::string("Delete '") +
                                                g_inst[i].name + "'?")) {
                    fs_rm_rf((this->inst_path + "/" + g_inst[i].name).c_str());
                    this->Toast("Deleted");
                    this->GotoInstalled(this->inst_path);
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
                if (prompt("Repo name", rp->label, v, sizeof(v))) {
                    snprintf(rp->label, sizeof(rp->label), "%s", v);
                    config_save(&g_cfg);
                }
                break;
            case 1:
                if (prompt("archive.org item id", rp->id, v, sizeof(v))) {
                    snprintf(rp->id, sizeof(rp->id), "%s", v);
                    rp->download_base[0] = '\0';
                    repo_set_url_default(rp);
                    config_save(&g_cfg);
                }
                break;
            case 2:
                if (prompt("Download URL", rp->download_base, v, sizeof(v))) {
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
                if (this->Confirm("Delete repo", "Delete this repo?")) {
                    config_remove_repo(&g_cfg.consoles[this->sel_ci],
                                       this->sel_ri);
                    config_save(&g_cfg);
                    this->Toast("Deleted");
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
                    if (prompt("Repo name", nullptr, nm, sizeof(nm)) &&
                        prompt("archive.org item id", nullptr, id, sizeof(id))) {
                        ConsoleGroup *g = config_add_console(&g_cfg, cname);
                        if (g && config_add_repo(g, nm, id)) {
                            config_sort(&g_cfg);
                            config_save(&g_cfg);
                            this->Toast("Added");
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
                        "A get  - all  Y filter  X refresh  L/R tabs  B back");
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
        } else if (down & HidNpadButton_X) {
            if (this->Confirm("Clear log",
                              "Clear all download history?")) {
                remove(DLLOG_PATH);
                this->Toast("Log cleared");
                this->GotoLog(); // refresh (stays in the Log view)
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
                if (prompt("Access Key", g_creds.access_key, v, sizeof(v))) {
                    snprintf(g_creds.access_key, sizeof(g_creds.access_key), "%s",
                             v);
                    creds_save(&g_creds);
                    this->Toast("Saved");
                }
            } else if (i == 1) {
                // Pre-filled with the current secret so it's easy to edit.
                if (prompt("Secret Key", g_creds.secret, v, sizeof(v))) {
                    snprintf(g_creds.secret, sizeof(g_creds.secret), "%s", v);
                    creds_save(&g_creds);
                    this->Toast("Saved");
                }
            } else if (i == 2) {
                if (this->Confirm("Clear credentials",
                                  "Remove the saved access key and secret?")) {
                    g_creds.access_key[0] = '\0';
                    g_creds.secret[0] = '\0';
                    creds_save(&g_creds);
                    this->Toast("Cleared");
                }
            }
            s32 keep = this->layout->Sel();
            this->GotoCreds();
            this->layout->SetSel(keep);
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
    net_init();
    tico_init(&g_tico);
    config_load(&g_cfg);
    config_sort(&g_cfg);
    creds_load(&g_creds);
    prefs_load(&g_prefs);
    queue_init(roms_root(&g_tico), g_prefs.max_downloads);

    this->screen = Screen::Home;
    this->sel_ci = 0;
    this->sel_ri = 0;
    this->pending = Pending::None;
    this->inst_path = roms_root(&g_tico);
    this->log_origin = Screen::Settings;

    this->layout = MainLayout::New();
    this->LoadLayout(this->layout);

    if (!g_tico.installed) {
        int opt = this->CreateShowDialog(
            "TICO not detected",
            "The TICO emulator was not found on this console.\n"
            "Downloads will go to the default folder:\n" +
                std::string(roms_root(&g_tico)) +
                "\n\nContinue without TICO?",
            {"Continue", "Exit"}, true);
        if (opt != 0) {
            this->Close();
            return;
        }
    }

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
    if (this->meta_running) {
        threadWaitForExit(&this->meta_thread);
        threadClose(&this->meta_thread);
        this->meta_running = false;
    }
    queue_exit();
    appletSetMediaPlaybackState(false);
    net_exit();
    psmExit();
    romfsExit();
}

// ---- app: in-app self-update download -------------------------------------
int MainApplication::UpdProgress(void *ud, u64 now, u64 total) {
    auto self = static_cast<MainApplication *>(ud);
    self->upd_now = now;
    self->upd_total = total;
    return 0; // never abort from here
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
    this->upd_running = true;

    this->layout->SetTitle("Updating");
    this->layout->SetSubtitle(std::string("Downloading ") + tag + "...");
    this->layout->ClearMenu();
    this->layout->AddRow("Downloading update - please wait");

    Result rc = threadCreate(&this->upd_thread, &MainApplication::UpdThread, this,
                             NULL, 0x40000, 0x2C, -2);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&this->upd_thread))) {
        return;
    }
    this->upd_running = false;
    this->CreateShowDialog("Update", "Could not start the downloader.", {"OK"},
                           true);
    this->GotoSettings();
}

void MainApplication::UpdTick() {
    if (!this->upd_done) {
        // Still downloading: show live progress in the subtitle.
        u64 now = this->upd_now, total = this->upd_total;
        int pct = total ? (int)((now * 100) / total) : 0;
        char s[160];
        snprintf(s, sizeof(s), "Downloading %s: %d%%  (%s / %s)  -  please wait",
                 this->upd_tag.c_str(), pct, human_size(now).c_str(),
                 total ? human_size(total).c_str() : "?");
        this->layout->SetSubtitle(s);
        return;
    }

    // Download finished: join the worker and install on the main thread.
    threadWaitForExit(&this->upd_thread);
    threadClose(&this->upd_thread);
    this->upd_running = false;

    bool ok = this->upd_ok;
    std::string tag = this->upd_tag, dl = this->upd_dl;
    if (ok) {
        romfsExit();
        std::string selfp = resolve_self_path();
        const char *self = selfp.c_str();
        char prev[1100];
        snprintf(prev, sizeof(prev), "%s.previous", self);
        install_over(self, prev); // best-effort backup
        bool inst = install_over(dl.c_str(), self);
        romfsInit();
        if (inst) {
            remove(dl.c_str());
            this->CreateShowDialog("Update",
                                   std::string("Updated to ") + tag +
                                       ".\nInstalled to:\n" + selfp +
                                       "\n\nClose and relaunch TicoDL+.",
                                   {"OK"}, true);
        } else {
            this->CreateShowDialog(
                "Update", std::string("Install failed. New build kept at:\n") + dl,
                {"OK"}, true);
        }
    } else {
        remove(dl.c_str());
        this->CreateShowDialog("Update", "Download failed.", {"OK"}, true);
    }
    this->GotoSettings();
}
