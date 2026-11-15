/* Read-only scrollable text modal (used for AI replies that are too long for a message). */
#ifndef VIEWER_H
#define VIEWER_H

/* Shows text (lines split on '\n', soft-wrapped to the screen width) until the user
   presses Esc, Enter or q. Up/Down scroll. Does not change the document. */
void view_text(const char *title, const char *text);

#endif
