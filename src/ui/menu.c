#define JSMN_STATIC
#include "../jsmn.h"

#include "menu.h"
#include "render.h"
#include "nuklear.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- jsmn helpers ---- */

static int jsoneq(const char *json, const jsmntok_t *t, const char *s) {
    return t->type == JSMN_STRING &&
           (int)strlen(s) == t->end - t->start &&
           strncmp(json + t->start, s, (size_t)(t->end - t->start)) == 0;
}

static void tok_str(const char *json, const jsmntok_t *t,
                    char *out, size_t out_size) {
    size_t len = (size_t)(t->end - t->start);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, json + t->start, len);
    out[len] = '\0';
}

/* Returns index of the token after the subtree rooted at t[i].
 * Works because OBJECT.size = key count, each key STRING.size = 1 (its value),
 * ARRAY.size = element count, STRING/PRIMITIVE value .size = 0. */
static int jsmn_skip(const jsmntok_t *t, int i) {
    int j = i + 1;
    for (int k = 0; k < t[i].size; k++)
        j = jsmn_skip(t, j);
    return j;
}

/* ---- action mapping ---- */

static MenuAction parse_action(const char *s) {
    if (strcmp(s, "quit")    == 0) return MENU_ACTION_QUIT;
    if (strcmp(s, "about")   == 0) return MENU_ACTION_ABOUT;
    if (strcmp(s, "open")    == 0) return MENU_ACTION_OPEN;
    if (strcmp(s, "go_prev") == 0) return MENU_ACTION_GO_PREV;
    if (strcmp(s, "go_next") == 0) return MENU_ACTION_GO_NEXT;
    if (strcmp(s, "go_home") == 0) return MENU_ACTION_GO_HOME;
    if (strcmp(s, "zoom_1x") == 0) return MENU_ACTION_ZOOM_1X;
    if (strcmp(s, "zoom_2x") == 0) return MENU_ACTION_ZOOM_2X;
    if (strcmp(s, "zoom_3x") == 0) return MENU_ACTION_ZOOM_3X;
    return MENU_ACTION_NONE;
}

/* ---- JSON loader ---- */

int menubar_load(MenuBar *mb, const char *path) {
    memset(mb, 0, sizeof *mb);

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *json = malloc((size_t)sz + 1);
    if (!json) { fclose(f); return -1; }
    if (fread(json, 1, (size_t)sz, f) != (size_t)sz) {
        free(json); fclose(f); return -1;
    }
    fclose(f);
    json[sz] = '\0';

    jsmntok_t toks[256];
    jsmn_parser p;
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, (size_t)sz, toks, 256);
    if (n < 1 || toks[0].type != JSMN_OBJECT) {
        fprintf(stderr, "menus.json: parse error (%d)\n", n);
        free(json); return -1;
    }

    /* iterate root object keys */
    int i = 1;
    for (int rk = 0; rk < toks[0].size; rk++) {
        if (!jsoneq(json, &toks[i], "menus") || toks[i+1].type != JSMN_ARRAY) {
            i = jsmn_skip(toks, i);
            continue;
        }
        int arr = i + 1;                          /* menus array token */
        int mi  = arr + 1;                        /* first menu object */
        for (int m = 0; m < toks[arr].size && mb->menu_count < MENU_MAX; m++) {
            if (toks[mi].type != JSMN_OBJECT) { mi = jsmn_skip(toks, mi); continue; }
            Menu *menu = &mb->menus[mb->menu_count++];
            int mj = mi + 1;
            for (int mk = 0; mk < toks[mi].size; mk++) {
                if (jsoneq(json, &toks[mj], "title")) {
                    tok_str(json, &toks[mj+1], menu->title, sizeof menu->title);
                } else if (jsoneq(json, &toks[mj], "items") &&
                           toks[mj+1].type == JSMN_ARRAY) {
                    int iarr = mj + 1;
                    int ii   = iarr + 1;
                    for (int it = 0; it < toks[iarr].size && menu->item_count < MENU_ITEM_MAX; it++) {
                        if (toks[ii].type != JSMN_OBJECT) { ii = jsmn_skip(toks, ii); continue; }
                        MenuItem *item = &menu->items[menu->item_count++];
                        int ij = ii + 1;
                        for (int ik = 0; ik < toks[ii].size; ik++) {
                            if (jsoneq(json, &toks[ij], "title"))
                                tok_str(json, &toks[ij+1], item->title, sizeof item->title);
                            else if (jsoneq(json, &toks[ij], "separator"))
                                item->separator = 1;
                            else if (jsoneq(json, &toks[ij], "action")) {
                                char act[32];
                                tok_str(json, &toks[ij+1], act, sizeof act);
                                item->action = parse_action(act);
                            } else if (jsoneq(json, &toks[ij], "key"))
                                tok_str(json, &toks[ij+1], item->key, sizeof item->key);
                            ij = jsmn_skip(toks, ij);
                        }
                        ii = jsmn_skip(toks, ii);
                    }
                }
                mj = jsmn_skip(toks, mj);
            }
            mi = jsmn_skip(toks, mi);
        }
        i = jsmn_skip(toks, i);
    }

    free(json);
    return 0;
}

/* ---- Nuklear rendering ---- */

MenuAction menu_draw(MenuBar *mb, struct nk_context *nk, int window_width, int bar_h) {
    MenuAction triggered = MENU_ACTION_NONE;

    int item_h = bar_h - 8;  /* row height for both the bar buttons and dropdown items */

    /* nk->style.font.width() measures text pixels using the currently active font */
    const struct nk_user_font *f = nk->style.font;

    if (nk_begin(nk, "menubar", nk_rect(0, 0, (float)window_width, (float)bar_h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_menubar_begin(nk);
        nk_layout_row_begin(nk, NK_STATIC, (float)item_h, mb->menu_count);

        for (int m = 0; m < mb->menu_count; m++) {
            Menu *menu = &mb->menus[m];

            /* button width: measured title + horizontal padding */
            float btn_w = f->width(f->userdata, f->height,
                                   menu->title, (int)strlen(menu->title))
                          + (float)bar_h;

            /* popup width: widest item label + padding for margins */
            float popup_w = 0;
            for (int it = 0; it < menu->item_count; it++) {
                if (menu->items[it].separator) continue;
                float w = f->width(f->userdata, f->height,
                                   menu->items[it].title,
                                   (int)strlen(menu->items[it].title));
                if (w > popup_w) popup_w = w;
            }
            popup_w += (float)bar_h;

            /* popup height: items at item_h, separators at 6px */
            int popup_h = 8;
            for (int it = 0; it < menu->item_count; it++)
                popup_h += menu->items[it].separator ? 6 : item_h;

            nk_layout_row_push(nk, btn_w);
            if (nk_menu_begin_label(nk, menu->title, NK_TEXT_LEFT,
                                    nk_vec2(popup_w, (float)popup_h))) {
                for (int it = 0; it < menu->item_count; it++) {
                    MenuItem *item = &menu->items[it];
                    if (item->separator) {
                        /* nk_rule_horizontal draws a real horizontal rule line */
                        nk_layout_row_dynamic(nk, 6, 1);
                        nk_rule_horizontal(nk, nk_rgb(160, 160, 160), nk_false);
                        continue;
                    }
                    nk_layout_row_dynamic(nk, (float)item_h, 1);
                    if (nk_menu_item_label(nk, item->title, NK_TEXT_LEFT))
                        triggered = item->action;
                }
                nk_menu_end(nk);
            }
        }

        nk_layout_row_end(nk);
        nk_menubar_end(nk);
    }
    nk_end(nk);

    if (triggered == MENU_ACTION_ABOUT)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
            "StackWorks II Pro",
            "StackWorks II Pro\nHyperCard stack viewer\nA work in progress.\n\n\xF0\x9F\x8D\x8C",
            NULL);

    return triggered;
}
