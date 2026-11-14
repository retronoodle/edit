/* Undo/redo: records edits as invertible operations and replays them. */
#ifndef UNDO_H
#define UNDO_H

/* Recording: called by the public edit functions in buffer.c after each change.
   (x, y) is where the change happened; (cx, cy) is the cursor before it. */
void undo_record_insert(int y, int x, const char *s, int n, int cx, int cy);
void undo_record_delete(int y, int x, const char *s, int n, int cx, int cy);
void undo_record_split(int y, int x, int cx, int cy);
void undo_record_join(int y, int x, int cx, int cy);
void undo_record_replace(int y, int x, char old, char ch, int cx, int cy);

/* Forces the next recorded edit to start a new undo step. */
void undo_break_group(void);

/* Everything recorded between begin and end undoes/redoes as one step. */
void undo_begin_group(void);
void undo_end_group(void);

void undo_undo(void);
void undo_redo(void);
void undo_clear(void);

#endif
