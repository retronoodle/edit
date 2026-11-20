/* Document buffer: line storage, editing operations and cursor/scroll state. */
#ifndef BUFFER_H
#define BUFFER_H

#include "editor.h"

void line_reserve(Line *ln, int need);
void insert_line_at(int idx, const char *s, int n);
void delete_line_at(int idx);
void free_all_lines(void);
void reset_document(void);
void init_editor(void);
void adjust_scroll(void);
void move_cursor(int dx, int dy);
int line_rows(int y);                          /* screen rows line y takes (1 unless word wrap is on) */
void cursor_set_wrapped(int y, int row, int col);   /* cursor to a wrap row and column within it */
void insert_utf8(const char *s, int n);   /* one whole character, n bytes */
void delete_char(void);
void backspace(void);
void insert_newline(void);
int brace_type_char(int ch);   /* brace help for a typed byte; 1 if handled */
int brace_backspace(void);     /* removes an empty brace pair; 1 if handled */

/* Multi-line edits used by clipboard and selection; each is recorded for undo. */
void insert_text(const char *s, int n);          /* inserts at the cursor; '\n' splits lines */
void delete_range(int y1, int x1, int y2, int x2);  /* deletes [start, end); cursor moves to start */

/* Raw text changes with no undo recording (used by undo/redo). */
void raw_insert_text(int y, int x, const char *s, int n);
void raw_delete_text(int y, int x, int n);
void raw_split(int y, int x);
void raw_join(int y);

#endif
