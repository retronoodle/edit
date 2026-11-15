/* UTF-8 helpers: decode/encode, character boundaries and display width. */
#ifndef UTF8_H
#define UTF8_H

/* Decodes one character at s[0..len). Returns bytes consumed (1..4).
   Invalid sequences consume one byte and set *cp to -1. */
int utf8_decode(const char *s, int len, int *cp);

/* Encodes cp into out (at least 4 bytes). Returns bytes written. */
int utf8_encode(int cp, char *out);

/* Display columns for a code point: 0 (combining), 1, or 2 (wide). */
int cp_width(int cp);

/* Byte offset of the start of the character after the one at i (i < len). */
int utf8_next(const char *text, int len, int i);

/* Byte offset of the start of the character before i (i > 0). */
int utf8_prev(const char *text, int len, int i);

/* Display column of byte offset i on a line. */
int utf8_col(const char *text, int len, int i);

/* Byte offset of the character whose start is at or before display column col. */
int utf8_byte_at_col(const char *text, int len, int col);

/* Total display width of text[0..len). */
int utf8_width(const char *text, int len);

/* Simple lowercase fold of a code point (for case-insensitive search). */
int cp_fold(int cp);

/* Matches pat[0..plen) at the start of text, case-folded. Returns the byte
   offset just past the match, or -1. */
int utf8_match_fold(const char *text, int len, const char *pat, int plen);

/* Soft wrap at tw columns. Each line has at least one row. */
int utf8_wrap_rows(const char *text, int len, int tw);

/* Byte range [start, end) of wrap row `row`, and the display column where it starts.
   Rows past the last one give start = end = len. */
void utf8_wrap_row(const char *text, int len, int tw, int row,
                   int *start, int *end, int *start_col);

/* Wrap row and display column within that row (clamped to tw-1) of byte offset `byte`. */
void utf8_wrap_cursor(const char *text, int len, int tw, int byte,
                      int *row, int *col);

#endif
