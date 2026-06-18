#include "card_view.h"
#include "field_draw.h"
#include <string.h>
#include <stdlib.h>

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
        if (t >= 0 && t < H)     pixels[t * pitch_px + x] = color;
        if (b-1 >= 0 && b-1 < H) pixels[(b-1) * pitch_px + x] = color;
    }
    for (int y = t; y < b; y++) {
        if (y < 0 || y >= H) continue;
        if (l >= 0 && l < W)     pixels[y * pitch_px + l] = color;
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

    int ww, wh;
    SDL_GetWindowSize(r->window, &ww, &wh);
    int zoom = (r->width > 0) ? ww / r->width : 1;
    if (zoom < 1) zoom = 1;
    int menu_bar_h = MENU_BAR_H(zoom);
    SDL_Rect dst = { 0, menu_bar_h, ww, wh - menu_bar_h };
    SDL_RenderClear(r->renderer);
    SDL_RenderCopy(r->renderer, r->tex, NULL, &dst);

    field_draw_text(r, card, bg);
    /* caller renders Nuklear then calls SDL_RenderPresent */
}
