/* Ask AI: an instruction plus the selection (and nearby text) go to the AI, and the
   reply replaces the selection or is inserted at the cursor as one undo step. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "term.h"
#include "ui.h"
#include "ai.h"
#include "aiask.h"
#include "aiedit.h"
#include "clipboard.h"
#include "clipcodec.h"

#define CONTEXT_LINES 20     /* lines of context above and below the selection */
#define CONTEXT_BYTES 4096   /* cap on the context kept on each side */

static const char AI_ASK_SYSTEM[] =
    "You edit text inside a code editor. Reply with only the replacement text: no "
    "explanation, no code fences. If no text is selected, reply with the text to "
    "insert at the cursor.";

/* Bytes from (y1,x1) to (y2,x2), lines joined by '\n'. Malloc'd, or NULL. */
static char *span_text(int y1, int x1, int y2, int x2) {
    int total = 0;
    for (int y = y1; y <= y2; y++) {
        int from = (y == y1) ? x1 : 0;
        int to = (y == y2) ? x2 : editor.lines[y].len;
        total += to - from + 1;
    }
    char *s = malloc(total + 1);
    if (!s) return NULL;
    int p = 0;
    for (int y = y1; y <= y2; y++) {
        int from = (y == y1) ? x1 : 0;
        int to = (y == y2) ? x2 : editor.lines[y].len;
        memcpy(s + p, editor.lines[y].text + from, to - from);
        p += to - from;
        if (y < y2) s[p++] = '\n';
    }
    s[p] = '\0';
    return s;
}

/* Keeps at most CONTEXT_BYTES of s (the tail when tail is 1, else the head), cut on a
   UTF-8 character boundary. Frees s. Returns the new string, or NULL on failure. */
static char *cap_context(char *s, int tail) {
    if (!s) return NULL;
    int n = (int)strlen(s);
    if (n <= CONTEXT_BYTES) return s;
    int a = tail ? n - CONTEXT_BYTES : 0;
    int b = tail ? n : CONTEXT_BYTES;
    if (tail) while (a < n && ((unsigned char)s[a] & 0xC0) == 0x80) a++;
    else while (b > 0 && ((unsigned char)s[b] & 0xC0) == 0x80) b--;
    char *out = malloc(b - a + 1);
    if (out) {
        memcpy(out, s + a, b - a);
        out[b - a] = '\0';
    }
    free(s);
    return out;
}

void ai_ask_selection(void) {
    char instr[512];
    if (!prompt_line("Ask AI: ", instr, sizeof(instr))) return;   /* Esc or empty */

    /* Gather the input before the blocking call. With no selection, the context is
       around the cursor. */
    int y1, x1, y2, x2;
    int has_sel = sel_range(&y1, &x1, &y2, &x2);
    if (!has_sel) {
        y1 = y2 = editor.cursor_y;
        x1 = x2 = editor.cursor_x;
    }
    int sel_len = 0;
    char *sel = has_sel ? sel_get_text(&sel_len) : NULL;

    int y0 = y1 - CONTEXT_LINES < 0 ? 0 : y1 - CONTEXT_LINES;
    int ye = y2 + CONTEXT_LINES >= editor.line_count ? editor.line_count - 1 : y2 + CONTEXT_LINES;
    char *before = cap_context(span_text(y0, 0, y1, x1), 1);
    char *after = cap_context(span_text(y2, x2, ye, editor.lines[ye].len), 0);

    char *input = ai_build_ask_input(instr, sel, before, after);
    free(sel);
    free(before);
    free(after);
    if (!input) {
        show_message("Out of memory.");
        return;
    }

    /* Show the wait, then block on the request */
    refresh_screen();
    gotoxy(0, screen_h - 2);
    printf("%-*s", screen_w - 1, "Thinking...");
    fflush(stdout);

    char *out = NULL;
    char err[256];
    int rc = ai_ask(AI_ASK_SYSTEM, input, &out, err, sizeof(err));
    free(input);
    if (rc != 0) {
        show_message(err);
        return;
    }

    char *reply = ai_strip_fences(out);
    free(out);
    if (!reply) {
        show_message("Out of memory.");
        return;
    }
    int len = clip_clean_paste(reply, (int)strlen(reply));
    if (len <= 0) {
        show_message("The AI returned nothing.");
        free(reply);
        return;
    }
    clip_paste_text(reply, len);   /* over the selection, or at the cursor; one undo step */
    free(reply);
}
