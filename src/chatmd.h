#ifndef CHATMD_H
#define CHATMD_H

/* Pure helpers for the AI chat pane: word-aware wrapping and Markdown to styled text. No I/O. */

#define CM_BOLD   1
#define CM_ITALIC 2
#define CM_CODE   4
#define CM_HEAD   8
#define CM_DIM    16

/* Renders Markdown into display text plus one CM_* style byte per output byte. Markers are
   removed (headings, **bold**, *italic*, `code`, fences, bullets, quotes, rules, links); anything
   unterminated stays literal. Both outputs are malloc'd; text is NUL-terminated and the style
   array has strlen(text) bytes. Returns 1 on success. */
int chatmd_render(const char *md, char **text, unsigned char **style);

/* One wrapped row of a single line (no newlines) of text[0..len) at width cw, starting at byte
   pos. Sets *end (end of the row) and returns where the next row starts (== len when done).
   With word set, a row breaks at a space instead of inside a word and the spaces at the break
   are dropped; a word wider than cw is split. Without it, rows break by character. Loop:
     pos = 0; do { nx = chatmd_row(t, len, pos, cw, word, &e); use [pos,e); pos = nx; } while (pos < len); */
int chatmd_row(const char *text, int len, int pos, int cw, int word, int *end);

#endif
