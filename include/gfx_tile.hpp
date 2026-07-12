#pragma once

// One-time "bake" helpers: render a rounded-rect fill or a vertical gradient
// into a GPU texture so the per-frame draw becomes a single blit instead of
// SDL2_gfx's software scanline fill. RenderRoundedRectangleFill (roundedBoxRGBA)
// costs roughly one hline per row of height *every frame*; for a screen of
// rows/cards that was the scroll/queue lag. Baking pays that cost once.
//
// Baking uses a render target; the previous target is saved and restored, so
// these are safe to call between frames or lazily on the first render. The tile
// is baked opaque (corners transparent) and tinted per-draw via alpha_mod, so
// one tile serves the selection fade and hero tint too.

#include <pu/Plutonium>

static inline pu::sdl2::Texture BakeRoundTile(const s32 w, const s32 h,
                                              const s32 radius,
                                              const pu::ui::Color c) {
    if (w <= 0 || h <= 0) {
        return nullptr;
    }
    auto rd = pu::ui::render::GetMainRenderer();
    if (!rd) {
        return nullptr;
    }
    SDL_Texture *t = SDL_CreateTexture(rd, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_Texture *prev = SDL_GetRenderTarget(rd);
    SDL_SetRenderTarget(rd, t);
    SDL_SetRenderDrawColor(rd, 0, 0, 0, 0);
    SDL_RenderClear(rd);
    roundedBoxRGBA(rd, 0, 0, w - 1, h - 1, radius, c.r, c.g, c.b, 255);
    SDL_SetRenderTarget(rd, prev);
    return t;
}

// Vertical two-stop gradient strip (1px wide, stretched at blit time). Used for
// scrollbar thumbs; the loop runs once at bake, not per frame.
static inline pu::sdl2::Texture BakeVGradient(const s32 h,
                                              const pu::ui::Color top,
                                              const pu::ui::Color bot) {
    if (h <= 0) {
        return nullptr;
    }
    auto rd = pu::ui::render::GetMainRenderer();
    if (!rd) {
        return nullptr;
    }
    SDL_Texture *t = SDL_CreateTexture(rd, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET, 1, h);
    if (!t) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_Texture *prev = SDL_GetRenderTarget(rd);
    SDL_SetRenderTarget(rd, t);
    for (s32 i = 0; i < h; i++) {
        const float f = h > 1 ? (float)i / (float)(h - 1) : 0.0f;
        const u8 r = (u8)((s32)top.r + ((s32)bot.r - (s32)top.r) * f);
        const u8 g = (u8)((s32)top.g + ((s32)bot.g - (s32)top.g) * f);
        const u8 b = (u8)((s32)top.b + ((s32)bot.b - (s32)top.b) * f);
        SDL_SetRenderDrawColor(rd, r, g, b, 255);
        SDL_RenderDrawPoint(rd, 0, i);
    }
    SDL_SetRenderTarget(rd, prev);
    return t;
}
