/* One blocking AI request using the global AI settings (Options > AI). See aiask.c. */
#ifndef AIASK_H
#define AIASK_H

#include <stddef.h>

/* Sends one request: system prompt (may be NULL or empty), user input, using the
   configured provider, model, base URL and key. On success returns 0 and sets *out
   to a malloc'd reply (caller frees). On failure returns -1, sets *out to NULL and
   fills err (size errsize) with a short message. Blocks until curl finishes (60 s cap). */
int ai_ask(const char *system, const char *input, char **out, char *err, int errsize);

/* Writes data to a new 0600 temp file and sets path (size n). Returns 1 on success.
   The caller removes the file. */
int ai_write_temp(char *path, size_t n, const char *data);

#endif
