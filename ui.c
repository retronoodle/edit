/* Screen drawing: status line, menus, editor area, messages and help. */
#include <stdio.h>
#include <stdlib.h>
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
#include "syntax.h"
#include "options.h"
#include "tabs.h"
#include "gitinfo.h"
#include "ui.h"

/* Separator between status parts: a thin box-drawing bar (UTF-8, one cell). */
#define STATUS_SEP " \xe2\x94\x82 "

/* Appends a separator and part to out (the first part gets no separator).
   Returns the byte offset where the part starts. */
static size_t add_status_part(char *out, size_t size, const char *part) {
    if (out[0]) strncat(out, STATUS_SEP, size - strlen(out) - 1);
    size_t at = strlen(out);
    strncat(out, part, size - strlen(out) - 1);
    return at;
}

/* Left side of the status line. Level 0 has every part; higher levels drop
   the directory, then the branch, then the F1 hint. branch_at gets the byte
   offset of the branch text, or -1 if it is not shown. */
static void build_status_left(char *out, size_t size, int level, const char *pos,
                              const GitInfo *gi, int *branch_at) {
    out[0] = '\0';
    *branch_at = -1;
    if (level < 3) add_status_part(out, size, "F1=Help");
    add_status_part(out, size, pos);
    if (level < 2 && gi->branch[0]) *branch_at = (int)add_status_part(out, size, gi->branch);
    if (level < 1 && gi->dir[0]) add_status_part(out, size, gi->dir);
}

void draw_status_line() {
    char pos[64];
    Line *cur = &editor.lines[editor.cursor_y];
    snprintf(pos, sizeof(pos), "Line:%d Col:%d %s %s",
             editor.cursor_y + 1,
             utf8_col(cur->text, cur->len, editor.cursor_x) + 1,
             editor.insert_mode ? "INS" : "OVR",
             editor.modified ? "*" : "");

    int fname_only_len = (int)strlen(editor.filename);
    int fname_len_with_space = fname_only_len + 1;
    if (fname_len_with_space > screen_w) fname_len_with_space = screen_w;
    int start = screen_w - fname_len_with_space;
    if (start < 0) start = 0;

    /* Pick the fullest left side that fits before the filename (in cells). */
    const GitInfo *gi = gitinfo_get();
    char left[GIT_DIR_MAX + 256];
    int branch_at = -1;
    for (int level = 0; level < 4; level++) {
        build_status_left(left, sizeof(left), level, pos, gi, &branch_at);
        if (utf8_width(left, (int)strlen(left)) <= start) break;
    }

    int left_len = (int)strlen(left);
    if (left_len > start) left_len = start;   /* only ASCII is cut here (the lowest level) */
    int cells = utf8_width(left, left_len);

    const char *base = options_chrome_sgr(CH_STATUS);
    fb_gotoxy(0, screen_h - 1);
    fb_puts(base);

    /* Left side, with the branch in its own color */
    int bl = branch_at >= 0 ? (int)strlen(gi->branch) : 0;
    if (branch_at >= 0 && branch_at + bl <= left_len) {
        fb_write(left, branch_at);
        fb_puts(options_chrome_sgr(CH_BRANCH));
        fb_write(left + branch_at, bl);
        fb_puts(base);
        fb_write(left + branch_at + bl, left_len - branch_at - bl);
    } else {
        fb_write(left, left_len);
    }
    for (; cells < start; cells++) fb_write(" ", 1);

    /* Filename, bold, after one space */
    int written = start;
    if (start < screen_w) {
        fb_write(" ", 1);
        int copy = screen_w - start - 1;
        if (copy > fname_only_len) copy = fname_only_len;
        fb_puts("\x1b[1m");
        fb_write(editor.filename, copy);
        fb_puts(base);
        written = start + 1 + copy;
    }
    for (; written < screen_w; written++) fb_write(" ", 1);
}

void draw_menu_bar() {
    const char *bar = options_chrome_sgr(CH_BAR);
    fb_gotoxy(0, 0);

    // Fill the bar with the chrome color
    char blank[screen_w];
    memset(blank, ' ', screen_w);
    fb_puts(bar);
    fb_write(blank, screen_w);

    // Draw each menu title; the open one uses its own color, the first letter is bold
    for (int i = 0; i < MENU_COUNT; i++) {
        fb_gotoxy(menus[i].x, 0);
        const char *sgr = (editor.menu_active && editor.selected_menu == i)
                              ? options_chrome_sgr(CH_BAR_SEL) : bar;
        fb_puts(sgr);
        fb_puts("\x1b[1m");
        fb_write(menus[i].title, 1);
        fb_puts(sgr);
        fb_puts(menus[i].title + 1);
    }

    // "EDIT" on the right side
    fb_gotoxy(screen_w - 4, 0);
    fb_puts(options_chrome_sgr(CH_BAR_SEL));
    fb_puts("EDIT");
    fb_puts(options_base_sgr());
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

/* Outer box: row 1 is the top border, items are on rows 2.., the bottom border follows. */
void dropdown_rect(int which, int *x, int *w) {
    int label_w, hint_w;
    dropdown_widths(which, &label_w, &hint_w);
    *w = 1 + label_w + 2 + hint_w + 1 + 2;   /* inner text plus the two side borders */
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
    const char *base = options_base_sgr();
    const char *dim = options_chrome_sgr(CH_DIM);
    const char *sel = options_chrome_sgr(CH_DROP_SEL);

    /* Top border */
    fb_gotoxy(x, 1);
    fb_puts(dim);
    fb_puts("\xe2\x94\x8c");                         /* top-left corner */
    for (int k = 0; k < width - 2; k++) fb_puts("\xe2\x94\x80");   /* horizontal line */
    fb_puts("\xe2\x94\x90");                         /* top-right corner */
    fb_puts(base);

    for (int i = 0; i < m->count; i++) {
        const char *label = item_label(&m->items[i], buf, sizeof(buf));
        int hl = (i == editor.selected_item);
        fb_gotoxy(x, 2 + i);

        fb_puts(dim);
        fb_puts("\xe2\x94\x82");                     /* left side */
        fb_puts(hl ? sel : base);
        fb_printf(" %-*s  ", label_w, label);
        fb_puts(hl ? sel : dim);
        fb_printf("%-*s", hint_w, m->items[i].hint);
        fb_puts(hl ? sel : base);
        fb_puts(" ");
        fb_puts(dim);
        fb_puts("\xe2\x94\x82");                     /* right side */
        fb_puts(base);
    }

    /* Bottom border */
    fb_gotoxy(x, 2 + m->count);
    fb_puts(dim);
    fb_puts("\xe2\x94\x94");                         /* bottom-left corner */
    for (int k = 0; k < width - 2; k++) fb_puts("\xe2\x94\x80");
    fb_puts("\xe2\x94\x98");                         /* bottom-right corner */
    fb_puts(base);
}

/* Width of the line-number gutter: digits for the last line number (at least 3),
   one space, then a separator bar. 0 when line numbers are off. */
int gutter_w(void) {
    if (!options.line_numbers) return 0;
    int digits = 1, n = editor.line_count;
    while (n >= 10) { digits++; n /= 10; }
    if (digits < 3) digits = 3;   /* keeps the gutter from jumping on short files */
    return digits + 2;
}

int text_w(void) {
    return screen_w - gutter_w();
}

/* Draws one screen row of a file line. scroll_x and column positions are in
   display columns; each character is drawn whole, so wide characters take
   two cells and never split. Control and invalid bytes show as '?'.
   The line number (if shown) sits in the gutter and is not part of the text. */
/* Per-byte token kinds for the line being drawn. Grown on demand; NULL if out of memory. */
static unsigned char *kind_buf = NULL;
static int kind_cap = 0;

static unsigned char *kind_buffer(int n) {
    if (n > kind_cap) {
        unsigned char *p = realloc(kind_buf, (size_t)n * 2);
        if (!p) return NULL;
        kind_buf = p;
        kind_cap = n * 2;
    }
    return kind_buf;
}

/* Draws one screen row of a file line. kinds (may be NULL) gives the token kind of each byte.
   Selection (reverse video) wins over token colors. */
static void draw_row(int file_line, const unsigned char *kinds, int has_sel, int sy1, int sx1, int sy2, int sx2) {
    int hl = 0;      /* 1 while drawing reverse video */
    int cur_kind = TK_PLAIN;   /* token color currently set (when not reversed) */
    int out = 0;     /* text cells emitted so far */
    int gw = gutter_w();
    int tw = text_w();

    if (gw) {
        fb_puts(options_chrome_sgr(CH_DIM));
        if (file_line < editor.line_count) fb_printf("%*d ", gw - 2, file_line + 1);
        else fb_printf("%*s ", gw - 2, "");
        fb_puts("\xe2\x94\x82");                    /* separator bar */
        fb_puts(options_base_sgr());
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
                int kind = kinds ? kinds[pos] : TK_PLAIN;
                if (selected != hl || (!selected && kind != cur_kind)) {
                    if (selected) fb_puts("\x1b[7m");
                    else fb_puts(kind == TK_PLAIN ? options_base_sgr() : syntax_sgr(kind, options.scheme));
                    hl = selected;
                    cur_kind = kind;
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

    if (hl || cur_kind != TK_PLAIN) fb_puts(options_base_sgr());
    while (out < tw) { fb_write(" ", 1); out++; }
}

/* Tab bar on row TAB_ROW: the active tab in reverse video. */
static void draw_tab_bar(void) {
    int first, end, x = 0;
    char label[64];
    tab_bar_range(&first, &end);

    const char *bar = options_chrome_sgr(CH_TAB_BAR);
    fb_gotoxy(0, TAB_ROW);
    fb_puts(bar);
    for (int i = first; i < end; i++) {
        int n = tab_bar_label(i, label, sizeof(label));
        int w = utf8_width(label, n);
        if (x + w > screen_w) break;
        int active = (i == tab_current());
        const char *sgr = active ? options_chrome_sgr(CH_TAB_ACT) : bar;
        fb_puts(sgr);
        /* Labels end with "* " when the tab has unsaved changes: color the marker */
        int marked = n >= 2 && label[n - 2] == '*';
        fb_write(label, marked ? n - 2 : n);
        if (marked) {
            fb_puts(options_chrome_sgr(CH_WARN));
            fb_write(label + n - 2, 1);
            fb_puts(sgr);
            fb_write(label + n - 1, 1);
        }
        x += w;
    }
    fb_puts(bar);
    while (x < screen_w) {
        fb_write(" ", 1);
        x++;
    }
    fb_puts(options_base_sgr());
}

void draw_editor_area() {
    int has_sel, sy1 = 0, sx1 = 0, sy2 = 0, sx2 = 0;
    has_sel = sel_range(&sy1, &sx1, &sy2, &sx2);

    /* Token colors need the parser state at the top visible line, so scan the lines above it first.
       The scan runs again each redraw; it is cheap next to the terminal output. */
    SynLang lang = options.syntax ? syntax_lang_for(editor.filename) : SYN_PLAIN;
    int st = SYN_ST_NONE;
    if (lang != SYN_PLAIN) {
        for (int f = 0; f < editor.scroll_y && f < editor.line_count; f++) {
            const Line *ln = &editor.lines[f];
            unsigned char *kb = kind_buffer(ln->len);
            if (ln->len > 0 && !kb) { lang = SYN_PLAIN; break; }   /* out of memory: plain text */
            st = syntax_scan_line(lang, st, ln->text, ln->len, kb);
        }
    }

    for (int screen_row = TEXT_TOP; screen_row <= screen_h - 3; screen_row++) {
        fb_gotoxy(0, screen_row);
        int file_line = screen_row - TEXT_TOP + editor.scroll_y;
        const unsigned char *kinds = NULL;
        if (lang != SYN_PLAIN && file_line < editor.line_count) {
            const Line *ln = &editor.lines[file_line];
            unsigned char *kb = kind_buffer(ln->len);
            if (kb) {
                st = syntax_scan_line(lang, st, ln->text, ln->len, kb);
                kinds = kb;
            }
        }
        draw_row(file_line, kinds, has_sel, sy1, sx1, sy2, sx2);
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
    printf("  N/O/S/A/E/W/T/V/X - Pick File menu item (New/Open/Save/Save As/Recent/Close Tab/Next/Previous/Exit)\n");
    printf("  F/N/R/G         - Pick Search menu item (Find/Find Next/Replace/Go to Line)\n");
    printf("  U/R/X/C/P/A     - Pick Edit menu item (Undo/Redo/Cut/Copy/Paste/Select All)\n");
    printf("  T/L/C           - Options: tab width / line endings / colors (menu stays open)\n");
    printf("  Open (O)        - Browse folders (Enter=open, T=type a name, R=recent)\n");
    printf("  Save As         - Browse folders, type the file name, Enter=save\n");
    printf("  ESC             - Close menu\n\n");
    printf("Keyboard Shortcuts:\n");
    printf("  Ctrl+N          - New tab\n");
    printf("  Ctrl+O          - Open file (in a new tab)\n");
    printf("  Ctrl+E          - Recent files (open one in a new tab)\n");
    printf("  Ctrl+S          - Save file\n");
    printf("  Ctrl+W          - Close tab\n");
    printf("  F6 / F7         - Next / previous tab (or click a tab)\n");
    printf("  Ctrl+Z / Ctrl+Y - Undo / Redo\n");
    printf("  Ctrl+X/C/V      - Cut / Copy / Paste\n");
    printf("  Ctrl+A          - Select all\n");
    printf("  Ctrl+F / F3     - Find / Find next\n");
    printf("  Ctrl+R          - Replace (Y/N/A=all, Esc=stop)\n");
    printf("  Ctrl+G          - Go to line\n");
    printf("  Alt+G           - Git panel (status, commit, push, branch, merge)\n");
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
