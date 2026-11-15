/* User options: tab width, line endings, color scheme and the saved config file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "options.h"
#include "recent.h"   /* home_file_path */

#define CONFIG_NAME ".velo_config"

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
    options_save();
}

/* Parses a whole number. Returns 1 and sets *out only if the text is all digits. */
static int parse_number(const char *s, int *out) {
    char *end;
    long n = strtol(s, &end, 10);
    if (end == s || *end != '\0') return 0;
    *out = (int)n;
    return 1;
}

/* Sets one option from a key and its value. Returns 1 if the key is known and the value is valid. */
static int apply_option(const char *key, const char *val) {
    int n;
    if (!parse_number(val, &n)) return 0;
    if (strcmp(key, "tab_width") == 0) {
        if (n != 2 && n != 4 && n != 8) return 0;
        options.tab_width = n;
    } else if (strcmp(key, "scheme") == 0) {
        if (n < 0 || n > 2) return 0;
        options.scheme = n;
    } else if (strcmp(key, "line_numbers") == 0) {
        if (n != 0 && n != 1) return 0;
        options.line_numbers = n;
    } else if (strcmp(key, "syntax") == 0) {
        if (n != 0 && n != 1) return 0;
        options.syntax = n;
    } else if (strcmp(key, "wrap") == 0) {
        if (n != 0 && n != 1) return 0;
        options.wrap = n;
    } else {
        return 0;
    }
    return 1;
}

static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int options_parse_line(const char *line) {
    while (is_blank(*line)) line++;
    if (*line == '\0' || *line == '#') return 0;

    const char *eq = strchr(line, '=');
    if (!eq) return 0;

    char key[32], val[32];
    size_t kl = (size_t)(eq - line);
    if (kl >= sizeof(key)) return 0;
    memcpy(key, line, kl);
    key[kl] = '\0';
    while (kl > 0 && is_blank(key[kl - 1])) key[--kl] = '\0';

    const char *v = eq + 1;
    while (is_blank(*v)) v++;
    snprintf(val, sizeof(val), "%s", v);
    size_t vl = strlen(val);
    while (vl > 0 && is_blank(val[vl - 1])) val[--vl] = '\0';

    return apply_option(key, val);
}

void options_load(void) {
    char path[512];
    if (!home_file_path(CONFIG_NAME, path, sizeof(path))) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        options_parse_line(line);
    }
    fclose(f);
}

void options_save(void) {
    char path[512];
    if (!home_file_path(CONFIG_NAME, path, sizeof(path))) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "tab_width=%d\n", options.tab_width);
    fprintf(f, "scheme=%d\n", options.scheme);
    fprintf(f, "line_numbers=%d\n", options.line_numbers);
    fprintf(f, "syntax=%d\n", options.syntax);
    fprintf(f, "wrap=%d\n", options.wrap);
    fclose(f);
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
