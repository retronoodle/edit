/* Unit tests for plans.c: slugs, saving and pruning. */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plans.h"

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL %s\n", msg); failures++; } } while (0)

static void slug_is(const char *in, const char *want) {
    char s[64];
    plans_slug(in, s, sizeof s);
    if (strcmp(s, want) != 0) { printf("FAIL slug(\"%s\") = \"%s\", want \"%s\"\n", in ? in : "(null)", s, want); failures++; }
}

int main(void) {
    slug_is("# Plan mode for the AI chat\nmore", "plan-mode-for-the-ai-chat");
    slug_is("\n\n  ## Fix: foo/bar!!  ", "fix-foo-bar");
    slug_is("", "plan");
    slug_is("###\n---\n", "plan");
    slug_is(NULL, "plan");
    slug_is("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    char tiny[4];
    plans_slug("hello world", tiny, sizeof tiny);
    CHECK(strcmp(tiny, "hel") == 0, "slug truncates to the buffer");

    char tmpl[] = "/tmp/velo_plans_XXXXXX";
    const char *base = getenv("VELO_TEST_TMP");
    char alt[256];
    if (base) { snprintf(alt, sizeof alt, "%s/velo_plans_XXXXXX", base); strcpy(tmpl, alt); }
    char *home = mkdtemp(tmpl);
    if (!home) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("USERPROFILE", home, 1);
    setenv("HOME", home, 1);

    char path[600];
    CHECK(plans_save("# First plan\nstep 1", path, sizeof path), "save works");
    FILE *f = fopen(path, "r");
    char buf[64] = "";
    if (f) { size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = '\0'; fclose(f); }
    CHECK(strcmp(buf, "# First plan\nstep 1\n") == 0, "file holds the plan plus a newline");
    CHECK(strstr(path, "-first-plan.md") != NULL, "file name has the slug");

    /* Same second, same title: no overwrite. */
    char p2[600];
    CHECK(plans_save("# First plan\nstep 1", p2, sizeof p2) && strcmp(path, p2) != 0, "no overwrite");

    /* Pruning keeps the newest PLANS_KEEP. */
    char dir[600];
    snprintf(dir, sizeof dir, "%s/.velo_plans", home);
    for (int i = 0; i < PLANS_KEEP + 5; i++) {
        char name[700];
        snprintf(name, sizeof name, "%s/19990101-0000%02d-old.md", dir, i);
        FILE *o = fopen(name, "w");
        if (o) fclose(o);
    }
    CHECK(plans_save("# Newest", path, sizeof path), "save after many");
    int count = 0, has_new = 0;
    char cmd[700];
    snprintf(cmd, sizeof cmd, "ls %s", dir);
    FILE *ls = popen(cmd, "r");
    char line[300];
    while (ls && fgets(line, sizeof line, ls)) {
        count++;
        if (strstr(line, "newest")) has_new = 1;
    }
    if (ls) pclose(ls);
    CHECK(count == PLANS_KEEP, "prunes to PLANS_KEEP");
    CHECK(has_new, "newest plan survives pruning");

    snprintf(cmd, sizeof cmd, "rm -rf %s", home);
    if (system(cmd) != 0) printf("note: cleanup failed\n");
    if (failures == 0) printf("test_plans: all passed\n");
    return failures ? 1 : 0;
}
