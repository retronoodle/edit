/* File operations for the sidebar: new file, new folder, copy. POSIX only (on Windows every
   call answers with an error). Each returns NULL on success or a short error message.
   Nothing here overwrites an existing file. */
#ifndef FILEOPS_H
#define FILEOPS_H

#include <stddef.h>

/* 1 if name is a single plain path part (not empty, no '/' or '\', not "." or ".."). */
int fileops_valid_name(const char *name);

/* Picks a name for `name` inside `dir` that does not exist yet: name itself, else
   "stem copy.ext", "stem copy 2.ext", ... Writes it to out. */
void fileops_unique_name(const char *dir, const char *name, char *out, size_t size);

/* Creates dir/name as an empty file or a folder; the new path goes to out. */
const char *fileops_new_file(const char *dir, const char *name, char *out, size_t size);
const char *fileops_new_dir(const char *dir, const char *name, char *out, size_t size);

/* Copies the file or folder `src` into folder `dstdir` under a unique name (the new path goes
   to out). Folders copy recursively, symlinks are skipped, and a folder can't go into itself. */
const char *fileops_copy(const char *src, const char *dstdir, char *out, size_t size);

#endif
