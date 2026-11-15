/* Selection and internal clipboard: Shift+arrows select, Ctrl+X/C/V cut/copy/paste. */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

/* Selection: the anchor is set where the cursor was on the first Shift+move. */
void sel_extend(void);                 /* start a selection at the cursor if none is active */
void sel_clear(void);
void sel_select_all(void);             /* anchor at the start, cursor at the end of the document */
int sel_range(int *y1, int *x1, int *y2, int *x2);  /* 1 if a non-empty selection, normalized */

/* Rectangular selection (Alt+Shift+arrows, sel_block = 1). Covers the same display columns on every line. */
void sel_extend_block(void);                        /* start or extend a block; plain selection while word wrap is on */
void sel_block_cols(int *c1, int *c2);              /* left and right display columns of the block (c2 exclusive) */

/* Deletes the selected text and clears the selection. No undo group of its own. */
void sel_delete_selection(void);

/* For typing and deletes over a selection. If text is selected, opens an undo
   group, deletes the selection and returns 1; the caller must then call
   undo_end_group(). Otherwise clears any empty selection and returns 0. */
int sel_begin_replace(void);

void clip_copy(void);
void clip_cut(void);
void clip_paste(void);                              /* Ctrl+V: system clipboard, else the internal one */
void clip_paste_text(const char *s, int len);       /* inserts text (e.g. a terminal paste) over any selection, as one undo step */

#endif
