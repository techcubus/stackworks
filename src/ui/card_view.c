#include "card_view.h"
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

/* Render field text directly onto the SDL renderer at 2× card coordinates.
 * Called after SDL_RenderCopy of the card bitmap texture. */
static void draw_field_text(Renderer *r, const Card *card, const Background *bg) {
    if (!bg) return;

    int ww, wh;
    SDL_GetWindowSize(r->window, &ww, &wh);
    /* scale factors: card pixels → screen pixels */
    int sx = ww / r->width;
    int sy = (wh - MENU_BAR_H) / r->height;
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;

    SDL_Color black = { 0, 0, 0, 255 };

    for (uint16_t i = 0; i < bg->part_count; i++) {
        const Part *p = &bg->parts[i];
        if (p->type != PART_FIELD || !p->visible) continue;

        const char *text = card_field_text(card, p->id);
        if (!text || !*text) continue;

        /* choose font: use field's point size scaled to screen; fall back to 12pt */
        int pt_size = (p->text_size > 0 ? (int)p->text_size : 12) * sx;
        TTF_Font *font = renderer_get_font(r, pt_size);
        if (!font) continue;

        /* field bounds in screen coordinates */
        int fx = p->rect.left  * sx;
        int fy = p->rect.top   * sy + MENU_BAR_H;
        int fw = (p->rect.right  - p->rect.left) * sx;
        int fh = (p->rect.bottom - p->rect.top)  * sy;
        if (fw <= 0 || fh <= 0) continue;

        /* clear any clip left by the previous field before drawing background */
        SDL_RenderSetClipRect(r->renderer, NULL);

        /* draw field background according to style */
        SDL_Rect field_rect = { fx, fy, fw, fh };
        if (p->style == FLD_STYLE_SHADOW) {
            int sh = 3 * sx;
            SDL_Rect shad = { fx + sh, fy + sh, fw, fh };
            SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(r->renderer, &shad);
        }
        SDL_SetRenderDrawColor(r->renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(r->renderer, &field_rect);
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(r->renderer, &field_rect);

        /* inset the clip by 2px for text padding */
        SDL_Rect clip = { fx + 2, fy + 2, fw - 4, fh - 4 };
        SDL_RenderSetClipRect(r->renderer, &clip);
        int tx = fx + 2;
        int ty_start = fy + 2;

        /* convert \r line separators to \n for SDL_ttf */
        char *copy = strdup(text);
        if (!copy) continue;
        for (char *q = copy; *q; q++)
            if ((unsigned char)*q == 0x0D) *q = '\n';

        if (*copy) {
            SDL_Surface *surf = TTF_RenderText_Blended_Wrapped(
                font, copy, black, (Uint32)(fw - 4));
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(r->renderer, surf);
                if (tex) {
                    SDL_Rect dst = { tx, ty_start, surf->w, surf->h };
                    SDL_RenderCopy(r->renderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
        }
        free(copy);
    }

    SDL_RenderSetClipRect(r->renderer, NULL);
    SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
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
    SDL_Rect dst = { 0, MENU_BAR_H, ww, wh - MENU_BAR_H };
    SDL_RenderClear(r->renderer);
    SDL_RenderCopy(r->renderer, r->tex, NULL, &dst);

    draw_field_text(r, card, bg);
    /* caller renders Nuklear then calls SDL_RenderPresent */
}
