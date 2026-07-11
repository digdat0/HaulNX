#pragma once

// A lightweight scrolling list element with real columns: a left cell (name,
// truncated with "...") and an optional right cell (size / info) that is
// right-aligned as a true column. Plutonium's built-in Menu packs a row into a
// single text label, which can't form aligned columns — this renders each cell
// itself. Navigation is driven by the app (MoveBy / SetSelected); OnInput is a
// no-op so there's a single source of truth for selection.

#include <pu/Plutonium>
#include <string>
#include <vector>
#include <set>

class TableList : public pu::ui::elm::Element {
  public:
    struct Row {
        std::string left;
        std::string right;
        pu::ui::Color lclr;
        pu::ui::Color rclr;
        bool has_right;
        float progress; // 0..1 draws a progress bar; <0 = none
        // Optional left icon (e.g. a console icon), BORROWED — owned by a
        // shared cache elsewhere, never freed by this list.
        pu::sdl2::Texture icon;
        // Optional short text drawn BEFORE the icon (e.g. a queue status), so
        // the icon becomes the second column. Uses lclr.
        std::string prefix;
        // Accent row (e.g. the active download): a distinct tinted background
        // and a taller progress bar, so it stands out even when not selected.
        bool accent;
        // Draw a subtle rounded "pill" behind the right cell (size/status/value
        // badges). Off for bare markers like a chevron.
        bool pill;
    };

  private:
    struct Cell {
        pu::sdl2::Texture tex;
        s32 w;
        s32 h;
    };

    s32 x, y, w, row_h;
    s32 rows_visible;
    s32 sel;
    s32 scroll_top;
    // Selection-highlight fade: restarted whenever the selection changes so
    // the blue eases in instead of teleporting between rows.
    s32 anim_sel = -1;
    s32 sel_alpha = 255;
    pu::ui::Color row_bg, row_alt_bg, focus_bg, scroll_clr, mark_bg, prog_clr,
        accent_bg, pill_bg;
    std::set<s32> marked;
    std::string font;
    std::vector<Row> rows;

    // Visible-window texture cache: only rebuilt when the window or content
    // changes, so a static list does not re-render text every frame.
    std::vector<Cell> cache_l, cache_r, cache_p; // p = optional prefix text
    s32 cache_top;
    bool dirty;

    // Touch: tap selects a row (tap the selected row again to activate it,
    // surfaced via ConsumeTouchActivate); vertical drag scrolls the list.
    bool tch_active = false;
    bool tch_dragged = false;
    s32 tch_start_y = 0;
    s32 tch_last_y = 0;
    s32 tch_acc = 0;
    s32 tch_row = -1;
    bool tch_activate = false;

    static constexpr s32 PadX = 26;
    static constexpr s32 DragThreshold = 16; // px before a touch counts as a drag
    static constexpr s32 IconGap = 12;       // gap between a row icon and its text
    // Rows render as floating rounded rectangles (matching the card view):
    // inset from the screen edges with a small gap between rows.
    static constexpr s32 RowMargin = 18;
    static constexpr s32 RowGap = 6;
    static constexpr s32 RowRadius = 12;
    // Fixed width reserved for the optional prefix column (e.g. queue status),
    // so the icon/text after it line up vertically across all rows.
    static constexpr s32 PrefixColW = 118;

    // Square draw size for a row icon (fits within the row height).
    s32 IconPx() const { return this->row_h - 28; }

    void EnsureVisible() {
        if (this->sel < this->scroll_top) {
            this->scroll_top = this->sel;
        } else if (this->sel >= this->scroll_top + this->rows_visible) {
            this->scroll_top = this->sel - this->rows_visible + 1;
        }
        s32 maxtop = (s32)this->rows.size() - this->rows_visible;
        if (maxtop < 0) {
            maxtop = 0;
        }
        if (this->scroll_top > maxtop) {
            this->scroll_top = maxtop;
        }
        if (this->scroll_top < 0) {
            this->scroll_top = 0;
        }
    }

    // RenderText returns a raw SDL_Texture* (sdl2::Texture) that must be freed
    // explicitly, or every cache rebuild leaks GPU memory.
    void FreeCache() {
        for (auto &c : this->cache_l) {
            if (c.tex) {
                pu::ui::render::DeleteTexture(c.tex);
            }
        }
        for (auto &c : this->cache_r) {
            if (c.tex) {
                pu::ui::render::DeleteTexture(c.tex);
            }
        }
        for (auto &c : this->cache_p) {
            if (c.tex) {
                pu::ui::render::DeleteTexture(c.tex);
            }
        }
        this->cache_l.clear();
        this->cache_r.clear();
        this->cache_p.clear();
    }

    void RebuildCache() {
        this->FreeCache();
        for (s32 i = 0; i < this->rows_visible; i++) {
            s32 ridx = this->scroll_top + i;
            Cell lc{nullptr, 0, 0}, rc{nullptr, 0, 0}, pc{nullptr, 0, 0};
            if (ridx >= 0 && ridx < (s32)this->rows.size()) {
                Row &r = this->rows[ridx];
                if (r.has_right && !r.right.empty()) {
                    rc.tex = pu::ui::render::RenderText(this->font, r.right,
                                                        r.rclr);
                    rc.w = pu::ui::render::GetTextureWidth(rc.tex);
                    rc.h = pu::ui::render::GetTextureHeight(rc.tex);
                }
                if (!r.prefix.empty()) {
                    pc.tex = pu::ui::render::RenderText(this->font, r.prefix,
                                                        r.lclr);
                    pc.w = pu::ui::render::GetTextureWidth(pc.tex);
                    pc.h = pu::ui::render::GetTextureHeight(pc.tex);
                }
                s32 prefix_inset = pc.tex ? PrefixColW : 0;
                s32 icon_inset = r.icon ? (this->IconPx() + IconGap) : 0;
                s32 left_max = this->w - 2 * (RowMargin + PadX) - prefix_inset -
                               icon_inset - (rc.tex ? rc.w + PadX : 0);
                if (left_max < 60) {
                    left_max = 60;
                }
                lc.tex = pu::ui::render::RenderText(this->font, r.left, r.lclr,
                                                    (u32)left_max);
                lc.w = pu::ui::render::GetTextureWidth(lc.tex);
                lc.h = pu::ui::render::GetTextureHeight(lc.tex);
            }
            this->cache_l.push_back(lc);
            this->cache_r.push_back(rc);
            this->cache_p.push_back(pc);
        }
        this->cache_top = this->scroll_top;
        this->dirty = false;
    }

    // Glossy progress-bar fill: vertical light->deep gradient of prog_clr with
    // a bright line along the top, echoing the icon's glossy arrow. Drawn as
    // 1px strips; the end strips taper inward to approximate the rounded cap.
    void RenderGlossBar(pu::ui::render::Renderer::Ref &drawer, s32 bx, s32 by,
                        s32 bw, s32 bh, s32 r) {
        auto mix = [](u8 a, u8 b, float t) {
            return (u8)(a + (s32)(((s32)b - (s32)a) * t));
        };
        pu::ui::Color top(mix(this->prog_clr.r, 255, 0.35f),
                          mix(this->prog_clr.g, 255, 0.35f),
                          mix(this->prog_clr.b, 255, 0.35f), 255);
        pu::ui::Color bot((u8)(this->prog_clr.r * 3 / 5),
                          (u8)(this->prog_clr.g * 3 / 5),
                          (u8)(this->prog_clr.b * 3 / 5), 255);
        for (s32 i = 0; i < bh; i++) {
            float t = bh > 1 ? (float)i / (bh - 1) : 0.0f;
            pu::ui::Color c(mix(top.r, bot.r, t), mix(top.g, bot.g, t),
                            mix(top.b, bot.b, t), 255);
            s32 de = i < bh - 1 - i ? i : bh - 1 - i; // distance to nearer edge
            s32 inset = de < r ? r - de : 0;
            if (bw - 2 * inset > 0) {
                drawer->RenderRectangleFill(c, bx + inset, by + i,
                                            bw - 2 * inset, 1);
            }
        }
        if (bw - 2 * r > 0) {
            drawer->RenderRectangleFill(pu::ui::Color(255, 255, 255, 85),
                                        bx + r, by, bw - 2 * r, 1);
        }
    }

  public:
    TableList(const s32 x, const s32 y, const s32 w, const s32 row_h,
              const s32 rows_visible)
        : x(x), y(y), w(w), row_h(row_h), rows_visible(rows_visible), sel(0),
          scroll_top(0), row_bg(22, 23, 27, 255), row_alt_bg(28, 30, 36, 255),
          // Blue selection highlight (theme overrides via SetThemeColors).
          focus_bg(45, 95, 180, 255), scroll_clr(80, 86, 100, 255),
          mark_bg(60, 80, 120, 255), prog_clr(146, 214, 36, 255),
          accent_bg(34, 54, 20, 255), pill_bg(255, 255, 255, 20),
          cache_top(-1), dirty(true) {
        this->font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge);
    }
    PU_SMART_CTOR(TableList)

    ~TableList() { this->FreeCache(); }

    void SetThemeColors(pu::ui::Color bg, pu::ui::Color alt, pu::ui::Color focus,
                        pu::ui::Color scroll, pu::ui::Color mark,
                        pu::ui::Color prog = {146, 214, 36, 255},
                        pu::ui::Color accent = {34, 54, 20, 255},
                        pu::ui::Color pill = {255, 255, 255, 20}) {
        this->row_bg = bg; this->row_alt_bg = alt; this->focus_bg = focus;
        this->scroll_clr = scroll; this->mark_bg = mark;
        this->prog_clr = prog; this->accent_bg = accent; this->pill_bg = pill;
        this->dirty = true;
    }

    void Clear() {
        this->rows.clear();
        this->sel = 0;
        this->scroll_top = 0;
        this->marked.clear();
        this->dirty = true;
        // Content changed under the finger: drop any in-progress touch so a
        // stale tap can't select/activate a row of the new list.
        this->tch_active = false;
        this->tch_row = -1;
        this->tch_activate = false;
    }
    void AddRow(const std::string &left, const pu::ui::Color lclr,
                pu::sdl2::Texture icon = nullptr) {
        this->rows.push_back(
            Row{left, "", lclr, lclr, false, -1.0f, icon, "", false, false});
        this->dirty = true;
    }
    void AddRow2(const std::string &left, const std::string &right,
                 const pu::ui::Color lclr, const pu::ui::Color rclr,
                 const float progress = -1.0f,
                 pu::sdl2::Texture icon = nullptr,
                 const std::string &prefix = "", bool accent = false,
                 bool pill = true) {
        this->rows.push_back(Row{left, right, lclr, rclr, true, progress, icon,
                                 prefix, accent, pill});
        this->dirty = true;
    }

    s32 Count() { return (s32)this->rows.size(); }
    s32 GetSelected() { return this->sel; }
    s32 RowsVisible() { return this->rows_visible; }
    void SetSelected(const s32 i) {
        s32 n = (s32)this->rows.size();
        if (n <= 0) {
            this->sel = 0;
            this->scroll_top = 0;
            return;
        }
        this->sel = i < 0 ? 0 : (i >= n ? n - 1 : i);
        this->EnsureVisible();
    }
    void MoveBy(const s32 d) { this->SetSelected(this->sel + d); }
    // Single-step move that wraps around the ends (top<->bottom).
    void Step(const s32 d) {
        s32 n = (s32)this->rows.size();
        if (n <= 0) {
            return;
        }
        s32 i = this->sel + d;
        if (i < 0) {
            i = n - 1;
        } else if (i >= n) {
            i = 0;
        }
        this->SetSelected(i);
    }

    void ToggleMark(s32 i) {
        if (this->marked.count(i)) this->marked.erase(i);
        else this->marked.insert(i);
    }
    bool IsMarked(s32 i) { return this->marked.count(i) > 0; }
    const std::set<s32> &Marked() { return this->marked; }
    int MarkedCount() { return (int)this->marked.size(); }
    void ClearMarks() { this->marked.clear(); }

    s32 GetX() override { return this->x; }
    s32 GetY() override { return this->y; }
    s32 GetWidth() override { return this->w; }
    s32 GetHeight() override { return this->row_h * this->rows_visible; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 rx,
                  const s32 ry) override {
        if (this->rows.empty()) {
            return; // nothing to show (e.g. card view active): no row stripes
        }
        if (this->dirty || this->cache_top != this->scroll_top) {
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
        for (s32 i = 0; i < this->rows_visible; i++) {
            s32 ridx = this->scroll_top + i;
            s32 rowy = ry + i * this->row_h;
            bool has = (ridx >= 0 && ridx < (s32)this->rows.size());
            if (!has) {
                continue; // empty slots stay bare page background
            }
            bool is_marked = this->marked.count(ridx) > 0;
            bool is_accent = this->rows[ridx].accent;
            bool has_bar = this->rows[ridx].progress >= 0.0f;
            s32 bar_bh = is_accent ? 12 : 8; // active download gets a thicker bar
            // Floating rounded row (single fill colour, like the cards).
            s32 rrx = rx + RowMargin;
            s32 rry = rowy + RowGap / 2;
            s32 rrw = this->w - 2 * RowMargin;
            s32 rrh = this->row_h - RowGap;
            // Vertical band for text/icon/pill: the whole row, or the space
            // above the progress bar when present (so nothing overlaps it).
            s32 cont_top = rry;
            s32 cont_h = rrh - (has_bar ? (bar_bh + 5) : 0);
            pu::ui::Color base = is_marked ? this->mark_bg
                                 : is_accent ? this->accent_bg
                                             : this->row_alt_bg;
            drawer->RenderRoundedRectangleFill(base, rrx, rry, rrw, rrh,
                                               RowRadius);
            if (ridx == this->sel) {
                // Selection: lifted charcoal fill easing in, wrapped in a
                // logo-green outline + soft outer glow (the "lit" row). The
                // glow rings stay within the RowGap between rows.
                auto f = this->focus_bg;
                f.a = (u8)this->sel_alpha;
                drawer->RenderRoundedRectangleFill(f, rrx, rry, rrw, rrh,
                                                   RowRadius);
                for (s32 g = 1; g <= 3; g++) {
                    auto gc = this->prog_clr;
                    gc.a = (u8)((36 - g * 10) * this->sel_alpha / 255);
                    drawer->RenderRoundedRectangle(gc, rrx - g, rry - g,
                                                   rrw + 2 * g, rrh + 2 * g,
                                                   RowRadius + g);
                }
                auto edge = this->prog_clr;
                edge.a = (u8)this->sel_alpha;
                for (s32 t = 0; t < 2; t++) {
                    drawer->RenderRoundedRectangle(
                        edge, rrx + t, rry + t, rrw - 2 * t, rrh - 2 * t,
                        RowRadius - t > 4 ? RowRadius - t : 4);
                }
            }
            // 1px top-edge gloss so rows read as raised surfaces (icon-style
            // bevel); slightly stronger on the selected row.
            drawer->RenderRectangleFill(
                pu::ui::Color(255, 255, 255,
                              (u8)(ridx == this->sel ? 45 : 18)),
                rrx + RowRadius, rry, rrw - 2 * RowRadius, 1);
            // ...and a 1px shade along the bottom to complete the bevel.
            drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 50),
                                        rrx + RowRadius, rry + rrh - 1,
                                        rrw - 2 * RowRadius, 1);
            // Marked rows carry a logo-green tag bar down the left edge:
            // green = "queued for action", scannable during multi-select.
            if (is_marked) {
                drawer->RenderRoundedRectangleFill(this->prog_clr, rrx + 9,
                                                   rry + 9, 5, rrh - 18, 2);
            }
            // Progress bar (e.g. active download): rounded track at the bottom.
            float prog = this->rows[ridx].progress;
            if (prog >= 0.0f) {
                if (prog > 1.0f) {
                    prog = 1.0f;
                }
                s32 bh = bar_bh;
                s32 by = rry + rrh - bh - 5;
                s32 bx = rrx + PadX;
                s32 bw = rrw - 2 * PadX;
                drawer->RenderRoundedRectangleFill(pu::ui::Color(0, 0, 0, 90),
                                                   bx, by, bw, bh, 4);
                s32 fw = (s32)(bw * prog);
                if (fw > 2) {
                    s32 r = fw < 8 ? fw / 2 : 4;
                    this->RenderGlossBar(drawer, bx, by, fw, bh, r);
                }
            }
            Cell &lc = this->cache_l[i];
            Cell &rc = this->cache_r[i];
            if (rc.tex) {
                s32 tex_x = rx + this->w - RowMargin - PadX - rc.w;
                s32 tex_y = cont_top + (cont_h - rc.h) / 2;
                if (this->rows[ridx].pill) {
                    // Subtle rounded chip behind the right value.
                    s32 padx = 14, pady = 7;
                    s32 pw = rc.w + 2 * padx;
                    s32 ph = rc.h + 2 * pady;
                    drawer->RenderRoundedRectangleFill(
                        this->pill_bg, tex_x - padx, tex_y - pady, pw, ph,
                        ph / 2);
                }
                drawer->RenderTexture(rc.tex, tex_x, tex_y);
            }
            // Columns, left to right: optional prefix text, optional icon,
            // then the main left text.
            Cell &pc = this->cache_p[i];
            s32 tx = rx + RowMargin + PadX;
            if (pc.tex) {
                drawer->RenderTexture(pc.tex, tx, cont_top + (cont_h - pc.h) / 2);
                tx += PrefixColW; // fixed column: icon lines up on every row
            }
            if (this->rows[ridx].icon) {
                s32 isz = this->IconPx();
                pu::ui::render::TextureRenderOptions o;
                o.width = isz;
                o.height = isz;
                drawer->RenderTexture(this->rows[ridx].icon, tx,
                                      cont_top + (cont_h - isz) / 2, o);
                tx += isz + IconGap;
            }
            if (lc.tex) {
                drawer->RenderTexture(lc.tex, tx, cont_top + (cont_h - lc.h) / 2);
            }
        }
        // Scrollbar thumb when the list overflows.
        s32 n = (s32)this->rows.size();
        if (n > this->rows_visible) {
            s32 track_h = this->row_h * this->rows_visible;
            s32 sb_w = 6;
            s32 sb_x = rx + this->w - sb_w;
            s32 thumb_h = (s32)((double)track_h * this->rows_visible / n);
            if (thumb_h < 32) {
                thumb_h = 32;
            }
            s32 maxtop = n - this->rows_visible;
            s32 thumb_y =
                ry + (maxtop > 0 ? (s32)((double)(track_h - thumb_h) *
                                         this->scroll_top / maxtop)
                                 : 0);
            // Thumb takes the signature green->blue gradient (echoes the
            // header strip), drawn in short segments.
            const pu::ui::Color g0(146, 214, 36, 255), g1(56, 130, 225, 255);
            for (s32 i = 0; i < thumb_h; i += 4) {
                float t = (float)i / (thumb_h - 1);
                pu::ui::Color c((u8)(g0.r + ((s32)g1.r - g0.r) * t),
                                (u8)(g0.g + ((s32)g1.g - g0.g) * t),
                                (u8)(g0.b + ((s32)g1.b - g0.b) * t), 255);
                s32 seg = thumb_h - i < 4 ? thumb_h - i : 4;
                drawer->RenderRectangleFill(c, sb_x, thumb_y + i, sb_w, seg);
            }
        }
    }

    // True once when the selected row was tapped again (touch "A press").
    // The app polls this each frame and synthesizes an A press from it.
    bool ConsumeTouchActivate() {
        bool v = this->tch_activate;
        this->tch_activate = false;
        return v;
    }

    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint tch) override {
        // Button navigation is driven by the application (single source of
        // truth); this handles touch only.
        const s32 h = this->row_h * this->rows_visible;
        if (!tch.IsEmpty()) {
            if (!this->tch_active) {
                if (!tch.HitsRegion(this->x, this->y, this->w, h)) {
                    return; // touch began outside the list
                }
                this->tch_active = true;
                this->tch_dragged = false;
                this->tch_start_y = tch.y;
                this->tch_last_y = tch.y;
                this->tch_acc = 0;
                s32 row = this->scroll_top + (tch.y - this->y) / this->row_h;
                this->tch_row =
                    (row >= 0 && row < (s32)this->rows.size()) ? row : -1;
            } else {
                // Finger moving: past the threshold it's a drag-scroll.
                if (!this->tch_dragged &&
                    (tch.y - this->tch_start_y > DragThreshold ||
                     this->tch_start_y - tch.y > DragThreshold)) {
                    this->tch_dragged = true;
                }
                if (this->tch_dragged) {
                    this->tch_acc += this->tch_last_y - tch.y;
                    s32 maxtop = (s32)this->rows.size() - this->rows_visible;
                    if (maxtop < 0) {
                        maxtop = 0;
                    }
                    while (this->tch_acc >= this->row_h) {
                        if (this->scroll_top < maxtop) {
                            this->scroll_top++;
                        }
                        this->tch_acc -= this->row_h;
                    }
                    while (this->tch_acc <= -this->row_h) {
                        if (this->scroll_top > 0) {
                            this->scroll_top--;
                        }
                        this->tch_acc += this->row_h;
                    }
                }
                this->tch_last_y = tch.y;
            }
        } else if (this->tch_active) {
            // Finger lifted: a short touch is a tap.
            this->tch_active = false;
            if (!this->tch_dragged && this->tch_row >= 0 &&
                this->tch_row < (s32)this->rows.size()) {
                if (this->tch_row == this->sel) {
                    this->tch_activate = true; // second tap = activate
                } else {
                    this->SetSelected(this->tch_row);
                }
            } else if (this->tch_dragged && !this->rows.empty()) {
                // Drag scrolled the selection out of view: pull it to the
                // nearest visible row so a following A press acts on
                // something the user can see.
                s32 lo = this->scroll_top;
                s32 hi = this->scroll_top + this->rows_visible - 1;
                if (hi >= (s32)this->rows.size()) {
                    hi = (s32)this->rows.size() - 1;
                }
                if (this->sel < lo) {
                    this->sel = lo;
                } else if (this->sel > hi) {
                    this->sel = hi;
                }
            }
        }
    }
};
