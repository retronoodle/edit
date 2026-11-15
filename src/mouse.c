/* Mouse input: maps screen cells to text positions and drives the menus. */
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "clipboard.h"
#include "menu.h"
#include "term.h"
#include "ui.h"
#include "utf8.h"
#include "tabs.h"
#include "mouse.h"

/* True while a left-button drag that started in the text area is in progress. */
static int dragging = 0;

/* Text rows are TEXT_TOP .. screen_h-3. Row 0 is the menu bar, TAB_ROW the tab bar;
   the last two are the message and status rows. */
static int text_bottom(void) {
    return screen_h - 3;
}

/* Puts the cursor at the character under cell (x, y). Rows outside the text area
   map to the lines just above or below the view, so dragging past an edge scrolls. */
static void cursor_to_cell(int x, int y) {
    int line = editor.scroll_y + (y - TEXT_TOP);
    if (line < 0) line = 0;
    if (line > editor.line_count - 1) line = editor.line_count - 1;

    int tx = x - gutter_w();   /* clicks in the gutter land on column 0 */
    if (tx < 0) tx = 0;

    Line *ln = &editor.lines[line];
    editor.cursor_y = line;
    editor.cursor_x = utf8_byte_at_col(ln->text, ln->len, editor.scroll_x + tx);
}

/* Scrolls the view by rows (negative is up). The cursor keeps its column and
   moves only if it would be left off screen. */
static void scroll_by(int rows) {
    int max_line = editor.line_count - 1;
    if (max_line < 0) max_line = 0;

    Line *old = &editor.lines[editor.cursor_y];
    int col = utf8_col(old->text, old->len, editor.cursor_x);

    editor.scroll_y += rows;
    if (editor.scroll_y < 0) editor.scroll_y = 0;
    if (editor.scroll_y > max_line) editor.scroll_y = max_line;

    int top = editor.scroll_y;
    int bottom = editor.scroll_y + TEXT_ROWS - 1;
    if (bottom > max_line) bottom = max_line;

    if (editor.cursor_y < top) editor.cursor_y = top;
    if (editor.cursor_y > bottom) editor.cursor_y = bottom;

    Line *ln = &editor.lines[editor.cursor_y];
    editor.cursor_x = utf8_byte_at_col(ln->text, ln->len, col);
}

/* Returns the menu whose title covers column x on the menu bar, or -1. */
static int title_at(int x) {
    for (int i = 0; i < MENU_COUNT; i++) {
        int w = (int)strlen(menus[i].title);
        if (x >= menus[i].x && x < menus[i].x + w) return i;
    }
    return -1;
}

static void close_menu(void) {
    editor.menu_active = 0;
    editor.dropdown_open = 0;
}

/* A press while a menu is open, or on the menu bar. Returns the picked ACT_*, if any. */
static int menu_click(const MouseEvent *ev) {
    if (ev->y == 0) {
        int i = title_at(ev->x);
        if (i < 0) {
            if (editor.menu_active) close_menu();
            return ACT_NONE;
        }
        editor.menu_active = 1;
        editor.selected_menu = i;
        editor.selected_item = 0;
        editor.dropdown_open = 1;
        return ACT_NONE;
    }

    if (editor.menu_active && editor.dropdown_open) {
        int x, w;
        dropdown_rect(editor.selected_menu, &x, &w);
        int count = menus[editor.selected_menu].count;
        /* Box: top border on row 1, items on rows 2..count+1, bottom border after */
        if (ev->y >= 1 && ev->y <= count + 2 && ev->x >= x && ev->x < x + w) {
            if (ev->y >= 2 && ev->y <= count + 1) {
                /* Same as moving to the item and pressing Enter */
                editor.selected_item = ev->y - 2;
                return menu_handle_key(KEY_ENTER);
            }
            return ACT_NONE;   /* a click on the frame keeps the menu open */
        }
    }

    if (editor.menu_active) close_menu();   /* click outside the menu */
    return ACT_NONE;
}

int mouse_handle(const MouseEvent *ev) {
    /* Clicks on the menu bar, and any click while a menu is open, go to the menus */
    if (ev->kind == MEV_PRESS && (editor.menu_active || ev->y == 0)) {
        dragging = 0;
        return menu_click(ev);
    }
    /* With a menu open, other mouse input is ignored */
    if (editor.menu_active) return ACT_NONE;

    /* A click on the tab bar switches to that tab */
    if (ev->kind == MEV_PRESS && ev->y == TAB_ROW) {
        dragging = 0;
        int i = tab_bar_hit(ev->x);
        if (i >= 0) tab_switch(i);
        return ACT_NONE;
    }

    switch (ev->kind) {
        case MEV_PRESS:
            if (ev->y < TEXT_TOP || ev->y > text_bottom()) return ACT_NONE;   /* message or status row */
            if (ev->shift) sel_extend();     /* Shift+click extends from the old cursor */
            else sel_clear();
            cursor_to_cell(ev->x, ev->y);
            dragging = 1;
            break;

        case MEV_DRAG:
            if (!dragging) break;
            sel_extend();                    /* anchor stays where the press was */
            cursor_to_cell(ev->x, ev->y);
            break;

        case MEV_RELEASE:
            dragging = 0;
            break;

        case MEV_WHEEL_UP:
            scroll_by(-3);
            break;

        case MEV_WHEEL_DOWN:
            scroll_by(3);
            break;
    }
    return ACT_NONE;
}
