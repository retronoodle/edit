/* Read-only scrollable text modal. Text is wrapped into rows once per screen size.
   view_diff() also tints added and removed lines and syntax-colors their code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "term.h"
#include "options.h"
#include "syntax.h"
#include "utf8.h"
#include "viewer.h"

/* What kind of diff line a row came from. */
enum { DL_PLAIN = 0, DL_ADD, DL_DEL, DL_HUNK, DL_META };

typedef struct {
    char *s;               /* malloc'd row text */
    int len;
    unsigned char cls;     /* DL_* */
    unsigned char *kinds;  /* malloc'd TK_* per byte, or NULL (all plain) */
} Row;

typedef struct {
    Row *rows;
    int n;
    int cap;
} Rows;

static void rows_free(Rows *r) {
    for (int i = 0; i < r->n; i++) {
        free(r->rows[i].s);
        free(r->rows[i].kinds);
    }
    free(r->rows);
    r->rows = NULL;
    r->n = r->cap = 0;
}

/* Adds a row. kinds (may be NULL) holds the token kind of each of its len bytes. */
static int rows_add(Rows *r, const char *s, int len, int cls, const unsigned char *kinds) {
    if (r->n == r->cap) {
        int nc = r->cap ? r->cap * 2 : 64;
        Row *p = realloc(r->rows, nc * sizeof *p);
        if (!p) return -1;
        r->rows = p;
        r->cap = nc;
    }
    Row *rw = &r->rows[r->n];
    rw->s = malloc(len + 1);
    if (!rw->s) return -1;
    memcpy(rw->s, s, len);
    rw->s[len] = '\0';
    rw->len = len;
    rw->cls = (unsigned char)cls;
    rw->kinds = NULL;
    if (kinds) {
        rw->kinds = malloc(len > 0 ? len : 1);
        if (!rw->kinds) {
            free(rw->s);
            return -1;
        }
        memcpy(rw->kinds, kinds, len);
    }
    r->n++;
    return 0;
}

/* Classes one sanitized line of a diff. in_hunk is 1 between an @@ header and the
   next file header; inside a hunk, "---" and "+++" are removed/added lines. */
static int diff_class(const char *line, int k, int *in_hunk) {
    if (strncmp(line, "diff --git ", 11) == 0) {
        *in_hunk = 0;
        return DL_META;
    }
    if (strncmp(line, "@@", 2) == 0) {
        *in_hunk = 1;
        return DL_HUNK;
    }
    if (!*in_hunk) {
        if (strncmp(line, "--- ", 4) == 0 || strncmp(line, "+++ ", 4) == 0 ||
            strncmp(line, "index ", 6) == 0 || strncmp(line, "new file", 8) == 0 ||
            strncmp(line, "deleted file", 12) == 0 || strncmp(line, "rename ", 7) == 0 ||
            strncmp(line, "similarity ", 11) == 0 || strncmp(line, "old mode", 8) == 0 ||
            strncmp(line, "new mode", 8) == 0 || strncmp(line, "Binary files", 12) == 0)
            return DL_META;
        return DL_PLAIN;
    }
    if (k == 0) return DL_PLAIN;   /* empty context line */
    switch (line[0]) {
        case '+':  return DL_ADD;
        case '-':  return DL_DEL;
        case '\\': return DL_META;   /* "\ No newline at end of file" */
        default:   return DL_PLAIN;  /* context line */
    }
}

/* Token kinds for a diff code line: the prefix byte is plain, the rest is scanned
   with the parser state from the previous line on that side. kinds has k + 1 bytes. */
static int scan_code(SynLang lang, int st, const char *line, int k, unsigned char *kinds) {
    kinds[0] = TK_PLAIN;
    if (k > 1) return syntax_scan_line(lang, st, line + 1, k - 1, kinds + 1);
    return st;
}

/* Splits text on '\n' and wraps each line at tw columns. Tabs become spaces and
   other control bytes are dropped, so the rows print cleanly. In diff mode each row
   also gets its class and, for code lines, its token kinds (scanned per logical
   line, so wrapping never splits a token's state). */
static void wrap_text(const char *text, int tw, Rows *out, SynLang lang, int diff) {
    const char *p = text;
    int in_hunk = 0;
    int st_old = SYN_ST_NONE, st_new = SYN_ST_NONE;
    while (1) {
        const char *nl = strchr(p, '\n');
        int plen = nl ? (int)(nl - p) : (int)strlen(p);

        char *line = malloc(plen + 1);
        if (!line) return;
        int k = 0;
        for (int i = 0; i < plen; i++) {
            unsigned char c = (unsigned char)p[i];
            if (c == '\t') line[k++] = ' ';
            else if (c >= 0x20 || c >= 0x80) line[k++] = (char)c;
        }
        line[k] = '\0';

        int cls = DL_PLAIN;
        unsigned char *kinds = NULL;
        if (diff) {
            cls = diff_class(line, k, &in_hunk);
            if (cls == DL_HUNK || (cls == DL_META && !in_hunk)) {
                st_old = st_new = SYN_ST_NONE;   /* a new hunk or file: state is unknown above */
            }
            if (lang != SYN_PLAIN && (cls == DL_ADD || cls == DL_DEL || (cls == DL_PLAIN && in_hunk))) {
                kinds = malloc(k + 1);
                if (!kinds) {
                    free(line);
                    return;
                }
                if (cls == DL_DEL) {
                    st_old = scan_code(lang, st_old, line, k, kinds);
                } else if (cls == DL_ADD) {
                    st_new = scan_code(lang, st_new, line, k, kinds);
                } else {   /* context: same text on both sides */
                    st_new = scan_code(lang, st_new, line, k, kinds);
                    st_old = st_new;
                }
            }
        }

        int rows = utf8_wrap_rows(line, k, tw);
        for (int r = 0; r < rows; r++) {
            int s, e, col;
            utf8_wrap_row(line, k, tw, r, &s, &e, &col);
            if (rows_add(out, line + s, e - s, cls, kinds ? kinds + s : NULL) != 0) break;
        }
        free(kinds);
        free(line);

        if (!nl) break;
        p = nl + 1;
    }
}

/* Background tints for added and removed lines, per scheme. Light has a pale
   background, so its tints are pale too. */
static const char *tint_sgr[4][2] = {
    { "\x1b[48;5;22m", "\x1b[48;5;52m" },
    { "\x1b[48;5;22m", "\x1b[48;5;52m" },
    { "\x1b[48;5;194m", "\x1b[48;5;224m" },
    { "\x1b[48;5;22m", "\x1b[48;5;52m" },
};

/* Foreground color for hunk headers, per scheme (added after the scheme's base SGR). */
static const char *hunk_sgr[4] = { "\x1b[36m", "\x1b[96m", "\x1b[38;5;30m", "\x1b[38;5;75m" };

/* Writes the SGR for a run of bytes: the token color (or the scheme's base), then
   the row's class on top of it (tint, header color, bold). */
static void run_sgr(char *out, size_t size, int cls, int kind) {
    int sch = options.scheme;
    if (kind != TK_PLAIN) snprintf(out, size, "%s", syntax_sgr(kind, sch));
    else snprintf(out, size, "%s", options_base_sgr());

    const char *extra = NULL;
    if (cls == DL_ADD) extra = tint_sgr[sch][0];
    else if (cls == DL_DEL) extra = tint_sgr[sch][1];
    else if (cls == DL_HUNK) extra = hunk_sgr[sch];
    else if (cls == DL_META) extra = "\x1b[1m";

    size_t n = strlen(out);
    if (extra && n < size) snprintf(out + n, size - n, "%s", extra);
}

/* Draws one full-width row, cut at a character boundary. */
static void put_row(int y, const char *sgr, const char *text) {
    int w = screen_w - 1;
    if (w < 0) w = 0;
    int n = text ? (int)strlen(text) : 0;
    if (n > w) {
        n = w;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    }
    fb_gotoxy(0, y);
    fb_puts(sgr);
    if (n > 0) fb_write(text, n);
    for (int i = n; i < w; i++) fb_write(" ", 1);
    fb_puts(options_base_sgr());
}

/* Draws one wrapped row. Style changes only at character boundaries, and the
   padding takes the row's class so tints run to the edge of the screen. */
static void put_diff_row(int y, const Row *rw) {
    int w = screen_w - 1;
    if (w < 0) w = 0;
    int n = rw->len;
    if (n > w) {
        n = w;
        while (n > 0 && ((unsigned char)rw->s[n] & 0xC0) == 0x80) n--;
    }

    char sgr[192];
    fb_gotoxy(0, y);
    run_sgr(sgr, sizeof sgr, rw->cls, TK_PLAIN);
    fb_puts(sgr);

    int run = 0, cur = TK_PLAIN;
    for (int i = 0; i < n; i++) {
        int kind = rw->kinds ? rw->kinds[i] : TK_PLAIN;
        if (kind != cur && ((unsigned char)rw->s[i] & 0xC0) != 0x80) {
            if (i > run) fb_write(rw->s + run, i - run);
            run = i;
            cur = kind;
            run_sgr(sgr, sizeof sgr, rw->cls, cur);
            fb_puts(sgr);
        }
    }
    if (n > run) fb_write(rw->s + run, n - run);

    run_sgr(sgr, sizeof sgr, rw->cls, TK_PLAIN);
    fb_puts(sgr);
    for (int i = n; i < w; i++) fb_write(" ", 1);
    fb_puts(options_base_sgr());
}

static int text_rows(void) {
    int n = screen_h - 3;   /* row 0 title, last two rows message and status */
    return n < 1 ? 1 : n;
}

static void draw(const char *title, const Rows *r, int top) {
    char head[256];
    snprintf(head, sizeof head, " %s", title);
    put_row(0, options_chrome_sgr(CH_BAR), head);

    int rows = text_rows();
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        if (idx < r->n) put_diff_row(1 + i, &r->rows[idx]);
        else put_row(1 + i, options_base_sgr(), "");
    }
    put_row(screen_h - 2, options_base_sgr(), "");
    put_row(screen_h - 1, options_chrome_sgr(CH_STATUS), " Up/Down Scroll  Esc Close");
    fb_flush();
}

static void view_impl(const char *title, const char *text, SynLang lang, int diff) {
    Rows r = {0};
    int tw = screen_w - 1;
    if (tw < 1) tw = 1;
    wrap_text(text ? text : "", tw, &r, lang, diff);

    int top = 0;
    int last_w = screen_w;
    int last_h = screen_h;
    clear_screen();
    draw(title, &r, top);

    while (1) {
        int key = read_key();
        int page = text_rows();
        int max_top = r.n - page;
        if (max_top < 0) max_top = 0;

        /* -1 is an idle timeout on POSIX (read_key returns it every ~100 ms), not a close */
        if (key == KEY_ESC || key == KEY_ENTER || key == 'q' || key == 'Q') break;

        if (key == KEY_UP) { if (top > 0) top--; }
        else if (key == KEY_DOWN) { if (top < max_top) top++; }
        else if (key == KEY_RESIZE) {
            term_update_size();
            if (screen_w != last_w || screen_h != last_h) {
                /* Rewrap for the new width and keep roughly the same place */
                rows_free(&r);
                tw = screen_w - 1;
                if (tw < 1) tw = 1;
                wrap_text(text ? text : "", tw, &r, lang, diff);
                last_w = screen_w;
                last_h = screen_h;
                max_top = r.n - text_rows();
                if (max_top < 0) max_top = 0;
                if (top > max_top) top = max_top;
            }
            clear_screen();
        } else {
            continue;
        }
        draw(title, &r, top);
    }

    rows_free(&r);
    clear_screen();
}

void view_text(const char *title, const char *text) {
    view_impl(title, text, SYN_PLAIN, 0);
}

void view_diff(const char *title, const char *text, SynLang lang) {
    view_impl(title, text, lang, 1);
}
