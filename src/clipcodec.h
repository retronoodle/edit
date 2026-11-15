/* Pure helpers for the clipboard: base64 for OSC 52, and cleanup of pasted text. No I/O, so tests can use them. */
#ifndef CLIPCODEC_H
#define CLIPCODEC_H

#include <stddef.h>

/* Base64-encodes text[0..len). Returns a malloc'd NUL-terminated string (free it), or NULL. */
char *clip_b64_encode(const char *text, int len);

/* Cleans pasted bytes in place: CRLF and lone CR become '\n', and control
   characters other than tab and newline are dropped. Returns the new length. */
int clip_clean_paste(char *text, int len);

#endif
