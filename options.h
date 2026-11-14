/* User options set from the Options menu. Not saved to disk yet. */
#ifndef OPTIONS_H
#define OPTIONS_H

typedef struct {
    int tab_width;   /* spaces inserted by Tab: 2, 4 or 8 */
    int crlf;        /* 1 = save with CRLF line endings, 0 = LF */
    int scheme;      /* index into the color schemes: 0 Default, 1 Blue, 2 Light */
    int line_numbers; /* 1 = show line numbers in a left gutter, 0 = off */
    int syntax;       /* 1 = color keywords, comments and strings, 0 = plain text */
} Options;

extern Options options;

/* Advances the option for an ACT_TAB_WIDTH / ACT_LINE_ENDS / ACT_COLOR_SCHEME / ACT_LINE_NUMBERS / ACT_SYNTAX action. */
void options_cycle(int action);

/* Writes the item text for an Options menu action, e.g. "Tab width: 4". */
void options_label(int action, char *buf, int size);

/* SGR sequence that resets attributes and sets the current color scheme. Use it instead of a plain reset. */
const char *options_base_sgr(void);

#endif
