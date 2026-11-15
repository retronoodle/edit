/* Crash recovery: periodic backup of unsaved changes to "<file>.recover". */
#ifndef RECOVERY_H
#define RECOVERY_H

void recovery_tick(void);                   /* call often from the main loop; backs up every modified tab when due */
void recovery_discard_name(const char *name); /* deletes the backup for this file name (after save, or when changes are dropped) */
void recovery_check_on_startup(void);       /* for each named tab with a backup, offers to restore it */

#endif
