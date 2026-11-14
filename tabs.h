/* Tabs: several documents open at once. The active document is the global
   `editor` (with undo.c's active history); each other tab keeps its own copy
   in its slot. Switching moves the data between the two. */
#ifndef TABS_H
#define TABS_H

#include "editor.h"

#define MAX_TABS 16

void tabs_init(void);                 /* one untitled tab, active; call after init_editor() */
void tabs_free_all(void);             /* frees every tab's text and history (at exit) */

int tab_count(void);
int tab_current(void);
Editor *tab_doc(int i);               /* document of tab i; the active tab is `editor` */
const char *tab_name(int i);          /* file name, or "untitled.txt" style for new tabs */
int tab_crlf(int i);                  /* line ending style of tab i */
int tab_named(int i);                 /* 0 = never saved or opened (untitled) */
void tab_mark_named(void);            /* the active tab now has a real file name */
int tab_find(const char *name);       /* index of the tab with this file open, or -1 */
int tab_is_blank(void);               /* active tab: untitled, unmodified, empty */
int tabs_first_modified(void);        /* index of the first modified tab, or -1 */

int tab_new(void);                    /* new untitled tab, made active. 0 if full */
void tab_switch(int i);
void tab_next(void);
void tab_prev(void);
void tab_close_current(void);         /* discards the active tab; caller handles unsaved changes */

/* Tab bar (row TAB_ROW). Tabs that do not fit are scrolled out of view. */
void tab_bar_range(int *first, int *end);             /* visible tabs: first <= i < end */
int tab_bar_label(int i, char *buf, int size);        /* text drawn for tab i; returns its length */
int tab_bar_hit(int x);                               /* tab under column x, or -1 */

#endif
