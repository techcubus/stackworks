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
    r->window = SDL_CreateWindow("cardviewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width * 2, height * 2 + MENU_BAR_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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

    r->nk = nk_sdl_init(r->window, r->renderer);
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    nk_sdl_font_stash_end();

    return r;
}

void renderer_destroy(Renderer *r) {
    if (!r) return;
    nk_sdl_shutdown();
    SDL_DestroyTexture(r->tex);
    SDL_DestroyRenderer(r->renderer);
    SDL_DestroyWindow(r->window);
    SDL_Quit();
    free(r);
}
