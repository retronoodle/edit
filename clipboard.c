/* Selection and internal clipboard. The clipboard holds plain text with '\n' between lines. */
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "undo.h"
#include "ui.h"
#include "clipboard.h"

static char *clip_text = NULL;
static int clip_len = 0;

void sel_extend(void) {
    if (!editor.sel_active) {
        editor.sel_active = 1;
        editor.sel_x = editor.cursor_x;
        editor.sel_y = editor.cursor_y;
    }
}

void sel_clear(void) {
    editor.sel_active = 0;
}

void sel_select_all(void) {
    editor.sel_active = 1;
    editor.sel_x = 0;
    editor.sel_y = 0;
    editor.cursor_y = editor.line_count - 1;
    editor.cursor_x = editor.lines[editor.cursor_y].len;
}

int sel_range(int *y1, int *x1, int *y2, int *x2) {
    if (!editor.sel_active) return 0;

    int ay = editor.sel_y, ax = editor.sel_x;
    int by = editor.cursor_y, bx = editor.cursor_x;
    if (ay == by && ax == bx) return 0;

    if (ay > by || (ay == by && ax > bx)) {
        int t = ay; ay = by; by = t;
        t = ax; ax = bx; bx = t;
    }
    *y1 = ay; *x1 = ax;
    *y2 = by; *x2 = bx;
    return 1;
}

void sel_delete_selection(void) {
    int y1, x1, y2, x2;
    if (sel_range(&y1, &x1, &y2, &x2)) {
        delete_range(y1, x1, y2, x2);
    }
    sel_clear();
}

int sel_begin_replace(void) {
    int y1, x1, y2, x2;
    if (!sel_range(&y1, &x1, &y2, &x2)) {
        sel_clear();
        return 0;
    }
    undo_begin_group();
    sel_delete_selection();
    return 1;
}

/* Copies the selected text into the internal clipboard. */
void clip_copy(void) {
    int y1, x1, y2, x2;
    if (!sel_range(&y1, &x1, &y2, &x2)) {
        show_message("No selection.");
        return;
    }

    int total = 0;
    for (int y = y1; y <= y2; y++) {
        int from = (y == y1) ? x1 : 0;
        int to = (y == y2) ? x2 : editor.lines[y].len;
        total += to - from;
        if (y < y2) total++;   /* the newline between lines */
    }

    char *buf = malloc(total + 1);
    if (!buf) return;

    int p = 0;
    for (int y = y1; y <= y2; y++) {
        int from = (y == y1) ? x1 : 0;
        int to = (y == y2) ? x2 : editor.lines[y].len;
        memcpy(buf + p, editor.lines[y].text + from, to - from);
        p += to - from;
        if (y < y2) buf[p++] = '\n';
    }
    buf[p] = '\0';

    free(clip_text);
    clip_text = buf;
    clip_len = total;
}

void clip_cut(void) {
    int y1, x1, y2, x2;
    if (!sel_range(&y1, &x1, &y2, &x2)) {
        show_message("No selection.");
        return;
    }
    clip_copy();
    undo_begin_group();
    sel_delete_selection();
    undo_end_group();
}

/* Pastes over any selection. The whole paste is one undo step. */
void clip_paste(void) {
    if (!clip_text || clip_len == 0) {
        show_message("Clipboard is empty.");
        return;
    }
    undo_begin_group();
    sel_delete_selection();
    insert_text(clip_text, clip_len);
    undo_end_group();
}
