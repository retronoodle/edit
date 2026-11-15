/* User options: tab width, line endings and color scheme. */
#include <stdio.h>

#include "menu.h"
#include "options.h"

Options options = {4, 0, 0, 0, 1, 0};

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
        case ACT_LINE_NUMBERS:
            options.line_numbers = !options.line_numbers;
            break;
        case ACT_SYNTAX:
            options.syntax = !options.syntax;
            break;
        case ACT_WRAP:
            options.wrap = !options.wrap;
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
        case ACT_LINE_NUMBERS:
            snprintf(buf, size, "Line numbers: %s", options.line_numbers ? "On" : "Off");
            break;
        case ACT_SYNTAX:
            snprintf(buf, size, "Syntax: %s", options.syntax ? "On" : "Off");
            break;
        case ACT_WRAP:
            snprintf(buf, size, "Word wrap: %s", options.wrap ? "On" : "Off");
            break;
        default:
            if (size > 0) buf[0] = '\0';
            break;
    }
}

const char *options_base_sgr(void) {
    return scheme_sgr[options.scheme];
}

/* Chrome colors, indexed [scheme][ChromeKind]; order matches the enum. */
static const char *chrome_sgr[3][CH_COUNT] = {
    /* Default: terminal colors, reverse video for bars */
    {
        "\x1b[0;7m",        /* CH_BAR */
        "\x1b[0;1m",        /* CH_BAR_SEL */
        "\x1b[0m",          /* CH_TAB_BAR */
        "\x1b[0;7m",        /* CH_TAB_ACT */
        "\x1b[0;7m",        /* CH_STATUS */
        "\x1b[0;7;1m",      /* CH_BRANCH */
        "\x1b[1;33m",       /* CH_WARN */
        "\x1b[0;2m",        /* CH_DIM */
        "\x1b[0;7m",        /* CH_DROP_SEL */
    },
    /* Blue: cyan bars on blue */
    {
        "\x1b[0;30;46m",    /* CH_BAR */
        "\x1b[0;1;37;44m",  /* CH_BAR_SEL */
        "\x1b[0;36;44m",    /* CH_TAB_BAR */
        "\x1b[0;1;30;46m",  /* CH_TAB_ACT */
        "\x1b[0;30;46m",    /* CH_STATUS */
        "\x1b[0;1;34;46m",  /* CH_BRANCH */
        "\x1b[1;31m",       /* CH_WARN */
        "\x1b[0;36;44m",    /* CH_DIM */
        "\x1b[0;30;46m",    /* CH_DROP_SEL */
    },
    /* Light: blue bars on white */
    {
        "\x1b[0;97;44m",    /* CH_BAR */
        "\x1b[0;1;30;47m",  /* CH_BAR_SEL */
        "\x1b[0;30;100m",   /* CH_TAB_BAR */
        "\x1b[0;1;97;44m",  /* CH_TAB_ACT */
        "\x1b[0;97;44m",    /* CH_STATUS */
        "\x1b[0;1;93;44m",  /* CH_BRANCH */
        "\x1b[1;31m",       /* CH_WARN */
        "\x1b[0;90;47m",    /* CH_DIM */
        "\x1b[0;97;44m",    /* CH_DROP_SEL */
    },
};

const char *options_chrome_sgr(ChromeKind kind) {
    return chrome_sgr[options.scheme][kind];
}
