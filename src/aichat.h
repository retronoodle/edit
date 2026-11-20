/* AI chat sidebar (Alt+J, Edit > AI Chat): a pane on the right where the user talks to the AI
   about the active file. The AI can read other project files with read-only tools (aitools.c),
   and proposes edits as a whole-file replacement that the user reviews and applies as one
   undo step. Requests block behind a "Thinking..." message, like the other AI features. */
#ifndef AICHAT_H
#define AICHAT_H

#include "term.h"

/* Columns the sidebar takes: 0 when it is closed or the screen is too narrow (< 80 columns).
   ui.c takes this off text_w(). */
int aichat_width(void);

/* 1 while the sidebar is open and has the keyboard. */
int aichat_focused(void);

/* Alt+J / Edit > AI Chat: opens and focuses; when open, focuses it or (if focused) closes it. */
void aichat_toggle(void);

/* Draws the pane into the frame buffer (columns screen_w - aichat_width() ..). */
void aichat_draw(void);

/* When the sidebar has focus: the screen cell for the hardware cursor (in the input box).
   Returns 0 when it does not have focus. */
int aichat_cursor(int *x, int *y);

/* Handles a key while the sidebar has focus. Returns 1 if consumed; 0 lets the editor
   handle it (Ctrl+Q, Ctrl+Z, F6/F7, Alt+G ...). */
int aichat_key(int key);

/* A terminal paste (KEY_PASTE) while the sidebar has focus. */
void aichat_paste(const char *text, int len);

/* Mouse: a click in the pane focuses it, the wheel scrolls its history. A click elsewhere
   drops focus. Returns 1 if the event was consumed. */
int aichat_mouse(const MouseEvent *ev);

#endif
