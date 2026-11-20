/* Menu bar: menu tables, menu-mode key handling and open/close state. */
#include <string.h>

#include "editor.h"
#include "term.h"
#include "menu.h"

#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Item order in each table is free; mnemonics pick items within one menu. */
static const MenuItem file_items[] = {
    {"New",        "Ctrl+N", 'N', ACT_NEW,     0, 0},
    {"Open...",    "Ctrl+O", 'O', ACT_OPEN,    0, 0},
    {"Save",       "Ctrl+S", 'S', ACT_SAVE,    0, 0},
    {"Save As...", "",       'A', ACT_SAVE_AS, 0, 0},
    {"Recent Files...", "Ctrl+E", 'E', ACT_RECENT, 0, 0},
    {"Quick Open...", "Ctrl+P", 'Q', ACT_QUICK_OPEN, 0, 0},
    {"Close Tab",  "Ctrl+W", 'W', ACT_CLOSE_TAB, 0, 0},
    {"Next Tab",   "F6",     'T', ACT_NEXT_TAB,  0, 0},
    {"Previous Tab", "F7",   'V', ACT_PREV_TAB,  0, 0},
    {"Exit",       "Ctrl+Q", 'X', ACT_EXIT,    0, 0},
};

static const MenuItem edit_items[] = {
    {"Undo",       "Ctrl+Z", 'U', ACT_UNDO,       0, 0},
    {"Redo",       "Ctrl+Y", 'R', ACT_REDO,       0, 0},
    {"Cut",        "Ctrl+X", 'X', ACT_CUT,        0, 0},
    {"Copy",       "Ctrl+C", 'C', ACT_COPY,       0, 0},
    {"Paste",      "Ctrl+V", 'P', ACT_PASTE,      0, 0},
    {"Select All", "Ctrl+A", 'A', ACT_SELECT_ALL, 0, 0},
    {"Ask AI...",  "Alt+I",  'I', ACT_AI_ASK,     0, 0},
    {"Explain...", "",       'E', ACT_AI_EXPLAIN, 0, 0},
};

static const MenuItem search_items[] = {
    {"Find",       "Ctrl+F", 'F', ACT_FIND,      0, 0},
    {"Find Next",  "F3",     'N', ACT_FIND_NEXT, 0, 0},
    {"Replace",    "Ctrl+R", 'R', ACT_REPLACE,   0, 0},
    {"Go to Line", "Ctrl+G", 'G', ACT_GOTO_LINE, 0, 0},
};

/* Toggle items: value shown in the label, dropdown stays open. */
static const MenuItem options_items[] = {
    {NULL, "", 'T', ACT_TAB_WIDTH,    1, 1},
    {NULL, "", 'L', ACT_LINE_ENDS,    1, 1},
    {NULL, "", 'C', ACT_COLOR_SCHEME, 1, 1},
    {NULL, "", 'N', ACT_LINE_NUMBERS, 1, 1},
    {NULL, "", 'Y', ACT_SYNTAX,       1, 1},
    {NULL, "", 'W', ACT_WRAP,         1, 1},
    {NULL, "", 'B', ACT_BRACE,        1, 1},
    {NULL, "", 'G', ACT_LANGUAGE,     1, 1},
    {"AI setup...", "", 'A', ACT_AI_SETUP, 0, 0},
};

const Menu menus[MENU_COUNT] = {
    {"File",    0,  file_items,    ARRAY_COUNT(file_items)},
    {"Edit",    6,  edit_items,    ARRAY_COUNT(edit_items)},
    {"Search",  12, search_items,  ARRAY_COUNT(search_items)},
    {"Options", 20, options_items, ARRAY_COUNT(options_items)},
};

static void open_menu(int which, int dropdown) {
    editor.menu_active = 1;
    editor.selected_menu = which;
    editor.selected_item = 0;
    editor.dropdown_open = dropdown;
}

int menu_open_key(int key) {
    switch (key) {
        case KEY_F10:    open_menu(0, 0); return 1;   /* F10 only highlights File */
        case KEY_ALT_F:  open_menu(0, 1); return 1;
        case KEY_ALT_E:  open_menu(1, 1); return 1;
        case KEY_ALT_S:  open_menu(2, 1); return 1;
        case KEY_ALT_O:  open_menu(3, 1); return 1;
    }
    return 0;
}

int menu_handle_key(int key) {
    if (!editor.menu_active) return ACT_NONE;

    if (!editor.dropdown_open) {
        /* Menu bar: move between menus, open the dropdown, or leave the menu */
        switch (key) {
            case KEY_LEFT:
                editor.selected_menu = (editor.selected_menu + MENU_COUNT - 1) % MENU_COUNT;
                break;
            case KEY_RIGHT:
                editor.selected_menu = (editor.selected_menu + 1) % MENU_COUNT;
                break;
            case KEY_DOWN:
            case KEY_ENTER:
                editor.dropdown_open = 1;
                editor.selected_item = 0;
                break;
            case KEY_ESC:
                editor.menu_active = 0;
                break;
        }
        return ACT_NONE;
    }

    /* Dropdown open */
    const Menu *m = &menus[editor.selected_menu];
    int item = -1;

    switch (key) {
        case KEY_LEFT:
            editor.selected_menu = (editor.selected_menu + MENU_COUNT - 1) % MENU_COUNT;
            editor.selected_item = 0;
            break;
        case KEY_RIGHT:
            editor.selected_menu = (editor.selected_menu + 1) % MENU_COUNT;
            editor.selected_item = 0;
            break;
        case KEY_UP:
            if (m->count) editor.selected_item = (editor.selected_item + m->count - 1) % m->count;
            break;
        case KEY_DOWN:
            if (m->count) editor.selected_item = (editor.selected_item + 1) % m->count;
            break;
        case KEY_ENTER:
            if (m->count) item = editor.selected_item;
            break;
        case KEY_ESC:
            editor.dropdown_open = 0;
            break;
        default:
            /* Letter shortcut, case-insensitive */
            if (key >= 32 && key <= 126) {
                int up = (key >= 'a' && key <= 'z') ? key - 32 : key;
                for (int i = 0; i < m->count; i++) {
                    if (m->items[i].mnemonic == up) { item = i; break; }
                }
            }
            break;
    }

    if (item < 0) return ACT_NONE;

    const MenuItem *it = &m->items[item];
    if (it->keep_open) {
        editor.selected_item = item;          /* toggles stay open */
    } else {
        editor.menu_active = 0;
        editor.dropdown_open = 0;
    }
    return it->action;
}
