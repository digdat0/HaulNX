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
    pu::ui::Color row_bg, row_alt_bg, focus_bg, scroll_clr, mark_bg;
    std::set<s32> marked;
    std::string font;
    std::vector<Row> rows;

    // Visible-window texture cache: only rebuilt when the window or content
    // changes, so a static list does not re-render text every frame.
    std::vector<Cell> cache_l, cache_r;
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
        this->cache_l.clear();
        this->cache_r.clear();
    }

    void RebuildCache() {
        this->FreeCache();
        for (s32 i = 0; i < this->rows_visible; i++) {
            s32 ridx = this->scroll_top + i;
            Cell lc{nullptr, 0, 0}, rc{nullptr, 0, 0};
            if (ridx >= 0 && ridx < (s32)this->rows.size()) {
                Row &r = this->rows[ridx];
                if (r.has_right && !r.right.empty()) {
                    rc.tex = pu::ui::render::RenderText(this->font, r.right,
                                                        r.rclr);
                    rc.w = pu::ui::render::GetTextureWidth(rc.tex);
                    rc.h = pu::ui::render::GetTextureHeight(rc.tex);
                }
                s32 icon_inset = r.icon ? (this->IconPx() + IconGap) : 0;
                s32 left_max = this->w - 2 * PadX - icon_inset -
                               (rc.tex ? rc.w + PadX : 0);
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
        }
        this->cache_top = this->scroll_top;
        this->dirty = false;
    }

  public:
    TableList(const s32 x, const s32 y, const s32 w, const s32 row_h,
              const s32 rows_visible)
        : x(x), y(y), w(w), row_h(row_h), rows_visible(rows_visible), sel(0),
          scroll_top(0), row_bg(22, 23, 27, 255), row_alt_bg(28, 30, 36, 255),
          // Teal selection highlight, distinct from the blue header/tab bar.
          focus_bg(28, 122, 116, 255), scroll_clr(80, 86, 100, 255),
          mark_bg(60, 80, 120, 255),
          cache_top(-1), dirty(true) {
        this->font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge);
    }
    PU_SMART_CTOR(TableList)

    ~TableList() { this->FreeCache(); }

    void SetThemeColors(pu::ui::Color bg, pu::ui::Color alt, pu::ui::Color focus,
                        pu::ui::Color scroll, pu::ui::Color mark) {
        this->row_bg = bg; this->row_alt_bg = alt; this->focus_bg = focus;
        this->scroll_clr = scroll; this->mark_bg = mark;
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
        this->rows.push_back(Row{left, "", lclr, lclr, false, -1.0f, icon});
        this->dirty = true;
    }
    void AddRow2(const std::string &left, const std::string &right,
                 const pu::ui::Color lclr, const pu::ui::Color rclr,
                 const float progress = -1.0f,
                 pu::sdl2::Texture icon = nullptr) {
        this->rows.push_back(Row{left, right, lclr, rclr, true, progress, icon});
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
        if (this->dirty || this->cache_top != this->scroll_top) {
            this->RebuildCache();
        }
        for (s32 i = 0; i < this->rows_visible; i++) {
            s32 ridx = this->scroll_top + i;
            s32 rowy = ry + i * this->row_h;
            bool has = (ridx >= 0 && ridx < (s32)this->rows.size());
            bool is_marked = has && this->marked.count(ridx);
            pu::ui::Color bg =
                (has && ridx == this->sel)
                    ? this->focus_bg
                    : is_marked ? this->mark_bg
                    : ((ridx % 2) ? this->row_alt_bg : this->row_bg);
            drawer->RenderRectangleFill(bg, rx, rowy, this->w, this->row_h);
            if (!has) {
                continue;
            }
            // Progress bar (e.g. active download): thin track along the bottom.
            float prog = this->rows[ridx].progress;
            if (prog >= 0.0f) {
                if (prog > 1.0f) {
                    prog = 1.0f;
                }
                s32 bh = 6;
                s32 by = rowy + this->row_h - bh - 4;
                s32 bx = rx + PadX;
                s32 bw = this->w - 2 * PadX;
                drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 120), bx, by,
                                            bw, bh);
                drawer->RenderRectangleFill(pu::ui::Color(120, 225, 150, 255), bx,
                                            by, (s32)(bw * prog), bh);
            }
            Cell &lc = this->cache_l[i];
            Cell &rc = this->cache_r[i];
            if (rc.tex) {
                drawer->RenderTexture(rc.tex, rx + this->w - PadX - rc.w,
                                      rowy + (this->row_h - rc.h) / 2);
            }
            // Optional left icon, then shift the left text past it.
            s32 tx = rx + PadX;
            if (this->rows[ridx].icon) {
                s32 isz = this->IconPx();
                pu::ui::render::TextureRenderOptions o;
                o.width = isz;
                o.height = isz;
                drawer->RenderTexture(this->rows[ridx].icon, rx + PadX,
                                      rowy + (this->row_h - isz) / 2, o);
                tx += isz + IconGap;
            }
            if (lc.tex) {
                drawer->RenderTexture(lc.tex, tx,
                                      rowy + (this->row_h - lc.h) / 2);
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
            drawer->RenderRectangleFill(this->scroll_clr, sb_x, thumb_y, sb_w,
                                        thumb_h);
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
            }
        }
    }
};
