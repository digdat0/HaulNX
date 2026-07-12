#pragma once

// A 4-wide grid of "cards" (big icon + title + subtitle) used as the optional
// card view for the console lists (Browse home, Installed root). Like
// TableList it is passive: the app drives selection (Move / SetSelected) and
// OnInput only handles touch (tap select, tap-again activate, drag scroll).
// Card icons are BORROWED from the shared console-icon cache — never freed
// here; the rendered text textures are cached and owned by this element.

#include <cmath>
#include <pu/Plutonium>
#include <gfx_tile.hpp>
#include <string>
#include <vector>

class CardGrid : public pu::ui::elm::Element {
  public:
    struct Card {
        std::string title;
        std::string subtitle;
        pu::sdl2::Texture icon; // borrowed
        bool pinned; // small logo-green dot in the card's top-left corner
        bool dim = false; // disabled entry: icon renders faded
        // Queue-mode extras (SetQueueCard): the strings double as the
        // "last rendered" keys for the per-frame diff updates.
        bool queue = false;
        std::string status;
        pu::ui::Color st_clr{255, 255, 255, 255};
        std::string chip;   // size · speed · eta joined into one pill
        std::string file;   // full filename (diff gate for the split below)
        std::string f1, f2; // wrapped filename lines (diff keys)
        float prog = -1.0f; // perimeter progress bar; -1 = none
        bool hero = false;  // active download: accent-tinted card
        s32 ring = 0;       // 0 live gradient, 1 done (solid green),
                            // 2 failed (solid red)
        std::string badge;  // queue-position badge text (diff key)
    };

  private:
    struct Cell {
        pu::sdl2::Texture t1_tex; // title line 1
        pu::sdl2::Texture t2_tex; // title line 2 (word-wrapped overflow)
        pu::sdl2::Texture sub_tex;
        s32 t1w, t1h, t2w, t2h, sw, sh;
        // queue-mode textures
        pu::sdl2::Texture st_tex = nullptr;
        pu::sdl2::Texture ch_tex = nullptr;
        pu::sdl2::Texture f_tex = nullptr;  // filename line 1
        pu::sdl2::Texture f2_tex = nullptr; // filename line 2 (wrap)
        pu::sdl2::Texture qp_tex = nullptr; // queue-position badge
        s32 stw = 0, sth = 0, chw = 0, chh = 0, fw = 0, fh = 0, f2w = 0,
            f2h = 0, qpw = 0, qph = 0;
    };

    s32 x, y, w, h;
    s32 sel;
    s32 scroll_row;
    // Selection fade + icon grow-in, restarted when the selection moves.
    s32 anim_sel = -1;
    s32 sel_alpha = 255;
    // Whole-grid fade-in after Clear(), so tab/screen switches ease in
    // instead of snapping. Rendered as a fading page-colored overlay.
    s32 enter_alpha = 255;
    pu::ui::Color page_bg{0, 0, 0, 0}; // layout bg; a=0 disables the fade
    std::vector<Card> cards;
    std::vector<Cell> cache; // one per card; rebuilt when dirty
    bool dirty;
    pu::ui::Color card_bg, focus_bg, title_clr, sub_clr;
    // Logo green for the selection outline/glow and the icon halo; the theme
    // passes its own variant (bright on dark, deep on light) via SetThemeColors.
    pu::ui::Color glow_clr{146, 214, 36, 255};
    // Darkening chip behind the subtitle (count/info line), matching the
    // table view's right-column pills.
    pu::ui::Color pill_clr{0, 0, 0, 95};
    // Unfilled part of the progress ring; the light theme passes a dark
    // translucent shade (white@20 vanishes on light card backgrounds).
    pu::ui::Color trk_clr{255, 255, 255, 20};
    // Baked rounded-fill tiles (one blit each instead of a software rounded
    // fill per card per frame) + scrollbar gradient strip; see gfx_tile.hpp.
    pu::sdl2::Texture tile_card = nullptr, tile_hero = nullptr,
                      tile_focus = nullptr, grad_tex = nullptr;
    bool tiles_dirty = true;
    std::string font_title, font_sub, font_tiny;

    // Touch state (mirrors TableList's behaviour).
    bool tch_active = false;
    bool tch_dragged = false;
    s32 tch_start_x = 0;
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
    // Single-card mode (self-update download): one enlarged queue-style card
    // centred in the element.
    static constexpr s32 SingleW = 840;
    static constexpr s32 SingleH = 480;
    static constexpr s32 SingleRadius = 22;
    static constexpr s32 SingleIconPx = 256;
    bool single = false;

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
            if (c.st_tex) {
                pu::ui::render::DeleteTexture(c.st_tex);
            }
            if (c.ch_tex) {
                pu::ui::render::DeleteTexture(c.ch_tex);
            }
            if (c.f_tex) {
                pu::ui::render::DeleteTexture(c.f_tex);
            }
            if (c.f2_tex) {
                pu::ui::render::DeleteTexture(c.f2_tex);
            }
            if (c.qp_tex) {
                pu::ui::render::DeleteTexture(c.qp_tex);
            }
        }
        this->cache.clear();
    }

    void FreeTiles() {
        pu::sdl2::Texture *ts[] = {&this->tile_card, &this->tile_hero,
                                   &this->tile_focus, &this->grad_tex};
        for (auto p : ts) {
            if (*p) {
                pu::ui::render::DeleteTexture(*p);
            }
        }
    }

    // Bake the card fill, hero tint and selection fill once for the current
    // theme (card size is fixed, so exact-size tiles need no stretch).
    void RebakeTiles() {
        this->FreeTiles();
        const s32 cw = this->CardW();
        this->tile_card = BakeRoundTile(cw, CardH, CardRadius, this->card_bg);
        this->tile_hero = BakeRoundTile(cw, CardH, CardRadius, this->glow_clr);
        this->tile_focus = BakeRoundTile(cw, CardH, CardRadius, this->focus_bg);
        this->grad_tex = BakeVGradient(256, this->glow_clr,
                                       pu::ui::Color(56, 130, 225, 255));
        this->tiles_dirty = false;
    }

    // (Re)render one cached text texture when its source changed or the
    // texture was dropped by a cache rebuild; empty text clears it.
    void UpdText(pu::sdl2::Texture &tex, s32 &tw, s32 &th, std::string &src,
                 const std::string &txt, const std::string &font,
                 const pu::ui::Color clr, const u32 max_w,
                 const bool force = false) {
        if (!force && src == txt && (tex || txt.empty())) {
            return;
        }
        if (tex) {
            pu::ui::render::DeleteTexture(tex);
            tex = nullptr;
        }
        src = txt;
        tw = th = 0;
        if (!txt.empty()) {
            tex = pu::ui::render::RenderText(font, txt, clr, max_w);
            tw = pu::ui::render::GetTextureWidth(tex);
            th = pu::ui::render::GetTextureHeight(tex);
        }
    }

    // Does `text` fit within max_w at the given font? (measure and discard)
    bool TitleFits(const std::string &text, const std::string &font,
                   const s32 max_w) {
        auto tex = pu::ui::render::RenderText(font, text, this->title_clr);
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
    void SplitTitle(const std::string &t, const std::string &font,
                    const s32 max_w, std::string &l1, std::string &l2) {
        l1 = t;
        l2.clear();
        if (this->TitleFits(t, font, max_w)) {
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
        if (words.size() < 2 || !this->TitleFits(words[0], font, max_w)) {
            // No space to break at (or the first word alone overflows):
            // split mid-word at the widest UTF-8 prefix that fits.
            std::vector<size_t> bnd; // char-start byte offsets + end
            for (size_t b = 0; b < t.size(); b++) {
                if (((u8)t[b] & 0xC0) != 0x80) {
                    bnd.push_back(b);
                }
            }
            bnd.push_back(t.size());
            size_t lo = 1, hi = bnd.size() - 1, best = 1;
            while (lo <= hi) {
                size_t mid = (lo + hi) / 2;
                if (this->TitleFits(t.substr(0, bnd[mid]), font, max_w)) {
                    best = mid;
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
            if (best >= bnd.size() - 1) {
                return; // fits after all (measurement drift): single line
            }
            l1 = t.substr(0, bnd[best]);
            l2 = t.substr(bnd[best]); // still ellipsizes if too long
            return;
        }
        std::string fit;
        size_t i = 0;
        for (; i < words.size(); i++) {
            std::string cand = fit.empty() ? words[i] : fit + " " + words[i];
            if (!fit.empty() && !this->TitleFits(cand, font, max_w)) {
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
            if (cd.queue) {
                // Queue cards render via SetQueueCard's diff path; the empty
                // cell makes it rebuild everything on the next update.
                this->cache.push_back(c);
                continue;
            }
            std::string l1, l2;
            this->SplitTitle(cd.title, this->font_title, (s32)max_tw, l1, l2);
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

    // Download/unzip progress traced around a rounded card outline clockwise
    // in the signature green->blue gradient: straight runs as short gradient
    // rects, corners as stamped dots (no arc primitive). ring: 0 live
    // gradient, 1 done (solid green), 2 failed (solid red).
    void DrawRing(pu::ui::render::Renderer::Ref &drawer, const s32 cx,
                  const s32 cy, const s32 cw, const s32 ch, const s32 rad,
                  const s32 inset, const s32 bt, const float prog,
                  const s32 ring) {
        const s32 x0 = cx + inset, y0 = cy + inset;
        const s32 pw = cw - 2 * inset, ph = ch - 2 * inset;
        const s32 R = rad - inset;
        const pu::ui::Color trk = this->trk_clr;
        for (s32 t = 0; t < bt; t++) {
            drawer->RenderRoundedRectangle(trk, x0 + t, y0 + t, pw - 2 * t,
                                           ph - 2 * t, R - t > 1 ? R - t : 1);
        }
        const s32 rc = R - bt / 2; // centerline corner radius
        const s32 lx = x0 + bt / 2, ty = y0 + bt / 2;
        const s32 rxr = x0 + pw - bt / 2;
        const s32 by = y0 + ph - bt / 2;
        const s32 hs = (rxr - lx) - 2 * rc;
        const s32 vs = (by - ty) - 2 * rc;
        const s32 q = (s32)(1.5708f * (float)rc + 0.5f);
        const s32 L = 2 * hs + 2 * vs + 4 * q;
        s32 fill = (s32)(prog * (float)L + 0.5f);
        if (fill > L) {
            fill = L;
        }
        // Terminal states swap the live gradient for a solid ring: green
        // when done, red when failed. g0 tracks the theme accent so the
        // green end stays legible on the light theme's pale cards.
        pu::ui::Color g0 = this->glow_clr;
        g0.a = 255;
        pu::ui::Color g1(56, 130, 225, 255);
        if (ring == 1) {
            g1 = g0;
        } else if (ring == 2) {
            g0 = g1 = pu::ui::Color(224, 82, 82, 255);
        }
        // Terminal rings are a single flat colour and never change, so draw
        // each straight edge as ONE rect instead of ~40 gradient strips — this
        // is the common case in a populated queue (done/failed cards), and it
        // ran every frame. Only the live gradient ring needs the strip loop.
        const bool solid = (ring != 0);
        // Live gradient ring: coarser segments (12px vs 6) halve the per-frame
        // draw calls with no visible change on a 6px-thick ring — this ring is
        // redrawn every frame per active download, so it scaled the queue lag.
        const s32 seg_cap = solid ? L : 12;
        auto grad = [&](s32 dd) {
            float t = (float)dd / (float)L;
            return pu::ui::Color((u8)(g0.r + ((s32)g1.r - g0.r) * t),
                                 (u8)(g0.g + ((s32)g1.g - g0.g) * t),
                                 (u8)(g0.b + ((s32)g1.b - g0.b) * t), 255);
        };
        s32 d = 0;
        // edge: 0 top(->right) 1 right(->down) 2 bottom(->left) 3 left(->up)
        auto straight = [&](s32 len, int edge) {
            s32 done = 0;
            while (done < len && d < fill) {
                s32 seg = fill - d < seg_cap ? fill - d : seg_cap;
                if (seg > len - done) {
                    seg = len - done;
                }
                auto c = grad(d);
                switch (edge) {
                case 0:
                    drawer->RenderRectangleFill(c, lx + rc + done, ty - bt / 2,
                                                seg, bt);
                    break;
                case 1:
                    drawer->RenderRectangleFill(c, rxr - bt / 2, ty + rc + done,
                                                bt, seg);
                    break;
                case 2:
                    drawer->RenderRectangleFill(c, rxr - rc - done - seg,
                                                by - bt / 2, seg, bt);
                    break;
                default:
                    drawer->RenderRectangleFill(c, lx - bt / 2,
                                                by - rc - done - seg, bt, seg);
                    break;
                }
                done += seg;
                d += seg;
            }
            d += len - done;
        };
        // corner: 0 TR, 1 BR, 2 BL, 3 TL (clockwise order)
        auto arc = [&](s32 ccx, s32 ccy, int corner) {
            for (s32 a = 0; a < q; a += 2) {
                if (d + a >= fill) {
                    break;
                }
                float p = ((float)a + 1.0f) / (float)q * 1.5708f;
                s32 ds = (s32)((float)rc * sinf(p) + 0.5f);
                s32 dc = (s32)((float)rc * cosf(p) + 0.5f);
                s32 px, py;
                switch (corner) {
                case 0:
                    px = ccx + ds;
                    py = ccy - dc;
                    break;
                case 1:
                    px = ccx + dc;
                    py = ccy + ds;
                    break;
                case 2:
                    px = ccx - ds;
                    py = ccy + dc;
                    break;
                default:
                    px = ccx - dc;
                    py = ccy - ds;
                    break;
                }
                drawer->RenderCircleFill(grad(d + a), px, py, bt / 2);
            }
            d += q;
        };
        straight(hs, 0);
        arc(rxr - rc, ty + rc, 0);
        straight(vs, 1);
        arc(rxr - rc, by - rc, 1);
        straight(hs, 2);
        arc(lx + rc, by - rc, 2);
        straight(vs, 3);
        arc(lx + rc, ty + rc, 3);
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
        // Extra size registered in Main.cpp; queue cards' chip + filename.
        this->font_tiny = pu::ui::MakeDefaultFontName(21);
    }
    PU_SMART_CTOR(CardGrid)

    ~CardGrid() {
        this->FreeCache();
        this->FreeTiles();
    }

    void SetThemeColors(pu::ui::Color bg, pu::ui::Color focus,
                        pu::ui::Color title, pu::ui::Color sub,
                        pu::ui::Color glow = {146, 214, 36, 255},
                        pu::ui::Color pill = {0, 0, 0, 95},
                        pu::ui::Color page = {0, 0, 0, 0},
                        pu::ui::Color track = {255, 255, 255, 20}) {
        this->card_bg = bg;
        this->focus_bg = focus;
        this->title_clr = title;
        this->sub_clr = sub;
        this->glow_clr = glow;
        this->pill_clr = pill;
        this->page_bg = page;
        this->trk_clr = track;
        this->dirty = true;
        this->tiles_dirty = true;
    }

    void SetSingle(const bool on) { this->single = on; }

    // True if queue card i could be on screen (one row of margin). Lets the
    // caller skip building off-screen cards' text every frame — the queue tick
    // otherwise formats every item (incl. completed/off-screen) per frame.
    bool QueueIndexVisible(const s32 i) {
        if (this->single) {
            return i == 0;
        }
        const s32 lo = this->scroll_row * Cols;
        const s32 hi = (this->scroll_row + this->VisRows() + 1) * Cols;
        return i >= lo && i < hi;
    }

    // Bake the tiles up front (renderer must be ready) so the first card screen
    // doesn't pay the one-time bake as a visible hitch.
    void PrewarmTiles() {
        if (this->tiles_dirty) {
            this->RebakeTiles();
        }
    }

    void Clear() {
        this->cards.clear();
        this->FreeCache();
        this->sel = 0;
        this->scroll_row = 0;
        this->single = false;
        // Enter fade removed for performance (re-rendered the whole grid for
        // ~8 frames per screen change; stuttered under download load).
        this->enter_alpha = 255;
        this->dirty = true;
        this->tch_active = false;
        this->tch_card = -1;
        this->tch_activate = false;
    }

    void AddCard(const std::string &title, const std::string &subtitle,
                 pu::sdl2::Texture icon, bool pinned = false,
                 bool dim = false) {
        this->cards.push_back(Card{title, subtitle, icon, pinned, dim});
        this->dirty = true;
    }

    // Queue card view: the queue refreshes every frame, so instead of the
    // Clear + AddCard rebuild it diff-updates cards in place — text textures
    // re-render only when their content actually changed.
    void SetQueueCount(const s32 n) {
        if ((s32)this->cards.size() == n) {
            return;
        }
        this->FreeCache();
        this->cards.assign((size_t)n, Card{});
        for (auto &c : this->cards) {
            c.queue = true;
        }
        this->cache.assign((size_t)n, Cell{});
        if (this->sel >= n) {
            this->sel = n > 0 ? n - 1 : 0;
        }
        this->EnsureVisible();
        this->dirty = false;
    }

    void SetQueueCard(const s32 i, const std::string &console,
                      pu::sdl2::Texture icon, const std::string &status,
                      const pu::ui::Color st_clr, const std::string &size,
                      const std::string &speed, const std::string &eta,
                      const std::string &file, const float prog,
                      const bool hero, const s32 ring = 0,
                      const s32 qpos = 0, const bool refresh_text = true) {
        if (i < 0 || i >= (s32)this->cards.size() ||
            i >= (s32)this->cache.size()) {
            return;
        }
        Card &cd = this->cards[i];
        Cell &ce = this->cache[i];
        cd.queue = true;
        cd.icon = icon;
        cd.prog = prog;
        cd.hero = hero;
        cd.ring = ring;
        // Only build (rasterize) text for cards that can be on screen. The
        // queue holds up to QUEUE_MAX items and finished ones accumulate, so
        // rendering every card's text on tab-entry stalled the switch for 1-2s.
        // Off-screen cards build when they scroll into view (the queue re-runs
        // this every frame); one extra row of margin hides the build latency.
        if (!this->single) {
            const s32 lo = this->scroll_row * Cols;
            const s32 hi = (this->scroll_row + this->VisRows() + 1) * Cols;
            if (i < lo || i >= hi) {
                return;
            }
        }
        const s32 cw = this->single ? SingleW : this->CardW();
        // The huge single card gets a size tier up on every text run.
        const std::string txt_font =
            this->single ? this->font_sub : this->font_tiny;
        const std::string st_font =
            this->single
                ? pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium)
                : this->font_sub;
        // Corner labels (console name left, status right) frame a big
        // centred icon, browse-card style. The console name is stable, so it
        // updates every frame (a no-op via the diff); the status % and the
        // size/speed/eta chip change constantly during a download, so their
        // (expensive) rasterization is throttled by the caller via refresh_text
        // — that per-active-card text churn is what scaled the queue lag.
        this->UpdText(ce.t1_tex, ce.t1w, ce.t1h, cd.title, console,
                      txt_font, this->title_clr, (u32)(cw - 120));
        if (this->single || refresh_text) {
            const bool recolor = cd.st_clr.r != st_clr.r ||
                                 cd.st_clr.g != st_clr.g ||
                                 cd.st_clr.b != st_clr.b ||
                                 cd.st_clr.a != st_clr.a;
            cd.st_clr = st_clr;
            this->UpdText(ce.st_tex, ce.stw, ce.sth, cd.status, status,
                          st_font, st_clr, (u32)(cw / 2 - 20), recolor);
            // size / speed / eta join into one pill, like the list view's chip.
            std::string chip = size;
            if (!speed.empty()) {
                chip += (chip.empty() ? "" : " · ") + speed;
            }
            if (!eta.empty()) {
                chip += (chip.empty() ? "" : " · ") + eta;
            }
            this->UpdText(ce.ch_tex, ce.chw, ce.chh, cd.chip, chip,
                          txt_font, this->sub_clr, (u32)(cw - 48));
        }
        // Queue-position badge ("#2") for waiting cards.
        this->UpdText(ce.qp_tex, ce.qpw, ce.qph, cd.badge,
                      qpos > 0 ? "#" + std::to_string(qpos) : "",
                      this->font_tiny, this->sub_clr, (u32)(cw / 4));
        // Filename wraps onto two lines. Split only when the name changes:
        // SplitTitle's measuring is too heavy for every frame.
        if (cd.file != file || (!ce.f_tex && !file.empty())) {
            cd.file = file;
            std::string l1, l2;
            if (!file.empty()) {
                this->SplitTitle(file, txt_font, cw - 36, l1, l2);
            }
            this->UpdText(ce.f_tex, ce.fw, ce.fh, cd.f1, l1, txt_font,
                          this->sub_clr, (u32)(cw - 36));
            this->UpdText(ce.f2_tex, ce.f2w, ce.f2h, cd.f2, l2,
                          txt_font, this->sub_clr, (u32)(cw - 36));
        }
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
        if (this->tiles_dirty) {
            this->RebakeTiles();
        }
        if (this->enter_alpha < 255) {
            s32 e = this->enter_alpha + 32;
            this->enter_alpha = e > 255 ? 255 : e;
        }
        if (this->single) {
            // One enlarged queue-style card, centred: the self-update
            // download. Always drawn "lit" (hero tint + green edge + icon
            // glow) since it is the whole screen's focus.
            const Card &cd = this->cards[0];
            Cell &qc = this->cache[0];
            const s32 scw = SingleW, sch = SingleH, rad = SingleRadius;
            const s32 cx = rx + (this->w - scw) / 2;
            const s32 cy = ry + (this->h - sch) / 2;
            drawer->RenderRoundedRectangleFill(this->card_bg, cx, cy, scw,
                                               sch, rad);
            auto hc = this->glow_clr;
            hc.a = 30;
            drawer->RenderRoundedRectangleFill(hc, cx, cy, scw, sch, rad);
            for (s32 g = 1; g <= 4; g++) {
                auto gc = this->glow_clr;
                gc.a = (u8)(40 - g * 9);
                drawer->RenderRoundedRectangle(gc, cx - g, cy - g,
                                               scw + 2 * g, sch + 2 * g,
                                               rad + g);
            }
            for (s32 t = 0; t < 2; t++) {
                drawer->RenderRoundedRectangle(this->glow_clr, cx + t, cy + t,
                                               scw - 2 * t, sch - 2 * t,
                                               rad - t);
            }
            drawer->RenderRectangleFill(pu::ui::Color(255, 255, 255, 45),
                                        cx + rad, cy, scw - 2 * rad, 1);
            drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 50), cx + rad,
                                        cy + sch - 1, scw - 2 * rad, 1);
            // Vertical layout, top to bottom: corner labels, big icon,
            // wrapped filename, chip pill — spaced out for the large card.
            const s32 ic_top = 76;             // icon top offset from cy
            const s32 isz = SingleIconPx;
            const s32 ic_bot = ic_top + isz;   // icon bottom offset
            const s32 chip_y = sch - 60;       // chip near the bottom edge
            if (qc.t1_tex) {
                drawer->RenderTexture(qc.t1_tex, cx + 30, cy + 30);
            }
            if (qc.st_tex) {
                drawer->RenderTexture(qc.st_tex, cx + scw - 30 - qc.stw,
                                      cy + 28);
            }
            if (cd.icon) {
                s32 gcx = cx + scw / 2;
                s32 gcy = cy + ic_top + isz / 2;
                for (s32 g = 0; g < 4; g++) {
                    auto gc = this->glow_clr;
                    gc.a = (u8)(14 + 5 * g);
                    drawer->RenderCircleFill(gc, gcx, gcy,
                                             isz / 2 + 2 - 6 * g);
                }
                pu::ui::render::TextureRenderOptions o;
                o.width = isz;
                o.height = isz;
                drawer->RenderTexture(cd.icon, cx + (scw - isz) / 2,
                                      cy + ic_top, o);
            }
            if (qc.f_tex) {
                // Sit the filename block in the gap between icon and chip,
                // centred vertically there.
                s32 fh = qc.fh + (qc.f2_tex ? qc.f2h + 2 : 0);
                s32 fy = cy + ic_bot + (chip_y - ic_bot - fh) / 2;
                drawer->RenderTexture(qc.f_tex, cx + (scw - qc.fw) / 2, fy);
                if (qc.f2_tex) {
                    drawer->RenderTexture(qc.f2_tex,
                                          cx + (scw - qc.f2w) / 2,
                                          fy + qc.fh + 2);
                }
            }
            if (qc.ch_tex) {
                s32 sx = cx + (scw - qc.chw) / 2;
                const s32 padx = 14, pady = 5;
                drawer->RenderRoundedRectangleFill(
                    this->pill_clr, sx - padx, cy + chip_y - pady,
                    qc.chw + 2 * padx, qc.chh + 2 * pady,
                    (qc.chh + 2 * pady) / 2);
                drawer->RenderTexture(qc.ch_tex, sx, cy + chip_y);
            }
            if (cd.prog >= 0.0f) {
                this->DrawRing(drawer, cx, cy, scw, sch, rad, 5, 8, cd.prog,
                               cd.ring);
            }
            if (this->enter_alpha < 255 && this->page_bg.a > 0) {
                auto veil = this->page_bg;
                veil.a = (u8)(255 - this->enter_alpha);
                drawer->RenderRectangleFill(veil, rx, ry, this->w, this->h);
            }
            return;
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
                const Card &cd = this->cards[idx];
                if (this->tile_card) {
                    drawer->RenderTexture(this->tile_card, cx, cy);
                } else {
                    drawer->RenderRectangleFill(this->card_bg, cx, cy, cw,
                                                CardH);
                }
                if (cd.queue && cd.hero) {
                    // Active download: accent-tinted "hero" card, matching
                    // the list view's accent row.
                    if (this->tile_hero) {
                        pu::ui::render::TextureRenderOptions o;
                        o.alpha_mod = 30;
                        drawer->RenderTexture(this->tile_hero, cx, cy, o);
                    } else {
                        auto hc = this->glow_clr;
                        hc.a = 30;
                        drawer->RenderRoundedRectangleFill(hc, cx, cy, cw,
                                                           CardH, CardRadius);
                    }
                }
                if (selected) {
                    // Lifted fill eases in, wrapped in a logo-green outline
                    // + soft outer glow (the "lit" card, matching the list).
                    if (this->tile_focus) {
                        pu::ui::render::TextureRenderOptions o;
                        o.alpha_mod = this->sel_alpha;
                        drawer->RenderTexture(this->tile_focus, cx, cy, o);
                    } else {
                        auto f = this->focus_bg;
                        f.a = (u8)this->sel_alpha;
                        drawer->RenderRoundedRectangleFill(f, cx, cy, cw, CardH,
                                                           CardRadius);
                    }
                    for (s32 g = 1; g <= 4; g++) {
                        auto gc = this->glow_clr;
                        gc.a = (u8)((40 - g * 9) * this->sel_alpha / 255);
                        drawer->RenderRoundedRectangle(gc, cx - g, cy - g,
                                                       cw + 2 * g,
                                                       CardH + 2 * g,
                                                       CardRadius + g);
                    }
                    auto edge = this->glow_clr;
                    edge.a = (u8)this->sel_alpha;
                    for (s32 t = 0; t < 2; t++) {
                        drawer->RenderRoundedRectangle(
                            edge, cx + t, cy + t, cw - 2 * t, CardH - 2 * t,
                            CardRadius - t > 4 ? CardRadius - t : 4);
                    }
                }
                // Bevel: 1px gloss along the top, 1px shade along the bottom,
                // so cards read as raised tiles (matches the list rows).
                drawer->RenderRectangleFill(
                    pu::ui::Color(255, 255, 255, (u8)(selected ? 45 : 18)),
                    cx + CardRadius, cy, cw - 2 * CardRadius, 1);
                drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 50),
                                            cx + CardRadius, cy + CardH - 1,
                                            cw - 2 * CardRadius, 1);
                if (cd.pinned) {
                    drawer->RenderCircleFill(this->glow_clr, cx + 16, cy + 16,
                                             5);
                }
                if (cd.queue) {
                    Cell &qc = this->cache[idx];
                    // Corner labels: console name top-left, status top-right,
                    // padded in past the progress ring.
                    if (qc.t1_tex) {
                        drawer->RenderTexture(qc.t1_tex, cx + 20, cy + 18);
                    }
                    if (qc.st_tex) {
                        drawer->RenderTexture(qc.st_tex,
                                              cx + cw - 20 - qc.stw, cy + 16);
                    }
                    // Icon in the exact browse-card spot: same size, same
                    // grow-upward and green glow bloom on selection.
                    if (cd.icon) {
                        s32 isz = IconPx;
                        if (selected) {
                            isz += (10 * this->sel_alpha) / 255;
                            s32 gcx = cx + cw / 2;
                            s32 gcy = cy + 10 + IconPx / 2;
                            for (s32 g = 0; g < 4; g++) {
                                auto gc = this->glow_clr;
                                gc.a = (u8)((14 + 5 * g) * this->sel_alpha /
                                            255);
                                drawer->RenderCircleFill(gc, gcx, gcy,
                                                         IconPx / 2 + 2 -
                                                             6 * g);
                            }
                        }
                        pu::ui::render::TextureRenderOptions o;
                        o.width = isz;
                        o.height = isz;
                        drawer->RenderTexture(cd.icon, cx + (cw - isz) / 2,
                                              cy + 10 - (isz - IconPx), o);
                    }
                    // Filename under the icon, up to two wrapped lines, with
                    // clear air above the pill below it.
                    if (qc.f_tex) {
                        s32 fy = qc.f2_tex ? cy + 152 : cy + 166;
                        drawer->RenderTexture(qc.f_tex, cx + (cw - qc.fw) / 2,
                                              fy);
                        if (qc.f2_tex) {
                            drawer->RenderTexture(qc.f2_tex,
                                                  cx + (cw - qc.f2w) / 2,
                                                  fy + qc.fh + 2);
                        }
                    }
                    // size · speed · eta as one pill along the bottom.
                    if (qc.ch_tex) {
                        s32 sx = cx + (cw - qc.chw) / 2;
                        const s32 padx = 12, pady = 4;
                        drawer->RenderRoundedRectangleFill(
                            this->pill_clr, sx - padx, cy + 222 - pady,
                            qc.chw + 2 * padx, qc.chh + 2 * pady,
                            (qc.chh + 2 * pady) / 2);
                        drawer->RenderTexture(qc.ch_tex, sx, cy + 222);
                    }
                    // Queue-position badge, tucked into the bottom-left
                    // corner on the chip line (waiting cards' chips are
                    // short, so the centred pill never reaches it).
                    if (qc.qp_tex) {
                        const s32 padx = 8, pady = 3;
                        drawer->RenderRoundedRectangleFill(
                            this->pill_clr, cx + 14, cy + 222 - pady,
                            qc.qpw + 2 * padx, qc.qph + 2 * pady,
                            (qc.qph + 2 * pady) / 2);
                        drawer->RenderTexture(qc.qp_tex, cx + 14 + padx,
                                              cy + 222);
                    }
                    if (cd.prog >= 0.0f) {
                        this->DrawRing(drawer, cx, cy, cw, CardH, CardRadius,
                                       4, 6, cd.prog, cd.ring);
                    }
                    continue;
                }
                if (cd.icon) {
                    // The selected card's icon grows slightly with the fade.
                    s32 isz = IconPx;
                    if (selected) {
                        isz += (10 * this->sel_alpha) / 255;
                        // Soft green glow blooming in behind the icon with the
                        // same fade. Largest ring (r=67) stays inside the card
                        // and clear of the title band at cy + 144.
                        s32 gcx = cx + cw / 2;
                        s32 gcy = cy + 10 + IconPx / 2;
                        for (s32 g = 0; g < 4; g++) {
                            auto gc = this->glow_clr;
                            gc.a = (u8)((14 + 5 * g) * this->sel_alpha / 255);
                            drawer->RenderCircleFill(gc, gcx, gcy,
                                                     IconPx / 2 + 2 - 6 * g);
                        }
                    }
                    pu::ui::render::TextureRenderOptions o;
                    o.width = isz;
                    o.height = isz;
                    // Disabled entries fade their icon; selecting one lights
                    // it back up with the focus fade.
                    if (cd.dim) {
                        o.alpha_mod =
                            selected ? 110 + (145 * this->sel_alpha) / 255
                                     : 110;
                    }
                    // Grow upward only: the bottom edge stays fixed so the
                    // enlarged icon never touches a two-line title below it.
                    drawer->RenderTexture(this->cards[idx].icon,
                                          cx + (cw - isz) / 2,
                                          cy + 10 - (isz - IconPx), o);
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
                    s32 sx = cx + (cw - ce.sw) / 2;
                    s32 sy = cy + 220;
                    s32 padx = 12, pady = 5;
                    drawer->RenderRoundedRectangleFill(
                        this->pill_clr, sx - padx, sy - pady,
                        ce.sw + 2 * padx, ce.sh + 2 * pady,
                        (ce.sh + 2 * pady) / 2);
                    drawer->RenderTexture(ce.sub_tex, sx, sy);
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
            // Thumb takes the signature green->blue gradient (matches the
            // list view's scrollbar); green end follows the theme accent.
            // Thumb takes the signature green->blue gradient via the baked
            // strip (stretched to the thumb height); flat only as a fallback.
            if (this->grad_tex) {
                pu::ui::render::TextureRenderOptions o;
                o.width = 6;
                o.height = thumb_h;
                drawer->RenderTexture(this->grad_tex, rx + this->w - 6, ty, o);
            } else {
                pu::ui::Color g0 = this->glow_clr;
                g0.a = 255;
                drawer->RenderRectangleFill(g0, rx + this->w - 6, ty, 6,
                                            thumb_h);
            }
        }
        // Enter fade: a page-coloured veil over the fresh grid thins out
        // across ~8 frames, easing screen/tab switches in.
        if (this->enter_alpha < 255 && this->page_bg.a > 0) {
            auto veil = this->page_bg;
            veil.a = (u8)(255 - this->enter_alpha);
            drawer->RenderRectangleFill(veil, rx, ry, this->w, this->h);
        }
    }

    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint tch) override {
        if (this->cards.empty() || this->single) {
            return; // single mode: nothing to select or scroll
        }
        if (!tch.IsEmpty()) {
            if (!this->tch_active) {
                if (!tch.HitsRegion(this->x, this->y, this->w, this->h)) {
                    return;
                }
                this->tch_active = true;
                this->tch_dragged = false;
                this->tch_start_x = tch.x;
                this->tch_start_y = tch.y;
                this->tch_last_y = tch.y;
                this->tch_acc = 0;
                this->tch_card = this->HitCard(tch.x, tch.y);
            } else {
                // Horizontal movement also counts as a drag so a tab swipe
                // passing through never reads as a tap on release.
                if (!this->tch_dragged &&
                    (tch.y - this->tch_start_y > DragThreshold ||
                     this->tch_start_y - tch.y > DragThreshold ||
                     tch.x - this->tch_start_x > DragThreshold ||
                     this->tch_start_x - tch.x > DragThreshold)) {
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
            } else if (this->tch_dragged) {
                // Drag scrolled the viewport away from the selection: pull the
                // selection to the nearest visible card (keep the column) so
                // a following A press acts on something the user can see.
                s32 row = this->sel / Cols;
                s32 col = this->sel % Cols;
                s32 lo = this->scroll_row;
                s32 hi = this->scroll_row + this->VisRows() - 1;
                if (row < lo || row > hi) {
                    s32 nrow = row < lo ? lo : hi;
                    s32 ns = nrow * Cols + col;
                    if (ns >= (s32)this->cards.size()) {
                        ns = (s32)this->cards.size() - 1;
                    }
                    this->sel = ns; // in view already: no EnsureVisible snap
                }
            }
        }
    }
};
