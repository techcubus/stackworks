#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear_sdl_renderer.h"

#include "render.h"
#include <stdlib.h>
#include <stdio.h>

Renderer *renderer_create(uint16_t width, uint16_t height) {
    Renderer *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->width = width;
    r->height = height;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        free(r); return NULL;
    }
    r->window = SDL_CreateWindow("StackWorks II Pro",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width * 2, height * 2 + MENU_BAR_H,
        SDL_WINDOW_SHOWN);
    if (!r->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit(); free(r); return NULL;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    r->renderer = SDL_CreateRenderer(r->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(r->window); SDL_Quit(); free(r); return NULL;
    }
    r->tex = SDL_CreateTexture(r->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!r->tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(r->renderer);
        SDL_DestroyWindow(r->window); SDL_Quit(); free(r); return NULL;
    }

    /* Find a system font before entering the Nuklear atlas stash so we can
     * load it into the atlas at the desired size. */
    static const char *font_paths[] = {
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        NULL
    };
    for (int i = 0; font_paths[i] && !r->font_path[0]; i++) {
        FILE *fp = fopen(font_paths[i], "rb");
        if (fp) { fclose(fp); snprintf(r->font_path, sizeof r->font_path, "%s", font_paths[i]); }
    }

    r->nk = nk_sdl_init(r->window, r->renderer);
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    struct nk_font *nk_font = NULL;
    if (r->font_path[0])
        nk_font = nk_font_atlas_add_from_file(atlas, r->font_path, MENU_BAR_H * 2 / 3, 0);
    nk_sdl_font_stash_end();
    /* Use the loaded TTF at 52px for all Nuklear widgets (menu bar). */
    if (nk_font)
        nk_style_set_font(r->nk, &nk_font->handle);

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
    } else if (!r->font_path[0]) {
        fprintf(stderr, "stackworks: no font found, field text will not render\n");
    }

    return r;
}

TTF_Font *renderer_get_font(Renderer *r, int pt_size) {
    if (!r->font_path[0] || pt_size <= 0) return NULL;
    for (int i = 0; i < r->font_cache_count; i++)
        if (r->font_cache[i].pt_size == pt_size)
            return r->font_cache[i].font;
    if (r->font_cache_count >= RENDERER_FONT_CACHE_MAX) return NULL;
    TTF_Font *f = TTF_OpenFont(r->font_path, pt_size);
    if (!f) return NULL;
    r->font_cache[r->font_cache_count++] = (FontCacheEntry){ pt_size, f };
    return f;
}

void renderer_destroy(Renderer *r) {
    if (!r) return;
    nk_sdl_shutdown();
    for (int i = 0; i < r->font_cache_count; i++)
        TTF_CloseFont(r->font_cache[i].font);
    TTF_Quit();
    SDL_DestroyTexture(r->tex);
    SDL_DestroyRenderer(r->renderer);
    SDL_DestroyWindow(r->window);
    SDL_Quit();
    free(r);
}
