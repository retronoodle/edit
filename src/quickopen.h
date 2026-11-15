/* Quick Open (Ctrl+P): type part of a file name to open any file under the current folder. */
#ifndef QUICKOPEN_H
#define QUICKOPEN_H

/* Shows the Quick Open list. Returns 1 if a file was opened (or its tab selected), 0 if cancelled. */
int do_quick_open(void);

#endif
