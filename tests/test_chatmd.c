#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chatmd.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* Rows of t at width cw joined with '|'. */
static void wrap(const char *t, int cw, int word, char *out) {
    int len = (int)strlen(t), pos = 0;
    out[0] = '\0';
    do {
        int e, nx = chatmd_row(t, len, pos, cw, word, &e);
        if (out[0]) strcat(out, "|");
        strncat(out, t + pos, e - pos);
        pos = nx;
    } while (pos < len);
}

static void md(const char *in, char *text, char *style) {
    char *t;
    unsigned char *s;
    CHECK(chatmd_render(in, &t, &s));
    strcpy(text, t);
    for (size_t i = 0; i < strlen(t); i++) style[i] = (char)('0' + s[i]);
    style[strlen(t)] = '\0';
    free(t);
    free(s);
}

int main(void) {
    char o[256], st[256];

    wrap("hello big world", 8, 1, o);  CHECK(!strcmp(o, "hello|big|world"));
    wrap("hello big world", 9, 1, o);  CHECK(!strcmp(o, "hello big|world"));
    wrap("abcdefghij", 4, 1, o);       CHECK(!strcmp(o, "abcd|efgh|ij"));
    wrap("ab cdefghij", 4, 1, o);      CHECK(!strcmp(o, "ab|cdef|ghij"));
    wrap("hello big", 8, 0, o);        CHECK(!strcmp(o, "hello bi|g"));
    wrap("", 5, 1, o);                 CHECK(!strcmp(o, ""));
    wrap("\xe4\xbd\xa0\xe5\xa5\xbd", 2, 1, o); CHECK(!strcmp(o, "\xe4\xbd\xa0|\xe5\xa5\xbd"));

    md("a **b** c", o, st);     CHECK(!strcmp(o, "a b c") && !strcmp(st, "00100"));
    md("*i* and `c`", o, st);   CHECK(!strcmp(o, "i and c") && !strcmp(st, "2000004"));
    md("# Title", o, st);       CHECK(!strcmp(o, "Title") && !strcmp(st, "99999"));
    md("- one\n- two", o, st);  CHECK(!strcmp(o, "\xe2\x80\xa2 one\n\xe2\x80\xa2 two"));
    md("> quote", o, st);       CHECK(!strncmp(o, "\xe2\x94\x82 quote", 9));
    md("[hi](http://x)", o, st); CHECK(!strcmp(o, "hi (http://x)"));
    md("```c\nint *x;\n```\nafter", o, st);
    CHECK(!strcmp(o, "int *x;\nafter") && st[0] == '4' && st[8] == '0');
    md("2 * 3 and **open", o, st);  CHECK(!strcmp(o, "2 * 3 and **open"));
    md("snake_case_name", o, st);   CHECK(!strcmp(o, "snake_case_name"));
    md("a `tick", o, st);           CHECK(!strcmp(o, "a `tick"));

    if (fails == 0) printf("test_chatmd: all passed\n");
    return fails != 0;
}
