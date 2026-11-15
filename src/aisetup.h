/* Options > AI setup: prompts for the AI provider, model, base URL and key, then saves. */
#ifndef AISETUP_H
#define AISETUP_H

/* Runs the AI setup prompts on the prompt row. Esc cancels with nothing saved.
   The key is never shown, only whether it is set or comes from an env var. */
void ai_setup(void);

#endif
