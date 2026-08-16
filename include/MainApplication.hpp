#pragma once

#include <pu/Plutonium>
#include <atomic>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <set>
#include <map>
#include <switch.h>
#include "TableList.hpp"
#include "CardGrid.hpp"
#include "httpsrv.h"
#include "net.h"
#include "dat.h"
#include "verify.h"
#include "iarchive.h"
#include "updman.h"

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
// accent underline beneath the active tab label (a short, thin rounded bar).
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
    // Leading button glyph of this hint ("A", "X", "L/R", ...), "" if none.
    // Used to make a touch on the footer act like pressing that button.
    std::string Token() const {
        size_t e = this->text.find(' ');
        std::string first =
            (e == std::string::npos) ? this->text : this->text.substr(0, e);
        return IsButtonToken(first) ? first : std::string();
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
    // Two-tone "HaulNX" wordmark (green Haul / blue NX), fixed in the header;
    // `title` holds only the per-screen breadcrumb after it. Member names are
    // legacy from the old three-part lockup; wm_plus is now unused.
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
    PillElement::Ref tab_pill;        // accent underline under the active tab
    GradientLineElement::Ref accent_line; // green->blue strip under the shell
    PulseDotElement::Ref queue_dot;   // "downloads running" pulse on the Queue tab
    PulseDotElement::Ref settings_dot; // "update available" pulse on the Settings tab
    IconElement::Ref empty_icon;      // big dimmed icon for empty states
    pu::ui::elm::TextBlock::Ref empty_text;
    pu::ui::elm::TextBlock::Ref empty_code; // big transfer code under the URL
    pu::ui::elm::TextBlock::Ref empty_hint; // smaller "what to do" line
    // Last font + unwrapped hint applied to empty_hint. Some screens (the Queue
    // tab) re-assert their empty state every frame, and both SetFont and the
    // word-wrap measuring pass are expensive enough to matter at 60fps — SetFont
    // re-renders the texture unconditionally. Wrapping is skipped and the font
    // left alone while neither input has changed.
    std::string empty_hint_font, empty_hint_raw;
    // Accent chip under the empty-state hint (a filled rounded pill + its text),
    // used by the import page to call out the app-utility alternative. Hidden
    // unless SetEmptyState is given a note.
    pu::ui::elm::Rectangle::Ref empty_chip;
    pu::ui::elm::TextBlock::Ref empty_chip_text;
    SpinnerElement::Ref spinner;      // background-work indicator

  public:
    MainLayout();
    PU_SMART_CTOR(MainLayout)

    // Empty state (big centred icon + message) shown when a list has nothing.
    // spacious=true gives the roomier import "instruction sheet" layout
    // (icon lifted, gap under the message, larger hint text).
    void SetEmptyState(pu::sdl2::Texture icon, const std::string &msg,
                       const std::string &hint = "", bool spacious = false,
                       const std::string &note = "", const std::string &code = "");
    void ClearEmptyState();
    // Loading spinner overlay.
    void ShowSpinner(const std::string &msg);
    void HideSpinner();
    // Hit-test a touch against the footer hint chips: returns the HidNpadButton
    // mask for the tapped hint (so a tap acts like pressing that button), or 0.
    u64 FooterButtonAt(s32 tx, s32 ty) const;

    void SetTitle(const std::string &t);
    void SetTitleIcon(pu::sdl2::Texture tex); // console icon after the title
    void SetStatus(const std::string &t);
    void SetNetLevel(int lit); // bars lit 1..3, -1 = disconnected
    void SetBattery(int pct, bool charging);
    void SetBatInfo(const std::string &t);
    void SetSubtitle(const std::string &t);
    void SetRomInfo(const std::string &t);
    void SetActiveTab(int idx); // 0=Library 1=Add 2=Queue 3=Settings
    void SetQueueActivity(bool active); // pulse the Queue tab while downloading
    void SetUpdateAvailable(bool avail); // pulse the Settings tab when an update is up
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
    // Update one row's right cell in place (no rebuild → scroll/selection kept).
    void SetRowRight(s32 i, const std::string &right, pu::ui::Color rclr);
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
    void SetMark(s32 i, bool on);
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
        DlPrefs,  // Downloads settings (concurrency/rate/skip/keep-awake)
        Appearance, // Appearance settings (theme/cards/group/language)
        ExtFilter, // Browse file-view extension filter editor
        Downloads, // manage downloads folder
        Language,  // language selector
        Search,    // global file search across cached repos
        Cache,     // metadata cache management
        Transfers, // settings submenu: import/export/restore/update over Wi-Fi
        Sources,   // settings submenu: manage consoles/repos + file-type filter
        Storage,   // settings submenu: SD space, ROM folder, temp + caches
        InstallFolders, // Storage sub-screen: per-console custom install folders
        Backups,   // Storage sub-screen: emulator/app rollback backups (view/delete)
        Account,   // settings submenu: archive.org creds + startup net check
        Updates,   // settings submenu: check now + auto-check toggle
        Diagnostics, // settings submenu: logs, self-test, tuning, reset
        About,     // settings submenu: getting started, release notes, credits
        SpeedTest, // Diagnostics sub-screen: live download/upload meters
        ViewLogs,  // settings submenu: download log + debug log
        DebugLog,  // debug.log viewer
        QueueState, // persisted queue-data (queue.json) viewer
        InstSearch, // search across installed games (roms folder)
        RomPicker, // SD-card folder browser for choosing a custom ROM root
        Import,    // waiting for a dl_sources.json upload over the LAN
        LiveRecv,  // transient: a file is arriving over the always-on live link
        RecvConsole, // Settings' "Install from PC": pick which console to fill
        ReleaseNotes, // GitHub release list (version + date)
        ReleaseNote,  // one release's notes
        Verify,       // DAT verification results for one console (Library tab)
        VerifyMissing, // browsable list of DAT titles absent from the library
        ArchiveSearch, // archive.org catalogue search: pick an item to add as a repo
        Tidy,          // "tidy library" scan: misfiled + duplicate files to fix
        VerifyAll,    // aggregate verify results, one row per console (Library tab)
        SortInbox,    // inbox sorter results: one row per staged file + outcome
        UsbMtp,       // embedded MTP: connect the console to a PC over USB
        Dats,         // Storage sub-screen: DAT files + auto-download (Fresh1G1R)
        RegionOrder,  // Dats sub-screen: 1G1R keep-preference region order
        AppUpdates,   // Updates sub-screen: emulator/app list (check/update/revert)
        LargestFiles, // Storage sub-screen: whole-library files, biggest first
        DataFiles,    // settings submenu: hosts DAT files + metadata cache
        MetaCache,    // Data Files sub-screen: cache on/off, browse, refresh all
        InboxFiles    // Storage sub-screen: view/select/delete files staged in the Inbox
    };
    enum class Pending { None, AddRepo, Manual, SortAssign };
    // Tab positions, left to right. The library is the front door of the app, so
    // Installed sits at 0 (labelled "Library") and Browse at 1 (labelled "Add");
    // Queue/Settings keep 2/3 so the notification-dot positions are unchanged.
    // Enum member names still describe the screen each tab opens.
    enum class Tab { Installed = 0, Browse = 1, Queue = 2, Settings = 3 };

  private:
    MainLayout::Ref layout;
    Screen screen;
    int sel_ci;
    int sel_ri;
    // Scope of the current search results, so B returns to the originating
    // screen and "new search" (Y) keeps the same scope. -1 = all.
    int search_ci = -1;
    int search_ri = -1;
    // Set when this search was launched from the Missing Games "find &
    // download" action: B should return to the Library tab, not to the
    // console's repo list (search_ci is only a match *scope* there, not
    // where the user actually came from).
    bool search_from_missing = false;
    Pending pending;
    std::string pending_id;  // archive id for a Manual-URL download
    std::string inst_path;   // current dir in the installed browser
    std::string picker_path; // current dir in the ROM-folder picker
    uint8_t appman_kind = 0; // Screen::AppUpdates section: UPD_KIND_EMU / _APP
    std::vector<UpdSource> appman_list; // entries shown on Screen::AppUpdates
    s32 appman_sel = 0;      // selection to restore after the list is (re)built
    int picker_console = -1; // ROM-folder picker target: -1 = the ROM root, else
                             // the console index whose custom install folder is
                             // being chosen (returns to that console's screen)
    bool picker_from_installed = false; // true when the per-console picker was
                             // opened from the Installed tab (so it returns
                             // there instead of the Storage folder list)
    Screen log_origin;       // screen to return to from the log viewer
    // Which file the shared text-log viewer is showing, and its labels.
    std::string log_view_path;
    std::string log_view_title;
    int log_clear_msg = 0;

    // One-shot startup dialogs (e.g. no network), run on the first
    // frame of the UI loop — OnLoad is too early to render a dialog.
    bool startup_checks = false;

    // Remembered list positions, so backing out and returning keeps your place.
    int home_sel = 0;
    int inst_sel = 0;   // Installed/Library root console list: last cursor row
    int repos_sel = 0;
    int repos_sel_ci = -1;
    int files_sel = 0;
    std::string files_sel_id;

    // One background worker: the Thread plus the running/done handshake every
    // background task below shares. Task-specific state (cancel flags, progress
    // counters) stays with its task. The worker sets `done`; the main thread
    // polls it from OnInput and calls Join(), which is the barrier that makes
    // the worker's writes visible before any result is read.
    struct BgTask {
        Thread thread;
        std::atomic<bool> running{false};
        std::atomic<bool> done{false};

        // Spawn entry(arg). False means the thread couldn't be created, and the
        // caller is expected to do the work inline instead.
        bool Start(ThreadFunc entry, void *arg) {
            this->done = false;
            Result rc = threadCreate(&this->thread, entry, arg, NULL, 0x40000,
                                     0x2C, -2);
            if (R_FAILED(rc) || R_FAILED(threadStart(&this->thread))) {
                return false;
            }
            this->running = true;
            return true;
        }

        // Reap a finished worker and go idle. No-op when not running.
        void Join() {
            if (!this->running) {
                return;
            }
            threadWaitForExit(&this->thread);
            threadClose(&this->thread);
            this->running = false;
        }
    };

    // In-app self-update download. Runs on its own thread so the UI keeps
    // rendering progress instead of looking hung; the install itself happens
    // back on the main thread (romfs/applet calls) once the download finishes.
    BgTask upd;
    std::atomic<bool> upd_ok{false};
    std::atomic<bool> upd_cancel{false};
    std::atomic<u64> upd_now{0};
    std::atomic<u64> upd_total{0};
    std::string upd_url;
    std::string upd_dl;
    std::string upd_tag;
    int upd_xslot = -1; // queue-tab external item tracking this download (-1 = none)

    // Tools update-manager install (same shape as the self-update above): a
    // background download of a chosen emulator/app release, finalized in UmiTick.
    BgTask umi;
    std::atomic<bool> umi_ok{false};
    std::atomic<bool> umi_cancel{false};
    std::atomic<u64> umi_now{0};
    std::atomic<u64> umi_total{0};
    std::string umi_url;      // release asset download URL
    std::string umi_dl;       // temp .part path
    std::string umi_dest;     // final on-device .nro path
    std::string umi_id;       // manifest id (backup folder)
    std::string umi_name;     // display name (logs/queue item)
    std::string umi_tag;      // version being installed
    std::string umi_bakver;   // version being replaced (backup filename)
    bool umi_fresh = false;   // true = fresh install, false = in-place update
    bool umi_zip = false;     // asset is an archive: extract, then install the .nro inside
    std::string umi_asset;    // release asset filename (its extension picks .nro vs zip)
    int umi_xslot = -1;

    // Background update *check* (release-list fetch), so "Check for updates"
    // doesn't freeze the UI during retries. Shows the attempt number (1/3).
    BgTask chk;
    std::atomic<bool> chk_ok{false};
    std::atomic<bool> chk_discard{false}; // B pressed: drop the result silently
    // Written by update_fetch_latest() in C, so it keeps the C-compatible
    // volatile int the update.h API takes; read-only (display) on the UI side.
    volatile int chk_attempt = 1;
    char chk_tag[64];
    char chk_url[1024];

    // Silent startup update check: a separate task from `chk` so it never owns
    // the UI (no dialog, no progress). On completion the result only lights the
    // Settings-tab dot + "Update available" chip; the user still taps "Check for
    // updates" to act. update_available latches true for the session.
    BgTask bgchk;
    std::atomic<bool> bgchk_ok{false};
    bool update_available = false;
    bool update_installed = false; // new build staged this session; awaiting restart
    char bgchk_tag[64];
    char bgchk_url[1024];

    // Background release check for the emulator/app update list (Screen::
    // AppUpdates): a worker rebuilds appman_list, reads each installed build's
    // version, and (for entries with a source) fetches the latest GitHub tag so
    // every row shows a clear Update / Up-to-date badge instead of looking the
    // same. The screen owns the UI (spinner + n/total) while it runs.
    BgTask appchk;
    std::atomic<bool> appchk_cancel{false};
    std::atomic<bool> appchk_net{false};  // do the network release check this run?
                                          // false = load versions only (open),
                                          // true = "check all" (X on the list)
    std::atomic<int> appchk_idx{0};   // network checks completed (progress)
    std::atomic<int> appchk_total{0}; // network checks to run
    std::vector<int8_t> appman_state; // per-entry APST_* (see AppChkThread)
    std::vector<std::string> appman_ver;    // installed version per entry ("" none)
    std::vector<std::string> appman_latest; // latest release tag per entry
    std::vector<std::string> appman_url;    // cached release .nro/.zip url per entry
    std::vector<std::string> appman_asset;  // cached asset hint per entry
    // When each entry was last checked against GitHub, keyed by id (epoch secs).
    // Persisted to UPDCHK_PATH so "checked N ago" survives leaving the screen and
    // relaunching; loaded lazily on first use. See load/save_check_times.
    std::map<std::string, uint64_t> appman_checked_at;
    bool appman_checked_loaded = false;

    // Background network self-test (Diagnostics -> Network self-test): checks
    // the LAN address and reaches archive.org off the UI thread so a slow or
    // dead connection can't freeze the app. The result only feeds a dialog.
    BgTask diag;
    std::atomic<bool> diag_lan{false}; // have a local IP (network is up)
    std::atomic<bool> diag_net{false}; // archive.org responded 2xx
    char diag_ip[46] = "";
    // The diag BgTask is shared between the self-test and the speed test;
    // diag_speed picks which live view + result DiagTick shows.
    bool diag_speed = false;
    bool diag_sp_ok = false;       // both phases finished cleanly
    bool diag_sp_cancelled = false; // user backed out mid-test
    double diag_mbps = 0.0;        // final download rate (Mbps)
    double diag_ul_mbps = 0.0;     // final upload rate (Mbps)
    // Live counters shared with the worker thread; the UI reads them each frame
    // to draw the meters and sets sp_prog.cancel to abort. See net.h SpeedProg.
    SpeedProg sp_prog{};

    // Background bulk metadata refresh (Manage data -> Refresh all metadata):
    // force-fetches every enabled repo's file list, with live (n/total)
    // progress and B to cancel between repos.
    BgTask ra;
    std::atomic<bool> ra_cancel{false};
    std::atomic<int> ra_idx{0};
    std::atomic<int> ra_total{0};
    std::atomic<int> ra_ok{0};
    std::atomic<int> ra_fail{0};

    // Background DAT auto-download (Storage -> DAT files -> Refresh): fetches the
    // Fresh1G1R McLean/no-intro listing once, then pulls the matching .dat for
    // each configured console into DATS_DIR. Same progress/cancel handshake as
    // the metadata refresh above.
    BgTask dat;
    std::atomic<bool> dat_cancel{false};
    std::atomic<int> dat_idx{0};
    std::atomic<int> dat_total{0};
    std::atomic<int> dat_ok{0};
    std::atomic<int> dat_fail{0};
    std::atomic<bool> dat_listing_fail{false}; // couldn't fetch the repo listing
    std::atomic<long> dat_listing_code{0}; // HTTP status of the failed listing GET
    int dat_xslot = -1; // queue-tab external item tracking the DAT batch (-1 = none)

    // Set when a Verify/Have-vs-Missing attempt found no DAT on the SD card but
    // one is downloadable, and the user agreed to fetch it on the spot. DatSyncTick
    // re-enters VerifyStart with these once that single-item sync lands, so
    // "no DAT yet" resolves in one prompt instead of a dead end.
    bool dat_sync_then_verify = false;
    std::string dsv_folder, dsv_target, dsv_label;
    bool dsv_force = false, dsv_goto_missing = false;

    // Background metadata (ia_fetch) load, so the file list doesn't freeze the
    // UI while a repo's metadata downloads. Shows an animated loading indicator.
    BgTask meta;
    std::atomic<bool> meta_ok{false};
    // B pressed while loading: the fetch can't be aborted mid-request, so it
    // finishes in the background and its result is discarded when reaped.
    std::atomic<bool> meta_discard{false};
    bool meta_force = false;
    std::string meta_done_subtitle;

    // Set once the user accepts a >4 GiB download despite the FAT32 size limit,
    // so queueing several large files in a row doesn't re-prompt each time.
    bool fat32_ack = false;

    // Background search scan: walks the metadata cache off the main thread so a
    // large cache shows an animated "Searching..." spinner instead of freezing.
    // B cancels: the scan bails out early and the results screen is skipped.
    BgTask search;
    std::atomic<bool> search_discard{false}; // B pressed: drop the scan result

    // Background Installed-tab search: same threaded/cancellable model as the
    // cache search above, so scanning a large ROM folder doesn't freeze the UI.
    BgTask isearch;
    std::atomic<bool> isearch_discard{false}; // B pressed: drop the scan result

    // Background archive.org catalogue search (discover items to add as repos).
    // Same threaded/cancellable model; the network request runs off the UI
    // thread so the "Searching..." spinner animates instead of the app hanging.
    BgTask arch;
    std::atomic<bool> arch_discard{false};    // B pressed: drop the search result
    std::string arch_query;                   // the query being searched
    std::string arch_console;                 // console a picked item is added to
    std::vector<ArchiveSearchItem> arch_hits; // results (worker writes, UI reads)
    volatile int arch_count = 0;              // worker: hit count, or -1 on error

    // Background "move to parent folder" for one or more Installed files. Runs on
    // a worker so the progress overlay renders. Deliberately has no cancel: a
    // half-cancelled batch is worse than one that finishes. Each file is an
    // atomic same-volume rename() (fs_move), so an app close or power-off
    // mid-batch leaves every file wholly in its old or new folder — never
    // partial, never corrupt — and the next launch simply re-lists what is on
    // disk. mv_names/mv_from/mv_to are fixed before the worker starts
    // (read-only on the worker thread); the counters are atomics.
    BgTask mv;
    std::atomic<int> mv_idx{0};
    std::atomic<int> mv_total{0};
    std::atomic<int> mv_ok{0};
    std::atomic<int> mv_fail{0};
    std::string mv_from; // folder the files live in now (the subfolder)
    std::string mv_to;   // parent folder they move into
    std::vector<std::string> mv_names; // entries to move

    // Background release-notes fetch (Settings -> View logs -> Release notes):
    // pulls the GitHub release history off the main thread so it doesn't freeze
    // during retries. The worker fills g_relnotes; the UI renders it when done.
    BgTask notes;
    std::atomic<bool> notes_ok{false};
    Screen notes_origin = Screen::ViewLogs; // where B leaves the release list

    // LAN collection import: a tiny HTTP server the user's PC uploads
    // dl_sources.json to. No thread — it is polled once per frame and only
    // exists while the Import screen is open.
    HttpSrv imp_srv;
    // Always-on read-only inventory server for the desktop companion. Distinct
    // from imp_srv (its own port): it stays up while the pref is on and is polled
    // every frame regardless of screen. inv_last_gen_ns throttles regeneration.
    HttpSrv inv_srv;
    bool inv_open = false;
    uint64_t inv_last_gen_ns = 0;
    // Set (to a tick) every time a push/pull request finishes on the inventory
    // server, so the companion_active inventory rescan below never lands on the
    // exact frame right after a completed request. WriteInventoryJson is a full
    // recursive directory sweep -- cheap for one console, but easily long enough
    // on a big library to miss a render frame -- and it runs on the very thread
    // that also owns httpsrv_poll's accept(). A desktop push queue reconnects for
    // its next file within milliseconds of the prior file's 200 OK, so without
    // this a multi-file Wi-Fi push (each one resets inv_last_gen_ns to 0 to force
    // a prompt regen) could have the rescan block accept() just long enough for
    // the next file's freshly-accepted connection to get reset (os error 10054).
    // See wifi-push-inventory-regen memory.
    uint64_t inv_push_cd_tick = 0;
    // Sleep/wake recovery for the always-on server. Sleeping drops Wi-Fi, which
    // leaves inv_srv listening on a dead interface — "on" but unreachable — until
    // the user toggles it. InvServerPoll watches the link (throttled by
    // inv_link_ck_ns) and rebinds the socket on a down->up edge (inv_link_up
    // tracks the last-seen state). See httpsrv_rebind.
    bool inv_link_up = true;       // was the Wi-Fi link up at the last check?
    uint64_t inv_link_ck_ns = 0;   // throttle the nifm poll (it is an IPC call)
    // Resume-from-sleep detection. The link watcher above only rebinds on an
    // observed down->up edge or a lease change, and neither happens on a short
    // sleep that keeps the same DHCP lease: the app doesn't run while asleep, so
    // on wake inv_link_up is still the pre-sleep "up" and the address matches, no
    // edge fires, and the socket stays dead on the interface sleep tore down.
    // InvServerPoll runs every frame, so a large jump in the RTC (which advances
    // during sleep, unlike frame cadence) between two consecutive polls can only
    // mean a suspend/resume; that forces one unconditional httpsrv_rebind. 0 =
    // not yet seeded (first poll after start just records the clock).
    int64_t inv_wake_ck_s = 0;     // RTC epoch seconds at the last poll
    // Bumped (to a monotonic tick) each time the user picks "Push list to PC" on
    // the Updates screen; published in inventory.json as "push_rev". A connected
    // companion baselines this at connect and adopts the device's sources/list
    // when it changes — the console can't open a socket back, so this is how a
    // console-initiated push reaches the polling PC. 0 = never pushed.
    uint64_t inv_push_rev = 0;
    // Newline-separated absolute folders (console folders + inbox) the inventory
    // server's pull/delete endpoints are confined to. Rebuilt by WriteInventoryJson
    // alongside the JSON; inv_srv.roots borrows this, so it must outlive the server.
    std::string inv_roots;
    // A push over the live link (app utility, while connected) shows a transient
    // "Receiving from PC" page. inv_was_receiving edge-detects the start so the
    // page opens once per transfer; inv_recv_active is true while it is shown.
    bool inv_was_receiving = false;
    bool inv_recv_active = false;
    int live_xslot = -1; // queue-tab external item tracking a live-link push (-1 = none)
    Tab inv_recv_ret_tab = Tab::Installed; // where to return when the push ends
    uint64_t live_recv_draw = 0;           // throttle for the progress subtitle
    // Background archive-extract for a Wi-Fi push landed in a console folder
    // (InvApplyFile's X-Dest-Folder branch). The move itself is synchronous
    // (a same-filesystem rename, effectively instant), but extract_archive is
    // not -- doing it inline on the render thread froze InvServerPoll's
    // accept() for the whole unzip, so a second queued push connecting right
    // after the first completed got reset instead of served. Same root cause
    // and fix shape as the USB/MTP hang (see mtp/mtp.cpp's extract worker):
    // move it off-thread and let the render loop carry on. One job at a
    // time -- the desktop's push queue already serializes Wi-Fi pushes.
    BgTask pxt;
    std::string pxt_path;      // archive path, already moved into its console folder
    std::string pxt_dir;       // that folder (extraction destination)
    std::string pxt_name;      // display name, for the completion log line
    std::string pxt_target;    // console key, for the completion log line
    std::atomic<bool> pxt_cancel{false};
    bool imp_open = false;
    bool usb_open = false; // true while the embedded-MTP connect screen is up
    bool usb_from_settings = false; // opened from Settings › Install from PC (vs
                                    // the Library console menu): a plain cancel
                                    // returns to whichever one launched it
    bool usb_seen_conn = false; // a host configured the link this session (so a
                                // later drop means the cable was unplugged)
    bool usb_active = false;    // a file is mid-transfer (updated each tick): while
                                // set, L/R/tab navigation is blocked so an accidental
                                // press can't abort the copy — only B cancels
    bool usb_recvd = false;     // at least one file completed this session: only then
                                // does exit route to the inbox sorter (a plain cancel
                                // with nothing received returns to the invoking tab)
    bool usb_bg = false;        // MTP responder up in the BACKGROUND because the
                                // inventory server is on — so a USB companion is
                                // recognized without opening the connect screen
                                // (parity with the always-on Wi-Fi server)
    uint64_t usb_bg_retry_ns = 0; // throttle background bring-up (fails while docked)
    int usb_status = 0;         // last polled mtp::Status; drives CompanionConnected()
    // Queue-tab external item per MTP transfer this session, keyed by the
    // transfer's session-unique id (mtp::Xfer.id) so the mapping survives the
    // progress ring dropping its oldest entry after 16 files — a positional
    // index would misalign there. .second is the queue slot, or -1 once reaped.
    // Reset on UsbMtpStop.
    std::vector<std::pair<u32, int>> usb_xslots;
    // Anti-ghost-duplicate guard: a yanked cable can make Windows' WPD/MTP
    // transport silently retry an interrupted push a couple of times before it
    // gives up, each retry a brand-new SendObjectInfo/SendObject pair the
    // responder has no way to recognize as a repeat -- which otherwise spawns
    // extra "failed" Queue cards on top of the one real "disconnected" card.
    // Records the (name, console) and tick of the last mirrored transfer to
    // end; UsbMtpTick swallows a same-name/console restart that starts right on
    // its heels instead of giving it its own card. Cleared implicitly by age,
    // not by session — a genuinely new push of the same filename, spaced out
    // normally, still shows.
    std::string usb_last_end_name;
    std::string usb_last_end_console;
    uint64_t    usb_last_end_ns = 0;
    int imp_grace = 0; // >0: a file is in hand, still serving the redirect
    bool imp_onboard = false; // import launched from the first-run welcome
    bool imp_nro = false; // receiver opened from Settings' update-over-Wi-Fi
    bool imp_rom = false; // receiver opened from the Installed tab for a ROM
    bool imp_dat = false; // receiver opened from the Installed tab for a DAT
    bool imp_prog = false; // subtitle currently shows receive progress
    // A ROM receive can be launched two ways: from the Installed tab (return to
    // the console there when done) or from Settings' "Install from PC" console
    // picker (return to that picker). This says which, so ImportReturn lands the
    // user back where they started.
    bool imp_rom_from_settings = false;
    // ROM receive streamed into the inbox instead of a fixed console folder:
    // on completion, run the inbox sorter over the just-received file rather
    // than saving it to one console. Set by RomRecvStart(..., autosort=true).
    bool imp_autosort = false;
    // ROM receive: which console the receiver was opened for, so completion
    // returns to it, and the resolved folder/label for the on-screen text.
    int imp_rom_ci = -1;
    std::string imp_rom_dir;
    std::string imp_rom_label;
    // Multi-file ROM receive: each file the sender pushes becomes a row in a
    // live list (the same style as the USB screen). Completed rows persist
    // while the next file arrives; the sorter/summary runs when the user backs
    // out. The receiver stays open across files instead of closing after one.
    enum RecvStatus { RECV_DONE = 0, RECV_FAILED, RECV_SKIPPED };
    struct RecvEntry { std::string name; uint64_t size; int status; };
    std::vector<RecvEntry> imp_recv;
    uint64_t imp_recv_draw = 0; // last list rebuild tick (10 Hz throttle)
    size_t imp_cur_total = 0;   // Content-Length of the file currently arriving
    int imp_xslot = -1;         // Queue-tab item for the file currently arriving
                                // over Wi-Fi ROM receive (-1 = none in flight)

  public:
    using Application::Application;
    PU_SMART_CTOR(MainApplication)

    void OnLoad() override;
    void Shutdown();
    static void SetLaunchPath(const std::string &p); // argv[0] from main()

    void Toast(const std::string &msg);
    void ToastErr(const std::string &msg);
    bool Confirm(const std::string &title, const std::string &msg,
                 bool yes_default = false);
    // Confirm for a destructive action: red-accented dialog, Cancel is the
    // default. If `permanent`, appends an "unrecoverable" warning line.
    bool ConfirmDanger(const std::string &title, const std::string &msg,
                       bool permanent = false);
    // Slide-out right-side option menu, now the app-wide replacement for the
    // centered Plutonium Dialog (it packs options into an unreadable grid past
    // three). A confirms the highlighted row, B or + closes. Returns the chosen
    // index, or -1 if cancelled. `danger` red-accents the panel for destructive
    // confirmations; `body` renders wrapped subtitle/message text above the list.
    // `from_left` slides the panel in from the left edge instead of the right;
    // the app uses left for the global "Tools" menu and right for per-console
    // "Options", so the two read as distinct at a glance.
    // Optional in-place behavior for one SideMenu row. Selecting `row` runs
    // `on_toggle` (which flips some state and returns the new on/off) WITHOUT
    // sliding the panel out and back, that row shows an ON/OFF badge in
    // green/red, and `footer()` renders a status line pinned to the panel's
    // bottom (e.g. the live IP:port/code). Used by the Tools inventory toggle.
    struct SideMenuLive {
        s32 row = -1;
        bool state = false;
        std::function<bool()> on_toggle;     // returns the new state
        std::function<std::string()> footer; // "" for no footer line
        // Called once per frame while the panel is open. The main render loop is
        // suspended by the modal, so a row backed by a live background service
        // (e.g. the inventory server) uses this to keep polling it — otherwise a
        // companion connecting while the panel is open would go unserved and the
        // footer's connection state could never update.
        std::function<void()> tick;
    };
    // Returned by SideMenu (in place of a row index) when switch_btn is pressed —
    // lets the Tools and per-console Options panels flip to each other (X↔Y).
    static const s32 SIDEMENU_SWITCH = -2;
    s32 SideMenu(const std::string &title, const std::vector<std::string> &opts,
                 s32 sel = 0, const std::string &body = "", bool danger = false,
                 bool from_left = false, pu::sdl2::Texture icon = nullptr,
                 SideMenuLive *live = nullptr, u64 switch_btn = 0);
    // Every centered CreateShowDialog call site funnels through this override so
    // the whole app shares the SideMenu look. It forwards to SideMenu and keeps
    // the base contract: with use_last_opt_as_cancel, a B/cancel returns the
    // last option's index rather than -1. icon/prepare_cb are ignored (the panel
    // carries its own styling).
    s32 CreateShowDialog(const std::string &title, const std::string &content,
                         const std::vector<std::string> &opts,
                         bool use_last_opt_as_cancel,
                         pu::sdl2::TextureHandle::Ref icon = {},
                         pu::ui::Application::DialogPrepareCallback prepare_cb =
                             nullptr);
    // Warn (and confirm) before queueing a download whose bytes, plus those the
    // queue still has outstanding, would exceed free SD space. Returns true to
    // proceed. add_size 0 (size unknown) or an unreadable disk never blocks.
    bool SpaceOkToQueue(uint64_t add_size);
    // Browse file list: X opens the view/selection menu; A with files marked
    // queues the whole selection after showing what it will cost.
    void FilesViewMenu();
    void QueueSelection();

    void GotoHome();
    void GotoRepos(int ci);
    void GotoFiles(int ci, int ri, bool force = false);
    void GotoQueue();
    void GotoSettings();
    void GotoInstalled(const std::string &path);
    void InstSortDialog(); // Installed browser sort picker (shared by X / ◀)
    // The in-folder file Options menu (X inside a console folder): Move to another
    // console, Rename, Sort, Delete. Select (Y) and Delete (▶) stay on their own
    // buttons; the menu just gathers every file action in one place.
    void InstFileMenu();
    void InstRenameSel(); // rename the file under the cursor (shared by menu)
    void InstDeleteSel(); // delete the marked set, else the file under the cursor
    void InstMoveDialog(); // pick a destination console, move the selection there
    // Global, library-wide actions (view inbox, verify all, tidy, 1G1R, USB) as a
    // left-sliding "Tools" panel. Shared by the Library and Browse tabs; nothing
    // in here depends on the highlighted console. Returns true if the user pressed
    // X to flip to the per-console Options panel (the caller reopens it).
    bool ToolsMenu();
    // The per-console Options panel (right slide) for the console at g_inst[i].
    // Returns true if the user pressed Y to flip to the global Tools panel.
    bool ConsoleOptionsMenu(s32 i);
    // The install-folder info / change dialog for the console at g_inst[i]. Lives
    // in the per-console Options menu (was its own Y button before Y became Tools).
    void InstFolderDialog(s32 i);
    // Read-only stats for the console at g_inst[i] (its Options "Console info"
    // row): install folder, installed count/size, DAT status, active repos, tabs.
    void ConsoleInfoDialog(s32 i);
    // Library-wide storage summary (Tools "Storage overview"): SD usage, total
    // installed games/size, DAT coverage, inbox backlog. Read-only.
    void StorageOverview();
    void GotoInstSearch(const std::string &query);
    void ISearchTick();
    void FinishInstSearch();
    static void InstSearchThread(void *arg);

    // archive.org catalogue search -> add a picked item as a repo (Add tab).
    void AddRepoChoose(const std::string &console); // pick: search or paste id
    void GotoArchSearch(const std::string &query, const std::string &console);
    void ArchSearchTick();
    void FinishArchSearch();
    void ArchAddSel(); // A on a result: confirm + add it under arch_console
    static void ArchSearchThread(void *arg);

    // Move selected Installed file(s) up into the parent folder.
    void MvStart(const std::vector<std::string> &names);
    // Move the named file(s) from the current folder into `dest` (an absolute
    // directory). Shared machinery for both "move up one folder" and the
    // move-to-console action; MvStart is just this with dest = parent.
    void MvStartTo(const std::vector<std::string> &names, const std::string &dest);
    void MvTick();   // poll progress / finish; called each frame while running
    void MvFinish(); // on the UI thread: toast, refresh, offer empty-folder delete
    static void MvThread(void *arg);
    void GotoRepoEdit(int ci, int ri);
    void GotoPicker(Pending what);
    void GotoLog();
    void GotoManage();
    void GotoCreds();
    void GotoDlPrefs();
    void GotoRomPicker(const std::string &path);
    void GotoAppearance();
    void GotoExtFilter();
    void GotoDownloads();
    void GotoLanguage();
    // New settings hierarchy: one screen per concern (see GotoSettings).
    void GotoSources();
    void GotoStorage();
    void GotoInstallFolders();
    void GotoInboxFiles(); // Storage sub-screen: view/select/delete Inbox files
    void GotoBackups(); // Storage sub-screen: view/delete rollback backups
    // Rows on Screen::Backups: full path + display label, one per stored build.
    std::vector<std::pair<std::string, std::string>> backup_rows;
    void GotoAccount();
    void GotoUpdates();
    void PushListToPc();          // console-initiated sources/list push (Updates)
    bool CompanionConnected() const; // a companion polled inventory.json within 15s
    void GotoDiagnostics();
    void GotoAbout();
    // Diagnostics / About actions.
    void StorageDetail();   // A on the SD-card status row: used/free breakdown
    void ExportBundle();    // concatenate the logs into one shareable file
    void NetSelfTest();     // kick off the background LAN + archive.org check
    void SpeedTest();       // kick off the background download+upload speed test
    void SpeedRender();     // redraw the live download/upload meters each frame
    void DiagTick();        // poll the self-test; show the result when done
    static void DiagThread(void *arg);
    static void SpeedThread(void *arg);
    void ResetDefaults();   // restore every pref to its built-in default
    void GettingStarted();  // re-runnable first-run onboarding prompt
    // Search cached metadata. scope_ci < 0 searches every repo; scope_ci >= 0
    // restricts to one console; scope_ri >= 0 further restricts to one repo.
    void GotoSearch(const std::string &query, int scope_ci = -1,
                    int scope_ri = -1);
    void SearchTick();
    void FinishSearch();
    static void SearchThread(void *arg);
    void GotoCache();
    void GotoDataFiles(); // settings submenu: hosts DAT files + metadata cache
    void GotoMetaCache(); // Data Files sub-screen: cache on/off, browse, refresh all
    void GotoTransfers();
    void GotoRecvConsole(); // Settings' "Install from PC" console picker
    void GotoViewLogs();
    void GotoDebugLog();
    void GotoXferLog();
    // Shared plain-text log viewer; `clear_msg` is the S_ id of its X confirm.
    void GotoTextLog(std::string path, std::string title, int clear_msg);
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
    static void RaThread(void *arg);   // coordinator: fans out to RaWorker
    static void RaWorker(void *arg);   // one parallel refresh worker

    // DAT files sub-screen (Storage): list what's in DATS_DIR and auto-download
    // matching No-Intro DATs from the Fresh1G1R repo, one per console.
    void GotoDats();
    void GotoRegionOrder(); // Dats sub-screen: reorder the 1G1R region priority
    void DatSyncStart();
    void DatSyncTick(); // poll progress / finish; called each frame while running
    static void DatSyncThread(void *arg);

    // Desktop-companion inventory server (read-only, HTTPSRV_INV_PORT).
    void WriteInventoryJson(); // regenerate INVENTORY_PATH from live app state
    void InvServerStart();     // open the listener + write an initial inventory
    void InvServerStop();      // close the listener
    void InvServerPoll();      // per-frame: throttled regen, then serve a request
    void InvApplyPush();       // apply a collection pushed to the inventory server
    void InvApplyFile();       // file a game streamed to the inventory server (inbox)
    static void PushExtractThread(void *arg); // off-thread unpack for the above
    static bool PushExtractProgress(void *ud, const char *entry, int done,
                                    uint64_t bytes_read); // its cancel hook
    void PushExtractTick();    // per-frame: reap the extract worker when done
    void InvApplyNro(char *body, size_t len); // stage an .nro pushed to the inv server
    void InvApplyDat(char *body, size_t len); // file a DAT pushed to the inv server (no modal)
    // overwrite an installed emulator's .nro in place (Emulators-tab update)
    void InvApplyEmuNro(const std::string &app, const std::string &part);
    void InvApplyEmuNroAt(const std::string &app, const std::string &dest,
                          const std::string &part, bool fresh = false);
    void LiveRecvBegin();      // a live-link push started: add a queue-tab item + jump
    void LiveRecvTick(size_t now, size_t total); // push its progress into the queue item
    void LiveRecvEnd(bool ok = true); // push finished: mark the queue item done/failed
    void LiveRecvHide();       // (legacy) user left the page; the transfer keeps running
    // Consolidated transfer plumbing: every non-download transfer (self-update,
    // DAT sync, PC->Switch receive) registers a queue-tab item via BeginXfer and
    // is driven from PollXfers, so all progress lands in one place (the Queue tab).
    int BeginXfer(const std::string &name, const std::string &target,
                  uint8_t xkind); // queue_ext_add + jump to the Queue tab
    void PollXfers();          // per-frame: mirror progress + reap the app pulls

    // ---- Tools: emulator / app update manager -----------------------------
    // Reads the shared update_sources.json manifest, checks each entry against
    // its GitHub release repo, and installs/updates/reverts on-device with a
    // per-app backup (see updman.h). kind selects the Emulators / Apps section.
    void GotoAppUpdates(uint8_t kind);   // the section list (own Settings screen)
    static void AppChkThread(void *arg); // worker: scan installs + check releases
    void AppChkTick();     // per-frame while checking: progress; build list on done
    void AppUpdatesRender(); // build the list rows from the check results
    void AppScanAll();     // X on the list: re-run with the network check on
    void AppRecheckOne(size_t idx); // re-check just one entry (no full re-pull)
    bool AppEntryMenu(size_t idx);       // one entry's actions; true if it changed
    void AppMarkChecked(const std::string &id); // stamp+persist an entry's check time
    std::string AppCheckedLabel(const std::string &id); // "checked 5m ago" / ""
    void AppSetSource(const UpdSource &e); // swkbd-edit the entry's GitHub repo
    void AppRevert(const UpdSource &e);  // roll back to a stored backup build
    // Background install kicked off from the manager: download the release .nro,
    // then (on the main thread, in UmiTick) validate, back up the current build,
    // and swap it in. Mirrored into a Queue-tab item by PollXfers.
    void UmiStart(const UpdSource &e, const std::string &url,
                  const std::string &tag, const std::string &dest,
                  const std::string &cur_ver, bool fresh,
                  const std::string &asset);
    static void UmiThread(void *arg);
    static int UmiProgress(void *ud, u64 now, u64 total);
    void UmiTick();                      // finalize once the download reports done

    // LAN collection import helpers.
    void ImportStart(bool onboarding = false);
    void ExportStart(); // mirror of import: serve this console's collection to a PC
    void UpdateWifiStart(); // same receiver, update-flavored screen + page
    // Receive a game into a console's folder. fromSettings marks the launch as
    // coming from the Settings "Install from PC" picker (vs the Installed tab),
    // so ImportReturn lands back at the picker rather than the Installed list.
    void RomRecvStart(int consoleIndex, bool fromSettings = false,
                      bool autosort = false);
    // Receive a verification DAT from a PC over the same LAN receiver. The
    // console is read from the DAT's own <header> (see DatApply) and it's saved
    // as DATS_DIR/<target>.dat so a later Verify finds it automatically.
    void DatRecvStart();
    void ImportReturn(); // where the import flow lands when it ends
    void ImportTick(); // serve one request per frame while the screen is open
    int ImportPoll();  // serve one request, logging what it did
    void ImportApply(); // consume the uploaded file, confirm, and write it
    void RomRecvSaveOne(); // move one finished ROM into place, record the row
    void RomRecvFinish();  // stop, then sort (auto-sort) or summarise + return
    // Rebuild the receive list (USB-style rows): one per completed file, plus a
    // live row for the file currently arriving when `receiving` is true.
    void RenderRecvList(size_t now, size_t total, bool receiving);
    void NroApply(char *body, size_t len); // an .nro was pushed: stage it
    void DatApply(char *body, size_t len); // a DAT was pushed: validate + save it
    // Staged-build epilogue shared by both update paths (GitHub download and a
    // LAN-pushed .nro): flip the update chips, then offer to relaunch in place.
    // True when the app is closing to restart — the caller must return at once.
    bool StagedRestartPrompt(const std::string &msg);
    void ImportStop();
    // Embedded MTP (USB PC transfer). GotoUsbMtp brings USB device mode up and
    // shows the connect screen; UsbMtpTick refreshes link state each frame;
    // UsbMtpStop tears device mode down and leaves the screen.
    void GotoUsbMtp(bool fromSettings = false);
    bool UsbResponderStart(); // build console folders + mtp::Start (screen + bg)
    void UsbMtpTick();
    void UsbMtpStop();
    void UsbMtpReturn(); // navigate out of the USB screen: sort a non-empty
                         // inbox, else back to the launch origin (Settings or
                         // Library)
    void RestoreBackup(); // swap the last import's backup back in
    void Welcome();       // first-run prompt while there are no collections

    // Background update-check helpers.
    void ChkStart();
    void ChkTick();   // poll progress / finish; called each frame while running
    void ChkFinish(); // handle the fetched (or failed) result on the UI thread
    static void ChkThread(void *arg);
    void BgChkStart();               // kick off the silent startup update check
    void BgChkPoll();                // reap it and light the Settings dot/chip
    static void BgChkThread(void *arg);

    // Release-notes viewer (Settings -> View logs -> Release notes).
    void GotoReleaseNotes(); // kick the fetch and show a spinner
    void NotesTick();        // poll the fetch; show the version list when done
    void ShowReleaseList();  // the fetched releases as a version list
    void ShowReleaseNote(int idx); // one release's notes, markdown flattened
    static void NotesThread(void *arg);

    // Background metadata load helpers.
    void StartMetaLoad(const std::string &id, const std::string &base,
                       const std::string &target, bool force,
                       const std::string &done_subtitle);
    void MetaTick();
    static void MetaThread(void *arg);

    // DAT verification (Library tab): hash a console's files off the UI thread
    // and classify each against a user-supplied No-Intro/Redump DAT. The worker
    // both parses the DAT and runs the scan; the UI polls vfy_job's counters.
    void VerifyStart(const std::string &folder, const std::string &target,
                     const std::string &label, bool force = false,
                     bool goto_missing = false);
    void VerifyTick();      // poll progress / build results; called each frame
    void VerifyResults();   // on the UI thread: list the classified files
    void VerifyRenameSel(); // rename the selected verified-but-misnamed file
    void VerifyRenameAll(); // rename every verified-but-misnamed file at once
    void VerifyReacquireSel(); // A on a BAD row: search this console for a replacement
    void VerifyMenu();      // Y: report / re-verify actions on the results screen
    void VerifyExportReport(); // write a full text report (incl. DAT-missing games)
    void VerifyExportFixdat(); // write a Logiqx fixdat of the missing DAT entries
    std::vector<std::string> VerifyMissingList(); // DAT titles no file verified to
    void VerifyMissingResults();  // browsable missing list; A hands off to search
    static void VerifyThread(void *arg);

    // Verify every console that has a DAT in one pass, then show a per-console
    // summary; A on a row drills into that console's full results. Reuses the
    // single-console engine (vfy_job/vfy_dat) as scratch, one console at a time.
    void VerifyAllStart();
    void VerifyAllTick();     // poll batch progress / build the summary each frame
    void VerifyAllResults();  // on the UI thread: one row per console with tallies
    static void VerifyAllThread(void *arg);

    // Inbox sorter: identify each file staged in INBOX_DIR (idgame.h), file the
    // confident ones into their console folder, and list the outcome so the user
    // can hand-pick a console for anything ambiguous (A -> console picker).
    // One staged file and where it ended up. Filed rows have been moved into a
    // console folder; needs_pick rows are waiting for the user to choose one.
    struct SortRow {
        std::string name;   // file name as it sits in the inbox
        std::string path;   // full inbox path (INBOX_DIR/name)
        std::string target; // resolved/guessed console target ("" if unknown)
        std::string label;  // display name for target (the console it went to)
        std::string dest;   // where it was filed (or would be), for the row text
        bool filed = false;     // already moved into a console folder
        bool needs_pick = true; // still needs a console chosen by hand
    };
    void SortInboxStart();
    void SortInboxResults();  // rebuild the results screen from sort_rows
    // Move one staged file into `target`'s install folder. Returns false and
    // leaves it in the inbox on a name clash or move failure. Updates the row.
    bool SortFileRow(struct SortRow &r, const std::string &target);
    void SortAssignPicked(const std::string &target); // picker chose for sort_pick_idx

  private:
    BgTask vfy;
    Dat vfy_dat{};
    VerifyJob vfy_job{};
    std::vector<int> vfy_order; // results row -> vfy_job.items index (sorted view)
    std::vector<std::string> vfy_missing; // DAT titles absent from the library
    std::vector<int> vfy_missing_order; // filtered/displayed row -> vfy_missing index
    std::string vfy_missing_filter; // Y-entered search text narrowing the list above
    bool vfy_goto_missing = false; // jump straight to the missing list when done
    // True when the missing list was opened straight from console Options
    // (the per-file Verify results screen was never shown), so B on an
    // uncleared filter should return to the Library tab instead of a results
    // screen the user never saw. False when reached via the Verify screen's
    // Y menu, where B correctly steps back to that per-file list.
    bool vfy_missing_direct = false;
    std::string vfy_folder;   // console folder scanned (for B-return context)
    std::string vfy_target;   // console target/folder key, for a forced re-verify
    std::string vfy_label;    // console display name, for the results title
    std::string vfy_dat_path_str; // resolved DATS_DIR/<target>.dat, read by worker
    std::atomic<bool> vfy_dat_ok{false}; // worker parsed the DAT successfully

    // Verify-all-consoles state. The worker verifies each row's folder in turn
    // (reusing vfy_job/vfy_dat), writing tallies back into the row it's on.
    struct VfyAllRow {
        std::string target, label, folder;
        int n_ok = 0, n_bad = 0, n_unknown = 0, n_err = 0, n_misnamed = 0;
        int n_missing = 0;
        bool completed = false; // finished without cancel and with a readable DAT
    };
    std::vector<VfyAllRow> vfy_all;      // one row per console with a DAT
    volatile int vfy_all_idx = 0;        // row being verified (worker writes)
    volatile bool vfy_all_cancel = false; // stop starting further consoles

    // Inbox sorter state. One row per file found in INBOX_DIR (see SortRow).
    std::vector<SortRow> sort_rows;
    int sort_pick_idx = -1; // row awaiting a console pick (index into sort_rows)

    // "Tidy library" scan: one issue per file worth fixing (report-only until
    // the user confirms each). kind 0 = an exact (hash-identical) duplicate, in
    // which case `target` holds the kept copy's path; kind 1 = misfiled, and
    // `target` is the console folder id it should move to; kind 2 = a redundant
    // 1G1R clone, with `target` holding the kept copy's name.
    struct TidyIssue {
        std::string path, name, console, target;
        int kind = 0;
    };
    std::vector<TidyIssue> tidy_issues;
    BgTask tidy;
    volatile bool tidy_cancel = false; // matches hash_file's volatile-bool cancel
    volatile int tidy_files_done = 0;
    volatile int tidy_files_total = 0;
    bool tidy_onegr = false; // the last scan was 1G1R (affects result labels)
    // When set, the tidy scan is limited to one console: tidy_only is its target
    // key (used to tag files + label the results) and tidy_only_path its resolved
    // install folder. Empty = whole-library scan.
    std::string tidy_only, tidy_only_path;
    void TidyStart(const std::string &only = "", const std::string &only_path = "");
    void TidyTick();
    void TidyResults();
    void TidyActSel(); // A on an issue: confirm + perform the single fix
    // 1G1R clone reduction: reuses the tidy results screen + confirm-each fix,
    // but the scan groups No-Intro-named files by title and flags all but the
    // most-preferred regional/revision copy in each group.
    void OneGRStart();
    static void OneGRThread(void *arg);
    static void TidyThread(void *arg);

    // "Largest files" scan: the whole library's biggest files, sorted
    // descending and capped — Storage's direct answer to "what do I delete to
    // free space", distinct from Tidy's problem-only view. Reuses tidy_gather's
    // walk (defined alongside TidyThread) but does no hashing/classification,
    // so there's no meaningful per-file progress to report.
    struct LargeFile {
        std::string path, name, console;
        uint64_t size = 0;
    };
    std::vector<LargeFile> large_files;
    BgTask lgf;
    volatile bool lgf_cancel = false;
    void LargeFilesStart();
    static void LargeFilesThread(void *arg);
    void LargeFilesTick();
    void GotoLargestFiles();
    void LargeFileOpenSel();   // A: jump to the file's console folder in the Library
    void LargeFileDeleteSel(); // X: delete it directly (danger confirm)
};
