/* Unit tests for linediff.c. Build and run with: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linediff.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

static int run(const char *a, const char *b, int expect_hunks) {
    LdHunk *h; int n;
    if (linediff(a, b, &h, &n) != 0) return 0;
    free(h);
    return n == expect_hunks;
}

int main(void) {
    LdHunk *h; int n;

    CHECK(run("a\nb\nc", "a\nb\nc", 0), "equal: no hunks");
    CHECK(run("a\nb\nc", "a\nX\nc", 1), "one changed line: one hunk");
    CHECK(run("a\nb\nc\nd\ne", "A\nb\nc\nd\nE", 2), "two distant changes: two hunks");

    linediff("a\nb\nc", "a\nX\nc", &h, &n);
    CHECK(n == 1 && h[0].old_start == 1 && h[0].old_count == 1 && h[0].new_count == 1, "change hunk range");
    CHECK(n == 1 && h[0].new_len == 1 && strncmp("a\nX\nc" + h[0].new_off, "X", 1) == 0, "change hunk text");
    free(h);

    linediff("a\nc", "a\nb\nc", &h, &n);
    CHECK(n == 1 && h[0].old_start == 1 && h[0].old_count == 0 && h[0].new_count == 1, "insert hunk");
    free(h);

    linediff("a\nb\nc", "a\nc", &h, &n);
    CHECK(n == 1 && h[0].old_start == 1 && h[0].old_count == 1 && h[0].new_count == 0, "delete hunk");
    free(h);

    linediff("a", "a\nb", &h, &n);
    CHECK(n == 1 && h[0].old_start == 1 && h[0].old_count == 0 && h[0].new_count == 1, "append at end");
    free(h);

    linediff("a\n", "a", &h, &n);   /* trailing newline removed: last empty line goes */
    CHECK(n == 1 && h[0].old_start == 1 && h[0].old_count == 1 && h[0].new_count == 0, "trailing newline removed");
    free(h);

    linediff("", "x\ny", &h, &n);
    CHECK(n == 1 && h[0].old_count == 1 && h[0].new_count == 2 && h[0].new_len == 3, "empty old");
    free(h);

    linediff("a\nb", "a\nb\nc\nd", &h, &n);
    CHECK(n == 1 && h[0].new_count == 2 && h[0].new_len == 3, "multi-line new text range");
    free(h);

    /* Big unrelated inputs hit the size cap and still give one covering hunk. */
    {
        int N = 3000;
        char *a = malloc(N * 3 + 1), *b = malloc(N * 3 + 1);
        a[0] = b[0] = '\0';
        for (int i = 0; i < N; i++) { strcat(a, "a\n"); strcat(b, "b\n"); }
        linediff(a, b, &h, &n);
        CHECK(n == 1 && h[0].old_start == 0 && h[0].old_count == N && h[0].new_count == N, "size cap: one hunk");
        free(h); free(a); free(b);
    }

    if (failures) { printf("test_linediff: %d failed\n", failures); return 1; }
    printf("test_linediff: all passed\n");
    return 0;
}
