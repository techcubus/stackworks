#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdint.h>

struct nk_context;

/* Menu bar height for a given integer zoom factor.
 * If zoom*20 > 40 (i.e. zoom >= 3) use zoom*10; otherwise floor at 40px. */
#define MENU_BAR_H(zoom) ((zoom) * 10 > 40 ? (zoom) * 10 : 40)
#define RENDERER_FONT_CACHE_MAX 16

typedef struct {
    int       pt_size;
    TTF_Font *font;
} FontCacheEntry;

typedef struct {
    SDL_Window        *window;
    SDL_Renderer      *renderer;
    SDL_Texture       *tex;
    uint16_t           width;
    uint16_t           height;
    struct nk_context *nk;
    char               font_path[256];
    FontCacheEntry     font_cache[RENDERER_FONT_CACHE_MAX];
    int                font_cache_count;
    /* Pre-baked Nuklear fonts for zoom levels 1×–5×; index = zoom - 1. */
    struct nk_font    *nk_fonts[5];
    SDL_Texture       *banana_tex;  /* 28×28 ARGB banana icon for the menu bar */
} Renderer;

Renderer *renderer_create(uint16_t width, uint16_t height);
void      renderer_destroy(Renderer *r);
TTF_Font *renderer_get_font(Renderer *r, int pt_size);

#endif /* UI_RENDER_H */
