#include "render.h"
#include <stdlib.h>
#include <string.h>
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
        width * 2, height * 2,
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
    return r;
}

void renderer_destroy(Renderer *r) {
    if (!r) return;
    SDL_DestroyTexture(r->tex);
    SDL_DestroyRenderer(r->renderer);
    SDL_DestroyWindow(r->window);
    SDL_Quit();
    free(r);
}

/* Blit a 1-bit bitmap into an ARGB8888 pixel buffer.
 * xor=0: black pixels write 0xFF000000; white pixels leave buffer unchanged.
 * xor=1: black pixels invert the destination (card layer over background). */
static void blit_1bit(uint32_t *pixels, int pitch_px,
                       const uint8_t *bitmap, uint16_t w, uint16_t h, int xor) {
    if (!bitmap) return;
    uint16_t row_bytes = (w + 7) / 8;
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            int bit = (bitmap[y * row_bytes + x/8] >> (7 - (x & 7))) & 1;
            if (!bit) continue;
            uint32_t *px = pixels + y * pitch_px + x;
            *px = xor ? (*px ^ 0x00FFFFFF) : 0xFF000000;
        }
    }
}

/* Draw a 1-pixel-wide rectangle outline into the pixel buffer. */
static void draw_rect_outline(uint32_t *pixels, int pitch_px,
                               int t, int l, int b, int r,
                               uint16_t W, uint16_t H, uint32_t color) {
    for (int x = l; x < r; x++) {
        if (x < 0 || x >= W) continue;
        if (t >= 0 && t < H)   pixels[t * pitch_px + x] = color;
        if (b-1 >= 0 && b-1 < H) pixels[(b-1) * pitch_px + x] = color;
    }
    for (int y = t; y < b; y++) {
        if (y < 0 || y >= H) continue;
        if (l >= 0 && l < W)   pixels[y * pitch_px + l] = color;
        if (r-1 >= 0 && r-1 < W) pixels[y * pitch_px + r-1] = color;
    }
}

static void draw_parts(uint32_t *pixels, int pitch_px,
                       const Part *parts, uint16_t count,
                       uint16_t W, uint16_t H) {
    for (uint16_t i = 0; i < count; i++) {
        const Part *p = &parts[i];
        if (!p->visible) continue;
        /* blue for buttons, green for fields */
        uint32_t color = (p->type == PART_BUTTON) ? 0xFF0000CC : 0xFF006600;
        draw_rect_outline(pixels, pitch_px,
                          p->rect.top, p->rect.left,
                          p->rect.bottom, p->rect.right,
                          W, H, color);
    }
}

void renderer_draw_card(Renderer *r, const Stack *s, uint32_t card_idx) {
    if (card_idx >= s->card_count) return;
    const Card *card = &s->cards[card_idx];

    void *raw; int pitch;
    if (SDL_LockTexture(r->tex, NULL, &raw, &pitch) < 0) return;
    uint32_t *pixels = raw;
    int pitch_px = pitch / 4;

    /* white background */
    for (int y = 0; y < r->height; y++)
        for (int x = 0; x < r->width; x++)
            pixels[y * pitch_px + x] = 0xFFFFFFFF;

    Background *bg = stack_find_bkgd(s, card->bkgd_id);
    if (bg) {
        blit_1bit(pixels, pitch_px, bg->bitmap, r->width, r->height, 0);
        draw_parts(pixels, pitch_px, bg->parts, bg->part_count, r->width, r->height);
    }

    /* card layer XOR'd on top */
    blit_1bit(pixels, pitch_px, card->bitmap, r->width, r->height, 1);
    draw_parts(pixels, pitch_px, card->parts, card->part_count, r->width, r->height);

    SDL_UnlockTexture(r->tex);

    /* scale to window if it was resized */
    int ww, wh;
    SDL_GetWindowSize(r->window, &ww, &wh);
    SDL_Rect dst = { 0, 0, ww, wh };
    SDL_RenderClear(r->renderer);
    SDL_RenderCopy(r->renderer, r->tex, NULL, &dst);
    SDL_RenderPresent(r->renderer);
}
