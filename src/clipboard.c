/* Selection and clipboard. The clipboard holds plain text with '\n' between lines.
   Copy goes to the internal clipboard and the system clipboard (OSC 52, or the
   Windows clipboard). Paste reads the system clipboard, falling back to the internal one. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* popen/pclose under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#include "editor.h"
#include "buffer.h"
#include "undo.h"
#include "ui.h"
#include "utf8.h"
#include "options.h"
#include "clipboard.h"
#include "clipcodec.h"

static char *clip_text = NULL;
static int clip_len = 0;
static int clip_sys_stale = 0;   /* 1 when the last copy stayed internal, so Ctrl+V must not read the system clipboard */

/* Larger copies stay internal: many terminals drop OSC 52 sequences over ~100 KB. */
#define OSC52_MAX 100000

/* A plain Shift+move (or any extend) turns a block selection into a plain one. */
void sel_extend(void) {
    if (!editor.sel_active) {
        editor.sel_active = 1;
        editor.sel_x = editor.cursor_x;
        editor.sel_y = editor.cursor_y;
    }
    editor.sel_block = 0;
}

/* Alt+Shift+arrows: extend a rectangular selection. Off while word wrap is on (falls back to plain). */
void sel_extend_block(void) {
    sel_extend();
    editor.sel_block = !options.wrap;
}

void sel_clear(void) {
    editor.sel_active = 0;
    editor.sel_block = 0;
}

void sel_select_all(void) {
    editor.sel_active = 1;
    editor.sel_block = 0;
    editor.sel_x = 0;
    editor.sel_y = 0;
    editor.cursor_y = editor.line_count - 1;
    editor.cursor_x = editor.lines[editor.cursor_y].len;
}

/* Display columns of a block's left and right edges: the smaller and larger of the anchor's and cursor's columns. */
void sel_block_cols(int *c1, int *c2) {
    const Line *a = &editor.lines[editor.sel_y];
    const Line *b = &editor.lines[editor.cursor_y];
    int ca = utf8_col(a->text, a->len, editor.sel_x);
    int cb = utf8_col(b->text, b->len, editor.cursor_x);
    *c1 = ca < cb ? ca : cb;
    *c2 = ca < cb ? cb : ca;
}

/* Byte offset of the first character whose start column is at or past col (len if none). */
static int byte_from_col(const char *t, int len, int col) {
    int c = 0, pos = 0;
    while (pos < len && c < col) {
        int nxt = utf8_next(t, len, pos);
        int cp;
        utf8_decode(t + pos, len - pos, &cp);
        c += cp_width(cp);
        pos = nxt;
    }
    return pos;
}

/* The selection, captured once. Edits move the cursor, so loops over the lines must not re-read it. */
typedef struct {
    int block;                 /* 1 = rectangular */
    int y1, x1, y2, x2;        /* plain span (bytes), normalized */
    int c1, c2;                /* block display columns */
} SelSpan;

/* Returns sel_range's result (1 if a non-empty selection). */
static int sel_span_capture(SelSpan *s) {
    int any = sel_range(&s->y1, &s->x1, &s->y2, &s->x2);
    s->block = editor.sel_block;
    if (s->block) sel_block_cols(&s->c1, &s->c2);
    return any;
}

/* Byte range [*from, *to) that the selection covers on line y. A block takes the same
   display columns on every line. */
static void sel_line_span(const SelSpan *s, int y, int *from, int *to) {
    const Line *ln = &editor.lines[y];
    if (s->block) {
        *from = byte_from_col(ln->text, ln->len, s->c1);
        *to = byte_from_col(ln->text, ln->len, s->c2);
        return;
    }
    *from = (y == s->y1) ? s->x1 : 0;
    *to = (y == s->y2) ? s->x2 : ln->len;
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
    SelSpan s;
    if (sel_span_capture(&s)) {
        if (s.block) {
            /* Each line loses its own slice. Line y1's slice start is where the cursor ends. */
            int top_x = 0;
            for (int y = s.y1; y <= s.y2; y++) {
                int from, to;
                sel_line_span(&s, y, &from, &to);
                if (y == s.y1) top_x = from;
                if (to > from) delete_range(y, from, y, to);
            }
            editor.cursor_y = s.y1;
            editor.cursor_x = top_x;
        } else {
            delete_range(s.y1, s.x1, s.y2, s.x2);
        }
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

#ifdef _WIN32
/* Puts UTF-8 text on the Windows clipboard as UTF-16. */
static void sys_clip_write(const char *text, int len) {
    int wide = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wide + 1) * sizeof(WCHAR));
    if (!h) return;
    WCHAR *p = GlobalLock(h);
    if (!p) { GlobalFree(h); return; }
    MultiByteToWideChar(CP_UTF8, 0, text, len, p, wide);
    p[wide] = L'\0';
    GlobalUnlock(h);
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, h);   /* the clipboard owns h now */
        CloseClipboard();
    } else {
        GlobalFree(h);
    }
}

/* Reads the Windows clipboard as UTF-8. Returns a malloc'd string and its length, or NULL. */
static char *sys_clip_read(int *len) {
    char *out = NULL;
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const WCHAR *w = GlobalLock(h);
        if (w) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);   /* includes the NUL */
            if (n > 1) {
                out = malloc(n);
                if (out) {
                    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL);
                    *len = n - 1;
                }
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}
#else
/* Sends text to the terminal's system clipboard with OSC 52 (kitty, Ghostty, WezTerm, ...).
   Terminals that don't support it ignore the sequence. */
static void sys_clip_write(const char *text, int len) {
    char *enc = clip_b64_encode(text, len);
    if (!enc) return;
    printf("\x1b]52;c;%s\x07", enc);
    fflush(stdout);
    free(enc);
}

/* Runs a shell command that prints clipboard text. Appends its output to *out (NULL if none). */
static int run_clip_cmd(const char *cmd, char **out, int *len) {
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    size_t cap = 0, n = 0;
    char *buf = NULL;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, p)) > 0) {
        if (n + got > cap) {
            size_t ncap = cap ? cap : 4096;
            while (ncap < n + got) ncap *= 2;
            char *q = realloc(buf, ncap);
            if (!q) break;
            buf = q;
            cap = ncap;
        }
        memcpy(buf + n, chunk, got);
        n += got;
    }
    pclose(p);
    if (n == 0) {
        free(buf);
        return 0;
    }
    *out = buf;
    *len = (int)n;
    return 1;
}

/* Reads the system clipboard with the first helper that works: Wayland, then X11.
   Returns a malloc'd string and its length, or NULL if none gave text. */
static char *sys_clip_read(int *len) {
    static const char *const cmds[] = {
        "wl-paste -n 2>/dev/null",
        "xclip -selection clipboard -o 2>/dev/null",
        "xsel --clipboard --output 2>/dev/null",
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        char *out = NULL;
        if (run_clip_cmd(cmds[i], &out, len)) return out;
    }
    return NULL;
}
#endif

/* Copies the selected text into the internal clipboard and the system clipboard. */
void clip_copy(void) {
    SelSpan s;
    if (!sel_span_capture(&s)) {
        show_message("No selection.");
        return;
    }

    int total = 0;
    for (int y = s.y1; y <= s.y2; y++) {
        int from, to;
        sel_line_span(&s, y, &from, &to);
        total += to - from;
        if (y < s.y2) total++;   /* the newline between lines */
    }

    char *buf = malloc(total + 1);
    if (!buf) return;

    int p = 0;
    for (int y = s.y1; y <= s.y2; y++) {
        int from, to;
        sel_line_span(&s, y, &from, &to);
        memcpy(buf + p, editor.lines[y].text + from, to - from);
        p += to - from;
        if (y < s.y2) buf[p++] = '\n';
    }
    buf[p] = '\0';

    free(clip_text);
    clip_text = buf;
    clip_len = total;

    if (total <= OSC52_MAX) {
        sys_clip_write(buf, total);
        clip_sys_stale = 0;
    } else {
        clip_sys_stale = 1;   /* the system clipboard still holds older text */
        show_message("Copied, but too large for the system clipboard (kept inside velo).");
    }
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

/* Inserts text over any selection. The whole paste is one undo step. */
void clip_paste_text(const char *s, int len) {
    if (len <= 0) return;
    undo_begin_group();
    sel_delete_selection();
    insert_text(s, len);
    undo_end_group();
}

/* Ctrl+V / Edit > Paste: the system clipboard if it has text, else the internal one. */
void clip_paste(void) {
    int len = 0;
    char *sys = clip_sys_stale ? NULL : sys_clip_read(&len);   /* stale: our newer copy is internal only */
    if (sys) {
        len = clip_clean_paste(sys, len);
        clip_paste_text(sys, len);
        free(sys);
        return;
    }
    if (!clip_text || clip_len == 0) {
        show_message("Nothing to paste. Ctrl+Shift+V pastes from other apps.");
        return;
    }
    clip_paste_text(clip_text, clip_len);
}
