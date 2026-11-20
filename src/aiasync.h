/* Non-blocking AI request for autocomplete. One request at a time; start it, poll it
   from the main loop, cancel it on any key. POSIX only (Windows reports unsupported). */
#ifndef AIASYNC_H
#define AIASYNC_H

/* Starts a request with the global AI settings. Returns 0 when running, -1 on failure
   (err, size errsize, gets the reason). Any request already running is cancelled. */
int aiasync_start(const char *system, const char *input, int max_tokens, char *err, int errsize);

/* 0 = still running (or nothing started), 1 = done: *out is the malloc'd reply text
   (caller frees), -1 = failed: err holds the reason. */
int aiasync_poll(char **out, char *err, int errsize);

/* Kills a running request and removes its temp files. Safe to call any time. */
void aiasync_cancel(void);

/* 1 while a request is running. */
int aiasync_busy(void);

#endif
