/* Explain selection: the selected text goes to the AI, and the reply is shown in a
   read-only viewer. The document is not changed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "term.h"
#include "ui.h"
#include "ai.h"
#include "aiask.h"
#include "aiexplain.h"
#include "clipboard.h"
#include "viewer.h"

#define EXPLAIN_BYTES 16384   /* cap on the selection sent to the AI */

static const char EXPLAIN_SYSTEM[] =
    "You explain code to a developer. Explain what the given code does, clearly and "
    "concisely, in plain text. Do not use markdown headings. Keep it short unless the "
    "code is long.";

/* Keeps at most EXPLAIN_BYTES of s, cut on a UTF-8 boundary. Frees s. */
static char *cap_selection(char *s) {
    if (!s) return NULL;
    int n = (int)strlen(s);
    if (n <= EXPLAIN_BYTES) return s;
    int b = EXPLAIN_BYTES;
    while (b > 0 && ((unsigned char)s[b] & 0xC0) == 0x80) b--;
    char *out = malloc(b + 1);
    if (out) {
        memcpy(out, s, b);
        out[b] = '\0';
    }
    free(s);
    return out;
}

void ai_explain_selection(void) {
    int y1, x1, y2, x2;
    if (!sel_range(&y1, &x1, &y2, &x2)) {
        show_message("Select some text to explain.");
        return;
    }

    int sel_len = 0;
    char *sel = sel_get_text(&sel_len);
    int truncated = sel && sel_len > EXPLAIN_BYTES;
    sel = cap_selection(sel);
    if (!sel) {
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
    int rc = ai_ask(EXPLAIN_SYSTEM, sel, &out, err, sizeof(err));
    free(sel);
    if (rc != 0) {
        show_message(err);
        return;
    }

    char title[160];
    if (truncated) snprintf(title, sizeof title, "AI explanation (selection cut to %d bytes)", EXPLAIN_BYTES);
    else snprintf(title, sizeof title, "AI explanation");
    view_text(title, out);
    free(out);
}
