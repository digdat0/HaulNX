#pragma once

#include <pu/Plutonium>
#include <string>

class MainLayout : public pu::ui::Layout {
  private:
    pu::ui::elm::Rectangle::Ref header;
    pu::ui::elm::TextBlock::Ref title;
    pu::ui::elm::TextBlock::Ref status;
    pu::ui::elm::TextBlock::Ref subtitle;
    pu::ui::elm::Menu::Ref menu;

  public:
    MainLayout();
    PU_SMART_CTOR(MainLayout)

    void SetTitle(const std::string &t);
    void SetStatus(const std::string &t);
    void SetSubtitle(const std::string &t);
    void ClearMenu();
    void AddRow(const std::string &name);
    s32 Sel();
    void SetSel(s32 i);
    s32 RowCount();
    void MoveBy(s32 delta);
    void MoveUp();
    void MoveDown();
    void PageUp();
    void PageDown();
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
        Log
    };
    enum class Pending { None, AddRepo, Manual };

  private:
    MainLayout::Ref layout;
    Screen screen;
    int sel_ci;
    int sel_ri;
    Pending pending;
    std::string pending_id;  // archive id for a Manual-URL download
    std::string inst_path;   // current dir in the installed browser
    Screen log_origin;       // screen to return to from the log viewer

  public:
    using Application::Application;
    PU_SMART_CTOR(MainApplication)

    void OnLoad() override;
    void Shutdown();

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

    void RefreshStatus();
    void HandleInput(u64 down, u64 held);
};
