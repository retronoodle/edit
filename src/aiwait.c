#include <stdio.h>
#include <string.h>

#include "editor.h"
#include "term.h"
#include "options.h"
#include "aiwait.h"

static int active;
static unsigned long started;
static char label[160];

void aiwait_begin(const char *l) {
    snprintf(label, sizeof label, "%s", l ? l : "Thinking...");
    if (!active) started = term_ticks_ms();
    active = 1;
}

void aiwait_end(void) {
    active = 0;
}

void aiwait_tick(void) {
    if (!active) return;
    static const char frames[] = "|/-\\";
    unsigned long ms = term_ticks_ms() - started;
    char line[256];
    snprintf(line, sizeof line, "%c %s %lus", frames[(ms / 100) % 4], label, ms / 1000);
    printf("\x1b" "7");
    gotoxy(0, screen_h - 2);
    printf("%s%-*.*s%s", options_base_sgr(), screen_w - 1, screen_w - 1, line, options_base_sgr());
    printf("\x1b" "8");
    fflush(stdout);
}
