#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <SDL2/SDL.h>
#include <stdint.h>

struct nk_context;

#define MENU_BAR_H 25

typedef struct {
    SDL_Window        *window;
    SDL_Renderer      *renderer;
    SDL_Texture       *tex;
    uint16_t           width;
    uint16_t           height;
    struct nk_context *nk;
} Renderer;

Renderer *renderer_create(uint16_t width, uint16_t height);
void      renderer_destroy(Renderer *r);

#endif /* UI_RENDER_H */
