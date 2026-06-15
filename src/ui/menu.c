#include "menu.h"
#include "render.h"
#include "nuklear.h"

int menu_draw(struct nk_context *nk, int window_width) {
    int quit = 0;
    if (nk_begin(nk, "menubar", nk_rect(0, 0, (float)window_width, MENU_BAR_H),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_menubar_begin(nk);
        nk_layout_row_begin(nk, NK_STATIC, MENU_BAR_H - 4, 1);
        nk_layout_row_push(nk, 45);
        if (nk_menu_begin_label(nk, "File", NK_TEXT_LEFT, nk_vec2(120, 60))) {
            nk_layout_row_dynamic(nk, 24, 1);
            if (nk_menu_item_label(nk, "Quit", NK_TEXT_LEFT))
                quit = 1;
            nk_menu_end(nk);
        }
        nk_layout_row_end(nk);
        nk_menubar_end(nk);
    }
    nk_end(nk);
    return quit;
}
