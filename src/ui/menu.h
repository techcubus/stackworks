#ifndef UI_MENU_H
#define UI_MENU_H

struct nk_context;

/* Build and draw the menu bar. Returns 1 if Quit was selected, 0 otherwise. */
int menu_draw(struct nk_context *nk, int window_width);

#endif /* UI_MENU_H */
