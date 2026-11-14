/* Document buffer: line storage, editing operations and cursor/scroll state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "undo.h"

Editor editor;

/* ---- Growable line storage: no fixed limit on lines or line length ---- */

static void die_oom(void) {
    fprintf(stderr, "edit: out of memory\n");
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

void adjust_scroll() {
    if (editor.cursor_x < editor.scroll_x) {
        editor.scroll_x = editor.cursor_x;
    } else if (editor.cursor_x >= editor.scroll_x + SCREEN_WIDTH) {
        editor.scroll_x = editor.cursor_x - SCREEN_WIDTH + 1;
    }

    if (editor.cursor_y < editor.scroll_y) {
        editor.scroll_y = editor.cursor_y;
    } else if (editor.cursor_y >= editor.scroll_y + SCREEN_HEIGHT - 2) {
        editor.scroll_y = editor.cursor_y - SCREEN_HEIGHT + 3;
    }
}

void move_cursor(int dx, int dy) {
    undo_break_group();   /* moving the cursor ends the current typing step */

    if (dy != 0) {
        editor.cursor_y += dy;
        if (editor.cursor_y < 0) editor.cursor_y = 0;
        if (editor.cursor_y >= editor.line_count) editor.cursor_y = editor.line_count - 1;

        int line_len = editor.lines[editor.cursor_y].len;
        if (editor.cursor_x > line_len) {
            editor.cursor_x = line_len;
        }
    }

    if (dx != 0) {
        editor.cursor_x += dx;
        if (editor.cursor_x < 0) editor.cursor_x = 0;

        int line_len = editor.lines[editor.cursor_y].len;
        if (editor.cursor_x > line_len) {
            editor.cursor_x = line_len;
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

void insert_char(char ch) {
    int y = editor.cursor_y, x = editor.cursor_x;
    Line *ln = &editor.lines[y];

    /* Insert mode shifts the tail right; overwrite mode replaces, except at end of line */
    if (editor.insert_mode || x >= ln->len) {
        raw_insert_text(y, x, &ch, 1);
        undo_record_insert(y, x, &ch, 1, x, y);
    } else {
        char old = ln->text[x];
        ln->text[x] = ch;
        undo_record_replace(y, x, old, ch, x, y);
    }
    editor.cursor_x++;
    editor.modified = 1;
}

void delete_char() {
    int y = editor.cursor_y, x = editor.cursor_x;
    Line *ln = &editor.lines[y];

    if (x < ln->len) {
        char c = ln->text[x];
        raw_delete_text(y, x, 1);
        undo_record_delete(y, x, &c, 1, x, y);
        editor.modified = 1;
    }
}

void backspace() {
    int y = editor.cursor_y, x = editor.cursor_x;

    if (x > 0) {
        int nx = x - 1;
        char c = editor.lines[y].text[nx];
        raw_delete_text(y, nx, 1);
        undo_record_delete(y, nx, &c, 1, x, y);
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

void insert_newline() {
    int y = editor.cursor_y, x = editor.cursor_x;

    raw_split(y, x);
    undo_record_split(y, x, x, y);

    editor.cursor_y++;
    editor.cursor_x = 0;
    editor.modified = 1;
}
