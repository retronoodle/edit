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

static void check_body(char *got, const char *want) {
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL body = \"%s\", want \"%s\"\n", got ? got : "(null)", want);
        failures++;
    }
    free(got);
}

static void check_url(const char *base, const char *want) {
    char *got = ai_openai_url(base);
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL url(\"%s\") = \"%s\", want \"%s\"\n", base ? base : "(null)",
               got ? got : "(null)", want);
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

static void check_fence(const char *in, const char *want) {
    char *got = ai_strip_fences(in);
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL strip_fences(\"%s\") = \"%s\", want \"%s\"\n", in, got ? got : "(null)", want);
        failures++;
    }
    free(got);
}

static void check_input(const char *ins, const char *sel, const char *before, const char *after,
                        const char *want) {
    char *got = ai_build_ask_input(ins, sel, before, after);
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL ask_input: got \"%s\", want \"%s\"\n", got ? got : "(null)", want);
        failures++;
    }
    free(got);
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

    /* Request bodies: exact shape */
    check_body(ai_build_anthropic_body("claude-x", "", "hi", 100),
               "{\"model\":\"claude-x\",\"max_tokens\":100,\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}");
    check_body(ai_build_anthropic_body("claude-x", "Be terse", "hi", 100),
               "{\"model\":\"claude-x\",\"max_tokens\":100,\"system\":\"Be terse\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}");
    check_body(ai_build_openai_body("m", "S", "I", 50),
               "{\"model\":\"m\",\"max_tokens\":50,\"messages\":[{\"role\":\"system\",\"content\":\"S\"},{\"role\":\"user\",\"content\":\"I\"}]}");
    check_body(ai_build_openai_body("m", "", "I", 50),
               "{\"model\":\"m\",\"max_tokens\":50,\"messages\":[{\"role\":\"user\",\"content\":\"I\"}]}");

    /* Request bodies: escaping survives a round trip, NULL input becomes "" */
    {
        const char *sys = "sys \"q\"\n";
        const char *in = "in\\x\t\xc3\xa9";
        char *a = ai_build_anthropic_body("m", sys, in, 1);
        check_get(a, "model", "m");
        check_get(a, "system", sys);
        check_get(a, "messages.0.content", in);
        free(a);

        char *o = ai_build_openai_body("m", sys, in, 1);
        check_get(o, "messages.0.content", sys);
        check_get(o, "messages.1.content", in);
        free(o);

        char *n = ai_build_anthropic_body("m", NULL, NULL, 1);
        check_get(n, "messages.0.content", "");
        check_get(n, "system", NULL);
        free(n);
    }

    /* OpenAI-compatible URL */
    check_url(NULL, "https://api.openai.com/v1/chat/completions");
    check_url("", "https://api.openai.com/v1/chat/completions");
    check_url("http://localhost:11434/v1", "http://localhost:11434/v1/chat/completions");
    check_url("https://openrouter.ai/api/v1/", "https://openrouter.ai/api/v1/chat/completions");
    check_url("https://x.test/v1///", "https://x.test/v1/chat/completions");
    check_url("https://x.test/v1/chat/completions", "https://x.test/v1/chat/completions");
    check_url("  http://h/v1  ", "http://h/v1/chat/completions");

    /* curl config: url and headers per provider, values escaped */
    {
        char *c = ai_curl_config("anthropic", "https://a.test/v1", "k\"1");
        const char *want = "url = \"https://a.test/v1\"\n"
                           "header = \"content-type: application/json\"\n"
                           "header = \"x-api-key: k\\\"1\"\n"
                           "header = \"anthropic-version: 2023-06-01\"\n";
        if (!c || strcmp(c, want) != 0) {
            printf("FAIL curl_config anthropic = \"%s\"\n", c ? c : "(null)");
            failures++;
        }
        free(c);

        c = ai_curl_config("openai", "https://o.test/chat/completions", "sk");
        want = "url = \"https://o.test/chat/completions\"\n"
               "header = \"content-type: application/json\"\n"
               "header = \"authorization: Bearer sk\"\n";
        if (!c || strcmp(c, want) != 0) {
            printf("FAIL curl_config openai = \"%s\"\n", c ? c : "(null)");
            failures++;
        }
        free(c);
    }

    /* Reply parsing: text, empty text, API error, junk */
    {
        char err[128];
        char *r = ai_parse_reply("anthropic", "{\"content\":[{\"type\":\"text\",\"text\":\"Hi\"}]}", err, sizeof err);
        if (!r || strcmp(r, "Hi") != 0) { printf("FAIL parse anthropic\n"); failures++; }
        free(r);

        r = ai_parse_reply("openai", "{\"choices\":[{\"message\":{\"content\":\"Yo\"}}]}", err, sizeof err);
        if (!r || strcmp(r, "Yo") != 0) { printf("FAIL parse openai\n"); failures++; }
        free(r);

        r = ai_parse_reply("openai", "{\"choices\":[{\"message\":{\"content\":\"\"}}]}", err, sizeof err);
        if (r || strcmp(err, "The AI returned no text") != 0) { printf("FAIL parse empty: %s\n", err); failures++; }

        r = ai_parse_reply("anthropic", "{\"error\":{\"message\":\"bad key\"}}", err, sizeof err);
        if (r || strcmp(err, "AI error: bad key") != 0) { printf("FAIL parse api error: %s\n", err); failures++; }

        r = ai_parse_reply("anthropic", "<html>oops", err, sizeof err);
        if (r || strcmp(err, "Unexpected reply from the AI") != 0) { printf("FAIL parse junk: %s\n", err); failures++; }
    }

    /* curl exit codes */
    if (!ai_curl_error(7) || strstr(ai_curl_error(7), "connect") == NULL) { printf("FAIL curl 7\n"); failures++; }
    if (!ai_curl_error(28) || strstr(ai_curl_error(28), "timed out") == NULL) { printf("FAIL curl 28\n"); failures++; }
    if (ai_curl_error(99) != NULL) { printf("FAIL curl 99\n"); failures++; }

    /* Ask AI: fence stripping */
    check_fence("```c\nint x;\n```", "int x;");
    check_fence("```\nabc\n```\n", "abc");
    check_fence("\n  ```\nline1\nline2\n```  \n", "line1\nline2");
    check_fence("```\r\nwin\r\n```", "win");
    check_fence("```\n```", "");
    check_fence("plain text", "plain text");
    check_fence("a ```b``` c", "a ```b``` c");
    check_fence("```", "```");
    check_fence("", "");
    if (ai_strip_fences(NULL) != NULL) { printf("FAIL strip NULL\n"); failures++; }

    /* Ask AI: user message */
    check_input("make it loud", "hi", "", "",
                "Instruction: make it loud\n\nSelected text (to be replaced):\nhi\n");
    check_input("x", "", "A", "B",
                "Instruction: x\n\nText before:\nA\n\n"
                "No text is selected; the reply is inserted at the cursor.\n\n"
                "Text after:\nB\n");
    check_input("y", NULL, NULL, NULL,
                "Instruction: y\n\nNo text is selected; the reply is inserted at the cursor.\n");

    if (failures == 0) printf("test_ai: all passed\n");
    return failures ? 1 : 0;
}
