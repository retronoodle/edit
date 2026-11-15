/* Watches the open file for outside changes (another program rewrote it). */
#ifndef WATCH_H
#define WATCH_H

void watch_record(void);     /* remember the active file's current mtime and size as "ours" */
int watch_tick(void);        /* call when idle; asks to reload if the file changed on disk. 1 = redraw needed */

#endif
