/* Unit tests for fileops.c, run in a temp folder. Build and run with: make test */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "fileops.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

static int exists(const char *p) { struct stat st; return lstat(p, &st) == 0; }

static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int has_text(const char *path, const char *text) {
    char buf[64] = "";
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, sizeof buf, f)) buf[0] = '\0';
    fclose(f);
    return strcmp(buf, text) == 0;
}

int main(void) {
    const char *home = getenv("HOME");
    char root[512], p[640], out[640], name[700];
    snprintf(root, sizeof root, "%s/.cache/claude-tmp/velo-fileops-test", home ? home : ".");
    snprintf(p, sizeof p, "rm -rf '%s'", root);
    if (system(p) != 0) {}
    snprintf(p, sizeof p, "mkdir -p '%s'", root);
    if (system(p) != 0) { printf("FAIL can't make temp dir\n"); return 1; }

    CHECK(fileops_valid_name("a.c") && fileops_valid_name(".env"), "plain names are valid");
    CHECK(!fileops_valid_name("") && !fileops_valid_name("..") && !fileops_valid_name(".")
          && !fileops_valid_name("a/b") && !fileops_valid_name("a\\b"), "bad names are rejected");

    CHECK(fileops_new_file(root, "a.c", out, sizeof out) == NULL && exists(out), "new file");
    CHECK(fileops_new_file(root, "a.c", out, sizeof out) != NULL, "new file never overwrites");
    CHECK(fileops_new_file(root, "../x", out, sizeof out) != NULL, "new file rejects ..");
    CHECK(fileops_new_dir(root, "sub", out, sizeof out) == NULL && exists(out), "new dir");
    CHECK(fileops_new_dir(root, "sub", out, sizeof out) != NULL, "new dir never overwrites");

    fileops_unique_name(root, "fresh.c", name, sizeof name);
    CHECK(strcmp(name, "fresh.c") == 0, "unique name keeps a free name");
    fileops_unique_name(root, "a.c", name, sizeof name);
    CHECK(strcmp(name, "a copy.c") == 0, "unique name adds ' copy'");
    snprintf(p, sizeof p, "%s/a copy.c", root);
    put(p, "x");
    fileops_unique_name(root, "a.c", name, sizeof name);
    CHECK(strcmp(name, "a copy 2.c") == 0, "unique name counts up");
    snprintf(p, sizeof p, "%s/.env", root);
    put(p, "k=1");
    fileops_unique_name(root, ".env", name, sizeof name);
    CHECK(strcmp(name, ".env copy") == 0, "leading dot is part of the stem");

    /* copy a file into the same folder, and into another */
    snprintf(p, sizeof p, "%s/a.c", root);
    put(p, "hello");
    CHECK(fileops_copy(p, root, out, sizeof out) == NULL && has_text(out, "hello")
          && strstr(out, "a copy 2.c"), "copy file next to the original");
    snprintf(name, sizeof name, "%s/sub", root);
    CHECK(fileops_copy(p, name, out, sizeof out) == NULL && has_text(out, "hello"), "copy file into a folder");

    /* copy a folder tree */
    char deep[700];
    snprintf(p, sizeof p, "%s/tree", root);
    mkdir(p, 0777);
    snprintf(deep, sizeof deep, "%s/tree/inner", root);
    mkdir(deep, 0777);
    snprintf(deep, sizeof deep, "%s/tree/inner/f.txt", root);
    put(deep, "deep");
    snprintf(name, sizeof name, "%s/sub", root);
    CHECK(fileops_copy(p, name, out, sizeof out) == NULL, "copy folder");
    snprintf(deep, sizeof deep, "%s/inner/f.txt", out);
    CHECK(has_text(deep, "deep"), "copy folder is recursive");
    CHECK(fileops_copy(p, root, out, sizeof out) == NULL && strstr(out, "tree copy"), "copy folder beside itself");

    snprintf(name, sizeof name, "%s/tree/inner", root);
    CHECK(fileops_copy(p, name, out, sizeof out) != NULL, "refuses to copy a folder into itself");
    CHECK(fileops_copy(p, p, out, sizeof out) != NULL, "refuses to copy a folder onto itself");
    snprintf(name, sizeof name, "%s/a.c", root);
    CHECK(fileops_copy(p, name, out, sizeof out) != NULL, "target must be a folder");

    snprintf(p, sizeof p, "rm -rf '%s'", root);
    if (system(p) != 0) {}
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all fileops tests passed\n");
    return 0;
}
