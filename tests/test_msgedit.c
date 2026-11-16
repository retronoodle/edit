/* Unit tests for msgedit.c. Build and run with: make test */
#include <stdio.h>
#include <string.h>
#include "msgedit.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

static int eq(const MsgEdit *me, const char *s) {
    return strcmp(me->text, s) == 0;
}

int main(void) {
    MsgEdit me;

    /* Typing and newlines */
    msgedit_clear(&me);
    msgedit_insert_cp(&me, 'a');
    msgedit_insert_cp(&me, 'b');
    msgedit_insert_cp(&me, '\n');
    msgedit_insert_cp(&me, 'c');
    CHECK(eq(&me, "ab\nc") && me.cur == 4, "typing and newline");
    CHECK(msgedit_nlines(&me) == 2, "two lines");

    /* Control codes and tabs are refused */
    int before = me.len;
    CHECK(msgedit_insert_cp(&me, '\t') == 0 && me.len == before, "tab refused");
    CHECK(msgedit_insert_cp(&me, 0x1B) == 0 && me.len == before, "escape refused");
    CHECK(msgedit_insert_cp(&me, 127) == 0 && me.len == before, "DEL refused");

    /* Multibyte characters: é (2 bytes), 中 (3 bytes), 😀 (4 bytes) */
    msgedit_clear(&me);
    msgedit_insert_cp(&me, 0xE9);
    msgedit_insert_cp(&me, 0x4E2D);
    msgedit_insert_cp(&me, 0x1F600);
    CHECK(me.len == 2 + 3 + 4, "multibyte bytes stored");
    msgedit_left(&me);
    CHECK(me.cur == 2 + 3, "left steps over a 4-byte char");
    msgedit_backspace(&me);
    CHECK(eq(&me, "\xC3\xA9\xF0\x9F\x98\x80") && me.cur == 2, "backspace removes whole 3-byte char");
    msgedit_delete(&me);
    CHECK(eq(&me, "\xC3\xA9") && me.cur == 2, "delete removes the char under the cursor");

    /* Backspace and delete at the edges do nothing */
    msgedit_clear(&me);
    msgedit_backspace(&me);
    msgedit_delete(&me);
    CHECK(me.len == 0 && me.cur == 0, "edges are safe on empty buffer");

    /* Left/right cross line ends fine as bytes */
    msgedit_set(&me, "one\ntwo");
    msgedit_home(&me);
    CHECK(me.cur == 4, "home goes to start of second line");
    msgedit_end(&me);
    CHECK(me.cur == 7, "end goes to end of second line");
    msgedit_right(&me);
    CHECK(me.cur == 7, "right at end stays put");

    /* Up/down keep the display column, clamp on short lines */
    msgedit_set(&me, "abcdef\nxy\nabcdef");
    me.cur = 4;                                  /* line 0, column 4 */
    msgedit_down(&me);
    CHECK(me.cur == 7 + 2, "down onto short line clamps to its end");
    msgedit_down(&me);
    CHECK(me.cur == 10 + 4, "down again returns to column 4");
    msgedit_up(&me);
    CHECK(me.cur == 7 + 2, "up from line 2 lands on the short line, clamped");
    msgedit_up(&me);
    CHECK(me.cur == 4, "up keeps column 4 on the first line");
    msgedit_up(&me);
    CHECK(me.cur == 4, "up on the first line does nothing");

    /* Down on the last line does nothing */
    me.cur = me.len;
    msgedit_down(&me);
    CHECK(me.cur == me.len, "down on the last line does nothing");

    /* Column counts characters, not bytes: é is one column */
    msgedit_set(&me, "\xC3\xA9xy\nabcd");
    me.cur = 3;   /* after "éx": display column 2 */
    msgedit_down(&me);
    CHECK(me.cur == 5 + 2, "column is in display cells, so multibyte lines line up");

    /* Lines and cursor position */
    msgedit_set(&me, "one\n\nthree");
    CHECK(msgedit_nlines(&me) == 3, "blank middle line counts");
    int s, l;
    CHECK(msgedit_line(&me, 1, &s, &l) && s == 4 && l == 0, "blank line has zero length");
    CHECK(msgedit_line(&me, 2, &s, &l) && s == 5 && l == 5, "last line bounds");
    CHECK(msgedit_line(&me, 3, &s, &l) == 0, "line past the end is refused");
    me.cur = 6;
    int line, col;
    msgedit_cursor(&me, &line, &col);
    CHECK(line == 2 && col == 1, "cursor line and column");

    /* Paste: CRLF becomes newlines, tabs and controls are dropped */
    msgedit_clear(&me);
    int added = msgedit_paste(&me, "a\r\nb\tc\x01" "d\rz", 10);
    CHECK(eq(&me, "a\nbcd\nz") && added == 7, "paste cleaned (CRLF, tab, control dropped)");

    /* A full buffer stops inserting */
    msgedit_clear(&me);
    for (int i = 0; i < MSGEDIT_MAX - 2; i++) msgedit_insert_cp(&me, 'x');
    CHECK(me.len == MSGEDIT_MAX - 2, "filled to just under the limit");
    CHECK(msgedit_insert_cp(&me, 0xE9) == 0, "two-byte char refused when it does not fit");
    CHECK(msgedit_insert_cp(&me, 'y') == 1 && me.len == MSGEDIT_MAX - 1, "one byte still fits");
    CHECK(msgedit_insert_cp(&me, 'z') == 0, "full buffer refuses");

    printf("%s\n", failures ? "msgedit: FAILURES" : "msgedit: all passed");
    return failures ? 1 : 0;
}
