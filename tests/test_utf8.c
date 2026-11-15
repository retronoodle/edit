/* Unit tests for utf8.c. Build with: make test */
#include <stdio.h>
#include <string.h>
#include "utf8.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
    int cp;
    char buf[4];

    /* decode / encode round trip */
    CHECK(utf8_decode("A", 1, &cp) == 1 && cp == 'A');
    CHECK(utf8_decode("\xC3\xA9", 2, &cp) == 2 && cp == 0xE9);          /* é */
    CHECK(utf8_decode("\xE6\x97\xA5", 3, &cp) == 3 && cp == 0x65E5);    /* 日 */
    CHECK(utf8_decode("\xF0\x9F\x98\x80", 4, &cp) == 4 && cp == 0x1F600); /* 😀 */
    CHECK(utf8_encode(0xE9, buf) == 2 && !memcmp(buf, "\xC3\xA9", 2));
    CHECK(utf8_encode(0x1F600, buf) == 4 && !memcmp(buf, "\xF0\x9F\x98\x80", 4));

    /* invalid input */
    CHECK(utf8_decode("\x80", 1, &cp) == 1 && cp == -1);                 /* stray continuation */
    CHECK(utf8_decode("\xC3", 1, &cp) == 1 && cp == -1);                 /* truncated */
    CHECK(utf8_decode("\xC3\x28", 2, &cp) == 1 && cp == -1);             /* bad continuation */
    CHECK(utf8_decode("\xED\xA0\x80", 3, &cp) == 1 && cp == -1);         /* surrogate */
    CHECK(utf8_decode("\xC0\xAF", 2, &cp) == 1 && cp == -1);             /* overlong */

    /* widths */
    CHECK(cp_width('a') == 1);
    CHECK(cp_width(0xE9) == 1);      /* é */
    CHECK(cp_width(0x65E5) == 2);    /* 日 */
    CHECK(cp_width(0x1F600) == 2);   /* 😀 */
    CHECK(cp_width(0x0301) == 0);    /* combining acute */
    CHECK(cp_width(-1) == 1);

    /* boundaries and columns on "é日é" */
    const char *s = "\xC3\xA9\xE6\x97\xA5" "e\xCC\x81";   /* é(2) 日(3) e+acute(3) = 8 bytes */
    int len = 8;
    CHECK(utf8_next(s, len, 0) == 2);
    CHECK(utf8_next(s, len, 2) == 5);
    CHECK(utf8_next(s, len, 5) == 8);       /* e and combining mark are one char */
    CHECK(utf8_prev(s, len, 8) == 5);
    CHECK(utf8_prev(s, len, 5) == 2);
    CHECK(utf8_prev(s, len, 2) == 0);
    CHECK(utf8_prev(s, len, 0) == 0);

    CHECK(utf8_col(s, len, 0) == 0);
    CHECK(utf8_col(s, len, 2) == 1);         /* after é */
    CHECK(utf8_col(s, len, 5) == 3);         /* after 日 (2 cols) */
    CHECK(utf8_col(s, len, 8) == 4);
    CHECK(utf8_width(s, len) == 4);

    CHECK(utf8_byte_at_col(s, len, 0) == 0);
    CHECK(utf8_byte_at_col(s, len, 1) == 2);
    CHECK(utf8_byte_at_col(s, len, 2) == 2);   /* middle of 日 snaps back */
    CHECK(utf8_byte_at_col(s, len, 3) == 5);
    CHECK(utf8_byte_at_col(s, len, 99) == 8);

    /* invalid byte is one char, one column */
    const char *bad = "a\x80" "b";
    CHECK(utf8_next(bad, 3, 1) == 2);
    CHECK(utf8_width(bad, 3) == 3);

    /* soft wrap at width 4: "abcdef" -> "abcd" | "ef" */
    const char *w6 = "abcdef";
    int st, en, sc, r, c;
    CHECK(utf8_wrap_rows(w6, 6, 4) == 2);
    CHECK(utf8_wrap_rows(w6, 6, 6) == 1);
    CHECK(utf8_wrap_rows(w6, 0, 4) == 1);                     /* empty line still one row */
    utf8_wrap_row(w6, 6, 4, 1, &st, &en, &sc);
    CHECK(st == 4 && en == 6 && sc == 4);
    utf8_wrap_row(w6, 6, 4, 0, &st, &en, &sc);
    CHECK(st == 0 && en == 4 && sc == 0);
    utf8_wrap_row(w6, 6, 4, 2, &st, &en, &sc);                /* past the last row */
    CHECK(st == 6 && en == 6);
    utf8_wrap_cursor(w6, 6, 4, 5, &r, &c);                    /* 'f' is on row 1, col 1 */
    CHECK(r == 1 && c == 1);
    utf8_wrap_cursor(w6, 6, 4, 6, &r, &c);                    /* end of text: after 'f' on row 1 */
    CHECK(r == 1 && c == 2);
    utf8_wrap_cursor(w6, 6, 4, 0, &r, &c);                    /* start of text */
    CHECK(r == 0 && c == 0);

    /* a wide char that does not fit moves to the next row: "a日b" at width 2 */
    const char *wd = "a\xE6\x97\xA5" "b";                    /* a(1) 日(2) b(1) */
    CHECK(utf8_wrap_rows(wd, 5, 2) == 3);                     /* "a" | "日" | "b" */
    CHECK(utf8_wrap_rows(wd, 5, 3) == 2);                     /* "a日" | "b" */
    utf8_wrap_row(wd, 5, 2, 1, &st, &en, &sc);
    CHECK(st == 1 && en == 4 && sc == 1);
    utf8_wrap_cursor(wd, 5, 2, 4, &r, &c);                    /* 'b' starts row 2 */
    CHECK(r == 2 && c == 0);

    /* grapheme clusters: ZWJ family is one char, two columns */
    const char *fam = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"; /* 👨‍👩‍👧 */
    int flen = 18;
    CHECK(utf8_next(fam, flen, 0) == flen);
    CHECK(utf8_width(fam, flen) == 2);
    CHECK(utf8_prev(fam, flen, flen) == 0);

    /* flag: two regional indicators are one char, two columns */
    const char *flag = "\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA";   /* 🇦🇺 */
    CHECK(utf8_next(flag, 8, 0) == 8);
    CHECK(utf8_width(flag, 8) == 2);
    CHECK(utf8_prev(flag, 8, 8) == 0);

    /* three regional indicators: flag, then a lone one */
    const char *ri3 = "\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    CHECK(utf8_next(ri3, 12, 0) == 8);
    CHECK(utf8_next(ri3, 12, 8) == 12);
    CHECK(utf8_prev(ri3, 12, 12) == 8);

    /* skin-tone modifier joins its base */
    const char *thumb = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";   /* 👍🏽 */
    CHECK(utf8_next(thumb, 8, 0) == 8);
    CHECK(utf8_width(thumb, 8) == 2);

    /* ordinary text is unchanged: plain ASCII and a lone ZWJ-free emoji */
    CHECK(utf8_next("ab", 2, 0) == 1);
    CHECK(utf8_next("\xF0\x9F\x98\x80" "a", 5, 0) == 4);

    /* case folding */
    CHECK(cp_fold('A') == 'a' && cp_fold('z') == 'z' && cp_fold('1') == '1');
    CHECK(cp_fold(0xC9) == 0xE9);    /* É -> é */
    CHECK(cp_fold(0xD7) == 0xD7);    /* × is not a letter */
    CHECK(cp_fold(0x100) == 0x101);  /* Ā -> ā */
    CHECK(cp_fold(0x139) == 0x13A);  /* Ĺ -> ĺ */
    CHECK(cp_fold(0x391) == 0x3B1);  /* Α -> α */
    CHECK(cp_fold(0x410) == 0x430);  /* А -> а */
    CHECK(cp_fold(0x400) == 0x450);  /* Ѐ -> ѐ */

    /* folded matching returns the byte end, which can differ from the pattern length */
    const char *ecs = "\xC3\x89" "cole";                         /* École */
    CHECK(utf8_match_fold(ecs, 6, "\xC3\xA9" "cole", 6) == 6);   /* école */
    CHECK(utf8_match_fold("Ecole", 5, "ECOLE", 5) == 5);            /* ASCII folds */
    CHECK(utf8_match_fold("Ecole", 5, "ecole", 5) == 5);
    CHECK(utf8_match_fold("Ecole", 5, "ecol", 4) == 4);           /* prefix match */
    CHECK(utf8_match_fold(ecs, 6, "ecoles", 6) == -1);            /* longer than text */
    CHECK(utf8_match_fold("\xCE\xB1\xCE\xB2", 4, "\xCE\x91", 2) == 2);   /* αβ vs Α */
    CHECK(utf8_match_fold("a", 1, "b", 1) == -1);
    CHECK(utf8_match_fold("a\x80", 2, "a\x81", 2) == -1);         /* invalid bytes differ */
    CHECK(utf8_match_fold("a\x80", 2, "a\x80", 2) == 2);          /* same invalid byte matches */

    if (failures == 0) printf("utf8 tests passed\n");
    return failures ? 1 : 0;
}
