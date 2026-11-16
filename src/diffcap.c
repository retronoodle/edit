/* Text shrinking for AI prompts: per-file diff budget and a line-capped head. Pure, no I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diffcap.h"

typedef struct {
    char *s;
    size_t n, cap;
} Buf;

/* Appends n bytes and keeps the buffer NUL-terminated. 0 on out of memory. */
static int buf_add(Buf *b, const char *p, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (b->n + n + 1 > cap) cap *= 2;
        char *s = realloc(b->s, cap);
        if (!s) return 0;
        b->s = s;
        b->cap = cap;
    }
    memcpy(b->s + b->n, p, n);
    b->n += n;
    b->s[b->n] = '\0';
    return 1;
}

static int is_header(const char *p, const char *end) {
    return (size_t)(end - p) >= 11 && memcmp(p, "diff --git ", 11) == 0;
}

/* Length of the first line of s..e, newline included. */
static size_t first_line(const char *s, const char *e) {
    const char *nl = memchr(s, '\n', (size_t)(e - s));
    return nl ? (size_t)(nl - s) + 1 : (size_t)(e - s);
}

char *diffcap_trim(const char *diff, size_t budget) {
    const char *end = diff + strlen(diff);
    size_t len = (size_t)(end - diff);
    Buf out = {NULL, 0, 0};

    if (!buf_add(&out, "", 0)) return NULL;
    if (len <= budget) {
        if (!buf_add(&out, diff, len)) { free(out.s); return NULL; }
        return out.s;
    }

    /* Section starts: the text before the first header (if any), then each "diff --git" line */
    size_t cap = 2;
    for (const char *q = diff; q < end; q++) if (*q == '\n') cap++;
    const char **starts = malloc(cap * sizeof *starts);
    if (!starts) { free(out.s); return NULL; }
    size_t nsec = 0;
    for (const char *p = diff; p < end; ) {
        if (p == diff || (p[-1] == '\n' && is_header(p, end))) starts[nsec++] = p;
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        p = nl ? nl + 1 : end;
    }

    size_t remaining = budget;
    for (size_t i = 0; i < nsec; i++) {
        const char *s = starts[i];
        const char *e = i + 1 < nsec ? starts[i + 1] : end;
        size_t seg = (size_t)(e - s);
        size_t allow = remaining / (nsec - i);
        size_t before = out.n;

        if (seg <= allow) {
            if (!buf_add(&out, s, seg)) goto fail;
        } else {
            size_t cut = allow;
            size_t head = first_line(s, e);
            if (cut < head) cut = head;   /* every file keeps its header line */
            else while (cut > head && s[cut - 1] != '\n') cut--;

            size_t omitted = 0;
            for (const char *q = s + cut; q < e; q++) if (*q == '\n') omitted++;
            if (e > s + cut && e[-1] != '\n') omitted++;

            if (!buf_add(&out, s, cut)) goto fail;
            if (omitted) {
                char mk[64];
                int w = snprintf(mk, sizeof mk, "[... %lu lines omitted]\n", (unsigned long)omitted);
                if (!buf_add(&out, mk, (size_t)w)) goto fail;
            }
        }

        size_t used = out.n - before;
        remaining = used > remaining ? 0 : remaining - used;
    }

    free(starts);
    return out.s;

fail:
    free(starts);
    free(out.s);
    return NULL;
}

char *diffcap_head(const char *text, size_t budget, const char *marker) {
    size_t len = strlen(text);
    Buf out = {NULL, 0, 0};

    if (!buf_add(&out, "", 0)) return NULL;
    if (len <= budget) {
        if (!buf_add(&out, text, len)) { free(out.s); return NULL; }
        return out.s;
    }

    size_t cut = budget;
    while (cut > 0 && text[cut - 1] != '\n') cut--;
    if (cut == 0) cut = budget;   /* one line longer than the budget: cut it mid-line */

    if (!buf_add(&out, text, cut) || !buf_add(&out, marker, strlen(marker))) {
        free(out.s);
        return NULL;
    }
    return out.s;
}
