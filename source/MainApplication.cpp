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
static ArchiveItem g_item;
static bool g_have_item = false;

static std::vector<int> g_files; // filtered indices into g_item.files
static std::vector<char> g_marks;
static std::string g_filter;
static char g_files_id[256], g_files_base[512], g_files_target[64];
static bool g_files_manual = false;

#define FILES_SUBTITLE \
    "A download  - all  Y filter  X refresh  L/R repo  B back"

struct DirEnt {
    std::string name;
    bool is_dir;
    uint64_t size;
};
static std::vector<DirEnt> g_inst;

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
    case Q_VERIFYING:   return "vrfy";
    case Q_EXTRACTING:  return "unzip";
    case Q_DONE:        return "done";
    case Q_SAVED:       return "saved";
    case Q_FAILED:      return "FAIL";
    case Q_CANCELLED:   return "cxl";
    default:            return "?";
    }
}

static bool flat_ref(int flat, int *ci, int *ri) {
    int k = 0;
    for (int c = 0; c < g_cfg.console_count; c++) {
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
        k += g_cfg.consoles[c].repo_count;
    }
    return k;
}

static bool file_installed(const char *target, const char *fname) {
    char p[1200];
    snprintf(p, sizeof(p), "%s/%s/%s", ROMS_ROOT, target, fname);
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
    snprintf(dir, sizeof(dir), "%s/%s", ROMS_ROOT, target);
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
        char row[220];
        snprintf(row, sizeof(row), "%s %-68.68s %10s", inst ? "*" : " ", f->name,
                 human_size(f->size).c_str());
        lay->AddRow(row);
    }
    if (g_files.empty()) {
        lay->AddRow("(no files match)");
    }
}

static void show_files(MainLayout *lay, const char *id, const char *base,
                       const char *target, bool force) {
    snprintf(g_files_id, sizeof(g_files_id), "%s", id ? id : "");
    snprintf(g_files_base, sizeof(g_files_base), "%s", base ? base : "");
    snprintf(g_files_target, sizeof(g_files_target), "%s", target ? target : "");
    if (g_have_item) {
        ia_free(&g_item);
        g_have_item = false;
    }
    if (id && id[0] &&
        ia_fetch(id, &g_item, g_prefs.use_cache && !force, CACHE_DIR)) {
        if (g_files_base[0]) {
            snprintf(g_item.download_base, sizeof(g_item.download_base), "%s",
                     g_files_base);
        }
        g_have_item = true;
    }
    g_filter.clear();
    rebuild_files(lay, g_files_target);
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
    this->SetBackgroundColor(pu::ui::Color(24, 26, 33, 255));
    const s32 sw = (s32)pu::ui::render::ScreenWidth;

    // Blue header bar across the top.
    this->header = pu::ui::elm::Rectangle::New(0, 0, sw, 150,
                                               pu::ui::Color(36, 72, 140, 255));
    this->Add(this->header);

    this->title = pu::ui::elm::TextBlock::New(45, 34, "TicoDL+");
    this->title->SetColor(pu::ui::Color(255, 255, 255, 255));
    this->Add(this->title);

    this->status = pu::ui::elm::TextBlock::New(sw - 430, 42, "");
    this->status->SetColor(pu::ui::Color(210, 222, 245, 255));
    this->Add(this->status);

    this->subtitle = pu::ui::elm::TextBlock::New(45, 100, "");
    this->subtitle->SetColor(pu::ui::Color(190, 205, 232, 255));
    this->Add(this->subtitle);

    // Menu fills the rest of the 1080 canvas (scales to 720 in handheld).
    this->menu = pu::ui::elm::Menu::New(
        0, 156, sw, pu::ui::Color(236, 238, 244, 255),
        pu::ui::Color(120, 170, 225, 255), 84, 11);
    this->Add(this->menu);
}

void MainLayout::SetTitle(const std::string &t) {
    // Keep the app name visible alongside the per-screen breadcrumb.
    this->title->SetText(t.empty() ? std::string("TicoDL+")
                                   : std::string("TicoDL+     ") + t);
}
void MainLayout::SetStatus(const std::string &t) { this->status->SetText(t); }
void MainLayout::SetSubtitle(const std::string &t) { this->subtitle->SetText(t); }
void MainLayout::ClearMenu() { this->menu->ClearItems(); }
void MainLayout::AddRow(const std::string &name) {
    auto it = pu::ui::elm::MenuItem::New(name);
    this->menu->AddItem(it);
}
s32 MainLayout::Sel() { return this->menu->GetSelectedIndex(); }
void MainLayout::SetSel(s32 i) {
    if (i >= 0 && i < (s32)this->menu->GetItems().size()) {
        this->menu->SetSelectedIndex((u32)i);
    }
}
s32 MainLayout::RowCount() { return (s32)this->menu->GetItems().size(); }
void MainLayout::MoveBy(s32 delta) {
    s32 n = (s32)this->menu->GetItems().size();
    if (n <= 0) {
        return;
    }
    s32 i = this->menu->GetSelectedIndex() + delta;
    if (i < 0) {
        i = 0;
    }
    if (i >= n) {
        i = n - 1;
    }
    this->menu->SetSelectedIndex((u32)i);
}
void MainLayout::MoveUp() { this->MoveBy(-1); }
void MainLayout::MoveDown() { this->MoveBy(1); }
void MainLayout::PageUp() { this->MoveBy(-this->menu->GetNumberOfItemsToShow()); }
void MainLayout::PageDown() { this->MoveBy(this->menu->GetNumberOfItemsToShow()); }

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
    u32 bat = 0;
    psmGetBatteryChargePercentage(&bat);
    std::string sd = (fb == UINT64_MAX) ? std::string("?") : human_size(fb);
    char s[80];
    snprintf(s, sizeof(s), "SD %s   %u%%", sd.c_str(), (unsigned)bat);
    this->layout->SetStatus(s);
}

// ---- screens --------------------------------------------------------------
void MainApplication::GotoHome() {
    this->screen = Screen::Home;
    this->layout->ClearMenu();
    if (g_prefs.group_consoles) {
        this->layout->SetTitle("Consoles");
        this->layout->SetSubtitle(
            "A open  Y add  X del  L queue  R installed  RS settings  ZL/ZR page");
        for (int i = 0; i < g_cfg.console_count; i++) {
            char row[96];
            snprintf(row, sizeof(row), "%s   (%d repos)",
                     g_cfg.consoles[i].console, g_cfg.consoles[i].repo_count);
            this->layout->AddRow(row);
        }
        if (g_cfg.console_count == 0) {
            this->layout->AddRow("(no collections - press Y to add)");
        }
    } else {
        this->layout->SetTitle("Repos");
        this->layout->SetSubtitle(
            "A browse  X edit  Y add  - delete  L queue  R installed  RS settings");
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
}

void MainApplication::GotoRepos(int ci) {
    this->screen = Screen::Repos;
    this->sel_ci = ci;
    ConsoleGroup *g = &g_cfg.consoles[ci];
    this->layout->SetTitle(std::string("Console: ") + g->console);
    this->layout->SetSubtitle("A browse  X edit  Y add  - delete  L queue  B back");
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
}

void MainApplication::GotoFiles(int ci, int ri) {
    g_files_manual = false;
    this->sel_ci = ci;
    this->sel_ri = ri;
    ConsoleGroup *g = &g_cfg.consoles[ci];
    Repo *rp = &g->repos[ri];
    this->layout->SetTitle(std::string(g->console) + " > " + rp->label);
    this->layout->SetSubtitle(FILES_SUBTITLE);
    this->screen = Screen::Files;
    show_files(this->layout.get(), rp->id, rp->download_base, g->target, false);
}

void MainApplication::GotoQueue() {
    this->screen = Screen::Queue;
    this->layout->SetTitle("Download Queue");
    this->layout->SetSubtitle("A cancel  X retry  Y clear  - log  ZL/ZR page  B back");
    this->layout->ClearMenu();
}

void MainApplication::GotoSettings() {
    this->screen = Screen::Settings;
    this->layout->SetTitle(std::string("Settings   (v") + APP_VERSION_STR + ")");
    this->layout->SetSubtitle("A toggle/edit  ZL/ZR page  B back");
    this->layout->ClearMenu();
    char r[96];
    snprintf(r, sizeof(r), "Metadata cache: %s", g_prefs.use_cache ? "ON" : "OFF");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Stay awake while downloading: %s",
             g_prefs.prevent_sleep ? "ON" : "OFF");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Group consoles: %s",
             g_prefs.group_consoles ? "ON" : "OFF");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Archive.org access key: %.50s",
             g_creds.access_key[0] ? g_creds.access_key : "<unset>");
    this->layout->AddRow(r);
    snprintf(r, sizeof(r), "Archive.org secret: %s",
             g_creds.secret[0] ? "<set>" : "<unset>");
    this->layout->AddRow(r);
    this->layout->AddRow("Check for updates");
    this->layout->AddRow("View download log");
    this->layout->AddRow("Download from URL");
    this->layout->AddRow("Controls / Help");
    this->layout->AddRow("Credits");
}

void MainApplication::GotoInstalled(const std::string &path) {
    this->screen = Screen::Installed;
    this->inst_path = path;
    g_inst = list_dir(path);
    std::string shown = path;
    if (shown.rfind(ROMS_ROOT, 0) == 0) {
        shown = "roms" + shown.substr(strlen(ROMS_ROOT));
    }
    this->layout->SetTitle(std::string("Installed: ") + shown);
    this->layout->SetSubtitle("A open  X rename  - delete  B back/up");
    this->layout->ClearMenu();
    for (int i = 0; i < (int)g_inst.size(); i++) {
        char row[220];
        if (g_inst[i].is_dir) {
            snprintf(row, sizeof(row), "[DIR] %.200s", g_inst[i].name.c_str());
        } else {
            snprintf(row, sizeof(row), "      %-46.46s  %s",
                     g_inst[i].name.c_str(), human_size(g_inst[i].size).c_str());
        }
        this->layout->AddRow(row);
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
    for (int i = 0; i < g_cfg.supported_count; i++) {
        ConsoleGroup *g = config_find_console(&g_cfg, g_cfg.supported[i]);
        char row[96];
        snprintf(row, sizeof(row), "%-16.16s  (%d repos)", g_cfg.supported[i],
                 g ? g->repo_count : 0);
        this->layout->AddRow(row);
    }
    if (g_cfg.supported_count == 0) {
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
    this->layout->SetSubtitle("B back");
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
    // Live-refresh the queue list while it's open.
    if (this->screen == Screen::Queue) {
        static QueueView qv[QUEUE_MAX];
        s32 keep = this->layout->Sel();
        int n = queue_snapshot(qv, QUEUE_MAX);
        this->layout->ClearMenu();
        for (int i = 0; i < n; i++) {
            const QueueItem *it = &qv[i].item;
            char info[64] = "";
            if (it->status == Q_DOWNLOADING && it->total) {
                int pct = (int)((it->now * 100) / it->total);
                if (it->speed) {
                    snprintf(info, sizeof(info), "%d%% / %s @ %s/s", pct,
                             human_size(it->total).c_str(),
                             human_size(it->speed).c_str());
                } else {
                    snprintf(info, sizeof(info), "%d%% / %s", pct,
                             human_size(it->total).c_str());
                }
            } else if (it->total) {
                snprintf(info, sizeof(info), "%s", human_size(it->total).c_str());
            }
            char row[220];
            snprintf(row, sizeof(row), "%-5s %-40.40s  %s", qstatus(it->status),
                     it->name, info);
            this->layout->AddRow(row);
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

    // Hold D-pad up/down to auto-repeat.
    {
        static int hold = 0;
        int dir = (held & HidNpadButton_Down)  ? 1
                  : (held & HidNpadButton_Up)  ? -1
                                               : 0;
        if (dir == 0) {
            hold = 0;
        } else {
            hold++;
            if (hold > 22 && ((hold - 22) % 3) == 0) {
                this->layout->MoveBy(dir);
            }
        }
    }
    if (down & HidNpadButton_ZL) {
        this->layout->PageUp();
    }
    if (down & HidNpadButton_ZR) {
        this->layout->PageDown();
    }
    if (down & HidNpadButton_Plus) {
        this->Close();
        return;
    }

    // Global tools (shoulders + right stick), available from list screens.
    // In the Files view L/R are repurposed to switch repos (handled below),
    // so the global queue/installed shortcuts skip that screen.
    if ((down & HidNpadButton_L) && this->screen != Screen::Queue &&
        this->screen != Screen::Files) {
        this->GotoQueue();
        return;
    }
    if ((down & HidNpadButton_R) && this->screen != Screen::Installed &&
        this->screen != Screen::Files) {
        this->GotoInstalled(ROMS_ROOT);
        return;
    }
    if (down & HidNpadButton_StickR) {
        this->GotoSettings();
        return;
    }

    switch (this->screen) {
    case Screen::Home: {
        if (g_prefs.group_consoles) {
            if ((down & HidNpadButton_A) && g_cfg.console_count > 0) {
                this->GotoRepos(this->layout->Sel());
            } else if (down & HidNpadButton_Y) {
                this->GotoPicker(Pending::AddRepo);
            } else if ((down & HidNpadButton_X) && g_cfg.console_count > 0) {
                int ci = this->layout->Sel();
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
                char msg[80];
                snprintf(msg, sizeof(msg), "Queue all %d file(s)?",
                         (int)g_files.size());
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
            this->layout->SetSubtitle("Refreshing...");
            show_files(this->layout.get(), g_files_id, g_files_base,
                       g_files_target, true);
            this->layout->SetSubtitle(FILES_SUBTITLE);
        } else if ((down & (HidNpadButton_L | HidNpadButton_R)) &&
                   !g_files_manual) {
            // Switch to the previous/next repo of the same console.
            ConsoleGroup *g = &g_cfg.consoles[this->sel_ci];
            if (g->repo_count > 1) {
                int dir = (down & HidNpadButton_R) ? 1 : -1;
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
            char v[1024] = {0};
            switch (i) {
            case 0:
                g_prefs.use_cache = !g_prefs.use_cache;
                prefs_save(&g_prefs);
                break;
            case 1:
                g_prefs.prevent_sleep = !g_prefs.prevent_sleep;
                prefs_save(&g_prefs);
                break;
            case 2:
                g_prefs.group_consoles = !g_prefs.group_consoles;
                prefs_save(&g_prefs);
                break;
            case 3:
                if (prompt("Access Key", g_creds.access_key, v, sizeof(v))) {
                    snprintf(g_creds.access_key, sizeof(g_creds.access_key), "%s",
                             v);
                    creds_save(&g_creds);
                    this->Toast("Saved");
                }
                break;
            case 4:
                if (prompt("Secret Key", nullptr, v, sizeof(v))) {
                    snprintf(g_creds.secret, sizeof(g_creds.secret), "%s", v);
                    creds_save(&g_creds);
                    this->Toast("Saved");
                }
                break;
            case 5: {
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
                long code = 0;
                if (!http_download(url, dl, NULL, NULL, NULL, 0, &code)) {
                    remove(dl);
                    this->CreateShowDialog("Update", "Download failed.", {"OK"},
                                           true);
                    break;
                }
                romfsExit();
                const char *self = DEFAULT_SELF_PATH;
                char prev[1100];
                snprintf(prev, sizeof(prev), "%s.previous", self);
                install_over(self, prev); // best-effort backup
                bool ok = install_over(dl, self);
                romfsInit();
                if (ok) {
                    remove(dl);
                    this->CreateShowDialog(
                        "Update",
                        std::string("Updated to ") + tag +
                            ".\nClose and relaunch TicoDL+.",
                        {"OK"}, true);
                } else {
                    this->CreateShowDialog(
                        "Update",
                        std::string("Install failed. New build kept at:\n") + dl,
                        {"OK"}, true);
                }
                break;
            }
            case 6:
                this->GotoLog();
                return;
            case 7: {
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
            case 8:
                this->CreateShowDialog(
                    "Controls",
                    "Navigate: D-pad (hold to repeat)   ZL/ZR: page\n"
                    "L: queue   R: installed   R-stick: settings\n"
                    "+: exit   B: back\n"
                    "Home: A open/browse  Y add  X edit/del  - delete\n"
                    "Files: A download  Y filter  X refresh\n"
                    "Queue: A cancel  X retry  Y clear",
                    {"OK"}, true);
                break;
            case 9:
                this->CreateShowDialog("Credits", "TicoDL+\ncreated by digdat0",
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

    case Screen::Installed: {
        if (down & HidNpadButton_B) {
            if (this->inst_path == ROMS_ROOT) {
                this->GotoHome();
            } else {
                auto p = this->inst_path.find_last_of('/');
                this->GotoInstalled(p == std::string::npos
                                        ? std::string(ROMS_ROOT)
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
            if (i >= 0 && i < g_cfg.supported_count) {
                const char *cname = g_cfg.supported[i];
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
                    this->layout->SetSubtitle(
                        "A download  - all  Y filter  X refresh  B back");
                    this->screen = Screen::Files;
                    show_files(this->layout.get(), this->pending_id.c_str(), base,
                               cname, false);
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
    config_load(&g_cfg);
    config_sort(&g_cfg);
    creds_load(&g_creds);
    prefs_load(&g_prefs);
    queue_init();

    this->screen = Screen::Home;
    this->sel_ci = 0;
    this->sel_ri = 0;
    this->pending = Pending::None;
    this->inst_path = ROMS_ROOT;
    this->log_origin = Screen::Settings;

    this->layout = MainLayout::New();
    this->LoadLayout(this->layout);
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
    queue_exit();
    appletSetMediaPlaybackState(false);
    net_exit();
    psmExit();
    romfsExit();
}
