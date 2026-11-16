/* Unit tests for diffcap.c. Build and run with: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diffcap.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

int main(void) {
    char *r;

    /* Input within budget comes back unchanged */
    r = diffcap_trim("diff --git a/x b/x\n+one\n", 1000);
    CHECK(r && strcmp(r, "diff --git a/x b/x\n+one\n") == 0, "short diff unchanged");
    free(r);

    /* Empty input */
    r = diffcap_trim("", 100);
    CHECK(r && r[0] == '\0', "empty diff gives empty output");
    free(r);

    /* Three files, a long middle one: every header survives, the middle one is cut,
       and the short file after it still gets its body */
    char diff[8192];
    size_t n = 0;
    n += snprintf(diff + n, sizeof diff - n, "diff --git a/one.c b/one.c\n+a\n");
    n += snprintf(diff + n, sizeof diff - n, "diff --git a/two.c b/two.c\n");
    for (int i = 0; i < 200; i++) n += snprintf(diff + n, sizeof diff - n, "+line %d of two\n", i);
    n += snprintf(diff + n, sizeof diff - n, "diff --git a/three.c b/three.c\n+c\n");

    r = diffcap_trim(diff, 1000);
    CHECK(r && strstr(r, "diff --git a/one.c") && strstr(r, "+a\n"), "first file kept whole");
    CHECK(r && strstr(r, "diff --git a/two.c"), "long file's header kept");
    CHECK(r && strstr(r, "lines omitted]"), "long file marked as cut");
    CHECK(r && strstr(r, "diff --git a/three.c") && strstr(r, "+c\n"), "file after the long one kept whole");
    CHECK(r && strlen(r) < strlen(diff), "output shorter than input");
    CHECK(r && strlen(r) <= 1000 + 64, "output within budget (plus marker slack)");
    free(r);

    /* Budget 0: every file still shows its header line */
    r = diffcap_trim(diff, 0);
    CHECK(r && strstr(r, "diff --git a/one.c") && strstr(r, "diff --git a/two.c")
              && strstr(r, "diff --git a/three.c"), "budget 0 keeps every header");
    free(r);

    /* One huge file: starts with its header, ends at a line boundary, marked */
    char big[8192];
    n = 0;
    n += snprintf(big + n, sizeof big - n, "diff --git a/big.c b/big.c\n");
    for (int i = 0; i < 300; i++) n += snprintf(big + n, sizeof big - n, "+row %d\n", i);
    r = diffcap_trim(big, 500);
    CHECK(r && strncmp(r, "diff --git a/big.c", 18) == 0, "single file keeps its header first");
    CHECK(r && strstr(r, "lines omitted]\n") != NULL, "single file marked as cut");
    CHECK(r && r[strlen(r) - 1] == '\n', "cut ends on a line boundary");
    free(r);

    /* diffcap_head */
    r = diffcap_head("a\nb\nc\nd\n", 5, "[cut]\n");
    CHECK(r && strcmp(r, "a\nb\n[cut]\n") == 0, "head cuts to whole lines and adds marker");
    free(r);

    r = diffcap_head("a\nb\n", 100, "[cut]\n");
    CHECK(r && strcmp(r, "a\nb\n") == 0, "head within budget unchanged, no marker");
    free(r);

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
