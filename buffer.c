/* Document buffer: line storage, editing operations and cursor/scroll state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"

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

void insert_char(char ch) {
    Line *ln = &editor.lines[editor.cursor_y];
    line_reserve(ln, ln->len + 2);

    /* Insert mode shifts the tail right; overwrite mode replaces, except at end of line */
    if (editor.insert_mode || editor.cursor_x >= ln->len) {
        memmove(&ln->text[editor.cursor_x + 1], &ln->text[editor.cursor_x],
                ln->len - editor.cursor_x + 1);
        ln->len++;
    }

    ln->text[editor.cursor_x] = ch;
    editor.cursor_x++;
    editor.modified = 1;
}

void delete_char() {
    Line *ln = &editor.lines[editor.cursor_y];

    if (editor.cursor_x < ln->len) {
        memmove(&ln->text[editor.cursor_x], &ln->text[editor.cursor_x + 1],
                ln->len - editor.cursor_x);
        ln->len--;
        editor.modified = 1;
    }
}

void backspace() {
    if (editor.cursor_x > 0) {
        editor.cursor_x--;
        delete_char();
    } else if (editor.cursor_y > 0) {
        /* Join this line onto the end of the previous one */
        Line *prev = &editor.lines[editor.cursor_y - 1];
        Line *cur = &editor.lines[editor.cursor_y];
        int prev_len = prev->len;

        line_reserve(prev, prev_len + cur->len + 1);
        memcpy(&prev->text[prev_len], cur->text, cur->len + 1);
        prev->len = prev_len + cur->len;

        delete_line_at(editor.cursor_y);

        editor.cursor_y--;
        editor.cursor_x = prev_len;
        editor.modified = 1;
    }
}

void insert_newline() {
    Line *cur = &editor.lines[editor.cursor_y];
    int tail_len = cur->len - editor.cursor_x;

    /* Copies the tail into a new line. The tail pointer targets the text
       buffer, which stays valid even if the lines array is reallocated. */
    insert_line_at(editor.cursor_y + 1, cur->text + editor.cursor_x, tail_len);

    cur = &editor.lines[editor.cursor_y];   /* re-fetch: array may have moved */
    cur->text[editor.cursor_x] = '\0';
    cur->len = editor.cursor_x;

    editor.cursor_y++;
    editor.cursor_x = 0;
    editor.modified = 1;
}
