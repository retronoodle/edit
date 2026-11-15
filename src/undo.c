/* Undo/redo: an operation log plus two stacks (undo and redo).
   Consecutive typing and deleting merge into one operation, so one undo
   step removes a whole word or a run of backspaces. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "ui.h"
#include "undo.h"

/* The active history. undo_take/undo_put move it to and from a per-tab UndoState. */
static OpList undo_list;
static OpList redo_list;
static int break_next = 1;   /* next record starts a new undo step */
static int next_group = 1;
static int in_compound = 0;      /* between undo_begin_group and undo_end_group */
static int compound_group = 0;   /* group id shared by every op in the compound step */
static int save_pos = 0;         /* undo depth when the file was last saved */
static int save_lost = 0;        /* 1 = that depth was dropped (new edit after undo) */

static void die_oom(void) {
    fprintf(stderr, "velo: out of memory\n");
    exit(1);
}

/* ---- Op lists ---- */

static void list_push(OpList *l, UndoOp op) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        UndoOp *p = realloc(l->ops, cap * sizeof(UndoOp));
        if (!p) die_oom();
        l->ops = p;
        l->cap = cap;
    }
    l->ops[l->count++] = op;
}

static UndoOp list_pop(OpList *l) {
    return l->ops[--l->count];
}

static UndoOp *list_top(OpList *l) {
    return l->count ? &l->ops[l->count - 1] : NULL;
}

static void list_clear(OpList *l) {
    for (int i = 0; i < l->count; i++) free(l->ops[i].text);
    free(l->ops);
    l->ops = NULL;
    l->count = 0;
    l->cap = 0;
}

/* ---- Recording helpers ---- */

static UndoOp make_op(OpKind kind, int group, int y, int x,
                      const char *s, int n, int cx, int cy) {
    UndoOp op;
    op.kind = kind;
    op.group = group;
    op.y = y;
    op.x = x;
    op.len = n;
    op.cx = cx;
    op.cy = cy;
    op.text = NULL;
    if (n > 0) {
        op.text = malloc(n);
        if (!op.text) die_oom();
        memcpy(op.text, s, n);
    }
    return op;
}

/* Adds text after the end of an existing op. */
static void op_append(UndoOp *op, const char *s, int n) {
    char *p = realloc(op->text, op->len + n);
    if (!p) die_oom();
    memcpy(p + op->len, s, n);
    op->text = p;
    op->len += n;
}

/* Adds text before the start of an existing op; the caller sets op->x. */
static void op_prepend(UndoOp *op, const char *s, int n) {
    char *p = realloc(op->text, op->len + n);
    if (!p) die_oom();
    memmove(p + n, p, op->len);
    memcpy(p, s, n);
    op->text = p;
    op->len += n;
}

/* Any new edit invalidates the redo history. If the save point was in that
   history, it is gone for good: a later edit must not match it by depth. */
static void new_edit(void) {
    if (save_pos > undo_list.count) save_lost = 1;
    list_clear(&redo_list);
}

void undo_mark_saved(void) {
    save_pos = undo_list.count;
    save_lost = 0;
    break_next = 1;   /* the next keystroke must not merge into the saved op */
}

void undo_mark_unsaved(void) {
    save_lost = 1;
}

/* Group id for a new op: the compound group if one is open, else a fresh one. */
static int cur_group(void) {
    return in_compound ? compound_group : ++next_group;
}

/* ---- Public recording API ---- */

void undo_break_group(void) {
    break_next = 1;
}

void undo_begin_group(void) {
    in_compound = 1;
    compound_group = ++next_group;
    break_next = 1;
}

void undo_end_group(void) {
    in_compound = 0;
    break_next = 1;
}

void undo_record_insert(int y, int x, const char *s, int n, int cx, int cy) {
    new_edit();
    UndoOp *t = (break_next || in_compound) ? NULL : list_top(&undo_list);
    if (t && t->kind == OP_INSERT && t->y == y && t->x + t->len == x) {
        op_append(t, s, n);
    } else {
        list_push(&undo_list, make_op(OP_INSERT, cur_group(), y, x, s, n, cx, cy));
    }
    break_next = 0;
}

void undo_record_delete(int y, int x, const char *s, int n, int cx, int cy) {
    new_edit();
    UndoOp *t = (break_next || in_compound) ? NULL : list_top(&undo_list);
    if (t && t->kind == OP_DELETE && t->y == y && t->x == x) {
        op_append(t, s, n);            /* Delete key: next char moves into x */
    } else if (t && t->kind == OP_DELETE && t->y == y && x + n == t->x) {
        op_prepend(t, s, n);           /* Backspace: next char to the left */
        t->x = x;
    } else {
        list_push(&undo_list, make_op(OP_DELETE, cur_group(), y, x, s, n, cx, cy));
    }
    break_next = 0;
}

void undo_record_split(int y, int x, int cx, int cy) {
    new_edit();
    list_push(&undo_list, make_op(OP_SPLIT, cur_group(), y, x, NULL, 0, cx, cy));
    break_next = 0;
}

void undo_record_join(int y, int x, int cx, int cy) {
    new_edit();
    list_push(&undo_list, make_op(OP_JOIN, cur_group(), y, x, NULL, 0, cx, cy));
    break_next = 0;
}

/* Overwrite mode: one character replaced by another, as a single step. */
void undo_record_replace(int y, int x, const char *old, int olen,
                         const char *ch, int clen, int cx, int cy) {
    new_edit();
    int g = cur_group();
    list_push(&undo_list, make_op(OP_DELETE, g, y, x, old, olen, cx, cy));
    list_push(&undo_list, make_op(OP_INSERT, g, y, x, ch, clen, cx, cy));
    break_next = 1;
}

/* ---- Applying ops ---- */

static void apply_forward(const UndoOp *op) {
    switch (op->kind) {
        case OP_INSERT: raw_insert_text(op->y, op->x, op->text, op->len); break;
        case OP_DELETE: raw_delete_text(op->y, op->x, op->len); break;
        case OP_SPLIT:  raw_split(op->y, op->x); break;
        case OP_JOIN:   raw_join(op->y); break;
    }
}

static void apply_inverse(const UndoOp *op) {
    switch (op->kind) {
        case OP_INSERT: raw_delete_text(op->y, op->x, op->len); break;
        case OP_DELETE: raw_insert_text(op->y, op->x, op->text, op->len); break;
        case OP_SPLIT:  raw_join(op->y); break;
        case OP_JOIN:   raw_split(op->y, op->x); break;
    }
}

/* Cursor position right after the op has been applied forward. */
static void cursor_after(const UndoOp *op, int *cx, int *cy) {
    switch (op->kind) {
        case OP_INSERT: *cx = op->x + op->len; *cy = op->y; break;
        case OP_DELETE: *cx = op->x;           *cy = op->y; break;
        case OP_SPLIT:  *cx = 0;               *cy = op->y + 1; break;
        case OP_JOIN:   *cx = op->x;           *cy = op->y; break;
    }
}

static void finish_step(int cx, int cy) {
    editor.cursor_x = cx;
    editor.cursor_y = cy;
    editor.modified = save_lost || undo_list.count != save_pos;
    break_next = 1;
    adjust_scroll();
}

/* ---- Undo / redo ---- */

void undo_undo(void) {
    if (!undo_list.count) {
        show_message("Nothing to undo.");
        return;
    }

    int g = list_top(&undo_list)->group;
    int cx = editor.cursor_x, cy = editor.cursor_y;
    while (undo_list.count && list_top(&undo_list)->group == g) {
        UndoOp op = list_pop(&undo_list);
        apply_inverse(&op);
        cx = op.cx;            /* ends as the cursor before the earliest op */
        cy = op.cy;
        list_push(&redo_list, op);
    }
    finish_step(cx, cy);
}

void undo_redo(void) {
    if (!redo_list.count) {
        show_message("Nothing to redo.");
        return;
    }

    int g = list_top(&redo_list)->group;
    int cx = editor.cursor_x, cy = editor.cursor_y;
    while (redo_list.count && list_top(&redo_list)->group == g) {
        UndoOp op = list_pop(&redo_list);
        apply_forward(&op);
        cursor_after(&op, &cx, &cy);   /* ends as the cursor after the last op */
        list_push(&undo_list, op);
    }
    finish_step(cx, cy);
}

void undo_clear(void) {
    list_clear(&undo_list);
    list_clear(&redo_list);
    break_next = 1;
    save_pos = 0;    /* a fresh or reloaded document is clean at depth 0 */
    save_lost = 0;
}

/* ---- Per-tab state ---- */

void undo_take(UndoState *s) {
    s->undo = undo_list;
    s->redo = redo_list;
    s->break_next = break_next;
    s->save_pos = save_pos;
    s->save_lost = save_lost;
    undo_list.ops = redo_list.ops = NULL;
    undo_list.count = undo_list.cap = 0;
    redo_list.count = redo_list.cap = 0;
    break_next = 1;
}

void undo_put(UndoState *s) {
    undo_clear();
    undo_list = s->undo;
    redo_list = s->redo;
    break_next = s->break_next;
    save_pos = s->save_pos;
    save_lost = s->save_lost;
    memset(s, 0, sizeof *s);
    s->break_next = 1;
}

void undo_state_free(UndoState *s) {
    list_clear(&s->undo);
    list_clear(&s->redo);
}
