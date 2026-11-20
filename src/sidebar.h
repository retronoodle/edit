/* File tree sidebar (Alt+B, Edit > File Tree): a pane on the left showing the startup folder as a
   tree. [+]/[-] expand and collapse folders, double-click (or Enter) opens a file in a tab, and
   keys or a right-click menu make new files and folders and copy and paste files and folders.
   Like the AI chat pane, ui.c takes sidebar_width() off text_w() and the editor text starts
   at text_x0(). */
#ifndef SIDEBAR_H
#define SIDEBAR_H

#include "term.h"

/* Remembers the folder velo started in as the tree root (call once at launch). */
void sidebar_set_root(void);

/* Columns the sidebar takes (including its border): 0 when closed or when the window is too
   narrow to leave room for the text. */
int sidebar_width(void);

/* 1 while the sidebar is open and has the keyboard. */
int sidebar_focused(void);

/* Takes the keyboard away from the sidebar (the AI chat took it). */
void sidebar_blur(void);

/* Alt+B / Edit > File Tree: opens and focuses; when open, focuses it or (if focused) closes it. */
void sidebar_toggle(void);

/* Draws the pane (and its popup menu, if open) into the frame buffer. */
void sidebar_draw(void);

/* When focused: the screen cell for the hardware cursor (the selected row). Returns 0 when not focused. */
int sidebar_cursor(int *x, int *y);

/* Handles a key while the sidebar has focus. Returns 1 if consumed; 0 lets the editor handle it
   (Ctrl+Q, Ctrl+S, Ctrl+Z, F6/F7, Alt+J ...). */
int sidebar_key(int key);

/* Mouse: clicks, double-clicks, right-click menu and the wheel inside the pane. A click elsewhere
   drops focus. Returns 1 if the event was consumed. */
int sidebar_mouse(const MouseEvent *ev);

#endif
