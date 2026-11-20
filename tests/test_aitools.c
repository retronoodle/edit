/* Unit tests for aitools.c: the path sandbox and the read-only tools, run in a temp folder. */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "aitools.h"

static int failures = 0;

static void expect(const char *what, int ok) {
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void expect_has(const char *what, char *got, const char *needle) {
    if (!got || !strstr(got, needle)) {
        printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got ? got : "(null)", needle);
        failures++;
    }
    free(got);
}

static void expect_lacks(const char *what, char *got, const char *needle) {
    if (!got || strstr(got, needle)) {
        printf("FAIL %s: got \"%s\", must not have \"%s\"\n", what, got ? got : "(null)", needle);
        failures++;
    }
    free(got);
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

int main(void) {
    expect("ok plain", aitools_path_ok("src/a.c") && aitools_path_ok("") && aitools_path_ok("./a.c"));
    expect("no ..", !aitools_path_ok("../x") && !aitools_path_ok("a/../b"));
    expect("no abs", !aitools_path_ok("/etc/passwd") && !aitools_path_ok("C:\\x") && !aitools_path_ok("a\\b"));
    expect("no hidden", !aitools_path_ok(".env") && !aitools_path_ok("a/.git/config"));
    expect("no skipped", !aitools_path_ok("node_modules/x") && !aitools_path_ok("build/a.o"));

    char outer[] = "/tmp/velo-aitools-XXXXXX";
    if (!mkdtemp(outer)) { printf("FAIL mkdtemp\n"); return 1; }
    char buf[600];
    snprintf(buf, sizeof buf, "%s/proj", outer); mkdir(buf, 0700);
    snprintf(buf, sizeof buf, "%s/proj/src", outer); mkdir(buf, 0700);
    snprintf(buf, sizeof buf, "%s/proj/build", outer); mkdir(buf, 0700);
    snprintf(buf, sizeof buf, "%s/secret.txt", outer); write_file(buf, "TOPSECRET\n");
    snprintf(buf, sizeof buf, "%s/proj/src/a.c", outer); write_file(buf, "one\ntwo needle\nthree\nfour\n");
    snprintf(buf, sizeof buf, "%s/proj/.env", outer); write_file(buf, "needle=KEY\n");
    snprintf(buf, sizeof buf, "%s/proj/build/x.c", outer); write_file(buf, "needle\n");
    snprintf(buf, sizeof buf, "%s/proj/bin.dat", outer); write_file(buf, "ab\0cd");
    {
        FILE *f = fopen(buf, "wb"); fwrite("a\0b needle", 1, 10, f); fclose(f);
    }
    char link[600], target[600];
    snprintf(link, sizeof link, "%s/proj/escape", outer);
    snprintf(target, sizeof target, "%s", outer);
    symlink(target, link);

    snprintf(buf, sizeof buf, "%s/proj", outer);
    if (chdir(buf) != 0) { printf("FAIL chdir\n"); return 1; }
    aitools_set_root();

    expect_has("list root", aitools_list_dir(""), "src/");
    expect_lacks("list hides hidden", aitools_list_dir(""), ".env");
    expect_lacks("list hides build", aitools_list_dir(""), "build");
    expect_has("list escape", aitools_list_dir("escape/.."), "Error");
    expect_has("read all", aitools_read_file("src/a.c", 0, 0), "2: two needle");
    expect_lacks("read range", aitools_read_file("src/a.c", 3, 1), "2: two");
    expect_has("read range more", aitools_read_file("src/a.c", 2, 1), "start_line=3");
    expect_has("read hidden", aitools_read_file(".env", 0, 0), "Error");
    expect_has("read binary", aitools_read_file("bin.dat", 0, 0), "binary");
    expect_has("read via symlink", aitools_read_file("escape/secret.txt", 0, 0), "Error");
    expect_has("read missing", aitools_read_file("nope.c", 0, 0), "Error");
    expect_has("grep hit", aitools_grep("needle", ""), "src/a.c:2: two needle");
    expect_lacks("grep skips hidden/build", aitools_grep("needle", ""), "KEY");
    expect_lacks("grep skips build", aitools_grep("needle", ""), "build/");
    expect_lacks("grep no symlink walk", aitools_grep("TOPSECRET", ""), "secret");
    expect_has("grep none", aitools_grep("zzz", ""), "no matches");
    expect_has("grep file", aitools_grep("three", "src/a.c"), "src/a.c:3: three");
    expect_has("run list", aitools_run("list_dir", "{}"), "src/");
    expect_has("run read", aitools_run("read_file", "{\"path\":\"src/a.c\",\"start_line\":4}"), "4: four");
    expect("run unknown", aitools_run("rm_rf", "{}") == NULL);

    if (failures == 0) printf("test_aitools: all passed\n");
    return failures ? 1 : 0;
}
