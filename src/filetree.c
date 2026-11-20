/* File tree model for the sidebar. See filetree.h. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "filetree.h"

static char *dupstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static void free_row(FtRow *r) {
    free(r->name);
    free(r->rel);
}

/* Folders first, then names case-insensitively (ties by exact bytes). */
static int entry_cmp(const void *a, const void *b) {
    const FtEntry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    const char *p = x->name, *q = y->name;
    while (*p && *q) {
        int c = tolower((unsigned char)*p) - tolower((unsigned char)*q);
        if (c) return c;
        p++;
        q++;
    }
    if (*p || *q) return *p ? 1 : -1;
    return strcmp(x->name, y->name);
}

static int reserve(FileTree *t, int extra) {
    if (t->n + extra <= t->cap) return 1;
    int cap = t->cap ? t->cap : 64;
    while (cap < t->n + extra) cap *= 2;
    FtRow *r = realloc(t->rows, (size_t)cap * sizeof *r);
    if (!r) return 0;
    t->rows = r;
    t->cap = cap;
    return 1;
}

/* Reads folder `rel` and inserts its children at row index `at`, at `depth`. */
static int insert_children(FileTree *t, const char *rel, int at, int depth) {
    FtEntry *ents = NULL;
    int n = 0;
    if (t->list(rel, &ents, &n, t->ctx) != 0) return 0;
    if (n > FT_MAX_CHILDREN) n = FT_MAX_CHILDREN;
    if (n > FT_MAX_ROWS - t->n) n = FT_MAX_ROWS - t->n;
    if (n > 0) qsort(ents, (size_t)n, sizeof *ents, entry_cmp);
    if (n <= 0 || !reserve(t, n)) {
        free(ents);
        return n <= 0;
    }
    memmove(&t->rows[at + n], &t->rows[at], (size_t)(t->n - at) * sizeof *t->rows);
    t->n += n;
    size_t rl = strlen(rel);
    for (int k = 0; k < n; k++) {
        FtRow *r = &t->rows[at + k];
        r->name = dupstr(ents[k].name);
        r->rel = malloc(rl + strlen(ents[k].name) + 2);
        if (r->rel) {
            if (rl) { memcpy(r->rel, rel, rl); r->rel[rl] = '/'; strcpy(r->rel + rl + 1, ents[k].name); }
            else strcpy(r->rel, ents[k].name);
        }
        r->depth = depth;
        r->is_dir = ents[k].is_dir;
        r->expanded = 0;
    }
    free(ents);
    return 1;
}

void ft_init(FileTree *t, FtListFn list, void *ctx) {
    t->rows = NULL;
    t->n = t->cap = 0;
    t->list = list;
    t->ctx = ctx;
    insert_children(t, "", 0, 0);
}

void ft_free(FileTree *t) {
    for (int i = 0; i < t->n; i++) free_row(&t->rows[i]);
    free(t->rows);
    t->rows = NULL;
    t->n = t->cap = 0;
}

int ft_expand(FileTree *t, int i) {
    if (i < 0 || i >= t->n) return 0;
    FtRow *r = &t->rows[i];
    if (!r->is_dir) return 0;
    if (r->expanded) return 1;
    char *rel = dupstr(r->rel);   /* rows may move while inserting */
    if (!rel) return 0;
    insert_children(t, rel, i + 1, t->rows[i].depth + 1);
    free(rel);
    t->rows[i].expanded = 1;
    return 1;
}

void ft_collapse(FileTree *t, int i) {
    if (i < 0 || i >= t->n || !t->rows[i].is_dir || !t->rows[i].expanded) return;
    int d = t->rows[i].depth, end = i + 1;
    while (end < t->n && t->rows[end].depth > d) free_row(&t->rows[end++]);
    memmove(&t->rows[i + 1], &t->rows[end], (size_t)(t->n - end) * sizeof *t->rows);
    t->n -= end - (i + 1);
    t->rows[i].expanded = 0;
}

int ft_toggle(FileTree *t, int i) {
    if (i < 0 || i >= t->n || !t->rows[i].is_dir) return 0;
    if (t->rows[i].expanded) { ft_collapse(t, i); return 0; }
    return ft_expand(t, i);
}

int ft_parent(const FileTree *t, int i) {
    if (i < 0 || i >= t->n) return -1;
    int d = t->rows[i].depth;
    for (int k = i - 1; k >= 0; k--)
        if (t->rows[k].depth < d) return k;
    return -1;
}

int ft_find(const FileTree *t, const char *rel) {
    for (int i = 0; i < t->n; i++)
        if (strcmp(t->rows[i].rel, rel) == 0) return i;
    return -1;
}

int ft_reveal(FileTree *t, const char *rel) {
    if (!rel || !*rel) return -1;
    char buf[1024];
    size_t len = strlen(rel);
    if (len >= sizeof buf) return -1;
    for (size_t k = 0; k < len; k++) {
        if (rel[k] != '/') continue;
        memcpy(buf, rel, k);
        buf[k] = '\0';
        int i = ft_find(t, buf);
        if (i < 0 || !ft_expand(t, i)) return -1;
    }
    return ft_find(t, rel);
}

void ft_refresh(FileTree *t) {
    char **open = NULL;
    int n_open = 0;
    for (int i = 0; i < t->n; i++) {
        if (!t->rows[i].expanded) continue;
        char **p = realloc(open, (size_t)(n_open + 1) * sizeof *open);
        if (!p) break;
        open = p;
        open[n_open] = dupstr(t->rows[i].rel);
        if (open[n_open]) n_open++;
    }
    FtListFn list = t->list;
    void *ctx = t->ctx;
    ft_free(t);
    ft_init(t, list, ctx);
    for (int k = 0; k < n_open; k++) {   /* parents come before their children */
        int i = ft_find(t, open[k]);
        if (i >= 0) ft_expand(t, i);
        free(open[k]);
    }
    free(open);
}
