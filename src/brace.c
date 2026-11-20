/* Bracket helpers for auto-closing and matching-bracket highlight. Pure, no I/O. */
#include <ctype.h>

#include "brace.h"

/* Lines searched each way for a partner, so a stray bracket can't stall a redraw */
#define BRACE_MAX_LINES 5000

static int is_opener(int ch) { return ch == '(' || ch == '[' || ch == '{'; }
static int is_closer(int ch) { return ch == ')' || ch == ']' || ch == '}'; }
static int is_quote(int ch) { return ch == '"' || ch == '\''; }
static int is_bracket(int ch) { return is_opener(ch) || is_closer(ch); }

int brace_closer(int ch) {
    switch (ch) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '"': return '"';
        case '\'': return '\'';
    }
    return 0;
}

int brace_should_autoclose(const char *line, int len, int x, int ch) {
    if (!brace_closer(ch)) return 0;
    if (x < 0 || x > len) return 0;

    /* Only before whitespace, a closer or end of line */
    if (x < len) {
        int nx = (unsigned char)line[x];
        if (!(nx == ' ' || nx == '\t' || is_closer(nx))) return 0;
    }

    /* Quotes: not inside a word (don't), not after a backslash or the same quote */
    if (is_quote(ch) && x > 0) {
        int pv = (unsigned char)line[x - 1];
        if (isalnum(pv) || pv == '_' || pv == '\\' || pv == ch) return 0;
    }
    return 1;
}

int brace_is_skip(const char *line, int len, int x, int ch) {
    if (!(is_closer(ch) || is_quote(ch))) return 0;
    if (x < 0 || x >= len) return 0;
    return (unsigned char)line[x] == ch;
}

int brace_pair_between(const char *line, int len, int x) {
    if (x <= 0 || x >= len) return 0;
    int open = (unsigned char)line[x - 1];
    int want = brace_closer(open);
    return want != 0 && (unsigned char)line[x] == want;
}

int brace_curly_between(const char *line, int len, int x) {
    if (x <= 0 || x >= len) return 0;
    return line[x - 1] == '{' && line[x] == '}';
}

int brace_find_match(const Line *lines, int count, int y, int x, int *my, int *mx) {
    if (y < 0 || y >= count || x < 0 || x >= lines[y].len) return 0;
    int ch = (unsigned char)lines[y].text[x];
    if (!is_bracket(ch)) return 0;

    int depth = 1;
    if (is_opener(ch)) {
        int other = brace_closer(ch);
        for (int ly = y; ly < count && ly - y <= BRACE_MAX_LINES; ly++) {
            const Line *ln = &lines[ly];
            for (int i = (ly == y) ? x + 1 : 0; i < ln->len; i++) {
                int c = (unsigned char)ln->text[i];
                if (c == ch) depth++;
                else if (c == other && --depth == 0) { *my = ly; *mx = i; return 1; }
            }
        }
    } else {
        int other = 0;
        if (ch == ')') other = '(';
        else if (ch == ']') other = '[';
        else other = '{';
        for (int ly = y; ly >= 0 && y - ly <= BRACE_MAX_LINES; ly--) {
            const Line *ln = &lines[ly];
            for (int i = (ly == y) ? x - 1 : ln->len - 1; i >= 0; i--) {
                int c = (unsigned char)ln->text[i];
                if (c == ch) depth++;
                else if (c == other && --depth == 0) { *my = ly; *mx = i; return 1; }
            }
        }
    }
    return 0;
}

int brace_match_for_cursor(const Line *lines, int count, int y, int x,
                           int *ay, int *ax, int *by, int *bx) {
    if (y < 0 || y >= count) return 0;
    const Line *ln = &lines[y];
    int at = -1;
    if (x < ln->len && is_bracket((unsigned char)ln->text[x])) at = x;
    else if (x > 0 && x - 1 < ln->len && is_bracket((unsigned char)ln->text[x - 1])) at = x - 1;
    if (at < 0) return 0;

    *ay = y;
    *ax = at;
    return brace_find_match(lines, count, y, at, by, bx);
}
