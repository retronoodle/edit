/* Unit tests for aiinstr.c. Build and run with: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aiinstr.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

int main(void) {
    char *r = aiinstr_join("base", "A.md", NULL, 100);
    CHECK(r && strcmp(r, "base") == 0, "no content returns base");
    free(r);

    r = aiinstr_join("base", "A.md", "", 100);
    CHECK(r && strcmp(r, "base") == 0, "empty content returns base");
    free(r);

    r = aiinstr_join("base", "A.md", "be nice", 100);
    CHECK(r && strcmp(r, "base\n\nProject instructions from A.md:\nbe nice") == 0, "content appended");
    free(r);

    r = aiinstr_join("base", "A.md", "abcdefghij", 4);
    CHECK(r && strstr(r, "abcd") && !strstr(r, "abcde") && strstr(r, "[truncated]"), "over cap truncated");
    free(r);

    return failures ? 1 : 0;
}
