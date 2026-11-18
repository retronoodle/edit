/* Mouse input: click to place the cursor, drag to select, wheel to scroll, click the menus. */
#ifndef MOUSE_H
#define MOUSE_H

#include "term.h"

/* Handles one mouse event. Returns an ACT_* for a menu item that was picked, or ACT_NONE. */
int mouse_handle(const MouseEvent *ev);

/* 1 after a wheel event: the view was scrolled away from the cursor and should not
   snap back until a key or non-wheel mouse input. */
extern int view_detached;

#endif
