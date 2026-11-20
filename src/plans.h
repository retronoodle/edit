/* Saved AI chat plans: Markdown files in ~/.velo_plans, named <yyyymmdd-hhmmss>-<slug>.md.
   Written only when the user approves a plan; the newest PLANS_KEEP are kept. POSIX only
   (plans_save fails on Windows). */
#ifndef PLANS_H
#define PLANS_H

#include <stddef.h>

#define PLANS_KEEP 20

/* Pure: a file-name slug from the plan's first non-empty line (leading '#', spaces and
   other punctuation skipped): lowercase letters and digits, runs of anything else become one
   '-', at most 40 chars, never empty ("plan" when nothing usable). */
void plans_slug(const char *text, char *out, size_t size);

/* Saves `text` as a new plan file and prunes old ones. Returns 1 and fills `path` (full
   path) on success, 0 on failure. Never overwrites a file. */
int plans_save(const char *text, char *path, size_t size);

#endif
