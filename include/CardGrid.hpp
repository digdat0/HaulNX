#pragma once

// A 4-wide grid of "cards" (big icon + title + subtitle) used as the optional
// card view for the console lists (Browse home, Installed root). Like
// TableList it is passive: the app drives selection (Move / SetSelected) and
// OnInput only handles touch (tap select, tap-again activate, drag scroll).
// Card icons are BORROWED from the shared console-icon cache — never freed
// here; the rendered text textures are cached and owned by this element.

#include <pu/Plutonium>
#include <string>
#include <vector>

class CardGrid : public pu::ui::elm::Element {
  public:
    struct Card {
        std::string title;
        std::string subtitle;
        pu::sdl2::Texture icon; // borrowed
    };

  private:
    struct Cell {
        pu::sdl2::Texture t1_tex; // title line 1
        pu::sdl2::Texture t2_tex; // title line 2 (word-wrapped overflow)
        pu::sdl2::Texture sub_tex;
        s32 t1w, t1h, t2w, t2h, sw, sh;
    };

    s32 x, y, w, h;
    s32 sel;
    s32 scroll_row;
    // Selection fade + icon grow-in, restarted when the selection moves.
    s32 anim_sel = -1;
    s32 sel_alpha = 255;
    std::vector<Card> cards;
    std::vector<Cell> cache; // one per card; rebuilt when dirty
    bool dirty;
    pu::ui::Color card_bg, focus_bg, title_clr, sub_clr;
    std::string font_title, font_sub;

    // Touch state (mirrors TableList's behaviour).
    bool tch_active = false;
    bool tch_dragged = false;
    s32 tch_start_y = 0;
    s32 tch_last_y = 0;
    s32 tch_acc = 0;
    s32 tch_card = -1;
    bool tch_activate = false;

    static constexpr s32 Cols = 4;
    static constexpr s32 Margin = 30;
    static constexpr s32 Gap = 20;
    static constexpr s32 CardH = 264;
    static constexpr s32 IconPx = 130;
    static constexpr s32 CardRadius = 14;
    static constexpr s32 DragThreshold = 16;

    s32 CardW() const { return (this->w - 2 * Margin - (Cols - 1) * Gap) / Cols; }
    s32 RowsTotal() const { return ((s32)this->cards.size() + Cols - 1) / Cols; }
    s32 VisRows() const { return (this->h + Gap) / (CardH + Gap); }
    s32 MaxScroll() const {
        s32 m = this->RowsTotal() - this->VisRows();
        return m < 0 ? 0 : m;
    }

    void EnsureVisible() {
        s32 row = this->sel / Cols;
        if (row < this->scroll_row) {
            this->scroll_row = row;
        } else if (row >= this->scroll_row + this->VisRows()) {
            this->scroll_row = row - this->VisRows() + 1;
        }
        if (this->scroll_row > this->MaxScroll()) {
            this->scroll_row = this->MaxScroll();
        }
        if (this->scroll_row < 0) {
            this->scroll_row = 0;
        }
    }

    void FreeCache() {
        for (auto &c : this->cache) {
            if (c.t1_tex) {
                pu::ui::render::DeleteTexture(c.t1_tex);
            }
            if (c.t2_tex) {
                pu::ui::render::DeleteTexture(c.t2_tex);
            }
            if (c.sub_tex) {
                pu::ui::render::DeleteTexture(c.sub_tex);
            }
        }
        this->cache.clear();
    }

    // Does `text` fit within max_w at the title font? (measure and discard)
    bool TitleFits(const std::string &text, const s32 max_w) {
        auto tex = pu::ui::render::RenderText(this->font_title, text,
                                              this->title_clr);
        if (!tex) {
            return true;
        }
        s32 tw = pu::ui::render::GetTextureWidth(tex);
        pu::ui::render::DeleteTexture(tex);
        return tw <= max_w;
    }

    // Word-wrap a long title onto two lines (RenderText only ellipsizes, so
    // find the longest word-prefix that fits; the rest becomes line 2, which
    // still ellipsizes if it's too long itself).
    void SplitTitle(const std::string &t, const s32 max_w, std::string &l1,
                    std::string &l2) {
        l1 = t;
        l2.clear();
        if (this->TitleFits(t, max_w)) {
            return;
        }
        std::vector<std::string> words;
        size_t pos = 0;
        while (pos < t.size()) {
            size_t sp = t.find(' ', pos);
            if (sp == std::string::npos) {
                words.push_back(t.substr(pos));
                break;
            }
            words.push_back(t.substr(pos, sp - pos));
            pos = sp + 1;
        }
        if (words.size() < 2) {
            return; // one huge word: let line 1 ellipsize
        }
        std::string fit;
        size_t i = 0;
        for (; i < words.size(); i++) {
            std::string cand = fit.empty() ? words[i] : fit + " " + words[i];
            if (!fit.empty() && !this->TitleFits(cand, max_w)) {
                break;
            }
            fit = cand;
        }
        if (i >= words.size()) {
            return; // fits after all (measurement drift): single line
        }
        l1 = fit;
        std::string rest;
        for (; i < words.size(); i++) {
            rest += (rest.empty() ? "" : " ") + words[i];
        }
        l2 = rest;
    }

    void RebuildCache() {
        this->FreeCache();
        const u32 max_tw = (u32)(this->CardW() - 30);
        for (auto &cd : this->cards) {
            Cell c{nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0};
            std::string l1, l2;
            this->SplitTitle(cd.title, (s32)max_tw, l1, l2);
            c.t1_tex = pu::ui::render::RenderText(this->font_title, l1,
                                                  this->title_clr, max_tw);
            c.t1w = pu::ui::render::GetTextureWidth(c.t1_tex);
            c.t1h = pu::ui::render::GetTextureHeight(c.t1_tex);
            if (!l2.empty()) {
                c.t2_tex = pu::ui::render::RenderText(this->font_title, l2,
                                                      this->title_clr, max_tw);
                c.t2w = pu::ui::render::GetTextureWidth(c.t2_tex);
                c.t2h = pu::ui::render::GetTextureHeight(c.t2_tex);
            }
            if (!cd.subtitle.empty()) {
                c.sub_tex = pu::ui::render::RenderText(
                    this->font_sub, cd.subtitle, this->sub_clr, max_tw);
                c.sw = pu::ui::render::GetTextureWidth(c.sub_tex);
                c.sh = pu::ui::render::GetTextureHeight(c.sub_tex);
            }
            this->cache.push_back(c);
        }
        this->dirty = false;
    }

    // Card index under an absolute screen point, or -1.
    s32 HitCard(const s32 px, const s32 py) {
        s32 gx = px - this->x - Margin;
        s32 gy = py - this->y;
        if (gx < 0 || gy < 0) {
            return -1;
        }
        s32 col = gx / (this->CardW() + Gap);
        s32 vr = gy / (CardH + Gap);
        if (col >= Cols || vr >= this->VisRows()) {
            return -1;
        }
        // Inside the card itself, not the gap after it?
        if (gx % (this->CardW() + Gap) >= this->CardW() ||
            gy % (CardH + Gap) >= CardH) {
            return -1;
        }
        s32 idx = (this->scroll_row + vr) * Cols + col;
        return (idx >= 0 && idx < (s32)this->cards.size()) ? idx : -1;
    }

  public:
    CardGrid(const s32 x, const s32 y, const s32 w, const s32 h)
        : x(x), y(y), w(w), h(h), sel(0), scroll_row(0), dirty(true),
          card_bg(28, 30, 36, 255), focus_bg(45, 95, 180, 255),
          title_clr(232, 234, 240, 255), sub_clr(150, 160, 185, 255) {
        // Medium title (wrapped to two lines for long console names) with a
        // Small info line beneath — fits the taller icon in the card.
        this->font_title =
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
        this->font_sub = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
    }
    PU_SMART_CTOR(CardGrid)

    ~CardGrid() { this->FreeCache(); }

    void SetThemeColors(pu::ui::Color bg, pu::ui::Color focus,
                        pu::ui::Color title, pu::ui::Color sub) {
        this->card_bg = bg;
        this->focus_bg = focus;
        this->title_clr = title;
        this->sub_clr = sub;
        this->dirty = true;
    }

    void Clear() {
        this->cards.clear();
        this->FreeCache();
        this->sel = 0;
        this->scroll_row = 0;
        this->dirty = true;
        this->tch_active = false;
        this->tch_card = -1;
        this->tch_activate = false;
    }

    void AddCard(const std::string &title, const std::string &subtitle,
                 pu::sdl2::Texture icon) {
        this->cards.push_back(Card{title, subtitle, icon});
        this->dirty = true;
    }

    s32 Count() { return (s32)this->cards.size(); }
    s32 GetSelected() { return this->sel; }

    void SetSelected(const s32 i) {
        s32 n = (s32)this->cards.size();
        if (n <= 0) {
            this->sel = 0;
            this->scroll_row = 0;
            return;
        }
        this->sel = i < 0 ? 0 : (i >= n ? n - 1 : i);
        this->EnsureVisible();
    }

    // 2D navigation: dx moves within the reading order (can cross rows), dy
    // moves a whole row up/down. No wrap at the edges.
    void Move(const s32 dx, const s32 dy) {
        s32 n = (s32)this->cards.size();
        if (n <= 0) {
            return;
        }
        if (dx != 0) {
            this->SetSelected(this->sel + dx);
        }
        if (dy != 0) {
            s32 ns = this->sel + dy * Cols;
            if (ns >= n) {
                // Down into a partial last row: land on its last card, but
                // only if we're not already on the last row.
                if (this->sel / Cols < this->RowsTotal() - 1) {
                    ns = n - 1;
                } else {
                    return;
                }
            }
            if (ns < 0) {
                return; // already on the top row
            }
            this->SetSelected(ns);
        }
    }

    void PageMove(const s32 dir) {
        this->SetSelected(this->sel + dir * this->VisRows() * Cols);
    }

    // True once when the selected card was tapped again (touch "A press").
    bool ConsumeTouchActivate() {
        bool v = this->tch_activate;
        this->tch_activate = false;
        return v;
    }

    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->w; }
    s32 GetHeight() override { return this->h; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (this->cards.empty()) {
            return;
        }
        if (this->dirty) {
            this->RebuildCache();
        }
        // Advance the selection fade (restart when the selection moved).
        if (this->anim_sel != this->sel) {
            this->anim_sel = this->sel;
            this->sel_alpha = 90;
        } else if (this->sel_alpha < 255) {
            s32 a = this->sel_alpha + 30;
            this->sel_alpha = a > 255 ? 255 : a;
        }
        const s32 cw = this->CardW();
        const s32 rv = this->VisRows();
        for (s32 vr = 0; vr < rv; vr++) {
            s32 row = this->scroll_row + vr;
            for (s32 col = 0; col < Cols; col++) {
                s32 idx = row * Cols + col;
                if (idx >= (s32)this->cards.size()) {
                    break;
                }
                s32 cx = rx + Margin + col * (cw + Gap);
                s32 cy = ry + vr * (CardH + Gap);
                bool selected = (idx == this->sel);
                drawer->RenderRoundedRectangleFill(this->card_bg, cx, cy, cw,
                                                   CardH, CardRadius);
                if (selected) {
                    // Fill eases in, plus a lighter outline "lift".
                    auto f = this->focus_bg;
                    f.a = (u8)this->sel_alpha;
                    drawer->RenderRoundedRectangleFill(f, cx, cy, cw, CardH,
                                                       CardRadius);
                    auto edge = this->focus_bg;
                    edge.r = (u8)(edge.r + 70 > 255 ? 255 : edge.r + 70);
                    edge.g = (u8)(edge.g + 70 > 255 ? 255 : edge.g + 70);
                    edge.b = (u8)(edge.b + 70 > 255 ? 255 : edge.b + 70);
                    edge.a = (u8)this->sel_alpha;
                    for (s32 t = 0; t < 3; t++) {
                        drawer->RenderRoundedRectangle(
                            edge, cx + t, cy + t, cw - 2 * t, CardH - 2 * t,
                            CardRadius - t > 4 ? CardRadius - t : 4);
                    }
                }
                if (this->cards[idx].icon) {
                    // The selected card's icon grows slightly with the fade.
                    s32 isz = IconPx;
                    if (selected) {
                        isz += (10 * this->sel_alpha) / 255;
                    }
                    pu::ui::render::TextureRenderOptions o;
                    o.width = isz;
                    o.height = isz;
                    drawer->RenderTexture(this->cards[idx].icon,
                                          cx + (cw - isz) / 2,
                                          cy + 10 - (isz - IconPx) / 2, o);
                }
                Cell &ce = this->cache[idx];
                // One-line titles centre in the two-line band; the small info
                // line sits at a fixed baseline below.
                if (ce.t1_tex) {
                    s32 ty = ce.t2_tex ? cy + 144 : cy + 158;
                    drawer->RenderTexture(ce.t1_tex, cx + (cw - ce.t1w) / 2,
                                          ty);
                }
                if (ce.t2_tex) {
                    drawer->RenderTexture(ce.t2_tex, cx + (cw - ce.t2w) / 2,
                                          cy + 178);
                }
                if (ce.sub_tex) {
                    drawer->RenderTexture(ce.sub_tex, cx + (cw - ce.sw) / 2,
                                          cy + 220);
                }
            }
        }
        // Scrollbar thumb when the grid overflows.
        s32 total = this->RowsTotal();
        if (total > rv) {
            s32 track_h = this->h;
            s32 thumb_h = (s32)((double)track_h * rv / total);
            if (thumb_h < 32) {
                thumb_h = 32;
            }
            s32 maxs = this->MaxScroll();
            s32 ty = ry + (maxs > 0 ? (s32)((double)(track_h - thumb_h) *
                                            this->scroll_row / maxs)
                                    : 0);
            drawer->RenderRectangleFill(this->sub_clr, rx + this->w - 6, ty, 6,
                                        thumb_h);
        }
    }

    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint tch) override {
        if (this->cards.empty()) {
            return;
        }
        if (!tch.IsEmpty()) {
            if (!this->tch_active) {
                if (!tch.HitsRegion(this->x, this->y, this->w, this->h)) {
                    return;
                }
                this->tch_active = true;
                this->tch_dragged = false;
                this->tch_start_y = tch.y;
                this->tch_last_y = tch.y;
                this->tch_acc = 0;
                this->tch_card = this->HitCard(tch.x, tch.y);
            } else {
                if (!this->tch_dragged &&
                    (tch.y - this->tch_start_y > DragThreshold ||
                     this->tch_start_y - tch.y > DragThreshold)) {
                    this->tch_dragged = true;
                }
                if (this->tch_dragged) {
                    this->tch_acc += this->tch_last_y - tch.y;
                    while (this->tch_acc >= CardH + Gap) {
                        if (this->scroll_row < this->MaxScroll()) {
                            this->scroll_row++;
                        }
                        this->tch_acc -= CardH + Gap;
                    }
                    while (this->tch_acc <= -(CardH + Gap)) {
                        if (this->scroll_row > 0) {
                            this->scroll_row--;
                        }
                        this->tch_acc += CardH + Gap;
                    }
                }
                this->tch_last_y = tch.y;
            }
        } else if (this->tch_active) {
            this->tch_active = false;
            if (!this->tch_dragged && this->tch_card >= 0 &&
                this->tch_card < (s32)this->cards.size()) {
                if (this->tch_card == this->sel) {
                    this->tch_activate = true; // second tap = activate
                } else {
                    this->SetSelected(this->tch_card);
                }
            }
        }
    }
};
