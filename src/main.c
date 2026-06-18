#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "nuklear.h"
#include "nuklear_sdl_renderer.h"
#include "stack.h"
#include "ui/render.h"
#include "ui/card_view.h"
#include "ui/menu.h"

static void update_title(Renderer *r, const Stack *s, uint32_t idx,
                         const char *filename) {
    char buf[256];
    snprintf(buf, sizeof buf, "StackWorks II Pro - %s, card %u of %u (%s)",
             s->name, idx + 1, s->card_count, filename);
    SDL_SetWindowTitle(r->window, buf);
}

int main(int argc, char *argv[]) {
    int dump = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'd') dump = 1;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-d] <stack.hc>\n", argv[0]);
        return 1;
    }

    Stack *s = stack_load(path);
    if (!s) return 1;

    /* basename only for the window title */
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    printf("loaded: %u cards, %u backgrounds, %ux%u\n",
           s->card_count, s->bkgd_count, s->card_width, s->card_height);

    if (dump) {
        stack_dump(s, stdout);
        stack_free(s);
        return 0;
    }

    if (s->card_count == 0) {
        fprintf(stderr, "no cards found\n");
        stack_free(s); return 1;
    }

    Renderer *r = renderer_create(s->card_width, s->card_height);
    if (!r) { stack_free(s); return 1; }

    MenuBar menubar;
    if (menubar_load(&menubar, "menus.json") != 0)
        fprintf(stderr, "warning: menus.json not found, menu bar will be empty\n");

    uint32_t cur = 0;
    update_title(r, s, cur, filename);

    int running = 1;
    while (running) {
        SDL_Event ev;

        nk_input_begin(r->nk);
        while (SDL_PollEvent(&ev)) {
            nk_sdl_handle_event(&ev);
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_q:
                    running = 0; break;
                case SDLK_RIGHT: case SDLK_SPACE: case SDLK_RETURN:
                    if (cur + 1 < s->card_count) cur++;
                    break;
                case SDLK_LEFT: case SDLK_BACKSPACE:
                    if (cur > 0) cur--;
                    break;
                case SDLK_HOME:
                    cur = 0; break;
                case SDLK_END:
                    cur = s->card_count - 1; break;
                default: break;
                }
                update_title(r, s, cur, filename);
                break;
            default: break;
            }
        }
        nk_input_end(r->nk);

        int ww, wh;
        SDL_GetWindowSize(r->window, &ww, &wh);
        int zoom = (s->card_width > 0) ? ww / s->card_width : 1;
        if (zoom < 1) zoom = 1;
        if (zoom > 5) zoom = 5;
        int menu_bar_h = MENU_BAR_H(zoom);

        /* Switch to the pre-baked Nuklear font that matches the current zoom. */
        if (r->nk_fonts[zoom - 1])
            nk_style_set_font(r->nk, &r->nk_fonts[zoom - 1]->handle);

        switch (menu_draw(&menubar, r->nk, ww, menu_bar_h)) {
        case MENU_ACTION_QUIT:    running = 0; break;
        case MENU_ACTION_GO_PREV:
            if (cur > 0) { cur--; update_title(r, s, cur, filename); } break;
        case MENU_ACTION_GO_NEXT:
            if (cur + 1 < s->card_count) { cur++; update_title(r, s, cur, filename); } break;
        case MENU_ACTION_GO_HOME:
            cur = 0; update_title(r, s, cur, filename); break;
#define SET_ZOOM(n) SDL_SetWindowSize(r->window, \
            s->card_width * (n), s->card_height * (n) + MENU_BAR_H(n))
        case MENU_ACTION_ZOOM_1X: SET_ZOOM(1); break;
        case MENU_ACTION_ZOOM_2X: SET_ZOOM(2); break;
        case MENU_ACTION_ZOOM_3X: SET_ZOOM(3); break;
        case MENU_ACTION_ZOOM_4X: SET_ZOOM(4); break;
        case MENU_ACTION_ZOOM_5X: SET_ZOOM(5); break;
#undef SET_ZOOM
        default: break;
        }

        renderer_draw_card(r, s, cur);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(r->renderer);
        SDL_Delay(16);
    }

    renderer_destroy(r);
    stack_free(s);
    return 0;
}
