#include "field_draw.h"
#include "../stack.h"
#include "render.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

/* Render one field's background box and text onto the SDL renderer. */
static void draw_one_field(Renderer *r, const Part *p, const char *text,
                            int sx, int sy) {
    int pt_size = (p->text_size > 0 ? (int)p->text_size : 12) * sx;
    TTF_Font *font = renderer_get_font(r, pt_size);
    if (!font) return;

    /* Apply Mac text style flags to the SDL_ttf font */
    int ttf_style = TTF_STYLE_NORMAL;
    if (p->text_style & 0x01) ttf_style |= TTF_STYLE_BOLD;
    if (p->text_style & 0x02) ttf_style |= TTF_STYLE_ITALIC;
    if (p->text_style & 0x04) ttf_style |= TTF_STYLE_UNDERLINE;
    TTF_SetFontStyle(font, ttf_style);

    int fx = p->rect.left  * sx;
    int fy = p->rect.top   * sy + MENU_BAR_H(sx);
    int fw = (p->rect.right  - p->rect.left) * sx;
    int fh = (p->rect.bottom - p->rect.top)  * sy;
    if (fw <= 0 || fh <= 0) return;

    SDL_RenderSetClipRect(r->renderer, NULL);
    SDL_Rect field_rect = { fx, fy, fw, fh };

    /* shadow: filled black rect offset 2 card-pixels down-right */
    if (p->style == FLD_STYLE_SHADOW) {
        int sh = 2 * sx;
        SDL_Rect shad = { fx + sh, fy + sh, fw, fh };
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(r->renderer, &shad);
    }

    /* white fill for all non-transparent styles */
    if (p->style != FLD_STYLE_TRANSPARENT) {
        SDL_SetRenderDrawColor(r->renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(r->renderer, &field_rect);
    }

    /* border for rectangle, shadow, and scrolling; opaque has no border */
    if (p->style == FLD_STYLE_RECTANGLE ||
        p->style == FLD_STYLE_SHADOW    ||
        p->style == FLD_STYLE_SCROLLING) {
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(r->renderer, &field_rect);
    }

    /* scroll bar: ~15 card-pixel wide column on the right */
    int scroll_w = 0;
    if (p->style == FLD_STYLE_SCROLLING) {
        scroll_w = 15 * sx;
        int sbx = fx + fw - scroll_w;
        SDL_Rect sb = { sbx, fy, scroll_w, fh };
        SDL_SetRenderDrawColor(r->renderer, 192, 192, 192, 255);
        SDL_RenderFillRect(r->renderer, &sb);
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
        SDL_RenderDrawLine(r->renderer, sbx, fy, sbx, fy + fh - 1);
    }

    int text_w = fw - scroll_w - 4;
    if (text_w < 1) text_w = 1;
    SDL_Rect clip = { fx + 2, fy + 2, text_w, fh - 4 };
    SDL_RenderSetClipRect(r->renderer, &clip);

    char *copy = strdup(text);
    if (!copy) return;
    for (char *q = copy; *q; q++)
        if ((unsigned char)*q == 0x0D) *q = '\n';

    if (*copy) {
        /* Outline style over a transparent field renders on the black bitmap;
         * Mac shows outline text as white-on-black, so use white here. */
        SDL_Color text_color = { 0, 0, 0, 255 };
        if ((p->text_style & 0x08) && p->style == FLD_STYLE_TRANSPARENT)
            text_color = (SDL_Color){ 255, 255, 255, 255 };

        SDL_Surface *surf = TTF_RenderText_Blended_Wrapped(
            font, copy, text_color, (Uint32)text_w);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(r->renderer, surf);
            if (tex) {
                SDL_Rect dst = { fx + 2, fy + 2, surf->w, surf->h };
                SDL_RenderCopy(r->renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }
    free(copy);
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
}

void field_draw_text(Renderer *r, const Card *card, const Background *bg) {
    int ww, wh;
    SDL_GetWindowSize(r->window, &ww, &wh);
    int sx = (r->width  > 0) ? ww / r->width  : 1;
    if (sx < 1) sx = 1;
    int menu_bar_h = MENU_BAR_H(sx);
    int sy = (r->height > 0) ? (wh - menu_bar_h) / r->height : 1;
    if (sy < 1) sy = 1;

    /* background-layer fields: card content overrides bg content per part */
    if (bg) {
        for (uint16_t i = 0; i < bg->part_count; i++) {
            const Part *p = &bg->parts[i];
            if (p->type != PART_FIELD || !p->visible) continue;
            const char *text = card_field_text(card, p->id);
            if (!text || !*text) text = bkgd_field_text(bg, p->id);
            if (!text || !*text) continue;
            draw_one_field(r, p, text, sx, sy);
        }
    }

    /* card-layer fields */
    for (uint16_t i = 0; i < card->part_count; i++) {
        const Part *p = &card->parts[i];
        if (p->type != PART_FIELD || !p->visible) continue;
        const char *text = card_field_text(card, p->id);
        if (!text || !*text) continue;
        draw_one_field(r, p, text, sx, sy);
    }

    SDL_RenderSetClipRect(r->renderer, NULL);
    SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
}
