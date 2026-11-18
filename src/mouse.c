/* Mouse input: maps screen cells to text positions and drives the menus. */
#include <ctype.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "clipboard.h"
#include "menu.h"
#include "term.h"
#include "ui.h"
#include "utf8.h"
#include "tabs.h"
#include "options.h"
#include "mouse.h"

/* True while a left-button drag that started in the text area is in progress. */
static int dragging = 0;

/* A second left press on the same cell within this time selects the word. */
#define DOUBLE_CLICK_MS 400
#define MENU_EDIT 1          /* index of Edit in menus[] (File, Edit, Search, Options) */

/* The last plain left press in the text area, for double-click detection. */
static int click_pending = 0;
static unsigned long last_click_ms = 0;
static int last_click_x = 0, last_click_y = 0;

/* Text rows are TEXT_TOP .. screen_h-3. Row 0 is the menu bar, TAB_ROW the tab bar;
   the last two are the message and status rows. */
static int text_bottom(void) {
    return screen_h - 3;
}

/* Puts the cursor at the character under cell (x, y). Rows outside the text area
   map to the lines just above or below the view, so dragging past an edge scrolls. */
static void cursor_to_cell(int x, int y) {
    int tx = x - gutter_w();   /* clicks in the gutter land on column 0 */
    if (tx < 0) tx = 0;

    if (options.wrap) {
        /* Walk down from the top line, counting screen rows, to find the row under y */
        int target = y - TEXT_TOP;
        if (target < 0) target = 0;
        int line = editor.scroll_y, used = 0;
        while (line < editor.line_count - 1 && target >= used + line_rows(line)) {
            used += line_rows(line);
            line++;
        }
        if (line > editor.line_count - 1) line = editor.line_count - 1;
        int row = target - used;
        int rows = line_rows(line);
        if (row >= rows) row = rows - 1;
        cursor_set_wrapped(line, row, tx);
        return;
    }

    int line = editor.scroll_y + (y - TEXT_TOP);
    if (line < 0) line = 0;
    if (line > editor.line_count - 1) line = editor.line_count - 1;

    Line *ln = &editor.lines[line];
    editor.cursor_y = line;
    editor.cursor_x = utf8_byte_at_col(ln->text, ln->len, editor.scroll_x + tx);
}

/* Selects the word under the cursor. Words are runs of letters, digits, '_' and
   non-ASCII bytes; spaces and other punctuation form their own runs. Every byte of
   a multi-byte UTF-8 character is in the word class, so a character is never split. */
static int word_class(unsigned char c) {
    if (isalnum(c) || c == '_' || c >= 0x80) return 0;
    if (c == ' ' || c == '\t') return 1;
    return 2;
}

static void select_word_at_cursor(void) {
    Line *ln = &editor.lines[editor.cursor_y];
    if (editor.cursor_x >= ln->len) return;   /* past the end: nothing to select */

    int cls = word_class((unsigned char)ln->text[editor.cursor_x]);
    int start = editor.cursor_x, end = editor.cursor_x;
    while (start > 0 && word_class((unsigned char)ln->text[start - 1]) == cls) start--;
    while (end < ln->len && word_class((unsigned char)ln->text[end]) == cls) end++;

    sel_clear();
    editor.cursor_x = start;
    sel_extend();                 /* anchor at the start of the word */
    editor.cursor_x = end;
}

/* Scrolls the view by rows (negative is up). The cursor does not move, so the
   view may go past it; the next key or click brings the view back. */
int view_detached = 0;

static void scroll_by(int rows) {
    int max_line = editor.line_count - 1;
    if (max_line < 0) max_line = 0;

    editor.scroll_y += rows;
    if (editor.scroll_y < 0) editor.scroll_y = 0;
    if (editor.scroll_y > max_line) editor.scroll_y = max_line;
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
    /* Only the wheel leaves the view detached; any other mouse input re-attaches it */
    view_detached = (ev->kind == MEV_WHEEL_UP || ev->kind == MEV_WHEEL_DOWN);

    /* Right-click opens the Edit menu; right-click with a menu open just closes it */
    if (ev->kind == MEV_RIGHT_PRESS) {
        dragging = 0;
        click_pending = 0;
        if (editor.menu_active) {
            editor.menu_active = 0;
            editor.dropdown_open = 0;
        } else {
            editor.menu_active = 1;
            editor.selected_menu = MENU_EDIT;
            editor.selected_item = 0;
            editor.dropdown_open = 1;
        }
        return ACT_NONE;
    }

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
            {
                unsigned long now = term_ticks_ms();
                int dbl = click_pending && !ev->shift
                       && now - last_click_ms <= DOUBLE_CLICK_MS
                       && ev->x == last_click_x && ev->y == last_click_y;
                /* A double-click ends the run, so a third click starts over */
                click_pending = !ev->shift && !dbl;
                last_click_ms = now;
                last_click_x = ev->x;
                last_click_y = ev->y;

                if (dbl) {
                    sel_clear();
                    cursor_to_cell(ev->x, ev->y);
                    select_word_at_cursor();
                    dragging = 0;            /* a drag would drop the word selection */
                    break;
                }
            }
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

        case MEV_RIGHT_PRESS:
            break;                           /* handled at the top */

        case MEV_WHEEL_UP:
            scroll_by(-3);
            break;

        case MEV_WHEEL_DOWN:
            scroll_by(3);
            break;
    }
    return ACT_NONE;
}
