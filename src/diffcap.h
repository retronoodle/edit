/* Shrinks text for AI prompts: a per-file diff budget and a line-capped head.
   Pure, no I/O. Unit tests in tests/test_diffcap.c (make test). */
#ifndef DIFFCAP_H
#define DIFFCAP_H

#include <stddef.h>

/* Returns a malloc'd copy of a git diff, about `budget` bytes long (NULL on out of memory).
   Each "diff --git" section gets an equal share of what earlier short sections left unused.
   A section over its share keeps its first line (the header) and whole lines up to the share,
   then "[... N lines omitted]". Headers are kept even at budget 0, so the total can run a little
   over budget when there are very many files. */
char *diffcap_trim(const char *diff, size_t budget);

/* Returns a malloc'd copy of text, cut back to whole lines within budget bytes (NULL on out of
   memory). If anything was cut, marker is appended. Text within budget is returned unchanged. */
char *diffcap_head(const char *text, size_t budget, const char *marker);

#endif
