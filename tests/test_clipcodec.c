/* Unit tests for clipcodec.c: base64 vectors and paste cleanup. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "clipcodec.h"

static int failures = 0;

static void check_b64(const char *in, const char *want) {
    char *got = clip_b64_encode(in, (int)strlen(in));
    if (!got || strcmp(got, want) != 0) {
        printf("FAIL b64(\"%s\") = \"%s\", want \"%s\"\n", in, got ? got : "(null)", want);
        failures++;
    }
    free(got);
}

static void check_clean(const char *in, const char *want) {
    char buf[256];
    strcpy(buf, in);
    int n = clip_clean_paste(buf, (int)strlen(in));
    if (n != (int)strlen(want) || memcmp(buf, want, n) != 0) {
        buf[n] = '\0';
        printf("FAIL clean(\"%s\") = \"%s\", want \"%s\"\n", in, buf, want);
        failures++;
    }
}

int main(void) {
    check_b64("", "");
    check_b64("f", "Zg==");
    check_b64("fo", "Zm8=");
    check_b64("foo", "Zm9v");
    check_b64("foobar", "Zm9vYmFy");
    check_b64("hello\n", "aGVsbG8K");

    check_clean("a\r\nb", "a\nb");
    check_clean("a\rb", "a\nb");
    check_clean("a\r\n\r\nb", "a\n\nb");
    check_clean("tab\there", "tab\there");
    check_clean("bell\x07 \x1b[x", "bell [x");
    check_clean("caf\xc3\xa9", "caf\xc3\xa9");

    if (failures == 0) printf("test_clipcodec: all passed\n");
    return failures ? 1 : 0;
}
