/* Unit tests for the config-file line parser in options.c. Build and run with `make test`. */
#include <stdio.h>
#include <string.h>

#include "options.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL: %s (line %d)\n", what, __LINE__); } \
} while (0)

/* Resets the globals to the defaults the editor starts with. */
static void reset_options(void) {
    options.tab_width = 4;
    options.crlf = 0;
    options.scheme = 0;
    options.line_numbers = 0;
    options.syntax = 1;
    options.wrap = 0;
}

static void test_valid(void) {
    reset_options();
    CHECK(options_parse_line("tab_width=8\n") == 1, "tab_width=8 applied");
    CHECK(options.tab_width == 8, "tab_width value is 8");

    CHECK(options_parse_line("scheme=2\r\n") == 1, "scheme=2 applied (CRLF line)");
    CHECK(options.scheme == 2, "scheme value is 2");

    CHECK(options_parse_line("line_numbers=1") == 1, "line_numbers=1 applied (no newline)");
    CHECK(options.line_numbers == 1, "line_numbers value is 1");

    CHECK(options_parse_line("wrap=1") == 1, "wrap=1 applied");
    CHECK(options.wrap == 1, "wrap value is 1");

    CHECK(options_parse_line("  syntax = 0 \r\n") == 1, "spaces around key and value");
    CHECK(options.syntax == 0, "syntax value is 0");
}

static void test_invalid_values(void) {
    reset_options();
    options.tab_width = 8;
    CHECK(options_parse_line("tab_width=7") == 0, "tab_width=7 rejected");
    CHECK(options.tab_width == 8, "tab_width kept after bad value");
    CHECK(options_parse_line("tab_width=") == 0, "empty value rejected");
    CHECK(options_parse_line("tab_width=4x") == 0, "trailing junk rejected");

    options.scheme = 1;
    CHECK(options_parse_line("scheme=3") == 0, "scheme=3 rejected");
    CHECK(options_parse_line("scheme=-1") == 0, "scheme=-1 rejected");
    CHECK(options.scheme == 1, "scheme kept after bad values");

    options.wrap = 0;
    CHECK(options_parse_line("wrap=yes") == 0, "wrap=yes rejected");
    CHECK(options_parse_line("wrap=2") == 0, "wrap=2 rejected");
    CHECK(options.wrap == 0, "wrap kept after bad values");
}

static void test_ignored_lines(void) {
    reset_options();
    CHECK(options_parse_line("") == 0, "empty line ignored");
    CHECK(options_parse_line("   \r\n") == 0, "blank line ignored");
    CHECK(options_parse_line("# wrap=1") == 0, "comment ignored");
    CHECK(options_parse_line("wrap") == 0, "line without = ignored");
    CHECK(options_parse_line("=1") == 0, "empty key ignored");
    CHECK(options_parse_line("foo=1") == 0, "unknown key ignored");
    CHECK(options.wrap == 0, "comment and unknown keys change nothing");
}

static void test_crlf_not_loaded(void) {
    /* Line endings are per file, so the config must never set them. */
    reset_options();
    options.crlf = 1;
    CHECK(options_parse_line("crlf=0") == 0, "crlf is not a config key");
    CHECK(options.crlf == 1, "crlf left alone by config");
}

int main(void) {
    test_valid();
    test_invalid_values();
    test_ignored_lines();
    test_crlf_not_loaded();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
