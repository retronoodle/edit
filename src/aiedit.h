/* Ask AI (Alt+I, Edit > Ask AI...): sends an instruction with the selection and sends the reply back into the buffer. See aiedit.c. */
#ifndef AIEDIT_H
#define AIEDIT_H

/* Prompts for an instruction, sends it with the selection (or the text around the
   cursor), and puts the reply in place of the selection, or at the cursor. One undo step. */
void ai_ask_selection(void);

/* Context helpers shared with autocomplete. ai_span_text: bytes from (y1,x1) to (y2,x2),
   lines joined by '\n' (malloc'd or NULL). ai_cap_context: keeps at most 4 KB of s (the
   tail when tail is 1, else the head) on a UTF-8 boundary; frees s and returns the result. */
char *ai_span_text(int y1, int x1, int y2, int x2);
char *ai_cap_context(char *s, int tail);

#endif
