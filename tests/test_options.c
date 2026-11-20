/* Unit tests for the config-file line parser in options.c. Build and run with `make test`. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* setenv / unsetenv under -std=c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
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
    options.brace = 1;
    options.ai_provider[0] = '\0';
    options.ai_model[0] = '\0';
    options.ai_base_url[0] = '\0';
    options.ai_key[0] = '\0';
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

    CHECK(options_parse_line("brace=0") == 1, "brace=0 applied");
    CHECK(options.brace == 0, "brace value is 0");
    CHECK(options_parse_line("brace=2") == 0, "brace=2 rejected");
    CHECK(options.brace == 0, "brace kept after bad value");
}

static void test_invalid_values(void) {
    reset_options();
    options.tab_width = 8;
    CHECK(options_parse_line("tab_width=7") == 0, "tab_width=7 rejected");
    CHECK(options.tab_width == 8, "tab_width kept after bad value");
    CHECK(options_parse_line("tab_width=") == 0, "empty value rejected");
    CHECK(options_parse_line("tab_width=4x") == 0, "trailing junk rejected");

    options.scheme = 1;
    CHECK(options_parse_line("scheme=3") == 1 && options.scheme == 3, "scheme=3 (Slate) accepted");
    CHECK(options_parse_line("scheme=4") == 0, "scheme=4 rejected");
    CHECK(options_parse_line("scheme=-1") == 0, "scheme=-1 rejected");
    CHECK(options.scheme == 3, "scheme kept after bad values");

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

static void test_ai(void) {
    reset_options();
    CHECK(options_parse_line("ai_provider=anthropic") == 1, "ai_provider=anthropic applied");
    CHECK(strcmp(options.ai_provider, "anthropic") == 0, "ai_provider value is anthropic");
    CHECK(options_parse_line("ai_provider=openai\r\n") == 1, "ai_provider=openai applied (CRLF)");
    CHECK(strcmp(options.ai_provider, "openai") == 0, "ai_provider value is openai");

    CHECK(options_parse_line("ai_model=claude-sonnet-5-5") == 1, "ai_model applied");
    CHECK(strcmp(options.ai_model, "claude-sonnet-5-5") == 0, "ai_model value kept");

    CHECK(options_parse_line("ai_base_url=https://x.example/v1?a=b") == 1, "base url with = applied");
    CHECK(strcmp(options.ai_base_url, "https://x.example/v1?a=b") == 0, "base url keeps ? and =");

    CHECK(options_parse_line("ai_key=sk-abc123  \r\n") == 1, "ai_key applied, trailing blanks trimmed");
    CHECK(strcmp(options.ai_key, "sk-abc123") == 0, "ai_key value trimmed");

    /* a long key (over 255 chars) must load whole, not truncated */
    {
        char line[600], longkey[301];
        memset(longkey, 'k', 300);
        longkey[300] = '\0';
        snprintf(line, sizeof(line), "ai_key=%s\n", longkey);
        CHECK(options_parse_line(line) == 1, "300-char ai_key applied");
        CHECK(strcmp(options.ai_key, longkey) == 0, "300-char ai_key not truncated");
    }

    CHECK(options_parse_line("ai_model=") == 1, "empty ai_model accepted");
    CHECK(options.ai_model[0] == '\0', "empty ai_model clears the value");
}

static void test_ai_invalid(void) {
    reset_options();
    strcpy(options.ai_provider, "anthropic");
    CHECK(options_parse_line("ai_provider=gemini") == 0, "unknown provider rejected");
    CHECK(strcmp(options.ai_provider, "anthropic") == 0, "provider kept after bad value");

    char longval[201];
    memset(longval, 'x', 200);
    longval[200] = '\0';
    char line[256];
    snprintf(line, sizeof(line), "ai_model=%s", longval);
    CHECK(options_parse_line(line) == 0, "model longer than field rejected");
    CHECK(options.ai_model[0] == '\0', "model unchanged after too-long value");
}

static void test_ai_key_lookup(void) {
    reset_options();
    strcpy(options.ai_key, "stored-key");

    strcpy(options.ai_provider, "anthropic");
    setenv("ANTHROPIC_API_KEY", "env-key", 1);
    CHECK(strcmp(options_ai_key(), "env-key") == 0, "env var beats stored key (anthropic)");

    setenv("OPENAI_API_KEY", "openai-env", 1);
    CHECK(strcmp(options_ai_key(), "env-key") == 0, "wrong-provider env ignored (anthropic)");

    strcpy(options.ai_provider, "openai");
    CHECK(strcmp(options_ai_key(), "openai-env") == 0, "OPENAI_API_KEY used for openai");

    unsetenv("OPENAI_API_KEY");
    CHECK(strcmp(options_ai_key(), "stored-key") == 0, "stored key used when env unset");

    unsetenv("ANTHROPIC_API_KEY");
    strcpy(options.ai_provider, "anthropic");
    CHECK(strcmp(options_ai_key(), "stored-key") == 0, "stored key used when anthropic env unset");

    options.ai_key[0] = '\0';
    CHECK(options_ai_key()[0] == '\0', "no key at all gives empty string");
}

int main(void) {
    test_valid();
    test_invalid_values();
    test_ignored_lines();
    test_crlf_not_loaded();
    test_ai();
    test_ai_invalid();
    test_ai_key_lookup();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
