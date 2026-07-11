#pragma once

#include <pu/Plutonium>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <switch.h>
#include "TableList.hpp"
#include "CardGrid.hpp"

// Draws a BORROWED texture (an icon owned by a shared cache) at a fixed size
// and position. Used for the console icon shown next to the header title.
class IconElement : public pu::ui::elm::Element {
    pu::sdl2::Texture tex; // borrowed — never freed here
    s32 x, y, sz;

  public:
    IconElement(s32 x, s32 y, s32 sz) : tex(nullptr), x(x), y(y), sz(sz) {}
    PU_SMART_CTOR(IconElement)
    void SetTexture(pu::sdl2::Texture t) { this->tex = t; }
    void SetPos(s32 nx, s32 ny) { this->x = nx; this->y = ny; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->sz; }
    s32 GetHeight() override { return this->sz; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (!this->tex) {
            return;
        }
        pu::ui::render::TextureRenderOptions o;
        o.width = this->sz;
        o.height = this->sz;
        drawer->RenderTexture(this->tex, rx, ry, o);
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

// A filled rounded rectangle at a settable position/size/colour. Used as the
// active-tab "pill" behind the tab label.
class PillElement : public pu::ui::elm::Element {
    s32 x, y, w, h, radius;
    pu::ui::Color clr;
    bool visible;

  public:
    PillElement(s32 x, s32 y, s32 w, s32 h, s32 radius, pu::ui::Color clr)
        : x(x), y(y), w(w), h(h), radius(radius), clr(clr), visible(true) {}
    PU_SMART_CTOR(PillElement)
    void SetBounds(s32 nx, s32 ny, s32 nw, s32 nh) {
        this->x = nx; this->y = ny; this->w = nw; this->h = nh;
    }
    void SetColor(pu::ui::Color c) { this->clr = c; }
    void SetVisible(bool v) { this->visible = v; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->w; }
    s32 GetHeight() override { return this->h; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (!this->visible) {
            return;
        }
        drawer->RenderRoundedRectangleFill(this->clr, rx, ry, this->w, this->h,
                                           this->radius);
        // Logo-green "lit" line along the bottom edge: the active-tab cue,
        // matching the green-lit selection. Constant colour — the pill only
        // ever sits on the charcoal tab shell.
        drawer->RenderRoundedRectangleFill(pu::ui::Color(146, 214, 36, 255),
                                           rx + this->radius,
                                           ry + this->h - 3,
                                           this->w - 2 * this->radius, 3, 1);
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

// An animated ring-of-dots spinner with an optional caption below it, centred
// in a region. Shown while metadata / refresh work runs in the background.
class SpinnerElement : public pu::ui::elm::Element {
    s32 x, y, w, h;          // region the spinner centres itself in
    bool active;
    pu::ui::Color dot_clr, text_clr;
    std::string msg, cached_msg, font;
    pu::sdl2::Texture msg_tex; // owned; re-rendered when msg changes

  public:
    SpinnerElement(s32 x, s32 y, s32 w, s32 h)
        : x(x), y(y), w(w), h(h), active(false), dot_clr(146, 214, 36, 255),
          text_clr(150, 160, 185, 255), msg_tex(nullptr) {
        this->font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge);
    }
    PU_SMART_CTOR(SpinnerElement)
    ~SpinnerElement() {
        if (this->msg_tex) {
            pu::ui::render::DeleteTexture(this->msg_tex);
        }
    }
    void SetColors(pu::ui::Color dot, pu::ui::Color text) {
        this->dot_clr = dot; this->text_clr = text;
    }
    void Show(const std::string &message) {
        this->active = true; this->msg = message;
    }
    void Hide() { this->active = false; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->w; }
    s32 GetHeight() override { return this->h; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (!this->active) {
            return;
        }
        const s32 cx = rx + this->w / 2;
        const s32 cy = ry + this->h / 2 - 30;
        const s32 ring = 34, dot = 7, n = 8;
        // Rotating "head": the dot nearest it is brightest, trailing dots fade.
        u64 phase = (armTicksToNs(armGetSystemTick()) / 90000000ULL) % n;
        for (s32 i = 0; i < n; i++) {
            double ang = 2.0 * 3.14159265 * i / n - 3.14159265 / 2.0;
            s32 dx = cx + (s32)(ring * cos(ang));
            s32 dy = cy + (s32)(ring * sin(ang));
            s32 dist = (i - (s32)phase + n) % n;   // 0 = head
            u8 a = (u8)(255 - dist * 26);
            auto c = this->dot_clr; c.a = a;
            drawer->RenderCircleFill(c, dx, dy, dot);
        }
        if (this->msg != this->cached_msg) {
            if (this->msg_tex) {
                pu::ui::render::DeleteTexture(this->msg_tex);
                this->msg_tex = nullptr;
            }
            if (!this->msg.empty()) {
                this->msg_tex = pu::ui::render::RenderText(this->font, this->msg,
                                                           this->text_clr);
            }
            this->cached_msg = this->msg;
        }
        if (this->msg_tex) {
            s32 mw = pu::ui::render::GetTextureWidth(this->msg_tex);
            drawer->RenderTexture(this->msg_tex, cx - mw / 2, cy + ring + 24);
        }
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

// A small filled dot that gently pulses (slow alpha ramp) while active — the
// "downloads running" indicator on the Queue tab. Kept slow and shallow so it
// reads as a soft breath, never a flash.
class PulseDotElement : public pu::ui::elm::Element {
    s32 x, y, r;
    pu::ui::Color clr;
    bool active;

  public:
    PulseDotElement(s32 x, s32 y, s32 r)
        : x(x), y(y), r(r), clr(146, 214, 36, 255), active(false) {}
    PU_SMART_CTOR(PulseDotElement)
    void SetActive(bool a) { this->active = a; }
    void SetPos(s32 nx, s32 ny) { this->x = nx; this->y = ny; }
    void SetColor(pu::ui::Color c) { this->clr = c; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return 2 * this->r; }
    s32 GetHeight() override { return 2 * this->r; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (!this->active) {
            return;
        }
        // Alpha eases between ~150 and 255 over ~1.7s (a calm breath).
        double t = (double)armTicksToNs(armGetSystemTick()) / 1.0e9;
        double s = 0.5 + 0.5 * sin(t * 2.0 * 3.14159265 / 1.7);
        pu::ui::Color c = this->clr;
        c.a = (u8)(150 + s * 105);
        drawer->RenderCircleFill(c, rx + this->r, ry + this->r, this->r);
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

// A thin horizontal green->blue gradient strip — the icon's ring gradient as
// the accent line under the header/tab shell. Rendered in short segments
// (per-pixel would be ~1300 draw calls a frame for no visible gain).
class GradientLineElement : public pu::ui::elm::Element {
    s32 x, y, w, h;
    pu::ui::Color c0, c1;

  public:
    GradientLineElement(s32 x, s32 y, s32 w, s32 h, pu::ui::Color left,
                        pu::ui::Color right)
        : x(x), y(y), w(w), h(h), c0(left), c1(right) {}
    PU_SMART_CTOR(GradientLineElement)
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->w; }
    s32 GetHeight() override { return this->h; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        constexpr s32 Seg = 4;
        const float span = this->w > Seg ? (float)(this->w - Seg) : 1.0f;
        for (s32 sx = 0; sx < this->w; sx += Seg) {
            float t = (float)sx / span;
            pu::ui::Color c(
                (u8)(this->c0.r + ((s32)this->c1.r - this->c0.r) * t),
                (u8)(this->c0.g + ((s32)this->c1.g - this->c0.g) * t),
                (u8)(this->c0.b + ((s32)this->c1.b - this->c0.b) * t), 255);
            s32 seg_w = this->w - sx < Seg ? this->w - sx : Seg;
            drawer->RenderRectangleFill(c, rx + sx, ry, seg_w, this->h);
        }
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

class MainLayout : public pu::ui::Layout {
  private:
    pu::ui::elm::Rectangle::Ref header;
    pu::ui::elm::Rectangle::Ref tab_bar;
    pu::ui::elm::Rectangle::Ref footer;
    IconElement::Ref header_logo; // app badge, top-left of the title
    // Tri-colour wordmark (green Tico / white DL / blue +), fixed in the
    // header; `title` holds only the per-screen breadcrumb after it.
    pu::ui::elm::TextBlock::Ref wm_tico, wm_dl, wm_plus;
    pu::ui::elm::TextBlock::Ref title;
    IconElement::Ref title_icon; // console icon shown after the title text
    pu::ui::elm::TextBlock::Ref status;
    pu::ui::elm::TextBlock::Ref net_icon;
    pu::ui::elm::TextBlock::Ref bat_info;
    pu::ui::elm::TextBlock::Ref rom_info;
    std::vector<pu::ui::elm::TextBlock::Ref> footer_segs;
    TableList::Ref list;
    CardGrid::Ref grid;    // card view for the console lists
    bool cards_mode = false; // which of list/grid is active for this screen
    std::vector<pu::ui::elm::TextBlock::Ref> tabs;
    PillElement::Ref tab_pill;        // rounded highlight behind the active tab
    GradientLineElement::Ref accent_line; // green->blue strip under the shell
    PulseDotElement::Ref queue_dot;   // "downloads running" pulse on the Queue tab
    IconElement::Ref empty_icon;      // big dimmed icon for empty states
    pu::ui::elm::TextBlock::Ref empty_text;
    SpinnerElement::Ref spinner;      // background-work indicator

  public:
    MainLayout();
    PU_SMART_CTOR(MainLayout)

    // Empty state (big centred icon + message) shown when a list has nothing.
    void SetEmptyState(pu::sdl2::Texture icon, const std::string &msg);
    void ClearEmptyState();
    // Loading spinner overlay.
    void ShowSpinner(const std::string &msg);
    void HideSpinner();

    void SetTitle(const std::string &t);
    void SetTitleIcon(pu::sdl2::Texture tex); // console icon after the title
    void SetStatus(const std::string &t);
    void SetNetColor(pu::ui::Color c);
    void SetNetIcon(const std::string &text, pu::ui::Color c);
    void SetBatInfo(const std::string &t);
    void SetSubtitle(const std::string &t);
    void SetRomInfo(const std::string &t);
    void SetActiveTab(int idx); // 0=Browse 1=Installed 2=Queue 3=Settings
    void SetQueueActivity(bool active); // pulse the Queue tab while downloading
    void RefreshTabs();
    void ApplyTheme();
    void ClearMenu();
    void AddRow(const std::string &name);
    void AddRow(const std::string &name, pu::ui::Color clr,
                pu::sdl2::Texture icon = nullptr);
    void AddRow2(const std::string &left, const std::string &right,
                 pu::ui::Color lclr, pu::ui::Color rclr, float progress = -1.0f,
                 pu::sdl2::Texture icon = nullptr,
                 const std::string &prefix = "", bool accent = false,
                 bool pill = true);
    // Card view (console lists). ClearMenu resets to list mode; a screen that
    // wants cards calls SetCardsMode(true) and AddCard instead of AddRow.
    void SetCardsMode(bool on);
    bool InCards() const { return this->cards_mode; }
    void AddCard(const std::string &title, const std::string &subtitle,
                 pu::sdl2::Texture icon);
    void CardMove(s32 dx, s32 dy);
    s32 Sel();
    void SetSel(s32 i);
    bool ConsumeTouchActivate(); // selected row tapped again (touch "A")
    s32 RowCount();
    void MoveBy(s32 delta);
    void Step(s32 delta); // wrap-around single step
    void MoveUp();
    void MoveDown();
    void PageUp();
    void PageDown();
    void ToggleMark(s32 i);
    int MarkedCount();
    const std::set<s32> &Marked();
    void ClearMarks();
};

class MainApplication : public pu::ui::Application {
  public:
    enum class Screen {
        Home,     // grouped: consoles | flat: repos
        Repos,    // a console's repos (grouped)
        Files,    // a repo's file list
        Queue,
        Settings,
        Installed,
        RepoEdit,
        Picker,   // pick a supported console
        Log,
        Manage,   // show/hide consoles on the Browse page
        Creds,    // archive.org credentials editor
        Advanced, // advanced settings sub-menu
        UISettings, // user interface settings sub-menu (theme/cards/consoles/language)
        Downloads, // manage downloads folder
        Language,  // language selector
        Search,    // global file search across cached repos
        Cache,     // metadata cache management
        ManageData, // settings submenu: downloads folder + metadata cache
        ViewLogs,  // settings submenu: download log + debug log
        DebugLog,  // debug.log viewer
        InstSearch // search across installed games (roms folder)
    };
    enum class Pending { None, AddRepo, Manual };
    enum class Tab { Browse = 0, Installed = 1, Queue = 2, Settings = 3 };

  private:
    MainLayout::Ref layout;
    Screen screen;
    int sel_ci;
    int sel_ri;
    Pending pending;
    std::string pending_id;  // archive id for a Manual-URL download
    std::string inst_path;   // current dir in the installed browser
    Screen log_origin;       // screen to return to from the log viewer

    // One-shot startup dialogs (TICO missing / no network), run on the first
    // frame of the UI loop — OnLoad is too early to render a dialog.
    bool startup_checks = false;

    // Remembered list positions, so backing out and returning keeps your place.
    int home_sel = 0;
    int repos_sel = 0;
    int repos_sel_ci = -1;
    int files_sel = 0;
    std::string files_sel_id;

    // In-app self-update download. Runs on its own thread so the UI keeps
    // rendering progress instead of looking hung; the install itself happens
    // back on the main thread (romfs/applet calls) once the download finishes.
    Thread upd_thread;
    volatile bool upd_running = false;
    volatile bool upd_done = false;
    volatile bool upd_ok = false;
    volatile bool upd_cancel = false;
    volatile u64 upd_now = 0;
    volatile u64 upd_total = 0;
    std::string upd_url;
    std::string upd_dl;
    std::string upd_tag;

    // Background update *check* (release-list fetch), so "Check for updates"
    // doesn't freeze the UI during retries. Shows the attempt number (1/3).
    Thread chk_thread;
    volatile bool chk_running = false;
    volatile bool chk_done = false;
    volatile bool chk_ok = false;
    volatile bool chk_discard = false; // B pressed: drop the result silently
    volatile int chk_attempt = 1;
    char chk_tag[64];
    char chk_url[1024];

    // Background bulk metadata refresh (Manage data -> Refresh all metadata):
    // force-fetches every enabled repo's file list, with live (n/total)
    // progress and B to cancel between repos.
    Thread ra_thread;
    volatile bool ra_running = false;
    volatile bool ra_done = false;
    volatile bool ra_cancel = false;
    volatile int ra_idx = 0;
    volatile int ra_total = 0;
    volatile int ra_ok = 0;
    volatile int ra_fail = 0;

    // Background metadata (ia_fetch) load, so the file list doesn't freeze the
    // UI while a repo's metadata downloads. Shows an animated loading indicator.
    Thread meta_thread;
    volatile bool meta_running = false;
    volatile bool meta_done = false;
    volatile bool meta_ok = false;
    bool meta_force = false;
    std::string meta_done_subtitle;

  public:
    using Application::Application;
    PU_SMART_CTOR(MainApplication)

    void OnLoad() override;
    void Shutdown();
    static void SetLaunchPath(const std::string &p); // argv[0] from main()

    void Toast(const std::string &msg);
    void ToastErr(const std::string &msg);
    bool Confirm(const std::string &title, const std::string &msg);
    // Confirm for a destructive action: red-accented dialog, Cancel is the
    // default. If `permanent`, appends an "unrecoverable" warning line.
    bool ConfirmDanger(const std::string &title, const std::string &msg,
                       bool permanent = false);

    void GotoHome();
    void GotoRepos(int ci);
    void GotoFiles(int ci, int ri, bool force = false);
    void GotoQueue();
    void GotoSettings();
    void GotoInstalled(const std::string &path);
    void GotoInstSearch(const std::string &query);
    void GotoRepoEdit(int ci, int ri);
    void GotoPicker(Pending what);
    void GotoLog();
    void GotoManage();
    void GotoCreds();
    void GotoAdvanced();
    void GotoUISettings();
    void GotoDownloads();
    void GotoLanguage();
    void GotoSearch(const std::string &query);
    void GotoCache();
    void GotoManageData();
    void GotoViewLogs();
    void GotoDebugLog();

    Tab CurrentTab();      // which tab the current screen belongs to
    void SwitchTab(int dir); // L/R: cycle to the prev/next tab
    void GotoTab(Tab t);
    void SyncTab();        // highlight the tab bar for the current screen

    void RefreshStatus();
    void HandleInput(u64 down, u64 held, const pu::ui::TouchPoint &touch);

    // Self-update download helpers.
    void UpdStart(const std::string &url, const std::string &dl,
                  const std::string &tag);
    void UpdTick(); // poll progress / finish; called each frame while running
    static void UpdThread(void *arg);
    static int UpdProgress(void *ud, u64 now, u64 total);

    // Bulk metadata refresh helpers.
    void RaStart();
    void RaTick(); // poll progress / finish; called each frame while running
    static void RaThread(void *arg);

    // Background update-check helpers.
    void ChkStart();
    void ChkTick();   // poll progress / finish; called each frame while running
    void ChkFinish(); // handle the fetched (or failed) result on the UI thread
    static void ChkThread(void *arg);

    // Background metadata load helpers.
    void StartMetaLoad(const std::string &id, const std::string &base,
                       const std::string &target, bool force,
                       const std::string &done_subtitle);
    void MetaTick();
    static void MetaThread(void *arg);
};
