#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "term.h"
#include "ui.h"
#include "utf8.h"
#include "ai.h"
#include "aiinstr.h"
#include "aiasync.h"
#include "aiwait.h"
#include "aiedit.h"
#include "aicomplete.h"
#include "clipboard.h"
#include "clipcodec.h"
#include "options.h"

#define CONTEXT_LINES 20
#define COMPLETE_MAX_LINES 10
#define COMPLETE_MAX_TOKENS 256

static const char COMPLETE_SYSTEM[] =
    "You are an autocomplete engine inside a code editor. Reply with only the text to "
    "insert at the cursor: no explanation, no code fences, at most 10 lines. Do not "
    "repeat text that already comes before the cursor.";

static char *suggestion;       /* NULL when none */
static int anchor_x, anchor_y; /* cursor when it was requested */

void aicomplete_dismiss(void) {
    aiwait_end();
    aiasync_cancel();
    free(suggestion);
    suggestion = NULL;
}

int aicomplete_busy(void) {
    return aiasync_busy();
}

void aicomplete_request(void) {
    aicomplete_dismiss();
    int y = editor.cursor_y, x = editor.cursor_x;
    int y0 = y - CONTEXT_LINES < 0 ? 0 : y - CONTEXT_LINES;
    int ye = y + CONTEXT_LINES >= editor.line_count ? editor.line_count - 1 : y + CONTEXT_LINES;
    char *before = ai_cap_context(ai_span_text(y0, 0, y, x), 1);
    char *after = ai_cap_context(ai_span_text(y, x, ye, editor.lines[ye].len), 0);
    char *input = ai_build_complete_input(before, after);
    free(before);
    free(after);
    if (!input) {
        show_message("Out of memory.");
        return;
    }
    char err[256];
    char *sys = aiinstr_system(COMPLETE_SYSTEM, SMALL_AGENTS_FILE, SMALL_AGENTS_CAP);
    int rc = aiasync_start(sys ? sys : COMPLETE_SYSTEM, input, COMPLETE_MAX_TOKENS, err, sizeof err);
    free(sys);
    free(input);
    if (rc != 0) {
        show_message(err);
        return;
    }
    anchor_x = x;
    anchor_y = y;
    aiwait_begin("AI completing...");
}

int aicomplete_tick(void) {
    char *reply = NULL;
    char err[256];
    int r = aiasync_poll(&reply, err, sizeof err);
    if (r == 0) {
        if (aiasync_busy()) aiwait_tick();
        else aiwait_end();   /* job gone without us ending it */
        return 0;
    }
    aiwait_end();
    if (r < 0) {
        show_message(err);
        return 1;
    }
    char *t = ai_trim_completion(reply, COMPLETE_MAX_LINES);
    free(reply);
    if (!t) return 1;
    int len = clip_clean_paste(t, (int)strlen(t));
    t[len > 0 ? len : 0] = '\0';
    if (len <= 0 || editor.cursor_x != anchor_x || editor.cursor_y != anchor_y) {
        free(t);
        return 1;
    }
    suggestion = t;
    return 1;
}

int aicomplete_accept(void) {
    if (!suggestion) return 0;
    if (editor.cursor_x != anchor_x || editor.cursor_y != anchor_y) {
        aicomplete_dismiss();
        return 0;
    }
    char *t = suggestion;
    suggestion = NULL;
    clip_paste_text(t, (int)strlen(t));   /* one undo step */
    free(t);
    return 1;
}

/* Writes s[0..len) clipped to `width` columns starting at display column col0 (so tabs
   line up), then returns the columns used. */
static int put_clipped(const char *s, int len, int col0, int width) {
    int i = 0, used = 0;
    while (i < len) {
        int cp;
        int n = utf8_decode(s + i, len - i, &cp);
        if (n <= 0) n = 1;
        int w = utf8_cell_w(cp, col0 + used);
        if (used + w > width) break;
        if (cp == '\t') {
            for (int k = 0; k < w; k++) fb_write(" ", 1);
        } else if (utf8_is_c0_ctrl(cp)) {
            char lab[2];
            utf8_ctrl_label(cp, lab);
            fb_write("^", 1);
            fb_write(lab, 1);
        } else {
            fb_write(s + i, n);
        }
        used += w;
        i += n;
    }
    return used;
}

void aicomplete_draw(int cx, int cy, int gutter, int width) {
    if (!suggestion || cy < TEXT_TOP) return;
    int right = gutter + width;
    int row = cy;
    const char *p = suggestion;
    int first = 1;
    while (row < TEXT_TOP + TEXT_ROWS) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        int x = first ? cx : gutter;
        if (x < right) {
            fb_gotoxy(x, row);
            fb_puts("\x1b[2m");   /* dim */
            int used = put_clipped(p, len, x - gutter, right - x);
            if (!first) {          /* hide the real text under a later ghost line */
                for (int k = x + used; k < right; k++) fb_write(" ", 1);
            }
            fb_puts(options_base_sgr());
        }
        if (!nl) break;
        p = nl + 1;
        first = 0;
        row++;
    }
}
