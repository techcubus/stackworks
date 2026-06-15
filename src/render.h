#ifndef RENDER_H
#define RENDER_H

#include <SDL2/SDL.h>
#include "stack.h"

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *tex;
    uint16_t      width;
    uint16_t      height;
} Renderer;

Renderer *renderer_create(uint16_t width, uint16_t height);
void      renderer_destroy(Renderer *r);
void      renderer_draw_card(Renderer *r, const Stack *s, uint32_t card_idx);

#endif /* RENDER_H */
