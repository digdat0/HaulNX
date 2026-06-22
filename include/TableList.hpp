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

class TableList : public pu::ui::elm::Element {
  public:
    struct Row {
        std::string left;
        std::string right;
        pu::ui::Color lclr;
        pu::ui::Color rclr;
        bool has_right;
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
    pu::ui::Color row_bg, row_alt_bg, focus_bg, scroll_clr;
    std::string font;
    std::vector<Row> rows;

    // Visible-window texture cache: only rebuilt when the window or content
    // changes, so a static list does not re-render text every frame.
    std::vector<Cell> cache_l, cache_r;
    s32 cache_top;
    bool dirty;

    static constexpr s32 PadX = 26;

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
                s32 left_max = this->w - 2 * PadX - (rc.tex ? rc.w + PadX : 0);
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
          cache_top(-1), dirty(true) {
        this->font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge);
    }
    PU_SMART_CTOR(TableList)

    ~TableList() { this->FreeCache(); }

    void Clear() {
        this->rows.clear();
        this->sel = 0;
        this->scroll_top = 0;
        this->dirty = true;
    }
    void AddRow(const std::string &left, const pu::ui::Color lclr) {
        this->rows.push_back(Row{left, "", lclr, lclr, false});
        this->dirty = true;
    }
    void AddRow2(const std::string &left, const std::string &right,
                 const pu::ui::Color lclr, const pu::ui::Color rclr) {
        this->rows.push_back(Row{left, right, lclr, rclr, true});
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
            pu::ui::Color bg =
                (has && ridx == this->sel)
                    ? this->focus_bg
                    : ((ridx % 2) ? this->row_alt_bg : this->row_bg);
            drawer->RenderRectangleFill(bg, rx, rowy, this->w, this->row_h);
            if (!has) {
                continue;
            }
            Cell &lc = this->cache_l[i];
            Cell &rc = this->cache_r[i];
            if (rc.tex) {
                drawer->RenderTexture(rc.tex, rx + this->w - PadX - rc.w,
                                      rowy + (this->row_h - rc.h) / 2);
            }
            if (lc.tex) {
                drawer->RenderTexture(lc.tex, rx + PadX,
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

    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {
        // Navigation is driven by the application (single source of truth).
    }
};
