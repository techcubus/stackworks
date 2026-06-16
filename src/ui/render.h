#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdint.h>

struct nk_context;

#define MENU_BAR_H 50
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
} Renderer;

Renderer *renderer_create(uint16_t width, uint16_t height);
void      renderer_destroy(Renderer *r);
TTF_Font *renderer_get_font(Renderer *r, int pt_size);

#endif /* UI_RENDER_H */
