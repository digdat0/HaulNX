#pragma once

#include <pu/Plutonium>
#include <string>
#include <vector>
#include <set>
#include <switch.h>
#include "TableList.hpp"

class MainLayout : public pu::ui::Layout {
  private:
    pu::ui::elm::Rectangle::Ref header;
    pu::ui::elm::Rectangle::Ref tab_bar;
    pu::ui::elm::Rectangle::Ref footer;
    pu::ui::elm::TextBlock::Ref title;
    pu::ui::elm::TextBlock::Ref status;
    pu::ui::elm::TextBlock::Ref net_icon;
    pu::ui::elm::TextBlock::Ref bat_info;
    pu::ui::elm::TextBlock::Ref rom_info;
    std::vector<pu::ui::elm::TextBlock::Ref> footer_segs;
    TableList::Ref list;
    std::vector<pu::ui::elm::TextBlock::Ref> tabs;
    pu::ui::elm::Rectangle::Ref tab_underline;

  public:
    MainLayout();
    PU_SMART_CTOR(MainLayout)

    void SetTitle(const std::string &t);
    void SetStatus(const std::string &t);
    void SetNetColor(pu::ui::Color c);
    void SetNetIcon(const std::string &text, pu::ui::Color c);
    void SetBatInfo(const std::string &t);
    void SetSubtitle(const std::string &t);
    void SetRomInfo(const std::string &t);
    void SetActiveTab(int idx); // 0=Browse 1=Installed 2=Queue 3=Settings
    void RefreshTabs();
    void ApplyTheme();
    void ClearMenu();
    void AddRow(const std::string &name);
    void AddRow(const std::string &name, pu::ui::Color clr);
    void AddRow2(const std::string &left, const std::string &right,
                 pu::ui::Color lclr, pu::ui::Color rclr, float progress = -1.0f);
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
        Downloads, // manage downloads folder
        Language,  // language selector
        Search,    // global file search across cached repos
        Cache,     // metadata cache management
        ManageData, // settings submenu: downloads folder + metadata cache
        ViewLogs,  // settings submenu: download log + debug log
        DebugLog   // debug.log viewer
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

    void GotoHome();
    void GotoRepos(int ci);
    void GotoFiles(int ci, int ri);
    void GotoQueue();
    void GotoSettings();
    void GotoInstalled(const std::string &path);
    void GotoRepoEdit(int ci, int ri);
    void GotoPicker(Pending what);
    void GotoLog();
    void GotoManage();
    void GotoCreds();
    void GotoAdvanced();
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
