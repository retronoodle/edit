/* Syntax highlighting: per-language line scanners that label each byte with a token kind. */
#ifndef SYNTAX_H
#define SYNTAX_H

typedef enum {
    SYN_PLAIN = 0,
    SYN_C,        /* C and C++ */
    SYN_JS,       /* JavaScript, TypeScript */
    SYN_PHP,
    SYN_PY,
    SYN_JSON,
    SYN_MD,
    SYN_INI
} SynLang;

/* Token kinds, one per byte. */
enum {
    TK_PLAIN = 0,
    TK_KEYWORD,
    TK_COMMENT,
    TK_STRING,
    TK_NUMBER,
    TK_PREPROC,   /* C preprocessor line */
    TK_HEADING    /* Markdown heading, INI section */
};

/* Parser state carried from one line to the next. */
enum {
    SYN_ST_NONE = 0,
    SYN_ST_BLOCK_COMMENT,   /* inside a C-style block comment */
    SYN_ST_TEMPLATE,        /* inside a JS `...` string */
    SYN_ST_TRIPLE_DQ,       /* inside a Python """ string */
    SYN_ST_TRIPLE_SQ        /* inside a Python ''' string */
};

/* Language for a file name, by extension. SYN_PLAIN when unknown. */
SynLang syntax_lang_for(const char *filename);

/* Scans one line. state_in is the state at the start of the line; the
   returned value is the state at its end. kinds[0..len-1] receives a TK_* per byte. */
int syntax_scan_line(SynLang lang, int state_in, const char *text, int len, unsigned char *kinds);

/* SGR sequence that colors a token kind for a color scheme (0 Default, 1 Blue, 2 Light). */
const char *syntax_sgr(int kind, int scheme);

#endif
