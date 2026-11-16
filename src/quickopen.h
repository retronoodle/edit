/* Quick Open (Ctrl+P): type part of a file name to open any file under the folder velo started in. */
#ifndef QUICKOPEN_H
#define QUICKOPEN_H

/* Remembers the startup folder. Call once at start, before any file opens. */
void quickopen_set_root(void);

/* Shows the Quick Open list. Returns 1 if a file was opened (or its tab selected), 0 if cancelled. */
int do_quick_open(void);

#endif
