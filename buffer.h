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
void insert_char(char ch);
void delete_char(void);
void backspace(void);
void insert_newline(void);

/* Raw text changes with no undo recording (used by undo/redo). */
void raw_insert_text(int y, int x, const char *s, int n);
void raw_delete_text(int y, int x, int n);
void raw_split(int y, int x);
void raw_join(int y);

#endif
