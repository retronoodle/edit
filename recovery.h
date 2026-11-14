/* Crash recovery: periodic backup of unsaved changes to "<file>.recover". */
#ifndef RECOVERY_H
#define RECOVERY_H

void recovery_tick(void);             /* call often from the main loop; writes a backup when due */
void recovery_discard(void);          /* deletes the backup (after save, or when changes are dropped) */
void recovery_check_on_startup(void); /* if a backup exists, offers to restore it */

#endif
