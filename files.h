/* File operations: new, open, save, save-as and exit. Each returns 1 on success or 0 if cancelled/failed. */
#ifndef FILES_H
#define FILES_H

#include <stdio.h>

int do_new(void);
int do_open(void);
int do_save(void);
int do_save_as(void);
int do_exit(void);

/* Replaces the document with the contents of an open stream (the caller closes it).
   Does not change the file name or modified flag. */
void load_stream(FILE *in);

#endif
