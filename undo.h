/* Undo/redo: records edits as invertible operations and replays them. */
#ifndef UNDO_H
#define UNDO_H

/* Recording: called by the public edit functions in buffer.c after each change.
   (x, y) is where the change happened; (cx, cy) is the cursor before it. */
void undo_record_insert(int y, int x, const char *s, int n, int cx, int cy);
void undo_record_delete(int y, int x, const char *s, int n, int cx, int cy);
void undo_record_split(int y, int x, int cx, int cy);
void undo_record_join(int y, int x, int cx, int cy);
void undo_record_replace(int y, int x, const char *old, int olen,
                         const char *ch, int clen, int cx, int cy);

/* Forces the next recorded edit to start a new undo step. */
void undo_break_group(void);

/* Everything recorded between begin and end undoes/redoes as one step. */
void undo_begin_group(void);
void undo_end_group(void);

void undo_undo(void);
void undo_redo(void);
void undo_clear(void);

/* Per-tab history. undo_take moves the active history into *s and leaves the
   active history empty; undo_put makes *s the active history (the previous
   active history is freed) and zeroes *s. */
typedef enum {
    OP_INSERT,   /* text inserted at (y, x) */
    OP_DELETE,   /* text deleted at (y, x) */
    OP_SPLIT,    /* line y split at column x (Enter) */
    OP_JOIN      /* line y+1 joined onto line y; x is line y's old length */
} OpKind;

typedef struct {
    OpKind kind;
    int group;       /* ops with the same group undo/redo together */
    int y, x;
    char *text;      /* heap copy for INSERT/DELETE, NULL otherwise */
    int len;
    int cx, cy;      /* cursor before the op; restored on undo */
} UndoOp;

typedef struct {
    UndoOp *ops;
    int count;
    int cap;
} OpList;

typedef struct {
    OpList undo;
    OpList redo;
    int break_next;
} UndoState;

void undo_take(UndoState *s);
void undo_put(UndoState *s);
void undo_state_free(UndoState *s);

#endif
