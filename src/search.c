/* Search: Find/Find Next (case-insensitive, wraps), Replace (Y/N/A, from cursor to end), Go to Line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "undo.h"
#include "clipboard.h"
#include "search.h"
#include "utf8.h"

static char last_find[256] = "";   /* remembered for Find Next (F3) */

/* Finds the first match at or after (sy, sx). If wrap is set, the search
   continues from the top of the document; otherwise it stops at the end.
   Returns 1 and sets the match span (*fy, *fx) to (*fe) on a hit. Matches start
   on character boundaries and compare case-folded (Unicode-aware). */
static int find_from(int sy, int sx, int wrap, int *fy, int *fx, int *fe) {
    int plen = (int)strlen(last_find);
    int lines = editor.line_count;
    if (plen == 0 || lines == 0) return 0;

    int visits = wrap ? lines + 1 : lines - sy;   /* wrap revisits the start line once */
    for (int k = 0; k < visits; k++) {
        int y = (sy + k) % lines;
        int x0 = (k == 0) ? sx : 0;
        const Line *ln = &editor.lines[y];
        for (int x = x0; x < ln->len; x = utf8_next(ln->text, ln->len, x)) {
            int end = utf8_match_fold(ln->text + x, ln->len - x, last_find, plen);
            if (end >= 0) {
                *fy = y;
                *fx = x;
                *fe = x + end;
                return 1;
            }
        }
    }
    return 0;
}

/* Selects a match: anchor at its start, cursor at its end (drawn in reverse video). */
static void select_match(int y, int x, int end) {
    editor.sel_active = 1;
    editor.sel_block = 0;
    editor.sel_y = y;
    editor.sel_x = x;
    editor.cursor_y = y;
    editor.cursor_x = end;
    undo_break_group();
    adjust_scroll();
}

/* Redraws with the current match shown, then asks Y/N/A. Returns 'y', 'n', 'a' or 27 (Esc). */
static int ask_replace(void) {
    refresh_screen();
    gotoxy(0, screen_h - 2);
    printf("%-*s", screen_w - 1, "Replace? (Y)es  (N)o  (A)ll  (Esc)");
    fflush(stdout);

    for (;;) {
        int c = getch_cp();
        if (c == -1 || c == 27) return 27;
        if (c == 'y' || c == 'Y') return 'y';
        if (c == 'n' || c == 'N') return 'n';
        if (c == 'a' || c == 'A') return 'a';
    }
}

void search_find(void) {
    char buf[256];
    sel_clear();
    if (!prompt_line("Find: ", buf, sizeof buf)) return;
    strcpy(last_find, buf);
    search_next();
}

void search_next(void) {
    if (last_find[0] == '\0') {
        search_find();
        return;
    }

    sel_clear();
    int fy, fx, fe;
    if (find_from(editor.cursor_y, editor.cursor_x, 1, &fy, &fx, &fe)) {
        select_match(fy, fx, fe);
    } else {
        show_message("Not found.");
    }
}

void search_replace(void) {
    char find[256], repl[256];
    sel_clear();
    if (!prompt_line("Find: ", find, sizeof find)) return;
    if (!prompt_line_opt("Replace with: ", repl, sizeof repl)) return;
    strcpy(last_find, find);

    int rlen = (int)strlen(repl);
    int count = 0, all = 0;
    int y = editor.cursor_y, x = editor.cursor_x;
    int fy, fx, fe;

    /* The whole run is one undo step. */
    undo_begin_group();
    while (find_from(y, x, 0, &fy, &fx, &fe)) {
        select_match(fy, fx, fe);

        int replace_it = all;
        if (!all) {
            int key = ask_replace();
            if (key == 27) break;
            if (key == 'a') all = 1;
            replace_it = (key == 'y' || key == 'a');
        }

        if (replace_it) {
            sel_clear();
            editor.cursor_y = fy;
            editor.cursor_x = fx;
            delete_range(fy, fx, fy, fe);
            insert_text(repl, rlen);   /* inserts at the cursor, which is now fx */
            count++;
            y = editor.cursor_y;
            x = editor.cursor_x;       /* continue after the inserted text */
        } else {
            y = fy;
            x = fe;
        }
    }
    undo_end_group();
    sel_clear();

    if (count == 0) {
        show_message("Not found.");
    } else {
        char msg[64];
        snprintf(msg, sizeof msg, "Replaced %d.", count);
        show_message(msg);
    }
}

void search_goto_line(void) {
    char buf[32];
    sel_clear();
    if (!prompt_line("Go to line: ", buf, sizeof buf)) return;

    char *end;
    long n = strtol(buf, &end, 10);
    if (*end != '\0') {
        show_message("Not a line number.");
        return;
    }
    if (n < 1) n = 1;
    if (n > editor.line_count) n = editor.line_count;

    editor.cursor_y = (int)n - 1;
    editor.cursor_x = 0;
    move_cursor(0, 0);   /* clamps the cursor and scrolls it into view */
}
