/* Read-only scrollable text modal. Text is wrapped into rows once per screen size. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "term.h"
#include "options.h"
#include "utf8.h"
#include "viewer.h"

typedef struct {
    char **rows;   /* malloc'd row strings */
    int n;
    int cap;
} Rows;

static void rows_free(Rows *r) {
    for (int i = 0; i < r->n; i++) free(r->rows[i]);
    free(r->rows);
    r->rows = NULL;
    r->n = r->cap = 0;
}

static int rows_add(Rows *r, const char *s, int len) {
    if (r->n == r->cap) {
        int nc = r->cap ? r->cap * 2 : 64;
        char **p = realloc(r->rows, nc * sizeof *p);
        if (!p) return -1;
        r->rows = p;
        r->cap = nc;
    }
    char *c = malloc(len + 1);
    if (!c) return -1;
    memcpy(c, s, len);
    c[len] = '\0';
    r->rows[r->n++] = c;
    return 0;
}

/* Splits text on '\n' and wraps each line at tw columns. Tabs become spaces and
   other control bytes are dropped, so the rows print cleanly. */
static void wrap_text(const char *text, int tw, Rows *out) {
    const char *p = text;
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

        int rows = utf8_wrap_rows(line, k, tw);
        for (int r = 0; r < rows; r++) {
            int s, e, col;
            utf8_wrap_row(line, k, tw, r, &s, &e, &col);
            if (rows_add(out, line + s, e - s) != 0) break;
        }
        free(line);

        if (!nl) break;
        p = nl + 1;
    }
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
        const char *t = (idx < r->n) ? r->rows[idx] : "";
        put_row(1 + i, options_base_sgr(), t);
    }
    put_row(screen_h - 2, options_base_sgr(), "");
    put_row(screen_h - 1, options_chrome_sgr(CH_STATUS), " Up/Down Scroll  Esc Close");
    fb_flush();
}

void view_text(const char *title, const char *text) {
    Rows r = {0};
    int tw = screen_w - 1;
    if (tw < 1) tw = 1;
    wrap_text(text ? text : "", tw, &r);

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
                wrap_text(text ? text : "", tw, &r);
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
