#include <stdio.h>
#include <SDL2/SDL.h>
#include "stack.h"
#include "render.h"

static void update_title(Renderer *r, const Stack *s, uint32_t idx) {
    char buf[64];
    snprintf(buf, sizeof buf, "cardviewer — card %u/%u", idx + 1, s->card_count);
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

    uint32_t cur = 0;
    update_title(r, s, cur);
    renderer_draw_card(r, s, cur);

    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            goto done;
        case SDL_KEYDOWN:
            switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE: case SDLK_q:
                goto done;
            case SDLK_RIGHT: case SDLK_SPACE: case SDLK_RETURN:
                if (cur + 1 < s->card_count) cur++;
                break;
            case SDLK_LEFT: case SDLK_BACKSPACE:
                if (cur > 0) cur--;
                break;
            case SDLK_HOME:
                cur = 0;
                break;
            case SDLK_END:
                cur = s->card_count - 1;
                break;
            default:
                continue;
            }
            update_title(r, s, cur);
            renderer_draw_card(r, s, cur);
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
                ev.window.event == SDL_WINDOWEVENT_RESIZED)
                renderer_draw_card(r, s, cur);
            break;
        }
    }

done:
    renderer_destroy(r);
    stack_free(s);
    return 0;
}
