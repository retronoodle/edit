/* Small multi-line text buffer for the git panel's commit message box.
   Pure, no I/O. Unit tests in tests/test_msgedit.c (make test). */
#ifndef MSGEDIT_H
#define MSGEDIT_H

#define MSGEDIT_MAX 8192   /* bytes, including the room for the NUL */

typedef struct {
    char text[MSGEDIT_MAX];
    int len;   /* bytes used */
    int cur;   /* cursor, a byte offset on a character boundary */
    int goal;  /* column Up/Down aim for (-1 = not set: taken from the cursor on the next Up/Down) */
} MsgEdit;

void msgedit_clear(MsgEdit *me);
void msgedit_set(MsgEdit *me, const char *s);        /* replaces the text, cursor at the end */

/* Inserts one code point at the cursor. A newline is allowed; other control codes and
   invalid values are refused. Returns 0 (no change) when it does not fit. */
int msgedit_insert_cp(MsgEdit *me, int cp);

/* Inserts pasted text: CRLF and CR become newlines, control characters and tabs are dropped
   (clip_clean_paste), and the text stops where the buffer is full. Returns the number of
   characters inserted. */
int msgedit_paste(MsgEdit *me, const char *text, int n);

void msgedit_backspace(MsgEdit *me);   /* removes the character before the cursor */
void msgedit_delete(MsgEdit *me);      /* removes the character under the cursor */
void msgedit_left(MsgEdit *me);
void msgedit_right(MsgEdit *me);
void msgedit_home(MsgEdit *me);        /* start of the current line */
void msgedit_end(MsgEdit *me);         /* end of the current line */
void msgedit_up(MsgEdit *me);          /* up one line, keeping the display column */
void msgedit_down(MsgEdit *me);

int msgedit_nlines(const MsgEdit *me); /* 1 for an empty buffer */

/* Line n (0-based): its first byte and length (no newline). Returns 0 when n is out of range. */
int msgedit_line(const MsgEdit *me, int n, int *start, int *len);

/* The cursor's line and its display column on that line. */
void msgedit_cursor(const MsgEdit *me, int *line, int *col);

#endif
