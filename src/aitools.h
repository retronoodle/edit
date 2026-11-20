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

#define AITOOLS_MAX_WRITE 100000   /* bytes of text one write_file/edit_file may produce */

/* Writing support for the chat (the tools only queue text; the chat opens it as unsaved tabs,
   nothing here writes a file). All POSIX only; Windows answers with an error.
   Resolves rel for a file to create or edit: same name rules as the read tools, the nearest
   existing parent folder must be inside the root, and the file itself must not be a link or
   a folder. abs_out gets the root-joined absolute path. Returns NULL on success, else an
   error text (static). */
const char *aitools_resolve_write(const char *rel, char *abs_out, int size);
/* Creates the missing parent folders of abs (a path from aitools_resolve_write). 0 on success. */
int aitools_make_parents(const char *abs);
/* Reads an existing text file (not binary, under 1 MB) for editing. malloc'd, or NULL with
   *err set to a static message. */
char *aitools_load_text(const char *abs, const char **err);
/* Pure: replaces the one occurrence of old in text by repl. Returns malloc'd text, or NULL with
   *err set (not found, found more than once, old empty, too big, out of memory). */
char *aitools_replace_once(const char *text, const char *old, const char *repl, const char **err);

/* Runs list_dir, read_file or grep by name with a JSON args object. Returns NULL for any
   other name (the caller handles it). */
char *aitools_run(const char *name, const char *args_json);

#endif
