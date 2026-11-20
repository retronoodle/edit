#ifndef AIINSTR_H
#define AIINSTR_H

#include <stddef.h>

#define AGENTS_FILE        "AGENTS.md"
#define AGENTS_CAP         16384
#define SMALL_AGENTS_FILE  "SMALL_AGENTS.md"
#define SMALL_AGENTS_CAP   4096

/* Remembers the startup folder, where the instruction files are looked up. */
void aiinstr_set_root(void);

/* Pure: base + a header naming the file + content (cut at cap bytes with a note).
   Returns a malloc'd copy of base when content is NULL or empty. NULL on no memory. */
char *aiinstr_join(const char *base, const char *name, const char *content, size_t cap);

/* Reads <startup folder>/file and joins it onto base. A missing or unreadable file gives a
   copy of base, so the AI call never fails because of it. Caller frees. */
char *aiinstr_system(const char *base, const char *file, size_t cap);

#endif
