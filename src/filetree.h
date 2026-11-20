/* File tree model for the sidebar: a flat list of the visible rows of a folder tree.
   Pure: directories are read through a callback, so tests can fake a filesystem.
   Paths are relative to the root and use '/' ("" is the root itself). */
#ifndef FILETREE_H
#define FILETREE_H

#define FT_MAX_CHILDREN 2000    /* entries kept per folder */
#define FT_MAX_ROWS 20000       /* visible rows kept in all */

typedef struct {
    char name[256];
    int is_dir;
} FtEntry;

/* Lists the folder `rel`. Returns a malloc'd array in *out (may be NULL when empty) and its
   length in *n; the tree frees it. Returns 0 on success, -1 when the folder can't be read. */
typedef int (*FtListFn)(const char *rel, FtEntry **out, int *n, void *ctx);

typedef struct {
    char *name;      /* last path part */
    char *rel;       /* path from the root */
    int depth;       /* 0 = directly under the root */
    int is_dir;
    int expanded;
} FtRow;

typedef struct {
    FtRow *rows;
    int n, cap;
    FtListFn list;
    void *ctx;
} FileTree;

/* Reads the root's children (collapsed). */
void ft_init(FileTree *t, FtListFn list, void *ctx);
void ft_free(FileTree *t);

/* Expands / collapses row i (no-ops for files and wrong states). ft_expand returns 1 if
   the row is now expanded. */
int ft_expand(FileTree *t, int i);
void ft_collapse(FileTree *t, int i);
int ft_toggle(FileTree *t, int i);

/* Index of the row's parent folder, or -1 for a top-level row. */
int ft_parent(const FileTree *t, int i);

/* Row index of `rel`, or -1 when it is not currently visible. */
int ft_find(const FileTree *t, const char *rel);

/* Expands every folder above `rel` so it shows, and returns its row index (or -1). */
int ft_reveal(FileTree *t, const char *rel);

/* Re-reads the disk, keeping the folders that were expanded. */
void ft_refresh(FileTree *t);

#endif
