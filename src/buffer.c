/* Document buffer: line storage, editing operations and cursor/scroll state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "undo.h"
#include "ui.h"
#include "utf8.h"
#include "options.h"
#include "brace.h"
#include "clipboard.h"

Editor editor;

/* ---- Growable line storage: no fixed limit on lines or line length ---- */

static void die_oom(void) {
    fprintf(stderr, "velo: out of memory\n");
    exit(1);
}

/* Makes ln->text hold at least `need` bytes (including the NUL). */
void line_reserve(Line *ln, int need) {
    if (need <= ln->cap) return;
    int cap = ln->cap ? ln->cap : 16;
    while (cap < need) cap *= 2;
    char *p = realloc(ln->text, cap);
    if (!p) die_oom();
    ln->text = p;
    ln->cap = cap;
}

static void line_init(Line *ln) {
    ln->text = NULL;
    ln->len = 0;
    ln->cap = 0;
    line_reserve(ln, 16);
    ln->text[0] = '\0';
}

/* Makes editor.lines hold at least `need` slots. */
static void lines_reserve(int need) {
    if (need <= editor.line_cap) return;
    int cap = editor.line_cap ? editor.line_cap : 64;
    while (cap < need) cap *= 2;
    Line *p = realloc(editor.lines, cap * sizeof(Line));
    if (!p) die_oom();
    editor.lines = p;
    editor.line_cap = cap;
}

/* Inserts a new line at idx holding n bytes copied from s. */
void insert_line_at(int idx, const char *s, int n) {
    lines_reserve(editor.line_count + 1);
    memmove(&editor.lines[idx + 1], &editor.lines[idx],
            (editor.line_count - idx) * sizeof(Line));
    editor.line_count++;
    line_init(&editor.lines[idx]);
    line_reserve(&editor.lines[idx], n + 1);
    memcpy(editor.lines[idx].text, s, n);
    editor.lines[idx].len = n;
    editor.lines[idx].text[n] = '\0';
}

void delete_line_at(int idx) {
    free(editor.lines[idx].text);
    memmove(&editor.lines[idx], &editor.lines[idx + 1],
            (editor.line_count - idx - 1) * sizeof(Line));
    editor.line_count--;
}

void free_all_lines(void) {
    for (int i = 0; i < editor.line_count; i++) free(editor.lines[i].text);
    free(editor.lines);
    editor.lines = NULL;
    editor.line_count = 0;
    editor.line_cap = 0;
}

/* Clears the document to one empty line and puts the cursor at the top. */
void reset_document(void) {
    free_all_lines();
    undo_clear();
    insert_line_at(0, "", 0);
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.modified = 0;
    editor.sel_active = 0;
    editor.sel_block = 0;
    editor.syn_override = 0;
}

void init_editor() {
    editor.lines = NULL;
    editor.line_count = 0;
    editor.line_cap = 0;
    editor.insert_mode = 1;
    editor.menu_active = 0;
    editor.selected_menu = 0;
    editor.dropdown_open = 0;
    editor.selected_item = 0;
    strcpy(editor.filename, "untitled.txt");
    reset_document();
}

int line_rows(int y) {
    if (!options.wrap) return 1;
    const Line *ln = &editor.lines[y];
    return utf8_wrap_rows(ln->text, ln->len, text_w());
}

/* Puts the cursor on wrap row `row` of line y at display column `col` within that row.
   The column is clamped to the row, so the cursor never lands on a neighbouring row. */
void cursor_set_wrapped(int y, int row, int col) {
    Line *ln = &editor.lines[y];
    int start, end, c0;
    utf8_wrap_row(ln->text, ln->len, text_w(), row, &start, &end, &c0);
    int x = utf8_byte_at_col(ln->text, ln->len, c0 + col);
    if (x < start) x = start;
    if (end < ln->len && x >= end) x = utf8_prev(ln->text, ln->len, end);
    editor.cursor_y = y;
    editor.cursor_x = x;
}

/* scroll_x is in display columns; cursor_x is a byte offset. With wrap on,
   long lines are folded instead, so scroll_x stays 0 and scroll_y counts
   file lines whose wrapped rows must fit on screen. */
void adjust_scroll() {
    Line *cur = &editor.lines[editor.cursor_y];
    int tw = text_w();

    if (options.wrap) {
        editor.scroll_x = 0;
        int crow, ccol;
        utf8_wrap_cursor(cur->text, cur->len, tw, editor.cursor_x, &crow, &ccol);

        if (editor.cursor_y < editor.scroll_y) {
            editor.scroll_y = editor.cursor_y;
        } else {
            /* Rows from the top line through the cursor's row must fit in the text area */
            int used = 0;
            for (int f = editor.scroll_y; f < editor.cursor_y; f++) used += line_rows(f);
            while (editor.scroll_y < editor.cursor_y && used + crow + 1 > TEXT_ROWS) {
                used -= line_rows(editor.scroll_y);
                editor.scroll_y++;
            }
        }
        return;
    }

    int cx = utf8_col(cur->text, cur->len, editor.cursor_x);

    if (cx < editor.scroll_x) {
        editor.scroll_x = cx;
    } else if (cx >= editor.scroll_x + tw) {
        editor.scroll_x = cx - tw + 1;
    }

    if (editor.cursor_y < editor.scroll_y) {
        editor.scroll_y = editor.cursor_y;
    } else if (editor.cursor_y >= editor.scroll_y + TEXT_ROWS) {
        editor.scroll_y = editor.cursor_y - TEXT_ROWS + 1;
    }
}

void move_cursor(int dx, int dy) {
    undo_break_group();   /* moving the cursor ends the current typing step */

    if (dy != 0 && options.wrap) {
        /* Up/down step one screen row, keeping the column within the row */
        Line *cur = &editor.lines[editor.cursor_y];
        int crow, ccol;
        utf8_wrap_cursor(cur->text, cur->len, text_w(), editor.cursor_x, &crow, &ccol);

        if (dy > 0) {
            if (crow + 1 < line_rows(editor.cursor_y)) {
                cursor_set_wrapped(editor.cursor_y, crow + 1, ccol);
            } else if (editor.cursor_y + 1 < editor.line_count) {
                cursor_set_wrapped(editor.cursor_y + 1, 0, ccol);
            }
        } else {
            if (crow > 0) {
                cursor_set_wrapped(editor.cursor_y, crow - 1, ccol);
            } else if (editor.cursor_y > 0) {
                int prev = editor.cursor_y - 1;
                cursor_set_wrapped(prev, line_rows(prev) - 1, ccol);
            }
        }
    } else if (dy != 0) {
        /* Keep the same display column when moving between lines */
        Line *cur = &editor.lines[editor.cursor_y];
        int col = utf8_col(cur->text, cur->len, editor.cursor_x);

        editor.cursor_y += dy;
        if (editor.cursor_y < 0) editor.cursor_y = 0;
        if (editor.cursor_y >= editor.line_count) editor.cursor_y = editor.line_count - 1;

        Line *nl = &editor.lines[editor.cursor_y];
        editor.cursor_x = utf8_byte_at_col(nl->text, nl->len, col);
    }

    if (dx != 0) {
        Line *ln = &editor.lines[editor.cursor_y];
        /* Step one whole character at a time */
        for (int i = 0; i < dx && editor.cursor_x < ln->len; i++) {
            editor.cursor_x = utf8_next(ln->text, ln->len, editor.cursor_x);
        }
        for (int i = 0; i < -dx && editor.cursor_x > 0; i++) {
            editor.cursor_x = utf8_prev(ln->text, ln->len, editor.cursor_x);
        }
    }

    adjust_scroll();
}

/* ---- Raw mutations: change the text only, never record undo ---- */

void raw_insert_text(int y, int x, const char *s, int n) {
    Line *ln = &editor.lines[y];
    line_reserve(ln, ln->len + n + 1);
    memmove(&ln->text[x + n], &ln->text[x], ln->len - x + 1);
    memcpy(&ln->text[x], s, n);
    ln->len += n;
}

void raw_delete_text(int y, int x, int n) {
    Line *ln = &editor.lines[y];
    memmove(&ln->text[x], &ln->text[x + n], ln->len - x - n + 1);
    ln->len -= n;
}

/* Splits line y at column x; the tail becomes line y+1. */
void raw_split(int y, int x) {
    insert_line_at(y + 1, editor.lines[y].text + x, editor.lines[y].len - x);
    Line *cur = &editor.lines[y];   /* re-fetch: array may have moved */
    cur->text[x] = '\0';
    cur->len = x;
}

/* Joins line y+1 onto the end of line y. */
void raw_join(int y) {
    Line *prev = &editor.lines[y];
    Line *cur = &editor.lines[y + 1];
    int prev_len = prev->len;

    line_reserve(prev, prev_len + cur->len + 1);
    memcpy(&prev->text[prev_len], cur->text, cur->len + 1);
    prev->len = prev_len + cur->len;

    delete_line_at(y + 1);
}

/* ---- Editing operations: change the text and record it for undo ---- */

/* Inserts one whole character (n UTF-8 bytes) at the cursor. Insert mode
   shifts the tail right; overwrite mode replaces the character under the
   cursor, except at end of line. */
void insert_utf8(const char *s, int n) {
    int y = editor.cursor_y, x = editor.cursor_x;
    Line *ln = &editor.lines[y];

    if (editor.insert_mode || x >= ln->len) {
        raw_insert_text(y, x, s, n);
        undo_record_insert(y, x, s, n, x, y);
    } else {
        int end = utf8_next(ln->text, ln->len, x);
        int old_n = end - x;
        undo_record_replace(y, x, ln->text + x, old_n, s, n, x, y);   /* copies both */
        raw_delete_text(y, x, old_n);
        raw_insert_text(y, x, s, n);
    }
    editor.cursor_x += n;
    editor.modified = 1;
}

/* Brace help for one typed byte with no selection: steps over a closer already
   there, or auto-closes the pair. Returns 1 if it handled the key. */
int brace_type_char(int ch) {
    if (!options.brace) return 0;
    int a, b, c, d;
    if (sel_range(&a, &b, &c, &d)) return 0;   /* a real selection: replace it instead */
    sel_clear();                                /* drop an empty, still-active selection */

    Line *ln = &editor.lines[editor.cursor_y];
    int x = editor.cursor_x;
    if (brace_is_skip(ln->text, ln->len, x, ch)) {
        move_cursor(1, 0);   /* step over the closer already there */
        return 1;
    }
    if (brace_should_autoclose(ln->text, ln->len, x, ch)) {
        char opener = (char)ch, closer = (char)brace_closer(ch);
        /* Insert both halves even in overwrite mode, so nothing after the cursor is lost */
        int saved_mode = editor.insert_mode;
        editor.insert_mode = 1;
        undo_begin_group();
        insert_utf8(&opener, 1);
        insert_utf8(&closer, 1);
        editor.cursor_x = x + 1;   /* between the pair */
        undo_end_group();
        editor.insert_mode = saved_mode;
        return 1;
    }
    return 0;
}

/* Backspace on an empty brace pair removes both halves as one undo step.
   Returns 1 if it handled the key. */
int brace_backspace(void) {
    if (!options.brace) return 0;
    Line *ln = &editor.lines[editor.cursor_y];
    if (!brace_pair_between(ln->text, ln->len, editor.cursor_x)) return 0;
    undo_begin_group();
    backspace();
    delete_char();
    undo_end_group();
    return 1;
}

void delete_char() {
    int y = editor.cursor_y, x = editor.cursor_x;
    Line *ln = &editor.lines[y];

    if (x < ln->len) {
        int n = utf8_next(ln->text, ln->len, x) - x;
        undo_record_delete(y, x, ln->text + x, n, x, y);
        raw_delete_text(y, x, n);
        editor.modified = 1;
    }
}

void backspace() {
    int y = editor.cursor_y, x = editor.cursor_x;

    if (x > 0) {
        Line *ln = &editor.lines[y];
        int nx;
        int all_spaces = 1;
        for (int i = 0; i < x; i++) {
            if (ln->text[i] != ' ') { all_spaces = 0; break; }
        }
        if (all_spaces) {
            /* Cursor inside leading spaces: delete back to the previous tab stop */
            nx = ((x - 1) / options.tab_width) * options.tab_width;
        } else {
            nx = utf8_prev(ln->text, ln->len, x);
        }
        int n = x - nx;
        undo_record_delete(y, nx, ln->text + nx, n, x, y);
        raw_delete_text(y, nx, n);
        editor.cursor_x = nx;
        editor.modified = 1;
    } else if (y > 0) {
        /* Join this line onto the end of the previous one */
        int prev_len = editor.lines[y - 1].len;
        raw_join(y - 1);
        undo_record_join(y - 1, prev_len, x, y);

        editor.cursor_y = y - 1;
        editor.cursor_x = prev_len;
        editor.modified = 1;
    }
}

/* Splits the line at the cursor and copies the indent down. */
static void newline_split(void) {
    int y = editor.cursor_y, x = editor.cursor_x;

    /* Leading spaces/tabs of this line, never past the cursor */
    int indent = 0;
    const char *t = editor.lines[y].text;
    while (indent < x && (t[indent] == ' ' || t[indent] == '\t')) indent++;

    raw_split(y, x);
    undo_record_split(y, x, x, y);

    editor.cursor_y++;
    editor.cursor_x = 0;
    if (indent > 0) {
        /* The split kept text[0..indent) on line y, so it still holds the indent */
        raw_insert_text(y + 1, 0, editor.lines[y].text, indent);
        undo_record_insert(y + 1, 0, editor.lines[y].text, indent, 0, y + 1);
        editor.cursor_x = indent;
    }
    editor.modified = 1;
}

void insert_newline() {
    int y = editor.cursor_y, x = editor.cursor_x;
    const Line *ln = &editor.lines[y];

    if (options.brace && brace_curly_between(ln->text, ln->len, x)) {
        /* Between an empty pair, e.g. {|}: put the closer on its own line
           and the cursor on an indented line in between. */
        int indent = 0;
        while (indent < x && (ln->text[indent] == ' ' || ln->text[indent] == '\t')) indent++;

        newline_split();   /* "{" / "}" */
        newline_split();   /* "{" / "" / "}" with the indent carried down */
        editor.cursor_y = y + 1;
        editor.cursor_x = indent;
        for (int i = 0; i < options.tab_width; i++) insert_utf8(" ", 1);
        return;
    }
    newline_split();
}

/* ---- Multi-line range edits ---- */

/* Deletes n chars at (y, x), recording them for undo. */
static void del_text(int y, int x, int n, int cx, int cy) {
    if (n <= 0) return;
    undo_record_delete(y, x, editor.lines[y].text + x, n, cx, cy);
    raw_delete_text(y, x, n);
}

/* Joins line y+1 onto line y, recording it for undo. */
static void join_lines(int y, int cx, int cy) {
    undo_record_join(y, editor.lines[y].len, cx, cy);
    raw_join(y);
}

void delete_range(int y1, int x1, int y2, int x2) {
    int cx = editor.cursor_x, cy = editor.cursor_y;

    if (y1 == y2) {
        del_text(y1, x1, x2 - x1, cx, cy);
    } else {
        /* Tail of the first line, then each middle line emptied and joined up,
           then the head of the last line and a final join. */
        del_text(y1, x1, editor.lines[y1].len - x1, cx, cy);
        for (int k = y1 + 1; k < y2; k++) {
            del_text(y1 + 1, 0, editor.lines[y1 + 1].len, cx, cy);
            join_lines(y1, cx, cy);
        }
        del_text(y1 + 1, 0, x2, cx, cy);
        join_lines(y1, cx, cy);
    }

    editor.cursor_x = x1;
    editor.cursor_y = y1;
    editor.modified = 1;
}

void insert_text(const char *s, int n) {
    int cx = editor.cursor_x, cy = editor.cursor_y;
    int y = cy, x = cx;
    int i = 0;

    while (i < n) {
        if (s[i] == '\n') {
            raw_split(y, x);
            undo_record_split(y, x, cx, cy);
            y++;
            x = 0;
            i++;
            continue;
        }
        int start = i;
        while (i < n && s[i] != '\n') i++;
        int k = i - start;
        raw_insert_text(y, x, s + start, k);
        undo_record_insert(y, x, s + start, k, cx, cy);
        x += k;
    }

    editor.cursor_x = x;
    editor.cursor_y = y;
    editor.modified = 1;
}
