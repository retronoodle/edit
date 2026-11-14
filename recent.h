/* Recent files: the last few files opened or saved, kept across runs. */
#ifndef RECENT_H
#define RECENT_H

#define MAX_RECENT 10

/* Reads the list from the per-user file. Missing or unreadable is fine (empty list). */
void recent_load(void);

/* Puts a file at the top of the list (stored as a full path, no duplicates) and saves. */
void recent_add(const char *name);

int recent_count(void);
const char *recent_get(int i);     /* i = 0 is the newest */

/* Drops one entry (e.g. the file no longer exists) and saves. */
void recent_remove(int i);

#endif
