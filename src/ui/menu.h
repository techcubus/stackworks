#ifndef UI_MENU_H
#define UI_MENU_H

struct nk_context;

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_QUIT,
    MENU_ACTION_ABOUT,
    MENU_ACTION_OPEN,
    MENU_ACTION_GO_PREV,
    MENU_ACTION_GO_NEXT,
    MENU_ACTION_GO_HOME,
    MENU_ACTION_ZOOM_1X,
    MENU_ACTION_ZOOM_2X,
    MENU_ACTION_ZOOM_3X,
} MenuAction;

#define MENU_MAX       8
#define MENU_ITEM_MAX 16

typedef struct {
    char       title[64];
    int        separator;
    MenuAction action;
    char       key[16];
} MenuItem;

typedef struct {
    char     title[64];
    MenuItem items[MENU_ITEM_MAX];
    int      item_count;
} Menu;

typedef struct {
    Menu menus[MENU_MAX];
    int  menu_count;
} MenuBar;

int        menubar_load(MenuBar *mb, const char *path);
MenuAction menu_draw(MenuBar *mb, struct nk_context *nk, int window_width, int bar_h);

#endif /* UI_MENU_H */
