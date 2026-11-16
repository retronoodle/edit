/* Read-only scrollable text modal (used for AI replies that are too long for a message,
   and for git diffs). */
#ifndef VIEWER_H
#define VIEWER_H

#include "syntax.h"

/* Shows text (lines split on '\n', soft-wrapped to the screen width) until the user
   presses Esc, Enter or q. Up/Down scroll. Does not change the document. */
void view_text(const char *title, const char *text);

/* Same, for a unified diff: added and removed lines are tinted, hunk and file headers
   colored, and code colored with lang (SYN_PLAIN for none). */
void view_diff(const char *title, const char *text, SynLang lang);

#endif
