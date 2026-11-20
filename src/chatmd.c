/* chatmd.c: word wrapping and Markdown rendering for the AI chat pane. */
#include <stdlib.h>
#include <string.h>

#include "chatmd.h"
#include "utf8.h"

/* ---- Wrapping ---- */

int chatmd_row(const char *t, int len, int pos, int cw, int word, int *end) {
    if (cw < 1) cw = 1;
    int col = 0, p = pos, brk = -1, seen_text = 0;
    while (p < len) {
        int cp;
        utf8_decode(t + p, len - p, &cp);
        int w = utf8_cell_w(cp, col);
        if (col > 0 && col + w > cw) {
            int e, nx;
            if (t[p] == ' ') {
                e = p;
                nx = p;
            } else if (word && brk >= 0) {
                e = brk;
                nx = brk;
            } else {
                *end = p;
                return p;
            }
            while (nx < len && t[nx] == ' ') nx++;
            *end = e;
            return nx;
        }
        if (t[p] == ' ') {
            if (seen_text && (p == pos || t[p - 1] != ' ')) brk = p;
        } else {
            seen_text = 1;
        }
        col += w;
        p = utf8_next(t, len, p);
    }
    *end = len;
    return len;
}

/* ---- Markdown ---- */

typedef struct {
    char *t;
    unsigned char *s;
    int n, cap;
    int fail;
} Out;

static void put(Out *o, const char *p, int len, int style) {
    if (o->fail) return;
    if (o->n + len + 1 > o->cap) {
        int nc = o->cap ? o->cap * 2 : 256;
        while (nc < o->n + len + 1) nc *= 2;
        char *nt = realloc(o->t, nc);
        if (!nt) { o->fail = 1; return; }
        o->t = nt;
        unsigned char *ns = realloc(o->s, nc);
        if (!ns) { o->fail = 1; return; }
        o->s = ns;
        o->cap = nc;
    }
    memcpy(o->t + o->n, p, len);
    memset(o->s + o->n, style, len);
    o->n += len;
}

static int is_alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Index in [from, e) of the closing marker m (1 or 2 chars of c), or -1. */
static int find_close(const char *l, int from, int e, char c, int m) {
    for (int i = from; i + m <= e; i++) {
        if (l[i] == '\\') { i++; continue; }
        if (l[i] != c) continue;
        if (m == 2) {
            if (l[i + 1] == c) return i;
        } else if (i + 1 >= e || l[i + 1] != c) {
            return i;
        } else {
            i++;   /* skip a doubled marker inside single-marker text */
        }
    }
    return -1;
}

static void inline_md(Out *o, const char *l, int s, int e, int base) {
    int i = s;
    while (i < e) {
        char c = l[i];
        if (c == '\\' && i + 1 < e && strchr("\\`*_[]#>-", l[i + 1])) {
            put(o, l + i + 1, 1, base);
            i += 2;
        } else if (c == '`') {
            int j = find_close(l, i + 1, e, '`', 1);
            if (j > i + 1) {
                put(o, l + i + 1, j - i - 1, base | CM_CODE);
                i = j + 1;
            } else {
                put(o, l + i, 1, base);
                i++;
            }
        } else if ((c == '*' || c == '_') && i + 1 < e && l[i + 1] == c && i + 2 < e && l[i + 2] != ' ' &&
                   !(c == '_' && i > s && is_alnum(l[i - 1]))) {
            int j = find_close(l, i + 2, e, c, 2);
            if (j > i + 2 && l[j - 1] != ' ' && !(c == '_' && j + 2 < e && is_alnum(l[j + 2]))) {
                inline_md(o, l, i + 2, j, base | CM_BOLD);
                i = j + 2;
            } else {
                put(o, l + i, 2, base);
                i += 2;
            }
        } else if ((c == '*' || c == '_') && i + 1 < e && l[i + 1] != ' ' && l[i + 1] != c &&
                   !(c == '_' && i > s && is_alnum(l[i - 1]))) {
            int j = find_close(l, i + 1, e, c, 1);
            if (j > i + 1 && l[j - 1] != ' ' && !(c == '_' && j + 1 < e && is_alnum(l[j + 1]))) {
                inline_md(o, l, i + 1, j, base | CM_ITALIC);
                i = j + 1;
            } else {
                put(o, l + i, 1, base);
                i++;
            }
        } else if (c == '[') {
            int mid = -1, close = -1;
            {
                int k = i + 1;
                while (k < e && l[k] != ']') k++;
                if (k + 1 < e && l[k + 1] == '(') mid = k;
            }
            if (mid > 0)
                for (int k = mid + 2; k < e; k++)
                    if (l[k] == ')') { close = k; break; }
            if (mid > i + 1 && close > mid + 2) {
                inline_md(o, l, i + 1, mid, base);
                put(o, " (", 2, base | CM_DIM);
                put(o, l + mid + 2, close - mid - 2, base | CM_DIM);
                put(o, ")", 1, base | CM_DIM);
                i = close + 1;
            } else {
                put(o, l + i, 1, base);
                i++;
            }
        } else {
            put(o, l + i, 1, base);
            i++;
        }
    }
}

/* True when l[s..e) (after trimming) is 3+ of one of - * _ and nothing else. */
static int is_rule(const char *l, int s, int e) {
    while (s < e && l[s] == ' ') s++;
    while (e > s && l[e - 1] == ' ') e--;
    if (e - s < 3 || (l[s] != '-' && l[s] != '*' && l[s] != '_')) return 0;
    for (int i = s; i < e; i++)
        if (l[i] != l[s]) return 0;
    return 1;
}

int chatmd_render(const char *md, char **text, unsigned char **style) {
    Out o = {0};
    int in_code = 0, pos = 0, len = (int)strlen(md);
    while (pos <= len) {
        const char *nl = memchr(md + pos, '\n', len - pos);
        int e = nl ? (int)(nl - md) : len;
        const char *l = md;
        int s = pos, drop = 0;
        int ts = s;
        while (ts < e && l[ts] == ' ') ts++;
        if (e - ts >= 3 && l[ts] == '`' && l[ts + 1] == '`' && l[ts + 2] == '`') {
            in_code = !in_code;
            drop = 1;
        } else if (in_code) {
            put(&o, l + s, e - s, CM_CODE);
        } else if (is_rule(l, s, e)) {
            for (int k = 0; k < 20; k++) put(&o, "\xe2\x94\x80", 3, CM_DIM);
        } else if (l[ts] == '#' && e > ts) {
            int h = ts;
            while (h < e && l[h] == '#') h++;
            if (h - ts <= 6 && h < e && l[h] == ' ') {
                while (h < e && l[h] == ' ') h++;
                inline_md(&o, l, h, e, CM_HEAD | CM_BOLD);
            } else {
                inline_md(&o, l, s, e, 0);
            }
        } else if (ts + 1 < e && l[ts] == '>' && l[ts + 1] == ' ') {
            put(&o, "\xe2\x94\x82 ", 4, CM_DIM);
            inline_md(&o, l, ts + 2, e, 0);
        } else if (ts + 1 < e && strchr("-*+", l[ts]) && l[ts + 1] == ' ') {
            put(&o, l + s, ts - s, 0);
            put(&o, "\xe2\x80\xa2", 3, 0);
            inline_md(&o, l, ts + 1, e, 0);
        } else {
            inline_md(&o, l, s, e, 0);
        }
        if (nl && !drop) put(&o, "\n", 1, 0);
        if (!nl) break;
        pos = e + 1;
    }
    if (o.fail) {
        free(o.t);
        free(o.s);
        return 0;
    }
    if (!o.t) {
        o.t = malloc(1);
        o.s = malloc(1);
        if (!o.t || !o.s) { free(o.t); free(o.s); return 0; }
    }
    o.t[o.n] = '\0';
    *text = o.t;
    *style = o.s;
    return 1;
}
