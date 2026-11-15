/* Unit tests for fuzzy.c. Build and run with: make test */
#include <stdio.h>
#include "fuzzy.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

int main(void) {
    CHECK(fuzzy_score("", "anything.c") == 0, "empty query matches everything");
    CHECK(fuzzy_score("abc", "xyz.c") == -1, "missing letters do not match");
    CHECK(fuzzy_score("cba", "abc.c") == -1, "order matters");
    CHECK(fuzzy_score("main", "src/main.c") >= 0, "subsequence in path matches");
    CHECK(fuzzy_score("mc", "src/main.c") >= 0, "gapped subsequence matches");
    CHECK(fuzzy_score("MAIN", "src/main.c") >= 0, "case-insensitive match");
    CHECK(fuzzy_score("MaIn", "src/Main.C") >= 0, "case-insensitive both ways");

    /* Basename beats the same letters spread over folders */
    CHECK(fuzzy_score("buf", "src/buffer.c") > fuzzy_score("buf", "build/x/u/f.c"),
          "file name match beats scattered folder match");

    /* Consecutive characters beat gapped ones */
    CHECK(fuzzy_score("buf", "src/buffer.c") > fuzzy_score("buf", "src/b_u_f.c"),
          "consecutive chars beat gapped chars");

    /* Shorter path wins when everything else is equal */
    CHECK(fuzzy_score("ui", "src/ui.c") > fuzzy_score("ui", "src/deep/nested/folder/ui.c"),
          "shorter path ranks higher");

    /* Either separator works in the query */
    CHECK(fuzzy_score("src\\main", "src/main.c") >= 0, "backslash in query matches slash");

    CHECK(fuzzy_score(NULL, "x") == -1, "NULL query is no match");

    printf(failures ? "\n%d FAILED\n" : "\nall fuzzy tests passed\n", failures);
    return failures ? 1 : 0;
}
