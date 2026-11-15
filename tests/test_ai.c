/* Unit tests for ai.c: JSON escape/unescape, reply lookup and error extraction. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ai.h"

static int failures = 0;

static void check_esc(const char *in, const char *want) {
    char *got = ai_json_escape(in, (int)strlen(in));
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL escape(\"%s\") = \"%s\", want \"%s\"\n", in, got ? got : "(null)", want);
        failures++;
    }
    free(got);
}

static void check_unesc(const char *in, const char *want) {
    char *got = ai_json_unescape(in, (int)strlen(in));
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL unescape(\"%s\") = \"%s\", want \"%s\"\n", in, got ? got : "(null)", want);
        failures++;
    }
    free(got);
}

static void check_unesc_fails(const char *in) {
    char *got = ai_json_unescape(in, (int)strlen(in));
    if (got) {
        printf("FAIL unescape(\"%s\") = \"%s\", want NULL\n", in, got);
        failures++;
    }
    free(got);
}

static void check_get(const char *json, const char *path, const char *want) {
    char *got = ai_json_get_string(json, path);
    int ok = want ? (got && strcmp(got, want) == 0) : (got == NULL);
    if (!ok) {
        printf("FAIL get(\"%s\", \"%s\") = \"%s\", want \"%s\"\n", json, path,
               got ? got : "(null)", want ? want : "(null)");
        failures++;
    }
    free(got);
}

static void check_error(const char *json, const char *want) {
    char *got = ai_json_error(json);
    int ok = want ? (got && strcmp(got, want) == 0) : (got == NULL);
    if (!ok) {
        printf("FAIL error(\"%s\") = \"%s\", want \"%s\"\n", json, got ? got : "(null)",
               want ? want : "(null)");
        failures++;
    }
    free(got);
}

static void check_round_trip(const char *in, int len) {
    char *esc = ai_json_escape(in, len);
    char *back = esc ? ai_json_unescape(esc, (int)strlen(esc)) : NULL;
    if (!back || memcmp(back, in, len) != 0 || back[len] != '\0') {
        printf("FAIL round trip of \"%s\"\n", in);
        failures++;
    }
    free(esc);
    free(back);
}

int main(void) {
    /* Escape */
    check_esc("", "");
    check_esc("a\"b\\c", "a\\\"b\\\\c");
    check_esc("\n\t\r", "\\n\\t\\r");
    check_esc("\x01", "\\u0001");
    check_esc("\x1f", "\\u001f");
    check_esc("caf\xc3\xa9", "caf\xc3\xa9");

    /* Unescape */
    check_unesc("", "");
    check_unesc("a\\\"b", "a\"b");
    check_unesc("\\n\\t\\/\\\\", "\n\t/\\");
    check_unesc("\\u00e9", "\xc3\xa9");
    check_unesc("\\u20ac", "\xe2\x82\xac");
    check_unesc("\\ud83d\\ude00", "\xf0\x9f\x98\x80");
    check_unesc("\\ud83d", "\xef\xbf\xbd");
    check_unesc("\\ude00", "\xef\xbf\xbd");
    check_unesc("\\ud83d\\u0041", "\xef\xbf\xbd" "A");
    check_unesc_fails("\\x");
    check_unesc_fails("\\u12");
    check_unesc_fails("trailing\\");

    /* Escape then unescape gives back the original bytes */
    check_round_trip("tab\there \"q\" \\ \x02 caf\xc3\xa9", (int)strlen("tab\there \"q\" \\ \x02 caf\xc3\xa9"));
    check_round_trip("line1\nline2\r\n", 13);

    /* Reply text lookup */
    check_get("{\"content\":[{\"type\":\"text\",\"text\":\"hi\\n\"}]}", "content.0.text", "hi\n");
    check_get("{\"id\":\"x\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Hello\"}}]}",
              "choices.0.message.content", "Hello");
    check_get("{\"a\":{\"b\":[1,2,{\"x\":\"no\"}],\"c\":true,\"d\":null},\"target\":\"yes\"}", "target", "yes");
    check_get("{\"k\":\"a \\\"}{\\\" b\",\"v\":\"ok\"}", "v", "ok");
    check_get("{\"we\\\"ird\":\"z\"}", "we\"ird", "z");
    check_get("{ \"content\" : [ { \"text\" : \"sp\" } ] }", "content.0.text", "sp");
    check_get("{\"content\":[{\"text\":\"x\"}]}", "content.1.text", NULL);
    check_get("{\"content\":[]}", "content.0.text", NULL);
    check_get("{\"a\":1}", "b", NULL);
    check_get("{\"a\":{\"b\":1}}", "a", NULL);
    check_get("{\"content\":null}", "content.0.text", NULL);
    check_get("{\"a\":", "a", NULL);
    check_get("{\"a\":\"x\"}", "", NULL);
    check_get("{\"a\":\"x\"}", "a.", NULL);

    /* Error message */
    check_error("{\"error\":{\"message\":\"bad key\"}}", "bad key");
    check_error("{\"error\":\"oops\"}", "oops");
    check_error("{\"message\":\"top\"}", "top");
    check_error("{\"ok\":true}", NULL);

    if (failures == 0) printf("test_ai: all passed\n");
    return failures ? 1 : 0;
}
