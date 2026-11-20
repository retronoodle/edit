/* Unit tests for filetree.c, using a fake filesystem. Build and run with: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filetree.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

/* Fake disk: a folder path and one entry; "name/" marks a folder. */
static const char *disk[][2] = {
    {"", "zeta.txt"}, {"", "src/"}, {"", "Alpha.c"}, {"", "docs/"}, {"", "beta.c"},
    {"src", "main.c"}, {"src", "lib/"}, {"src/lib", "deep.c"},
    {"docs", "a.md"},
    {NULL, NULL}
};
static int list_calls = 0;

static int fake_list(const char *rel, FtEntry **out, int *n, void *ctx) {
    (void)ctx;
    list_calls++;
    int known = rel[0] == '\0';
    int cnt = 0;
    for (int i = 0; disk[i][0]; i++) {
        if (strcmp(disk[i][0], rel) == 0) { known = 1; cnt++; }
    }
    if (!known) return -1;
    *out = cnt ? malloc((size_t)cnt * sizeof **out) : NULL;
    int k = 0;
    for (int i = 0; disk[i][0]; i++) {
        if (strcmp(disk[i][0], rel) != 0) continue;
        const char *nm = disk[i][1];
        size_t l = strlen(nm);
        FtEntry *e = &(*out)[k++];
        e->is_dir = nm[l - 1] == '/';
        snprintf(e->name, sizeof e->name, "%.*s", (int)(e->is_dir ? l - 1 : l), nm);
    }
    *n = cnt;
    return 0;
}

int main(void) {
    FileTree t;
    ft_init(&t, fake_list, NULL);
    CHECK(t.n == 5, "root lists 5 rows");
    CHECK(strcmp(t.rows[0].name, "docs") == 0 && strcmp(t.rows[1].name, "src") == 0,
          "folders first, sorted");
    CHECK(strcmp(t.rows[2].name, "Alpha.c") == 0 && strcmp(t.rows[3].name, "beta.c") == 0
          && strcmp(t.rows[4].name, "zeta.txt") == 0, "files sorted ignoring case");
    CHECK(t.rows[0].depth == 0 && !t.rows[0].expanded, "starts collapsed at depth 0");

    int calls = list_calls;
    CHECK(ft_expand(&t, 1) == 1 && t.n == 7, "expand src adds 2 rows");
    CHECK(strcmp(t.rows[2].name, "lib") == 0 && strcmp(t.rows[3].name, "main.c") == 0,
          "children sit under parent, dirs first");
    CHECK(t.rows[2].depth == 1 && strcmp(t.rows[3].rel, "src/main.c") == 0, "child depth and rel");
    CHECK(ft_expand(&t, 1) == 1 && list_calls == calls + 1, "re-expanding does not re-read");
    CHECK(ft_expand(&t, 3) == 0 && t.n == 7, "files do not expand");

    CHECK(ft_expand(&t, 2) == 1 && t.n == 8 && t.rows[3].depth == 2, "nested expand");
    CHECK(ft_parent(&t, 3) == 2 && ft_parent(&t, 2) == 1 && ft_parent(&t, 1) == -1, "parent lookup");
    ft_collapse(&t, 1);
    CHECK(t.n == 5 && !t.rows[1].expanded, "collapse drops all descendants");
    CHECK(ft_toggle(&t, 1) == 1 && t.n == 7, "toggle expands again");
    CHECK(ft_toggle(&t, 1) == 0 && t.n == 5, "toggle collapses");

    int i = ft_reveal(&t, "src/lib/deep.c");
    CHECK(i >= 0 && strcmp(t.rows[i].name, "deep.c") == 0 && t.rows[i].depth == 2, "reveal expands ancestors");
    CHECK(ft_reveal(&t, "src/nope.c") == -1, "reveal of a missing path fails");
    CHECK(ft_find(&t, "docs") == 0 && ft_find(&t, "zzz") == -1, "find");

    ft_collapse(&t, ft_find(&t, "src/lib"));
    int rows_before = t.n;
    ft_refresh(&t);
    CHECK(t.n == rows_before && t.rows[ft_find(&t, "src")].expanded, "refresh keeps expanded folders");
    CHECK(ft_find(&t, "src/lib") >= 0 && !t.rows[ft_find(&t, "src/lib")].expanded, "refresh keeps collapsed ones collapsed");
    ft_free(&t);

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all filetree tests passed\n");
    return 0;
}
