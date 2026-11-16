/* Small multi-line text buffer for the git panel's commit message box. Pure, no I/O. */
#include <stdlib.h>
#include <string.h>
#include "msgedit.h"
#include "utf8.h"
#include "clipcodec.h"

void msgedit_clear(MsgEdit *me) {
    me->text[0] = '\0';
    me->len = 0;
    me->cur = 0;
    me->goal = -1;
}

void msgedit_set(MsgEdit *me, const char *s) {
    size_t n = strlen(s);
    if (n > MSGEDIT_MAX - 1) n = MSGEDIT_MAX - 1;
    memcpy(me->text, s, n);
    me->text[n] = '\0';
    me->len = (int)n;
    me->cur = me->len;
    me->goal = -1;
}

/* Inserts n bytes at the cursor. Returns 0 (no change) when they do not fit. */
static int insert_bytes(MsgEdit *me, const char *s, int n) {
    if (n <= 0) return 1;
    if (me->len + n > MSGEDIT_MAX - 1) return 0;
    memmove(me->text + me->cur + n, me->text + me->cur, (size_t)(me->len - me->cur));
    memcpy(me->text + me->cur, s, (size_t)n);
    me->len += n;
    me->cur += n;
    me->text[me->len] = '\0';
    me->goal = -1;
    return 1;
}

int msgedit_insert_cp(MsgEdit *me, int cp) {
    if (cp == '\n') return insert_bytes(me, "\n", 1);
    if (cp < 32 || cp == 127) return 0;
    if (cp >= 0x80 && cp < 0xA0) return 0;
    if (cp > 0x10FFFF) return 0;
    char enc[4];
    int n = utf8_encode(cp, enc);
    if (n <= 0) return 0;
    return insert_bytes(me, enc, n);
}

int msgedit_paste(MsgEdit *me, const char *text, int n) {
    char *copy = malloc((size_t)n + 1);
    if (!copy) return 0;
    memcpy(copy, text, (size_t)n);
    copy[n] = '\0';
    int clean = clip_clean_paste(copy, n);

    int added = 0;
    for (int i = 0; i < clean; ) {
        int cp;
        int w = utf8_decode(copy + i, clean - i, &cp);
        if (w <= 0) w = 1;
        if (cp >= 0 && msgedit_insert_cp(me, cp)) added++;   /* invalid bytes (cp < 0) are skipped */
        i += w;
    }
    free(copy);
    return added;
}

void msgedit_backspace(MsgEdit *me) {
    if (me->cur <= 0) return;
    int p = utf8_prev(me->text, me->len, me->cur);
    int k = me->cur - p;
    memmove(me->text + p, me->text + me->cur, (size_t)(me->len - me->cur));
    me->len -= k;
    me->cur = p;
    me->text[me->len] = '\0';
    me->goal = -1;
}

void msgedit_delete(MsgEdit *me) {
    if (me->cur >= me->len) return;
    int nx = utf8_next(me->text, me->len, me->cur);
    int k = nx - me->cur;
    memmove(me->text + me->cur, me->text + nx, (size_t)(me->len - nx));
    me->len -= k;
    me->text[me->len] = '\0';
    me->goal = -1;
}

void msgedit_left(MsgEdit *me) {
    me->cur = utf8_prev(me->text, me->len, me->cur);
    me->goal = -1;
}

void msgedit_right(MsgEdit *me) {
    me->cur = utf8_next(me->text, me->len, me->cur);
    me->goal = -1;
}

/* Start and end (exclusive, before the newline) of the line that holds byte pos. */
static void line_bounds(const MsgEdit *me, int pos, int *start, int *end) {
    int s = pos;
    while (s > 0 && me->text[s - 1] != '\n') s--;
    int e = pos;
    while (e < me->len && me->text[e] != '\n') e++;
    *start = s;
    *end = e;
}

void msgedit_home(MsgEdit *me) {
    int s, e;
    line_bounds(me, me->cur, &s, &e);
    me->cur = s;
    me->goal = -1;
}

void msgedit_end(MsgEdit *me) {
    int s, e;
    line_bounds(me, me->cur, &s, &e);
    me->cur = e;
    me->goal = -1;
}

void msgedit_up(MsgEdit *me) {
    int s, e;
    line_bounds(me, me->cur, &s, &e);
    if (s == 0) return;   /* already on the first line */
    if (me->goal < 0) me->goal = utf8_col(me->text + s, me->cur - s, me->cur - s);
    int ps, pe;
    line_bounds(me, s - 1, &ps, &pe);
    me->cur = ps + utf8_byte_at_col(me->text + ps, pe - ps, me->goal);
}

void msgedit_down(MsgEdit *me) {
    int s, e;
    line_bounds(me, me->cur, &s, &e);
    if (e >= me->len) return;   /* already on the last line */
    if (me->goal < 0) me->goal = utf8_col(me->text + s, me->cur - s, me->cur - s);
    int ns = e + 1, ne;
    int dummy;
    line_bounds(me, ns, &dummy, &ne);
    me->cur = ns + utf8_byte_at_col(me->text + ns, ne - ns, me->goal);
}

int msgedit_nlines(const MsgEdit *me) {
    int n = 1;
    for (int i = 0; i < me->len; i++) if (me->text[i] == '\n') n++;
    return n;
}

int msgedit_line(const MsgEdit *me, int n, int *start, int *len) {
    if (n < 0) return 0;
    int line = 0, s = 0;
    for (int i = 0; i <= me->len; i++) {
        if (i == me->len || me->text[i] == '\n') {
            if (line == n) {
                *start = s;
                *len = i - s;
                return 1;
            }
            line++;
            s = i + 1;
        }
    }
    return 0;
}

void msgedit_cursor(const MsgEdit *me, int *line, int *col) {
    int l = 0, s = 0;
    for (int i = 0; i < me->cur; i++) {
        if (me->text[i] == '\n') { l++; s = i + 1; }
    }
    *line = l;
    *col = utf8_col(me->text + s, me->cur - s, me->cur - s);
}
