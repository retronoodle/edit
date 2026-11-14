/* Screen drawing: status line, menus, editor area, messages and help. */
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include "editor.h"
#include "term.h"
#include "clipboard.h"
#include "utf8.h"
#include "menu.h"
#include "options.h"
#include "tabs.h"
#include "ui.h"

void draw_status_line() {
    char buf[screen_w + 1];
    memset(buf, ' ', screen_w);
    buf[screen_w] = '\0';

    char left[256];
    Line *cur = &editor.lines[editor.cursor_y];
    snprintf(left, sizeof(left), "F1=Help | Line:%d Col:%d %s %s",
             editor.cursor_y + 1,
             utf8_col(cur->text, cur->len, editor.cursor_x) + 1,
             editor.insert_mode ? "INS" : "OVR",
             editor.modified ? "*" : "");

    int left_len = (int)strlen(left);
    if (left_len > screen_w) left_len = screen_w;
    memcpy(buf, left, left_len);

    int fname_only_len = (int)strlen(editor.filename);
    int fname_len_with_space = fname_only_len + 1;
    if (fname_len_with_space > screen_w) fname_len_with_space = screen_w;
    int start = screen_w - fname_len_with_space;
    if (start < 0) start = 0;

    if (start < screen_w) {
        buf[start] = ' ';
        int copy = screen_w - start - 1;
        if (copy > fname_only_len) copy = fname_only_len;
        memcpy(&buf[start + 1], editor.filename, copy);
    }

    fb_gotoxy(0, screen_h - 1);
    fb_write(buf, screen_w);
}

void draw_menu_bar() {
    fb_gotoxy(0, 0);

    // Draw entire menu bar with spaces
    char blank[screen_w];
    memset(blank, ' ', screen_w);
    fb_write(blank, screen_w);

    // Draw each menu title
    for (int i = 0; i < MENU_COUNT; i++) {
        fb_gotoxy(menus[i].x, 0);

        if (editor.menu_active && editor.selected_menu == i) {
            // Highlighted (inverse video)
            fb_printf("\x1b[7m%s%s", menus[i].title, options_base_sgr());
        } else {
            fb_printf("%s", menus[i].title);
        }
    }

    // Draw "EDIT" on the right side
    fb_gotoxy(screen_w - 4, 0);
    fb_puts("EDIT");
}

/* Item text: the fixed label, or the live value for Options items. */
static const char *item_label(const MenuItem *it, char *buf, int size) {
    if (!it->dynamic) return it->label;
    options_label(it->action, buf, size);
    return buf;
}

/* Column widths fit the longest label and hint in one menu. */
static void dropdown_widths(int which, int *label_w, int *hint_w) {
    const Menu *m = &menus[which];
    char buf[64];
    *label_w = 0;
    *hint_w = 0;
    for (int i = 0; i < m->count; i++) {
        int l = (int)strlen(item_label(&m->items[i], buf, sizeof(buf)));
        int h = (int)strlen(m->items[i].hint);
        if (l > *label_w) *label_w = l;
        if (h > *hint_w) *hint_w = h;
    }
}

void dropdown_rect(int which, int *x, int *w) {
    int label_w, hint_w;
    dropdown_widths(which, &label_w, &hint_w);
    *w = 1 + label_w + 2 + hint_w + 1;
    *x = menus[which].x;
    if (*x + *w > screen_w) *x = screen_w - *w;
}

void draw_dropdown() {
    if (!editor.dropdown_open) return;

    const Menu *m = &menus[editor.selected_menu];
    char buf[64];
    int label_w, hint_w, x, width;
    dropdown_widths(editor.selected_menu, &label_w, &hint_w);
    dropdown_rect(editor.selected_menu, &x, &width);

    for (int i = 0; i < m->count; i++) {
        const char *label = item_label(&m->items[i], buf, sizeof(buf));
        fb_gotoxy(x, 1 + i);

        int hl = (i == editor.selected_item);
        if (hl) fb_puts("\x1b[7m");   // highlight
        fb_printf(" %-*s  %-*s ", label_w, label, hint_w, m->items[i].hint);
        if (hl) fb_puts(options_base_sgr());
    }
}

/* Width of the line-number gutter: digits for the last line number (at least 3)
   plus one space. 0 when line numbers are off. */
int gutter_w(void) {
    if (!options.line_numbers) return 0;
    int digits = 1, n = editor.line_count;
    while (n >= 10) { digits++; n /= 10; }
    if (digits < 3) digits = 3;   /* keeps the gutter from jumping on short files */
    return digits + 1;
}

int text_w(void) {
    return screen_w - gutter_w();
}

/* Draws one screen row of a file line. scroll_x and column positions are in
   display columns; each character is drawn whole, so wide characters take
   two cells and never split. Control and invalid bytes show as '?'.
   The line number (if shown) sits in the gutter and is not part of the text. */
static void draw_row(int file_line, int has_sel, int sy1, int sx1, int sy2, int sx2) {
    int hl = 0;      /* 1 while drawing reverse video */
    int out = 0;     /* text cells emitted so far */
    int gw = gutter_w();
    int tw = text_w();

    if (gw) {
        if (file_line < editor.line_count) {
            fb_printf("\x1b[2m%*d ", gw - 1, file_line + 1);   /* dim number */
            fb_puts(options_base_sgr());
        } else {
            fb_printf("%*s", gw, "");
        }
    }

    if (file_line < editor.line_count) {
        const Line *ln = &editor.lines[file_line];
        int pos = 0, col = 0;
        int right = editor.scroll_x + tw;

        while (pos < ln->len && col < right) {
            int nxt = utf8_next(ln->text, ln->len, pos);
            int cp;
            utf8_decode(ln->text + pos, ln->len - pos, &cp);
            int w = cp_width(cp);

            int start = col > editor.scroll_x ? col : editor.scroll_x;
            int end = col + w < right ? col + w : right;
            if (end > start) {
                int selected = 0;
                if (has_sel && file_line >= sy1 && file_line <= sy2) {
                    int from = (file_line == sy1) ? sx1 : 0;
                    int to = (file_line == sy2) ? sx2 : ln->len;
                    selected = pos >= from && pos < to;
                }
                if (selected != hl) {
                    fb_puts(selected ? "\x1b[7m" : options_base_sgr());
                    hl = selected;
                }
                while (out < start - editor.scroll_x) { fb_write(" ", 1); out++; }

                int fully_visible = col >= editor.scroll_x && col + w <= right;
                if (!fully_visible) {
                    for (int k = start; k < end; k++) fb_write(" ", 1);   /* wide char cut at an edge */
                } else if (cp < 0 || cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
                    fb_write("?", 1);
                } else {
                    fb_write(ln->text + pos, nxt - pos);   /* includes any combining marks */
                }
                out = end - editor.scroll_x;
            }
            col += w;
            pos = nxt;
        }
    }

    if (hl) fb_puts(options_base_sgr());
    while (out < tw) { fb_write(" ", 1); out++; }
}

/* Tab bar on row TAB_ROW: the active tab in reverse video. */
static void draw_tab_bar(void) {
    int first, end, x = 0;
    char label[64];
    tab_bar_range(&first, &end);

    fb_gotoxy(0, TAB_ROW);
    for (int i = first; i < end; i++) {
        int n = tab_bar_label(i, label, sizeof(label));
        int w = utf8_width(label, n);
        if (x + w > screen_w) break;
        int active = (i == tab_current());
        if (active) fb_puts("\x1b[7m");
        fb_write(label, n);
        if (active) fb_puts(options_base_sgr());
        x += w;
    }
    while (x < screen_w) {
        fb_write(" ", 1);
        x++;
    }
}

void draw_editor_area() {
    int has_sel, sy1 = 0, sx1 = 0, sy2 = 0, sx2 = 0;
    has_sel = sel_range(&sy1, &sx1, &sy2, &sx2);

    for (int screen_row = TEXT_TOP; screen_row <= screen_h - 3; screen_row++) {
        fb_gotoxy(0, screen_row);
        int file_line = screen_row - TEXT_TOP + editor.scroll_y;
        draw_row(file_line, has_sel, sy1, sx1, sy2, sx2);
    }

    /* Message row: left blank here; messages are printed over it */
    char blank[screen_w];
    memset(blank, ' ', screen_w - 1);
    fb_gotoxy(0, screen_h - 2);
    fb_write(blank, screen_w - 1);
}

void refresh_screen() {
    fb_reset();
    fb_puts("\x1b[?25l");   // hide cursor while drawing
    fb_puts(options_base_sgr());

    draw_menu_bar();
    draw_tab_bar();
    draw_editor_area();
    draw_dropdown();
    draw_status_line();

    /* Leave the color scheme active so message and prompt rows match */
    fb_puts(options_base_sgr());

    Line *cur = &editor.lines[editor.cursor_y];
    int screen_x = utf8_col(cur->text, cur->len, editor.cursor_x) - editor.scroll_x + gutter_w();
    int screen_y = editor.cursor_y - editor.scroll_y + TEXT_TOP;
    fb_gotoxy(screen_x, screen_y);
    fb_puts("\x1b[?25h");   // show cursor

    // One write for the whole frame
    fb_flush();
}

/* ---- Status messages and prompts (bottom row, screen_h-2) ---- */

/* Shows a message briefly; the next redraw clears it. */
void show_message(const char *msg) {
    gotoxy(0, screen_h - 2);
    printf("%-*s", screen_w - 1, msg);
    fflush(stdout);
#ifdef _WIN32
    Sleep(900);
#else
    sleep(1);
#endif
}

/* Reads a line of text on the prompt row. Enter accepts, Esc cancels.
   Reads char-by-char so it works in raw mode (POSIX) and cooked mode alike.
   Returns 1 if a non-empty line was entered. */
int prompt_line_opt(const char *msg, char *buf, int size) {
    int len = 0;
    buf[0] = '\0';

    gotoxy(0, screen_h - 2);
    printf("%-*s", screen_w - 1, "");
    gotoxy(0, screen_h - 2);
    printf("%s", msg);
    fflush(stdout);
    set_cursor_visibility(1);

    while (1) {
        int c = getch_cp();                  /* 0 = non-text key, -1 = nothing usable */
        if (c < 0) continue;
        if (c == 13 || c == 10) break;
        if (c == 27) {                       // Esc: cancel
            buf[0] = '\0';
            return 0;
        }
        if (c == 8 || c == 127) {            // Backspace: remove one whole character
            if (len > 0) {
                int p = utf8_prev(buf, len, len);
                int w = utf8_width(buf + p, len - p);
                len = p;
                buf[len] = '\0';
                for (int i = 0; i < w; i++) printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (c >= 32 && c != 127 && (c < 0x80 || c >= 0xA0) && c < KEY_BASE) {
            char enc[4];
            int n = utf8_encode(c, enc);
            if (len + n < size) {
                memcpy(buf + len, enc, n);
                len += n;
                buf[len] = '\0';
                fwrite(enc, 1, n, stdout);
                fflush(stdout);
            }
        }
    }
    buf[len] = '\0';
    return 1;
}

int prompt_line(const char *msg, char *buf, int size) {
    return prompt_line_opt(msg, buf, size) && buf[0] != '\0';
}

void show_help(void) {
    clear_screen();
    printf("EDIT Help\n\n");
    printf("Menu Access:\n");
    printf("  F10 or Alt      - Activate menu bar\n");
    printf("  Alt+F           - Open File menu\n");
    printf("  Alt+E           - Open Edit menu\n");
    printf("  Alt+S           - Open Search menu\n");
    printf("  Alt+O           - Open Options menu\n");
    printf("  Arrow Keys      - Navigate menus\n");
    printf("  Enter           - Select menu item\n");
    printf("  N/O/S/A/W/T/V/X - Pick File menu item (New/Open/Save/Save As/Close Tab/Next/Previous/Exit)\n");
    printf("  F/N/R/G         - Pick Search menu item (Find/Find Next/Replace/Go to Line)\n");
    printf("  U/R/X/C/P/A     - Pick Edit menu item (Undo/Redo/Cut/Copy/Paste/Select All)\n");
    printf("  T/L/C           - Options: tab width / line endings / colors (menu stays open)\n");
    printf("  Open (O)        - Browse folders (Enter=open, T=type a name)\n");
    printf("  ESC             - Close menu\n\n");
    printf("Keyboard Shortcuts:\n");
    printf("  Ctrl+N          - New tab\n");
    printf("  Ctrl+O          - Open file (in a new tab)\n");
    printf("  Ctrl+S          - Save file\n");
    printf("  Ctrl+W          - Close tab\n");
    printf("  F6 / F7         - Next / previous tab (or click a tab)\n");
    printf("  Ctrl+Z / Ctrl+Y - Undo / Redo\n");
    printf("  Ctrl+X/C/V      - Cut / Copy / Paste\n");
    printf("  Ctrl+A          - Select all\n");
    printf("  Ctrl+F / F3     - Find / Find next\n");
    printf("  Ctrl+R          - Replace (Y/N/A=all, Esc=stop)\n");
    printf("  Ctrl+G          - Go to line\n");
    printf("  Ctrl+Q          - Exit\n\n");
    printf("Editing:\n");
    printf("  Arrow Keys      - Move cursor\n");
    printf("  Shift+Arrows    - Select text\n");
    printf("  Insert          - Toggle insert/overwrite mode\n");
    printf("  Delete          - Delete character\n");
    printf("  Backspace       - Backspace\n");
    printf("  Enter           - New line\n");
    printf("  Tab             - Insert spaces (tab width in Options)\n\n");
    printf("Files are read and saved as UTF-8.\n\n");
    printf("Press any key to continue...");
    fflush(stdout);
    getch_cp();
    clear_screen();
}
