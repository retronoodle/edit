/* File operations: new tab, open, save, save-as, close tab and exit. Each returns 1 on success or 0 if cancelled/failed. */
#ifndef FILES_H
#define FILES_H

#include <stdio.h>

int do_new(void);                  /* new untitled tab */
int do_open(void);                 /* browse for a file, open it in a tab */
int do_open_recent(void);          /* pick from the recent files list, open it in a tab */
int open_path(const char *name, int create_missing);   /* open a named file in a tab (see files.c) */
int do_save(void);
int do_save_as(void);
int do_close_tab(void);            /* asks about unsaved changes, then closes the active tab */
int do_exit(void);                 /* asks about every modified tab */

/* Replaces the document with the contents of an open stream (the caller closes it).
   Does not change the file name or modified flag. */
void load_stream(FILE *in);

#endif
