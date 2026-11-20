/* AI autocomplete (Alt+Space, Edit > Complete): asks the AI in the background for text
   at the cursor and shows it as grey ghost text. Tab accepts it as one undo step; any
   other key dismisses it. The buffer is never changed while the ghost text shows. */
#ifndef AICOMPLETE_H
#define AICOMPLETE_H

/* Starts a background request for the cursor position (cancels any earlier one). */
void aicomplete_request(void);

/* Main-loop poll. Returns 1 when the screen needs a redraw (a suggestion arrived or failed). */
int aicomplete_tick(void);

/* 1 while a request is running (the main loop polls faster). */
int aicomplete_busy(void);

/* Tab: inserts the suggestion at the cursor. Returns 1 if there was one, else 0 (key unused). */
int aicomplete_accept(void);

/* Drops the suggestion and cancels a running request. Cheap when idle. */
void aicomplete_dismiss(void);

/* Draws the ghost text into the frame buffer. (cx, cy) is the cursor's screen cell,
   gutter the left text column, width the text columns. Does nothing when idle. */
void aicomplete_draw(int cx, int cy, int gutter, int width);

#endif
