/* Animated "AI is working" indicator on the message row (spinner + elapsed seconds).
   Draw-only: aiwait_tick() does nothing unless aiwait_begin() was called. */
#ifndef AIWAIT_H
#define AIWAIT_H

void aiwait_begin(const char *label);   /* start (or relabel) a wait */
void aiwait_tick(void);                 /* redraw the indicator; call every ~100 ms */
void aiwait_end(void);                  /* stop; the next refresh blanks the row */

#endif
