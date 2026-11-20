/* Pure line diff: the changed hunks between two texts (lines split on '\n'). */
#ifndef LINEDIFF_H
#define LINEDIFF_H

/* Old lines [old_start, old_start+old_count) become the new lines
   [new_start, new_start+new_count). new_off/new_len are the byte range of those new
   lines in the new text, joined by '\n' with no trailing newline. */
typedef struct {
    int old_start, old_count;
    int new_start, new_count;
    int new_off, new_len;
} LdHunk;

/* Hunks in top-to-bottom order. Returns 0 on success (*n may be 0 when the texts are
   equal; free *out with free()), -1 on out of memory. Very large middles fall back to
   one hunk. A text with k newlines has k+1 lines, like the editor buffer. */
int linediff(const char *a, const char *b, LdHunk **out, int *n);

#endif
