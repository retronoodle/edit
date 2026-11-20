/* Read-only project tools for the AI chat (list_dir, read_file, grep). Everything is
   confined to the folder velo started in: no writes, no shell, no hidden files or folders
   (.git, .env, ...), no build/vendor folders, no paths that escape through ".." or links.
   Each call returns malloc'd text for the model; failures are returned as "Error: ..." text so
   the model can recover. Not available on Windows (the tools answer with an error). */
#ifndef AITOOLS_H
#define AITOOLS_H

#define AITOOLS_MAX_OUT   8192   /* bytes returned by one call; longer output is cut with a note */
#define AITOOLS_MAX_ENTRIES 200  /* list_dir */
#define AITOOLS_MAX_HITS    50   /* grep */

/* Remembers the current folder as the sandbox root. Call once at launch. */
void aitools_set_root(void);

/* Pure: 1 if rel (a path relative to the root, "" = the root) is allowed. Rejects absolute
   paths, ".." parts, hidden parts (leading '.') and skipped folder names. */
int aitools_path_ok(const char *rel);

char *aitools_list_dir(const char *rel);
/* start_line is 1-based (<1 means 1); max_lines <1 means 200 (capped at 500). */
char *aitools_read_file(const char *rel, int start_line, int max_lines);
/* rel may be "" (whole project), a folder or a file. Plain substring match. */
char *aitools_grep(const char *pattern, const char *rel);

/* Runs list_dir, read_file or grep by name with a JSON args object. Returns NULL for any
   other name (the caller handles it). */
char *aitools_run(const char *name, const char *args_json);

#endif
