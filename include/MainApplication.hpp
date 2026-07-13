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

// One footer hint segment ("A cancel") rendered as Switch-style button chips:
// each recognised button token gets a small rounded chip, with its label text
// after it. Owns the rendered text textures; rebuilt lazily when the hint or
// label colour changes.
class FooterHintElement : public pu::ui::elm::Element {
    struct Pair {
        pu::sdl2::Texture btn; // owned
        pu::sdl2::Texture lbl; // owned
        s32 bw, bh, lw, lh;
    };
    s32 x, y, h;
    std::string text, font;
    pu::ui::Color lbl_clr;
    std::vector<Pair> pairs;
    s32 width;
    bool dirty;

    static constexpr s32 ChipPadX = 10;
    static constexpr s32 ChipPadY = 4;
    static constexpr s32 BtnGap = 8;   // chip -> its label
    static constexpr s32 PairGap = 16; // label -> next chip

    static bool IsButtonToken(const std::string &t) {
        static const char *toks[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR",
                                     "L/R", "ZL/ZR", "+", "-", "◀", "▶", "◀▶"};
        for (auto s : toks) {
            if (t == s) {
                return true;
            }
        }
        return false;
    }

    void FreePairs() {
        for (auto &p : this->pairs) {
            if (p.btn) {
                pu::ui::render::DeleteTexture(p.btn);
            }
            if (p.lbl) {
                pu::ui::render::DeleteTexture(p.lbl);
            }
        }
        this->pairs.clear();
    }

    void Rebuild() {
        this->FreePairs();
        this->width = 0;
        // Tokenize on single spaces; a button token opens a new chip+label
        // pair, any other token joins the current pair's label.
        std::vector<std::pair<std::string, std::string>> parts;
        size_t i = 0;
        while (i < this->text.size()) {
            size_t e = this->text.find(' ', i);
            if (e == std::string::npos) {
                e = this->text.size();
            }
            std::string tok = this->text.substr(i, e - i);
            i = e + 1;
            if (tok.empty()) {
                continue;
            }
            if (IsButtonToken(tok)) {
                parts.push_back({tok, ""});
            } else if (parts.empty()) {
                parts.push_back({"", tok});
            } else {
                auto &lbl = parts.back().second;
                if (!lbl.empty()) {
                    lbl += " ";
                }
                lbl += tok;
            }
        }
        for (auto &pp : parts) {
            Pair p{nullptr, nullptr, 0, 0, 0, 0};
            if (!pp.first.empty()) {
                p.btn = pu::ui::render::RenderText(
                    this->font, pp.first, pu::ui::Color(240, 242, 246, 255));
                p.bw = pu::ui::render::GetTextureWidth(p.btn);
                p.bh = pu::ui::render::GetTextureHeight(p.btn);
            }
            if (!pp.second.empty()) {
                p.lbl = pu::ui::render::RenderText(this->font, pp.second,
                                                   this->lbl_clr);
                p.lw = pu::ui::render::GetTextureWidth(p.lbl);
                p.lh = pu::ui::render::GetTextureHeight(p.lbl);
            }
            if (this->width > 0) {
                this->width += PairGap;
            }
            if (p.btn) {
                this->width += p.bw + 2 * ChipPadX;
                if (p.lbl) {
                    this->width += BtnGap;
                }
            }
            if (p.lbl) {
                this->width += p.lw;
            }
            this->pairs.push_back(p);
        }
        this->dirty = false;
    }

  public:
    FooterHintElement(s32 x, s32 y, s32 h)
        : x(x), y(y), h(h), lbl_clr(192, 199, 210, 255), width(0),
          dirty(false) {
        this->font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    }
    PU_SMART_CTOR(FooterHintElement)
    ~FooterHintElement() { this->FreePairs(); }
    void SetHint(const std::string &t) {
        if (t == this->text) {
            return;
        }
        this->text = t;
        this->dirty = true;
    }
    void SetLabelColor(pu::ui::Color c) {
        this->lbl_clr = c;
        this->dirty = true;
    }
    s32 Width() {
        if (this->dirty) {
            this->Rebuild();
        }
        return this->width;
    }
    void SetX(s32 nx) { this->x = nx; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->width; }
    s32 GetHeight() override { return this->h; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (this->dirty) {
            this->Rebuild();
        }
        s32 cx = rx;
        for (auto &p : this->pairs) {
            if (p.btn) {
                s32 ch = p.bh + 2 * ChipPadY;
                s32 cy = ry + (this->h - ch) / 2;
                drawer->RenderRoundedRectangleFill(
                    pu::ui::Color(255, 255, 255, 28), cx, cy,
                    p.bw + 2 * ChipPadX, ch, ch / 2);
                drawer->RenderTexture(p.btn, cx + ChipPadX, cy + ChipPadY);
                cx += p.bw + 2 * ChipPadX;
                if (p.lbl) {
                    cx += BtnGap;
                }
            }
            if (p.lbl) {
                drawer->RenderTexture(p.lbl, cx, ry + (this->h - p.lh) / 2);
                cx += p.lw;
            }
            cx += PairGap;
        }
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

// Drawn Wi-Fi style signal bars for the header status area: three ascending
// bars, lit up to the current level in logo green; dim red bars when offline.
class NetBarsElement : public pu::ui::elm::Element {
    s32 x, y;
    int lit; // number of bars lit (1..3); -1 = disconnected
  public:
    NetBarsElement(s32 x, s32 y) : x(x), y(y), lit(-1) {}
    PU_SMART_CTOR(NetBarsElement)
    void SetLevel(int l) { this->lit = l; }
    void SetPos(s32 nx, s32 ny) { this->x = nx; this->y = ny; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return 27; } // 3 bars of 7px + 2 gaps of 3px
    s32 GetHeight() override { return 24; }
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        for (int i = 0; i < 3; i++) {
            s32 bh = 8 + i * 8; // 8 / 16 / 24
            pu::ui::Color c = this->lit < 0 ? pu::ui::Color(200, 60, 60, 120)
                              : this->lit > i
                                  ? pu::ui::Color(146, 214, 36, 255)
                                  : pu::ui::Color(255, 255, 255, 45);
            drawer->RenderRoundedRectangleFill(c, rx + i * 10, ry + 24 - bh, 7,
                                               bh, 2);
        }
    }
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}
};

// Drawn battery outline for the header: vertical body + top nub, filled from
// the bottom by percent. Green normally, amber low, red critical, blue while
// charging. The header shell is charcoal in both themes, colours are constant.
class BatteryElement : public pu::ui::elm::Element {
    s32 x, y;
    int pct; // -1 = unknown (hidden)
    bool charging;
  public:
    BatteryElement(s32 x, s32 y) : x(x), y(y), pct(-1), charging(false) {}
    PU_SMART_CTOR(BatteryElement)
    void Set(int p, bool chg) { this->pct = p; this->charging = chg; }
    void SetPos(s32 nx, s32 ny) { this->x = nx; this->y = ny; }
    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return 14; }
    s32 GetHeight() override { return 28; } // 3 nub + 1 gap + 24 body
    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (this->pct < 0) {
            return;
        }
        const pu::ui::Color shell(255, 255, 255, 130);
        drawer->RenderRoundedRectangleFill(shell, rx + 4, ry, 6, 3, 1);
        drawer->RenderRoundedRectangle(shell, rx, ry + 4, 14, 24, 3);
        s32 fh = (18 * (this->pct > 100 ? 100 : this->pct)) / 100;
        if (fh > 0) {
            pu::ui::Color c = this->charging ? pu::ui::Color(66, 138, 230, 255)
                              : this->pct <= 15
                                  ? pu::ui::Color(224, 78, 78, 255)
                              : this->pct <= 30
                                  ? pu::ui::Color(245, 175, 95, 255)
                                  : pu::ui::Color(146, 214, 36, 255);
            drawer->RenderRoundedRectangleFill(c, rx + 3,
                                               ry + 7 + (18 - fh), 8, fh, 2);
        }
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
    pu::ui::elm::TextBlock::Ref title; // first breadcrumb segment
    // Breadcrumb continuation: green "›" separators + up to two more path
    // segments ("SNES › repo"); hidden off-screen when unused.
    std::vector<pu::ui::elm::TextBlock::Ref> bc_seps, bc_parts;
    s32 bc_end_x = 0; // right edge of the breadcrumb (for the title icon)
    s32 title_x0 = 0; // fixed breadcrumb anchor after the wordmark
    IconElement::Ref title_icon; // console icon shown after the title text
    pu::ui::elm::TextBlock::Ref status;
    NetBarsElement::Ref net_bars;   // drawn signal bars (was a text glyph)
    BatteryElement::Ref bat_icon;   // drawn battery outline
    pu::ui::elm::TextBlock::Ref bat_info;
    pu::ui::elm::TextBlock::Ref rom_info;
    std::vector<FooterHintElement::Ref> footer_segs;
    TableList::Ref list;
    CardGrid::Ref grid;    // card view for the console lists
    bool cards_mode = false; // which of list/grid is active for this screen
    std::vector<pu::ui::elm::TextBlock::Ref> tabs;
    PillElement::Ref tab_pill;        // rounded highlight behind the active tab
    GradientLineElement::Ref accent_line; // green->blue strip under the shell
    PulseDotElement::Ref queue_dot;   // "downloads running" pulse on the Queue tab
    IconElement::Ref empty_icon;      // big dimmed icon for empty states
    pu::ui::elm::TextBlock::Ref empty_text;
    pu::ui::elm::TextBlock::Ref empty_hint; // smaller "what to do" line
    SpinnerElement::Ref spinner;      // background-work indicator

  public:
    MainLayout();
    PU_SMART_CTOR(MainLayout)

    // Empty state (big centred icon + message) shown when a list has nothing.
    void SetEmptyState(pu::sdl2::Texture icon, const std::string &msg,
                       const std::string &hint = "");
    void ClearEmptyState();
    // Loading spinner overlay.
    void ShowSpinner(const std::string &msg);
    void HideSpinner();

    void SetTitle(const std::string &t);
    void SetTitleIcon(pu::sdl2::Texture tex); // console icon after the title
    void SetStatus(const std::string &t);
    void SetNetLevel(int lit); // bars lit 1..3, -1 = disconnected
    void SetBattery(int pct, bool charging);
    void SetBatInfo(const std::string &t);
    void SetSubtitle(const std::string &t);
    void SetRomInfo(const std::string &t);
    void SetActiveTab(int idx); // 0=Browse 1=Installed 2=Queue 3=Settings
    void SetQueueActivity(bool active); // pulse the Queue tab while downloading
    void RefreshTabs();
    void ApplyTheme();
    // Bake the list/grid tiles up front so the first screen doesn't hitch.
    void PrewarmTiles() { this->list->PrewarmTiles(); this->grid->PrewarmTiles(); }
    // True if queue card i could be on screen (skip off-screen text building).
    bool QueueCardVisible(s32 i) { return this->grid->QueueIndexVisible(i); }
    // fade=false skips the list's enter fade (per-frame queue rebuilds).
    void ClearMenu(bool fade = true);
    void AddRow(const std::string &name);
    void AddRow(const std::string &name, pu::ui::Color clr,
                pu::sdl2::Texture icon = nullptr, bool pin = false);
    void AddRow2(const std::string &left, const std::string &right,
                 pu::ui::Color lclr, pu::ui::Color rclr, float progress = -1.0f,
                 pu::sdl2::Texture icon = nullptr,
                 const std::string &prefix = "", bool accent = false,
                 bool pill = true, bool pin = false, s32 bar = 0);
    // Card view (console lists). ClearMenu resets to list mode; a screen that
    // wants cards calls SetCardsMode(true) and AddCard instead of AddRow.
    void SetCardsMode(bool on);
    bool InCards() const { return this->cards_mode; }
    void AddCard(const std::string &title, const std::string &subtitle,
                 pu::sdl2::Texture icon, bool pinned = false,
                 bool dim = false);
    // Queue card view: per-frame diff updates instead of Clear + AddCard.
    // Single-card mode shows one enlarged centred card (self-update).
    void SetSingleCard(bool on);
    void SetQueueCount(s32 n);
    void SetQueueCard(s32 i, const std::string &console,
                      pu::sdl2::Texture icon, const std::string &status,
                      pu::ui::Color st_clr, const std::string &size,
                      const std::string &speed, const std::string &eta,
                      const std::string &file, float prog, bool hero,
                      s32 ring = 0, s32 qpos = 0, bool refresh_text = true);
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
        QueueState, // persisted queue-data (queue.json) viewer
        InstSearch, // search across installed games (roms folder)
        RomPicker  // SD-card folder browser for choosing a custom ROM root
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
    std::string picker_path; // current dir in the ROM-folder picker
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
    void GotoRomPicker(const std::string &path);
    void GotoUISettings();
    void GotoDownloads();
    void GotoLanguage();
    void GotoSearch(const std::string &query);
    void GotoCache();
    void GotoManageData();
    void GotoViewLogs();
    void GotoDebugLog();
    void GotoQueueState();

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
