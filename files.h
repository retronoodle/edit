/* File operations: new, open, save, save-as and exit. Each returns 1 on success or 0 if cancelled/failed. */
#ifndef FILES_H
#define FILES_H

int do_new(void);
int do_open(void);
int do_save(void);
int do_save_as(void);
int do_exit(void);

#endif
