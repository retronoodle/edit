/* Ask AI (Alt+I, Edit > Ask AI...): sends an instruction with the selection and sends the reply back into the buffer. See aiedit.c. */
#ifndef AIEDIT_H
#define AIEDIT_H

/* Prompts for an instruction, sends it with the selection (or the text around the
   cursor), and puts the reply in place of the selection, or at the cursor. One undo step. */
void ai_ask_selection(void);

#endif
