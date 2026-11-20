/* Bracket helpers for auto-closing and matching-bracket highlight. Pure, no I/O.
   Unit tests in tests/test_brace.c (make test). */
#ifndef BRACE_H
#define BRACE_H

#include "editor.h"   /* Line */

/* Closing char for an opener ( [ { " ' ; 0 for anything else. */
int brace_closer(int ch);

/* 1 if typing ch at byte x of the line should insert its closer too. */
int brace_should_autoclose(const char *line, int len, int x, int ch);

/* 1 if typing ch at byte x should just step over the closer already there. */
int brace_is_skip(const char *line, int len, int x, int ch);

/* 1 if byte x sits between an empty pair, e.g. the cursor in "(|)". */
int brace_pair_between(const char *line, int len, int x);

/* 1 if byte x sits between an empty {} pair only (Enter splits just these). */
int brace_curly_between(const char *line, int len, int x);

/* Partner of the bracket at (y, x). Returns 1 and sets (*my, *mx) if found.
   Counts nesting across lines; stops after a fixed number of lines. Ignores strings. */
int brace_find_match(const Line *lines, int count, int y, int x, int *my, int *mx);

/* Brackets to highlight for the cursor: the one under it, else the one before it.
   Returns 1 with both positions when a partner exists. */
int brace_match_for_cursor(const Line *lines, int count, int y, int x,
                           int *ay, int *ax, int *by, int *bx);

#endif
