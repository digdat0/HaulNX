#pragma once

// A grid of "cards" (big icon + title + subtitle) used as the optional card
// view for the console lists (Browse home, Installed root) - 4-wide by
// default, see SetCols. Installed's game list additionally has a "poster"
// mode (SetPoster) that narrows to 6-7 columns and leads each card with box
// art instead of a small icon, see CardH()/Card::art. Like TableList it is
// passive: the app drives selection (Move / SetSelected) and OnInput only
// handles touch (tap select, tap-again activate, drag scroll). Card icons
// are BORROWED from the shared console-icon/box-art caches — never freed
// here; the rendered text textures are cached and owned by this element.

#include <cmath>
#include <pu/Plutonium>
#include <gfx_tile.hpp>
#include <set>
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
        // Poster mode only: true when `icon` is a real box art cover (already
        // portrait, stretched to fill the image area); false renders it
        // centred at its natural size instead (the no-cover console-icon
        // fallback would otherwise squash into a tall rectangle).
        bool art = false;
        // Queue-mode extras (SetQueueCard): the strings double as the
        // "last rendered" keys for the per-frame diff updates.
        bool queue = false;
        std::string status;
        pu::ui::Color st_clr{255, 255, 255, 255};
        std::string chip;   // top pill line: size (now/total while active)
        std::string chip2;  // bottom pill line: speed · eta joined
        std::string file;   // full filename (diff gate for the split below)
        std::string f1, f2; // wrapped filename lines (diff keys)
        float prog = -1.0f; // perimeter progress bar; -1 = none
        bool hero = false;  // active download: accent-tinted card
        s32 ring = 0;       // 0 live gradient, 1 done (solid green),
                            // 2 failed (solid red)
        std::string badge;  // queue-position badge text (diff key)
        // Single-card queue view only: a real console icon has generous
        // transparent padding baked into its PNG (roughly a 60-75% content
        // box), so drawing it edge-to-edge at the full icon slot still reads
        // as comfortably inset. The self-update card's app-logo texture has
        // none -- full bleed to every edge -- so at the same slot size it
        // reads as oversized and crowds the corner labels/border above it.
        // Set true (see console_display_icon's "HaulNX" case) to draw that
        // texture visibly smaller within the same slot instead.
        bool logo_icon = false;
    };

  private:
    struct Cell {
        pu::sdl2::Texture t1_tex; // title line 1
        pu::sdl2::Texture t2_tex; // title line 2 (word-wrapped overflow)
        pu::sdl2::Texture sub_tex;
        s32 t1w, t1h, t2w, t2h, sw, sh;
        // Set once BuildCell has actually rasterized this cell's text. A
        // freshly (re)sized cache starts every cell unbuilt regardless of
        // aggregate-init order, since this has its own default -- see
        // RebuildCache/BuildCell.
        bool built = false;
        // queue-mode textures
        pu::sdl2::Texture st_tex = nullptr;
        pu::sdl2::Texture ch_tex = nullptr;  // pill line 1: size
        pu::sdl2::Texture ch2_tex = nullptr; // pill line 2: speed · eta
        pu::sdl2::Texture f_tex = nullptr;  // filename line 1
        pu::sdl2::Texture f2_tex = nullptr; // filename line 2 (wrap)
        pu::sdl2::Texture qp_tex = nullptr; // queue-position badge
        s32 stw = 0, sth = 0, chw = 0, chh = 0, ch2w = 0, ch2h = 0, fw = 0,
            fh = 0, f2w = 0, f2h = 0, qpw = 0, qph = 0;
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
    // Multi-select marks (Installed's poster view, Y button) — mirrors
    // TableList::marked. A blue border (distinct from the green focus
    // outline, which tracks the cursor rather than the selection set) tags
    // marked cards in OnRender.
    std::set<s32> marked;
    bool dirty;
    pu::ui::Color card_bg, focus_bg, title_clr, sub_clr, fail_clr{224, 82, 82, 255};
    // Logo green for the selection outline/glow and the icon halo; the theme
    // passes its own variant (bright on dark, deep on light) via SetThemeColors.
    pu::ui::Color glow_clr{146, 214, 36, 255};
    // Live-ring/scrollbar gradient's far stop -- the other half of the
    // selected accent pair (glow_clr is the near stop); see SetThemeColors.
    pu::ui::Color glow2_clr{56, 130, 225, 255};
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

    // Column count is an instance setting (SetCols) rather than a constant:
    // the console/settings grids stay 4-wide, but the Installed game-list
    // poster view narrows the cards to fit 6-7 across. Defaults to the
    // original 4 so every other screen is unaffected.
    s32 Cols = 4;
    static constexpr s32 Margin = 30;
    static constexpr s32 Gap = 20;
    static constexpr s32 CardHNormal = 264;
    static constexpr s32 IconPx = 130;
    static constexpr s32 CardRadius = 14;
    // Poster mode (Installed game list): the card's top area is the box art
    // itself at its native 600x900 (2:3) ratio instead of a small centred
    // icon, with the title + size beneath. bool per-card (Card::art) picks
    // stretch-to-fill (real cover) vs centred-natural-size (no-cover
    // fallback icon).
    bool poster = false;
    static constexpr s32 PosterPad = 12;
    static constexpr s32 PosterTextH = 78; // title line + gap + size line
    // Fixed row height, NOT derived from the card's width. Deriving the
    // image height from card width (the original approach) made a 6-7 wide
    // poster row taller than the whole viewport, so only a single row ever
    // fit and the grid effectively became a sideways one-card-at-a-time
    // scroll. Fixing the row height instead, and deriving the image's
    // *width* from the height budget left for it (see the OnRender poster
    // branch), keeps two full rows on screen with the rest reachable by
    // scrolling down.
    static constexpr s32 PosterRowH = 400;
    // Queue cards (SetQueueCard) opt into the same tall row height as poster
    // mode, at the same 6-wide column count, so the grid reads as one
    // consistent card size across Library/Collections/Queue -- see
    // SetQueueTall. Kept as its own flag rather than reusing `poster` since
    // queue cards render via their own dedicated path (Card::queue) and don't
    // want poster mode's box-art/marquee-title behavior, just its row height.
    bool queue_tall = false;
    s32 CardH() const {
        return (this->poster || this->queue_tall) ? PosterRowH : CardHNormal;
    }
    // Slow left-right "reveal" scroll for a poster title too long to fit the
    // card: pause at the start, glide to show the tail, pause, glide back.
    // Shared across every overflowing title (one global phase) rather than
    // per-card, so scrolling titles move in lockstep instead of a jittery mix
    // of phases when several are visible at once.
    s32 marquee_frame = 0;
    // Free-running frame counter driving the active-transfer ("hero") card's
    // progress-ring shimmer and its under-card shadow pulse -- decorative
    // motion so the one card that's actually transferring right now reads as
    // alive at a glance, matching the list view's accent row (TableList's
    // anim_frame). Increments once per OnRender call, independent of
    // marquee_frame (which only ticks in poster mode).
    s32 anim_frame = 0;
    s32 MarqueeOffset(const s32 overflow) const {
        if (overflow <= 0) {
            return 0;
        }
        const s32 pause = 70;             // ~1.2s hold at each end, @60fps
        const s32 px_frames = 4;          // frames per pixel (~15px/s)
        const s32 glide = overflow * px_frames;
        const s32 cycle = 2 * pause + 2 * glide;
        s32 t = this->marquee_frame % cycle;
        if (t < pause) {
            return 0;
        }
        t -= pause;
        if (t < glide) {
            return -(t / px_frames);
        }
        t -= glide;
        if (t < pause) {
            return -overflow;
        }
        t -= pause;
        return -overflow + (t / px_frames);
    }
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
    s32 VisRows() const { return (this->h + Gap) / (CardH() + Gap); }
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
            if (c.ch2_tex) {
                pu::ui::render::DeleteTexture(c.ch2_tex);
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
        const s32 ch = this->CardH();
        this->tile_card = BakeRoundTile(cw, ch, CardRadius, this->card_bg);
        this->tile_hero = BakeRoundTile(cw, ch, CardRadius, this->glow_clr);
        this->tile_focus = BakeRoundTile(cw, ch, CardRadius, this->focus_bg);
        this->grad_tex = BakeVGradient(256, this->glow_clr, this->glow2_clr);
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

    // Only re-sizes the cache to match `cards` -- no text is rasterized here
    // any more (see BuildCell). This used to render every card's title and
    // subtitle up front, the moment ANY card changed: switching to a Home/
    // Installed-root screen with ~60 console cards paid ~60 TTF renders
    // (title + subtitle each) synchronously before the first frame could even
    // draw, regardless of how many of those cards were ever scrolled into
    // view. TableList's own RebuildCache already only ever touches its
    // visible window for exactly this reason (see its comment) -- this
    // mirrors that by deferring the actual render to OnRender, the first time
    // each card is actually about to be drawn.
    void RebuildCache() {
        this->FreeCache();
        this->cache.assign(this->cards.size(), Cell{});
        this->dirty = false;
    }

    // Rasterize one card's title/subtitle text on first draw (OnRender calls
    // this when a non-queue card's cell isn't built yet). Queue cards never
    // reach here -- they render via SetQueueCard's own diff-updated path,
    // gated on visibility there the same way, and OnRender's queue branch
    // returns before this would be called.
    void BuildCell(const s32 idx) {
        Cell &c = this->cache[idx];
        const Card &cd = this->cards[idx];
        const u32 max_tw = (u32)(this->CardW() - 30);
        if (this->poster) {
            // Poster cards keep the title to one line, small (font_tiny,
            // smaller than the list/root cards' font_sub - the art above
            // already carries most of the "which game is this" load, and
            // six-wide cards don't leave room for a wrapped two-line title).
            // Rendered at *full* width (no max_w) rather than ellipsized:
            // OnRender clips it to the card and, when it's too long to fit,
            // marquees it so the whole name is still reachable instead of
            // being permanently cut off.
            c.t1_tex = pu::ui::render::RenderText(this->font_tiny, cd.title,
                                                  this->title_clr);
            c.t1w = pu::ui::render::GetTextureWidth(c.t1_tex);
            c.t1h = pu::ui::render::GetTextureHeight(c.t1_tex);
        } else {
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
        }
        if (!cd.subtitle.empty()) {
            c.sub_tex = pu::ui::render::RenderText(
                this->poster ? this->font_tiny : this->font_sub, cd.subtitle,
                this->sub_clr, max_tw);
            c.sw = pu::ui::render::GetTextureWidth(c.sub_tex);
            c.sh = pu::ui::render::GetTextureHeight(c.sub_tex);
        }
        c.built = true;
    }

    // Download/unzip progress traced around a rounded card outline clockwise
    // in the signature green->blue gradient: straight runs as short gradient
    // rects, corners as stamped dots (no arc primitive). ring: 0 live
    // gradient, 1 done (solid green), 2 failed (solid red).
    // `shimmer` (active/hero card only -- see the call site) sweeps a soft
    // bright band clockwise around the traced portion of the ring, the same
    // technique as the list view's RenderGradBar shimmer, so the card that's
    // actually transferring keeps moving on its own instead of sitting as a
    // static gradient.
    void DrawRing(pu::ui::render::Renderer::Ref &drawer, const s32 cx,
                  const s32 cy, const s32 cw, const s32 ch, const s32 rad,
                  const s32 inset, const s32 bt, const float prog,
                  const s32 ring, const bool shimmer = false,
                  const s32 anim_frame = 0) {
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
        // Terminal states swap the live gradient for a solid ring: the near
        // accent color when done, red when failed. Both g0/g1 track the
        // selected accent pair (see SetThemeColors) so the ring stays
        // legible on the light theme's pale cards and follows any accent
        // preset the user picks.
        pu::ui::Color g0 = this->glow_clr;
        g0.a = 255;
        pu::ui::Color g1 = this->glow2_clr;
        g1.a = 255;
        if (ring == 1) {
            g1 = g0;
        } else if (ring == 2) {
            g0 = g1 = this->fail_clr;
        }
        // Terminal rings are a single flat colour and never change, so draw
        // each straight edge as ONE rect instead of ~40 gradient strips — this
        // is the common case in a populated queue (done/failed cards), and it
        // ran every frame. Only the live gradient ring needs the strip loop.
        const bool solid = (ring != 0);
        const bool shim = shimmer && !solid;
        const s32 spread = 46; // shimmer half-width, in px
        s32 shimmer_x = 0;
        if (shim) {
            s32 period = L + 2 * spread;
            if (period < 1) period = 1;
            shimmer_x = ((anim_frame * 6) % period) - spread;
        }
        // Live gradient ring: coarser segments (12px vs 6) halve the per-frame
        // draw calls with no visible change on a 6px-thick ring — this ring is
        // redrawn every frame per active download, so it scaled the queue lag.
        const s32 seg_cap = solid ? L : 12;
        auto grad = [&](s32 dd) {
            float t = (float)dd / (float)L;
            pu::ui::Color c((u8)(g0.r + ((s32)g1.r - g0.r) * t),
                            (u8)(g0.g + ((s32)g1.g - g0.g) * t),
                            (u8)(g0.b + ((s32)g1.b - g0.b) * t), 255);
            if (shim) {
                s32 dist = dd - shimmer_x;
                if (dist < 0) dist = -dist;
                if (dist < spread) {
                    float k = 1.0f - (float)dist / (float)spread;
                    k *= k;
                    c.r = (u8)(c.r + (255 - c.r) * k * 0.55f);
                    c.g = (u8)(c.g + (255 - c.g) * k * 0.55f);
                    c.b = (u8)(c.b + (255 - c.b) * k * 0.55f);
                }
            }
            return c;
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
        const s32 ch = this->CardH();
        s32 col = gx / (this->CardW() + Gap);
        s32 vr = gy / (ch + Gap);
        if (col >= Cols || vr >= this->VisRows()) {
            return -1;
        }
        // Inside the card itself, not the gap after it?
        if (gx % (this->CardW() + Gap) >= this->CardW() ||
            gy % (ch + Gap) >= ch) {
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
                        pu::ui::Color track = {255, 255, 255, 20},
                        pu::ui::Color fail = {224, 82, 82, 255},
                        pu::ui::Color glow2 = {56, 130, 225, 255}) {
        this->card_bg = bg;
        this->focus_bg = focus;
        this->title_clr = title;
        this->sub_clr = sub;
        this->glow_clr = glow;
        this->pill_clr = pill;
        this->page_bg = page;
        this->trk_clr = track;
        this->fail_clr = fail;
        this->glow2_clr = glow2;
        this->dirty = true;
        this->tiles_dirty = true;
    }

    void SetSingle(const bool on) { this->single = on; }

    // Column count for the plain grid (console/settings lists stay 4; the
    // Installed game-list poster view calls this with 6-7). Safe to call
    // even when unchanged - only rebuilds when it actually differs.
    void SetCols(s32 n) {
        if (n < 1) {
            n = 1;
        }
        if (this->Cols != n) {
            this->Cols = n;
            this->dirty = true;
            this->tiles_dirty = true;
        }
    }

    // Poster mode: cards lead with the box art itself (or a centred fallback
    // icon) instead of a small centred square icon - see CardH()/Card::art.
    void SetPoster(const bool on) {
        if (this->poster != on) {
            this->poster = on;
            this->dirty = true;
            this->tiles_dirty = true;
        }
    }

    // Queue cards: same tall row height as poster mode, without poster's
    // box-art/marquee behavior. See queue_tall.
    void SetQueueTall(const bool on) {
        if (this->queue_tall != on) {
            this->queue_tall = on;
            this->dirty = true;
            this->tiles_dirty = true;
        }
    }

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

    void Clear(const bool fade = true) {
        this->cards.clear();
        this->FreeCache();
        this->marked.clear();
        this->sel = 0;
        this->scroll_row = 0;
        this->single = false;
        // Every screen but Installed's game list (and Queue) wants the plain
        // 4-wide grid; resetting here means only those screens have to opt
        // back into SetCols/SetPoster/SetQueueTall each time they rebuild,
        // instead of every other card screen having to opt out.
        if (this->Cols != 4 || this->poster || this->queue_tall) {
            this->Cols = 4;
            this->poster = false;
            this->queue_tall = false;
            this->tiles_dirty = true;
        }
        // The fade re-renders the whole grid for ~8 frames, which stutters
        // under download load — so the call site (MainLayout::ClearMenu)
        // only passes fade=true when queue_io_active() says nothing is
        // actively moving bytes right now. `fade=false` (screens that
        // rebuild every tick, e.g. Queue) always skips it regardless.
        this->enter_alpha = fade ? 0 : 255;
        this->dirty = true;
        this->tch_active = false;
        this->tch_card = -1;
        this->tch_activate = false;
    }

    void AddCard(const std::string &title, const std::string &subtitle,
                 pu::sdl2::Texture icon, bool pinned = false,
                 bool dim = false, bool art = false) {
        this->cards.push_back(Card{title, subtitle, icon, pinned, dim, art});
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
                      const s32 qpos = 0, const bool refresh_text = true,
                      const bool logo_icon = false, const bool art = false) {
        if (i < 0 || i >= (s32)this->cards.size() ||
            i >= (s32)this->cache.size()) {
            return;
        }
        Card &cd = this->cards[i];
        Cell &ce = this->cache[i];
        cd.queue = true;
        cd.icon = icon;
        cd.logo_icon = logo_icon;
        // Real cover art (SteamGridDB), same flag poster mode's AddCard/
        // SetCardIcon use -- see Card::art. Reused here so the queue-card
        // icon draw below can fit its aspect ratio instead of stretching.
        cd.art = art;
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
            // Two-line pill: size (now/total while a transfer is active) on
            // its own line, speed · eta joined on the line below. Used to be
            // one "size · speed · eta" line, but that didn't fit once queue
            // cards went narrower to match the poster grid's 6-wide cards —
            // the extra row height freed up by going taller at the same time
            // is exactly what the second line uses.
            this->UpdText(ce.ch_tex, ce.chw, ce.chh, cd.chip, size, txt_font,
                          this->sub_clr, (u32)(cw - 48));
            std::string chip2 = speed;
            if (!eta.empty()) {
                chip2 += (chip2.empty() ? "" : " · ") + eta;
            }
            this->UpdText(ce.ch2_tex, ce.ch2w, ce.ch2h, cd.chip2, chip2,
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

    // Multi-select marks, mirroring TableList's ToggleMark/SetMark/Marked.
    void ToggleMark(s32 i) {
        if (this->marked.count(i)) this->marked.erase(i);
        else this->marked.insert(i);
    }
    void SetMark(s32 i, bool on) {
        if (on) this->marked.insert(i);
        else this->marked.erase(i);
    }
    bool IsMarked(s32 i) { return this->marked.count(i) > 0; }
    const std::set<s32> &Marked() { return this->marked; }
    int MarkedCount() { return (int)this->marked.size(); }
    void ClearMarks() { this->marked.clear(); }

    // Lazy box-art resolve for poster cards (BoxArtIconsPoll), mirroring
    // TableList::SetRowIcon: swap one card's icon in place, no cache rebuild
    // (the cached text textures don't depend on the icon). `art` marks it as
    // a real cover so it renders stretch-fill instead of centred-natural.
    void SetCardIcon(const s32 i, pu::sdl2::Texture icon,
                     const bool art = true) {
        if (i < 0 || i >= (s32)this->cards.size()) {
            return;
        }
        this->cards[i].icon = icon;
        this->cards[i].art = art;
    }
    // Lazy folder-count resolve (see HomeCountsPoll/InstRootCountsPoll),
    // mirroring SetCardIcon: patch one card's subtitle in place. Unlike this
    // ->dirty (which forces RebuildCache to re-rasterize every card's title
    // and subtitle), re-rendering just this cell's cached texture keeps the
    // whole point of deferring the count off the build path -- filling one
    // card back in shouldn't pay for every other card on screen too.
    void SetCardSubtitle(const s32 i, const std::string &subtitle) {
        if (i < 0 || i >= (s32)this->cards.size()) {
            return;
        }
        this->cards[i].subtitle = subtitle;
        if (i >= (s32)this->cache.size() || !this->cache[i].built) {
            // Not drawn yet (still off-screen, or the very first frame after
            // a rebuild hasn't reached it) -- BuildCell will rasterize this
            // subtitle correctly the first time it's actually shown, so
            // there's nothing to patch here.
            return;
        }
        Cell &c = this->cache[i];
        if (c.sub_tex) {
            pu::ui::render::DeleteTexture(c.sub_tex);
            c.sub_tex = nullptr;
        }
        c.sw = c.sh = 0;
        if (!subtitle.empty()) {
            const u32 max_tw = (u32)(this->CardW() - 30);
            c.sub_tex = pu::ui::render::RenderText(
                this->poster ? this->font_tiny : this->font_sub, subtitle,
                this->sub_clr, max_tw);
            c.sw = pu::ui::render::GetTextureWidth(c.sub_tex);
            c.sh = pu::ui::render::GetTextureHeight(c.sub_tex);
        }
    }
    // First card index on screen, and how many slots the visible rows span
    // (may run past the last real card) - the same "is this on screen yet"
    // window TableList's ScrollTop/RowsVisible give the list path.
    s32 FirstVisibleCard() const { return this->scroll_row * Cols; }
    s32 VisibleCardCount() const { return this->VisRows() * Cols; }

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
            // Ease-out: big jump first, tapering off, instead of a linear
            // ramp — reads as settling into place rather than a metronome.
            s32 step = (255 - this->enter_alpha) / 3;
            if (step < 6) step = 6;
            s32 e = this->enter_alpha + step;
            this->enter_alpha = e > 255 ? 255 : e;
        }
        this->anim_frame++;
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
                // A real console icon PNG has its own generous transparent
                // padding baked in (see Card::logo_icon), so it reads fine
                // drawn edge-to-edge in the full isz slot. The app-logo
                // texture (used for the self-update card) has none, so
                // drawing it at the same full size crowded the corner
                // labels/border above -- shrink just the drawn image, still
                // centred on the same glow-ring/layout box, to roughly match
                // how a padded icon actually looks in this slot.
                const s32 draw_sz = cd.logo_icon ? isz * 62 / 100 : isz;
                pu::ui::render::TextureRenderOptions o;
                o.width = draw_sz;
                o.height = draw_sz;
                drawer->RenderTexture(cd.icon, gcx - draw_sz / 2,
                                      gcy - draw_sz / 2, o);
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
                // Two-line pill: size on top, speed · eta below -- each line
                // centred independently since they're rarely the same width.
                const s32 padx = 14, pady = 5, gap = 14;
                s32 blockw = qc.chw;
                if (qc.ch2_tex && qc.ch2w > blockw) {
                    blockw = qc.ch2w;
                }
                s32 blockh = qc.chh + (qc.ch2_tex ? gap + qc.ch2h : 0);
                s32 by0 = cy + chip_y;
                drawer->RenderRoundedRectangleFill(
                    this->pill_clr, cx + (scw - blockw) / 2 - padx,
                    by0 - pady, blockw + 2 * padx, blockh + 2 * pady,
                    (qc.chh + 2 * pady) / 2);
                drawer->RenderTexture(qc.ch_tex, cx + (scw - qc.chw) / 2, by0);
                if (qc.ch2_tex) {
                    drawer->RenderTexture(qc.ch2_tex,
                                          cx + (scw - qc.ch2w) / 2,
                                          by0 + qc.chh + gap);
                }
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
        if (this->poster) {
            // One shared phase for every overflowing poster title this frame
            // (see MarqueeOffset) - only ticks in poster mode, nobody else
            // uses it.
            this->marquee_frame++;
        }
        const s32 cw = this->CardW();
        const s32 ch = this->CardH();
        const s32 rv = this->VisRows();
        for (s32 vr = 0; vr < rv; vr++) {
            s32 row = this->scroll_row + vr;
            for (s32 col = 0; col < Cols; col++) {
                s32 idx = row * Cols + col;
                if (idx >= (s32)this->cards.size()) {
                    break;
                }
                s32 cx = rx + Margin + col * (cw + Gap);
                s32 cy = ry + vr * (ch + Gap);
                bool selected = (idx == this->sel);
                const Card &cd = this->cards[idx];
                if (this->tile_card) {
                    drawer->RenderTexture(this->tile_card, cx, cy);
                } else {
                    drawer->RenderRectangleFill(this->card_bg, cx, cy, cw,
                                                ch);
                }
                if (selected && !(cd.queue && cd.hero)) {
                    // Focused card gets a soft, static drop shadow (no pulse
                    // — that's the active-transfer hero card's cue below) so
                    // the glow ring above reads as it physically lifting off
                    // the grid, not just changing color. Skipped when the
                    // hero pulse is already drawing its own shadow here.
                    drawer->RenderShadowSimple(cx, cy + ch, cw, 8, 70);
                }
                if (cd.queue && cd.hero) {
                    // Active download: accent-tinted "hero" card, matching
                    // the list view's accent row -- including its under-card
                    // shadow pulse, so the transferring card reads as alive
                    // in card view too, not just list view.
                    s32 ph = this->anim_frame % 90;
                    s32 tri = ph < 45 ? ph : (90 - ph);
                    drawer->RenderShadowSimple(cx, cy + ch, cw, 10,
                                               90 + tri * 70 / 45);
                    if (this->tile_hero) {
                        pu::ui::render::TextureRenderOptions o;
                        o.alpha_mod = 30;
                        drawer->RenderTexture(this->tile_hero, cx, cy, o);
                    } else {
                        auto hc = this->glow_clr;
                        hc.a = 30;
                        drawer->RenderRoundedRectangleFill(hc, cx, cy, cw,
                                                           ch, CardRadius);
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
                        drawer->RenderRoundedRectangleFill(f, cx, cy, cw, ch,
                                                           CardRadius);
                    }
                    for (s32 g = 1; g <= 4; g++) {
                        auto gc = this->glow_clr;
                        gc.a = (u8)((40 - g * 9) * this->sel_alpha / 255);
                        drawer->RenderRoundedRectangle(gc, cx - g, cy - g,
                                                       cw + 2 * g,
                                                       ch + 2 * g,
                                                       CardRadius + g);
                    }
                    auto edge = this->glow_clr;
                    edge.a = (u8)this->sel_alpha;
                    for (s32 t = 0; t < 2; t++) {
                        drawer->RenderRoundedRectangle(
                            edge, cx + t, cy + t, cw - 2 * t, ch - 2 * t,
                            CardRadius - t > 4 ? CardRadius - t : 4);
                    }
                }
                if (this->marked.count(idx)) {
                    // Multi-select tag (Installed's poster view, Y button): a
                    // green border, distinct from the blue focus outline
                    // above since that one tracks the cursor, not which
                    // cards are in the selection set — both can show at once.
                    pu::ui::Color mc(146, 214, 36, 255);
                    for (s32 t = 0; t < 3; t++) {
                        drawer->RenderRoundedRectangle(
                            mc, cx + t, cy + t, cw - 2 * t, ch - 2 * t,
                            CardRadius - t > 4 ? CardRadius - t : 4);
                    }
                }
                // Bevel: 1px gloss along the top, 1px shade along the bottom,
                // so cards read as raised tiles (matches the list rows).
                drawer->RenderRectangleFill(
                    pu::ui::Color(255, 255, 255, (u8)(selected ? 45 : 18)),
                    cx + CardRadius, cy, cw - 2 * CardRadius, 1);
                drawer->RenderRectangleFill(pu::ui::Color(0, 0, 0, 50),
                                            cx + CardRadius, cy + ch - 1,
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
                    // grow-upward and green glow bloom on selection. Tall
                    // queue cards push it well down from the top edge (was
                    // flush at 10px, which read as crowded once the row grew
                    // to poster height) -- the freed space above then lets
                    // the filename/chip block below sit with tighter, less
                    // scattered gaps instead of one big empty band before the
                    // chip and another after it.
                    const s32 icon_top = this->queue_tall ? 70 : 10;
                    if (cd.icon) {
                        s32 isz = IconPx;
                        if (selected) {
                            isz += (10 * this->sel_alpha) / 255;
                            s32 gcx = cx + cw / 2;
                            s32 gcy = cy + icon_top + IconPx / 2;
                            for (s32 g = 0; g < 4; g++) {
                                auto gc = this->glow_clr;
                                gc.a = (u8)((14 + 5 * g) * this->sel_alpha /
                                            255);
                                drawer->RenderCircleFill(gc, gcx, gcy,
                                                         IconPx / 2 + 2 -
                                                             6 * g);
                            }
                        }
                        // A real console icon PNG carries its own transparent
                        // padding, so it reads fine drawn edge-to-edge in this
                        // slot. The self-update card's app-logo texture (see
                        // Card::logo_icon) and real box art (Card::art --
                        // a SteamGridDB cover, set on a console via the box
                        // art picker) have neither: they bleed to the image
                        // edge, so at full size here -- just 10px from the
                        // card's rounded top edge -- they crowded/overran
                        // both the border and the corner label above it (the
                        // "art pushes above the top border" report). Shrink
                        // the drawn box the same way for both, and for real
                        // art fit its actual aspect ratio inside that box
                        // instead of stretching -- covers are commonly
                        // portrait (taller than wide), and a hardcoded square
                        // stretch squashed them.
                        s32 slot_top = cy + icon_top - (isz - IconPx);
                        // The app-logo texture (HaulNX self-update card) and
                        // real box art both lack baked-in padding, but the
                        // logo is a simple square mark that reads fine much
                        // larger than a photographic cover does -- 62% left
                        // it looking tiny once given a safe top margin, so
                        // it gets its own, bigger box. At full selection
                        // (isz maxed, slot_top flush with the card top) this
                        // still leaves ~10px of clearance above the icon,
                        // same margin as the unselected default.
                        s32 box_isz = cd.logo_icon  ? isz * 85 / 100
                                     : cd.art        ? isz * 62 / 100
                                                      : isz;
                        s32 draw_w = box_isz, draw_h = box_isz;
                        if (cd.art) {
                            s32 rw = pu::ui::render::GetTextureWidth(cd.icon);
                            s32 rh = pu::ui::render::GetTextureHeight(cd.icon);
                            if (rw > 0 && rh > 0) {
                                if (rw >= rh) {
                                    draw_h = box_isz * rh / rw;
                                } else {
                                    draw_w = box_isz * rw / rh;
                                }
                            }
                        }
                        pu::ui::render::TextureRenderOptions o;
                        o.width = draw_w;
                        o.height = draw_h;
                        // Centering the shrunk logo box in the same slot a
                        // full-size console icon occupies reads as too high
                        // -- the console icons fill that slot top-to-bottom
                        // so their weight sits differently. Nudge the logo
                        // down within its own box only; console icons
                        // (draw_h == isz here) are untouched.
                        s32 draw_y = slot_top + (isz - draw_h) / 2;
                        if (cd.logo_icon) draw_y += 12;
                        drawer->RenderTexture(
                            cd.icon, cx + (cw - draw_w) / 2, draw_y, o);
                    }
                    // Filename under the icon, up to two wrapped lines, sitting
                    // close under it -- tall cards' icon moved down (icon_top
                    // above) already carved out the row's extra height, so
                    // this gap stays tight rather than stacking more air here
                    // too.
                    if (qc.f_tex) {
                        s32 fy = this->queue_tall
                                     ? (qc.f2_tex ? cy + 232 : cy + 246)
                                     : (qc.f2_tex ? cy + 152 : cy + 166);
                        drawer->RenderTexture(qc.f_tex, cx + (cw - qc.fw) / 2,
                                              fy);
                        if (qc.f2_tex) {
                            drawer->RenderTexture(qc.f2_tex,
                                                  cx + (cw - qc.f2w) / 2,
                                                  fy + qc.fh + 2);
                        }
                    }
                    // Pill: size on top, speed · eta below it when present --
                    // two lines now that queue cards are narrower (6-wide,
                    // matching the poster grid); previously one "size · speed
                    // · eta" line, which no longer fit at this width. chip_y
                    // is unchanged from the original tall layout, but the
                    // filename above now sits lower (see icon_top), so the
                    // gap above this pill shrank along with it instead of
                    // needing a separate adjustment here.
                    const s32 chip_y = this->queue_tall ? cy + 300 : cy + 222;
                    if (qc.ch_tex) {
                        // Wider gap between the two pill lines than a normal
                        // text line-gap (4px elsewhere) so "size" and "speed ·
                        // eta" read as two distinct facts, not a wrapped
                        // paragraph -- also what pulls line 2's bottom margin
                        // in from the tall card's edge.
                        const s32 padx = 12, pady = 4, gap = 14;
                        s32 blockw = qc.chw;
                        if (qc.ch2_tex && qc.ch2w > blockw) {
                            blockw = qc.ch2w;
                        }
                        s32 blockh = qc.chh + (qc.ch2_tex ? gap + qc.ch2h : 0);
                        drawer->RenderRoundedRectangleFill(
                            this->pill_clr, cx + (cw - blockw) / 2 - padx,
                            chip_y - pady, blockw + 2 * padx,
                            blockh + 2 * pady, (qc.chh + 2 * pady) / 2);
                        drawer->RenderTexture(qc.ch_tex,
                                              cx + (cw - qc.chw) / 2, chip_y);
                        if (qc.ch2_tex) {
                            drawer->RenderTexture(
                                qc.ch2_tex, cx + (cw - qc.ch2w) / 2,
                                chip_y + qc.chh + gap);
                        }
                    }
                    // Queue-position badge, tucked into the bottom-left
                    // corner level with the chip's top line (waiting cards
                    // have no chip content, so it never collides with one).
                    if (qc.qp_tex) {
                        const s32 padx = 8, pady = 3;
                        drawer->RenderRoundedRectangleFill(
                            this->pill_clr, cx + 14, chip_y - pady,
                            qc.qpw + 2 * padx, qc.qph + 2 * pady,
                            (qc.qph + 2 * pady) / 2);
                        drawer->RenderTexture(qc.qp_tex, cx + 14 + padx,
                                              chip_y);
                    }
                    if (cd.prog >= 0.0f) {
                        this->DrawRing(drawer, cx, cy, cw, ch, CardRadius,
                                       4, 6, cd.prog, cd.ring, cd.hero,
                                       this->anim_frame);
                    }
                    continue;
                }
                Cell &ce = this->cache[idx];
                if (!ce.built) {
                    // First time this card is actually about to be drawn --
                    // see RebuildCache/BuildCell for why this isn't done for
                    // every card up front.
                    this->BuildCell(idx);
                }
                if (this->poster) {
                    // Poster card: box art (or a centred fallback icon) fills
                    // the top, title + size sit in the band below it. Real
                    // cover art already fills the whole image area, so the
                    // whole-card selection border/glow above is enough there;
                    // a plain fallback logo icon is much smaller than that
                    // area though, so it still gets its own glow bloom below
                    // (matching the plain icon+text card's selected icon).
                    //
                    // The image's width is derived from the *height* budget
                    // left after the fixed text band (not from the card's
                    // full width, unlike the root console cards) — CardH()
                    // is a fixed poster row height so two rows fit the
                    // viewport, and at 6-wide a full-width 2:3 image would
                    // blow well past that budget. Capping by iw_max still
                    // lets it use the full card width if the budget ever
                    // allows it (e.g. fewer columns).
                    const s32 img_h_budget = ch - 2 * PosterPad - PosterTextH;
                    const s32 iw_max = cw - 2 * PosterPad;
                    s32 iw, ih;
                    if (cd.art) {
                        // Real cover art doesn't all share one shape: game
                        // covers (SteamGridDB grids) are portrait 600x900,
                        // but console art can come from the icons-catalog
                        // fallback instead (see ba_icon_url in boxart.c),
                        // which is square-ish, not 2:3. Fit to the texture's
                        // *actual* aspect ratio -- stretching every cover to
                        // a hardcoded 2:3 box, as before, squashed those
                        // square icons into a tall rectangle.
                        s32 rw = pu::ui::render::GetTextureWidth(cd.icon);
                        s32 rh = pu::ui::render::GetTextureHeight(cd.icon);
                        if (rw > 0 && rh > 0) {
                            ih = img_h_budget;
                            iw = ih * rw / rh;
                            if (iw > iw_max) {
                                iw = iw_max;
                                ih = iw * rh / rw;
                            }
                        } else {
                            // No texture info (shouldn't happen) -- fall
                            // back to the old fixed 2:3 assumption.
                            iw = img_h_budget * 2 / 3;
                            if (iw > iw_max) {
                                iw = iw_max;
                            }
                            ih = iw * 3 / 2;
                        }
                    } else {
                        iw = img_h_budget * 2 / 3;
                        if (iw > iw_max) {
                            iw = iw_max;
                        }
                        ih = iw * 3 / 2;
                    }
                    if (iw < 1) {
                        iw = 1;
                    }
                    if (ih < 1) {
                        ih = 1;
                    }
                    const s32 ix = cx + (cw - iw) / 2;
                    const s32 iy = cy + PosterPad;
                    if (cd.icon) {
                        pu::ui::render::TextureRenderOptions o;
                        if (cd.art) {
                            // Fit to the aspect computed above -- never
                            // distorted, since iw/ih already match the
                            // texture's real ratio (or the 2:3 fallback).
                            o.width = iw;
                            o.height = ih;
                            drawer->RenderTexture(cd.icon, ix, iy, o);
                        } else {
                            // No cover: centre the fallback icon at a
                            // natural square size instead of squashing a
                            // logo-shaped image into a tall rectangle.
                            s32 isz = (iw < ih ? iw : ih) - 16;
                            if (isz < 1) {
                                isz = 1;
                            }
                            const s32 icx = ix + iw / 2;
                            const s32 icy = iy + ih / 2;
                            // Selected: same soft green glow bloom the plain
                            // icon+text card gives its icon -- lost when
                            // these console/settings cards moved to poster
                            // geometry (the "no separate icon-bloom" call
                            // above was about real cover art, which already
                            // fills the whole image area; a plain logo icon
                            // here still needs its own selected cue).
                            if (selected) {
                                for (s32 g = 0; g < 4; g++) {
                                    auto gc = this->glow_clr;
                                    gc.a = (u8)((14 + 5 * g) *
                                               this->sel_alpha / 255);
                                    drawer->RenderCircleFill(
                                        gc, icx, icy,
                                        isz / 2 + 2 - 6 * g);
                                }
                            }
                            o.width = isz;
                            o.height = isz;
                            // Disabled entries (e.g. an off repo) fade their
                            // icon the same way the plain icon+text branch
                            // does below -- this fallback path is what those
                            // cards actually render through once they're at
                            // poster geometry.
                            if (cd.dim) {
                                o.alpha_mod = 110;
                            }
                            drawer->RenderTexture(cd.icon, icx - isz / 2,
                                                  icy - isz / 2, o);
                        }
                    }
                    // Title sits flush under the image -- keeps it riding
                    // high in the card instead of drifting toward the
                    // count/size pill -- which gets a deliberately generous
                    // gap of its own below (PosterTextH grew to make room
                    // for both).
                    s32 ty = iy + ih;
                    const s32 band_x = cx + PosterPad;
                    const s32 band_w = cw - 2 * PosterPad;
                    if (ce.t1_tex) {
                        const s32 overflow = ce.t1w - band_w;
                        if (overflow > 0) {
                            // Too long for the card even at the smaller
                            // poster font: clip to the text band and slide
                            // it back and forth (MarqueeOffset) so the whole
                            // name is reachable instead of staying cut off.
                            const s32 off = this->MarqueeOffset(overflow);
                            SDL_Rect clip{band_x, ty, band_w, ce.t1h};
                            SDL_RenderSetClipRect(
                                pu::ui::render::GetMainRenderer(), &clip);
                            drawer->RenderTexture(ce.t1_tex, band_x + off, ty);
                            SDL_RenderSetClipRect(
                                pu::ui::render::GetMainRenderer(), nullptr);
                        } else {
                            drawer->RenderTexture(
                                ce.t1_tex, cx + (cw - ce.t1w) / 2, ty);
                        }
                        ty += ce.t1h + 20;
                    }
                    if (ce.sub_tex) {
                        s32 sx = cx + (cw - ce.sw) / 2;
                        s32 padx = 10, pady = 4;
                        drawer->RenderRoundedRectangleFill(
                            this->pill_clr, sx - padx, ty - pady,
                            ce.sw + 2 * padx, ce.sh + 2 * pady,
                            (ce.sh + 2 * pady) / 2);
                        drawer->RenderTexture(ce.sub_tex, sx, ty);
                    }
                    continue;
                }
                // Cards with no info line (the settings sections) would leave
                // the icon+title hugging the top, with the empty subtitle band
                // as dead space below. Centre the icon+title block vertically
                // in that case; cards that carry a subtitle keep the fixed
                // 3-band layout (Browse/Installed counts, the Updates chip).
                s32 voff = 0;
                if (!ce.sub_tex) {
                    s32 block_bot = ce.t2_tex ? 178 + ce.t2h : 158 + ce.t1h;
                    voff = (ch - (block_bot - 10)) / 2 - 10;
                    if (voff < 0) {
                        voff = 0;
                    }
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
                        s32 gcy = cy + 10 + IconPx / 2 + voff;
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
                                          cy + 10 - (isz - IconPx) + voff, o);
                }
                // One-line titles centre in the two-line band; the small info
                // line sits at a fixed baseline below.
                if (ce.t1_tex) {
                    s32 ty = (ce.t2_tex ? cy + 144 : cy + 158) + voff;
                    drawer->RenderTexture(ce.t1_tex, cx + (cw - ce.t1w) / 2,
                                          ty);
                }
                if (ce.t2_tex) {
                    drawer->RenderTexture(ce.t2_tex, cx + (cw - ce.t2w) / 2,
                                          cy + 178 + voff);
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
                    const s32 ch = this->CardH();
                    this->tch_acc += this->tch_last_y - tch.y;
                    while (this->tch_acc >= ch + Gap) {
                        if (this->scroll_row < this->MaxScroll()) {
                            this->scroll_row++;
                        }
                        this->tch_acc -= ch + Gap;
                    }
                    while (this->tch_acc <= -(ch + Gap)) {
                        if (this->scroll_row > 0) {
                            this->scroll_row--;
                        }
                        this->tch_acc += ch + Gap;
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
