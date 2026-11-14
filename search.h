/* Find, Find Next, Replace and Go to Line. */
#ifndef SEARCH_H
#define SEARCH_H

void search_find(void);          /* prompt for a term and jump to its first match */
void search_next(void);          /* jump to the next match (wraps); prompts if no term yet */
void search_replace(void);       /* prompt for find/replace text, then Y/N/A per match */
void search_goto_line(void);     /* prompt for a line number and move the cursor there */

#endif
