/* Unit tests for brace.c. Build and run with `make test`. */
#include <stdio.h>
#include <string.h>

#include "brace.h"

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

/* Makes a Line array from C strings (test helper). */
static Line mk(const char *s) {
    Line l;
    l.text = (char *)s;
    l.len = (int)strlen(s);
    l.cap = l.len + 1;
    return l;
}

int main(void) {
    /* Enter-split is only for {} */
    CHECK(brace_curly_between("{}", 2, 1));
    CHECK(!brace_curly_between("\"\"", 2, 1));
    CHECK(!brace_curly_between("()", 2, 1));
    CHECK(!brace_curly_between("''", 2, 1));

    /* closer table */
    CHECK(brace_closer('(') == ')');
    CHECK(brace_closer('[') == ']');
    CHECK(brace_closer('{') == '}');
    CHECK(brace_closer('"') == '"');
    CHECK(brace_closer('\'') == '\'');
    CHECK(brace_closer('a') == 0);
    CHECK(brace_closer(')') == 0);

    /* autoclose: at end of line, before space, before closer */
    CHECK(brace_should_autoclose("", 0, 0, '('));
    CHECK(brace_should_autoclose("abc", 3, 3, '['));
    CHECK(brace_should_autoclose("a b", 3, 1, '{'));   /* before space */
    CHECK(brace_should_autoclose("()", 2, 1, '('));    /* before closer */
    CHECK(!brace_should_autoclose("abc", 3, 1, '('));  /* before a letter */
    CHECK(!brace_should_autoclose("x", 1, 0, 'x'));     /* not a bracket */

    /* quotes: not inside a word, not after a backslash or the same quote */
    CHECK(brace_should_autoclose("", 0, 0, '"'));
    CHECK(!brace_should_autoclose("don", 3, 3, '\''));
    CHECK(!brace_should_autoclose("\\", 1, 1, '"'));
    CHECK(!brace_should_autoclose("\"", 1, 1, '"'));
    CHECK(brace_should_autoclose("x = ", 4, 4, '"'));

    /* skip over the closer */
    CHECK(brace_is_skip(")", 1, 0, ')'));
    CHECK(brace_is_skip("\"a\"", 3, 2, '"'));
    CHECK(!brace_is_skip(")", 1, 0, ']'));
    CHECK(!brace_is_skip("x", 1, 0, ')'));
    CHECK(!brace_is_skip("", 0, 0, ')'));
    CHECK(!brace_is_skip("(", 1, 0, '('));   /* openers never skip */

    /* empty pair */
    CHECK(brace_pair_between("()", 2, 1));
    CHECK(brace_pair_between("{}", 2, 1));
    CHECK(brace_pair_between("\"\"", 2, 1));
    CHECK(!brace_pair_between("(x)", 3, 1));
    CHECK(!brace_pair_between("()", 2, 0));
    CHECK(!brace_pair_between("()", 2, 2));
    CHECK(!brace_pair_between("", 0, 0));

    /* same-line match, nested */
    {
        Line ls[1];
        int my, mx;
        ls[0] = mk("f(a(b), c)");
        CHECK(brace_find_match(ls, 1, 0, 1, &my, &mx) && my == 0 && mx == 9);   /* ( -> ) */
        CHECK(brace_find_match(ls, 1, 0, 9, &my, &mx) && my == 0 && mx == 1);   /* ) -> ( */
        CHECK(brace_find_match(ls, 1, 0, 3, &my, &mx) && my == 0 && mx == 5);   /* inner ( -> ) */
        CHECK(brace_find_match(ls, 1, 0, 5, &my, &mx) && my == 0 && mx == 3);   /* inner ) -> ( */
        CHECK(!brace_find_match(ls, 1, 0, 0, &my, &mx));                        /* not a bracket */
        CHECK(!brace_find_match(ls, 1, 0, 99, &my, &mx));                       /* out of range */
    }

    /* multi-line match */
    {
        Line ls[4];
        int my, mx;
        ls[0] = mk("if {");
        ls[1] = mk("  x();");
        ls[2] = mk("");
        ls[3] = mk("}");
        CHECK(brace_find_match(ls, 4, 0, 3, &my, &mx) && my == 3 && mx == 0);
        CHECK(brace_find_match(ls, 4, 3, 0, &my, &mx) && my == 0 && mx == 3);
    }

    /* unmatched */
    {
        Line ls[2];
        int my, mx;
        ls[0] = mk("(((");
        ls[1] = mk("))");
        CHECK(!brace_find_match(ls, 2, 0, 0, &my, &mx));   /* opener with no close after depth */
        CHECK(brace_find_match(ls, 2, 1, 1, &my, &mx) && my == 0 && mx == 1);
        ls[0] = mk("a)");
        CHECK(!brace_find_match(ls, 1, 0, 1, &my, &mx));
    }

    /* cursor highlight: under cursor first, then before it */
    {
        Line ls[1];
        int ay, ax, by, bx;
        ls[0] = mk("(x)");
        CHECK(brace_match_for_cursor(ls, 1, 0, 0, &ay, &ax, &by, &bx) && bx == 2);   /* cursor on ( */
        CHECK(brace_match_for_cursor(ls, 1, 0, 3, &ay, &ax, &by, &bx) && bx == 0);   /* cursor after ) */
        CHECK(brace_match_for_cursor(ls, 1, 0, 1, &ay, &ax, &by, &bx) && bx == 2);   /* after ( */
        ls[0] = mk("(xy)");
        CHECK(!brace_match_for_cursor(ls, 1, 0, 2, &ay, &ax, &by, &bx));             /* between x and y */
        CHECK(!brace_match_for_cursor(ls, 1, 5, 0, &ay, &ax, &by, &bx));             /* bad row */
    }

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_brace: all passed\n");
    return 0;
}
