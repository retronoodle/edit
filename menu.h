/* Menu bar: menu tables, menu-mode keys and the actions they trigger. */
#ifndef MENU_H
#define MENU_H

/* Actions a menu item can trigger. velo.c runs them. */
enum {
    ACT_NONE = 0,
    ACT_NEW, ACT_OPEN, ACT_RECENT, ACT_SAVE, ACT_SAVE_AS, ACT_CLOSE_TAB, ACT_NEXT_TAB, ACT_PREV_TAB, ACT_EXIT,
    ACT_UNDO, ACT_REDO, ACT_CUT, ACT_COPY, ACT_PASTE, ACT_SELECT_ALL,
    ACT_FIND, ACT_FIND_NEXT, ACT_REPLACE, ACT_GOTO_LINE,
    ACT_TAB_WIDTH, ACT_LINE_ENDS, ACT_COLOR_SCHEME, ACT_LINE_NUMBERS, ACT_SYNTAX
};

typedef struct {
    const char *label;   /* text shown; NULL when dynamic */
    const char *hint;    /* shortcut text shown to the right, may be "" */
    char mnemonic;       /* letter that picks the item (uppercase) */
    int action;          /* ACT_* to run */
    int keep_open;       /* 1: dropdown stays open after running (toggle items) */
    int dynamic;         /* 1: label comes from options_label() */
} MenuItem;

typedef struct {
    const char *title;
    int x;               /* screen column of the title and its dropdown */
    const MenuItem *items;
    int count;
} Menu;

#define MENU_COUNT 4
extern const Menu menus[MENU_COUNT];   /* File, Edit, Search, Options */

/* Opens a menu for F10 (bar only) or Alt+F/E/S/O (dropdown). Returns 1 if the key was one of these. */
int menu_open_key(int key);

/* Handles a key while the menu is active. Returns the chosen ACT_*, or ACT_NONE. */
int menu_handle_key(int key);

#endif
