/* User options: tab width, line endings and color scheme. */
#include <stdio.h>

#include "menu.h"
#include "options.h"

Options options = {4, 0, 0};

static const char *scheme_names[] = {"Default", "Blue", "Light"};

/* Each starts with a reset so the scheme never leaks into other attributes. */
static const char *scheme_sgr[] = {
    "\x1b[0m",          /* Default: terminal colors */
    "\x1b[0;37;44m",    /* Blue: white on blue */
    "\x1b[0;30;47m",    /* Light: black on white */
};

void options_cycle(int action) {
    switch (action) {
        case ACT_TAB_WIDTH:
            options.tab_width = options.tab_width == 2 ? 4 : options.tab_width == 4 ? 8 : 2;
            break;
        case ACT_LINE_ENDS:
            options.crlf = !options.crlf;
            break;
        case ACT_COLOR_SCHEME:
            options.scheme = (options.scheme + 1) % 3;
            break;
    }
}

void options_label(int action, char *buf, int size) {
    switch (action) {
        case ACT_TAB_WIDTH:
            snprintf(buf, size, "Tab width: %d", options.tab_width);
            break;
        case ACT_LINE_ENDS:
            snprintf(buf, size, "Line endings: %s", options.crlf ? "CRLF" : "LF");
            break;
        case ACT_COLOR_SCHEME:
            snprintf(buf, size, "Colors: %s", scheme_names[options.scheme]);
            break;
        default:
            if (size > 0) buf[0] = '\0';
            break;
    }
}

const char *options_base_sgr(void) {
    return scheme_sgr[options.scheme];
}
