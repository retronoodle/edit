/* Pure line diff: trims the common head and tail, then an LCS over the middle. */
#include <stdlib.h>
#include <string.h>
#include "linediff.h"

#define LCS_CELLS_MAX 4000000L

typedef struct { const char *p; int len; } Ln;

static Ln *split(const char *s, int *n) {
    int cnt = 1;
    for (const char *c = s; *c; c++) if (*c == '\n') cnt++;
    Ln *v = malloc(sizeof *v * cnt);
    if (!v) return NULL;
    int k = 0;
    const char *start = s;
    for (const char *c = s;; c++) {
        if (*c == '\n' || !*c) {
            v[k].p = start;
            v[k].len = (int)(c - start);
            k++;
            start = c + 1;
            if (!*c) break;
        }
    }
    *n = k;
    return v;
}

static int same(const Ln *x, const Ln *y) {
    return x->len == y->len && memcmp(x->p, y->p, x->len) == 0;
}

/* Appends/extends a hunk; adjacent changes merge. */
static int add(LdHunk **h, int *n, int *cap, int os, int oc, int ns, int nc) {
    if (*n > 0) {
        LdHunk *l = &(*h)[*n - 1];
        if (l->old_start + l->old_count == os && l->new_start + l->new_count == ns) {
            l->old_count += oc;
            l->new_count += nc;
            return 0;
        }
    }
    if (*n == *cap) {
        int c = *cap ? *cap * 2 : 8;
        LdHunk *t = realloc(*h, sizeof **h * c);
        if (!t) return -1;
        *h = t;
        *cap = c;
    }
    LdHunk x = { os, oc, ns, nc, 0, 0 };
    (*h)[(*n)++] = x;
    return 0;
}

int linediff(const char *a, const char *b, LdHunk **out, int *nout) {
    *out = NULL;
    *nout = 0;
    int na = 0, nb = 0, rc = -1, cap = 0, nh = 0;
    LdHunk *h = NULL;
    Ln *A = split(a, &na), *B = split(b, &nb);
    int *t = NULL;
    if (!A || !B) goto done;

    int pre = 0;
    while (pre < na && pre < nb && same(&A[pre], &B[pre])) pre++;
    int suf = 0;
    while (suf < na - pre && suf < nb - pre && same(&A[na - 1 - suf], &B[nb - 1 - suf])) suf++;
    int ma = na - pre - suf, mb = nb - pre - suf;   /* the middle */
    const Ln *a0 = A + pre, *b0 = B + pre;

    if (ma > 0 || mb > 0) {
        if (ma == 0 || mb == 0 || (long long)(ma + 1) * (mb + 1) > LCS_CELLS_MAX) {
            if (add(&h, &nh, &cap, pre, ma, pre, mb)) goto done;
        } else {
            int w = mb + 1;
            t = calloc((size_t)(ma + 1) * w, sizeof *t);
            if (!t) goto done;
            for (int i = ma - 1; i >= 0; i--)
                for (int j = mb - 1; j >= 0; j--) {
                    if (same(&a0[i], &b0[j])) t[i * w + j] = 1 + t[(i + 1) * w + j + 1];
                    else t[i * w + j] = t[(i + 1) * w + j] >= t[i * w + j + 1] ? t[(i + 1) * w + j] : t[i * w + j + 1];
                }
            int i = 0, j = 0;
            while (i < ma || j < mb) {
                int r;
                if (i < ma && j < mb && same(&a0[i], &b0[j])) { i++; j++; continue; }
                if (j >= mb || (i < ma && t[(i + 1) * w + j] >= t[i * w + j + 1])) r = add(&h, &nh, &cap, pre + i++, 1, pre + j, 0);
                else r = add(&h, &nh, &cap, pre + i, 0, pre + j++, 1);
                if (r) goto done;
            }
        }
    }
    for (int k = 0; k < nh; k++) {
        int first = h[k].new_start, cnt = h[k].new_count;
        if (cnt == 0) continue;
        h[k].new_off = (int)(B[first].p - b);
        h[k].new_len = (int)(B[first + cnt - 1].p + B[first + cnt - 1].len - B[first].p);
    }
    *out = h;
    *nout = nh;
    h = NULL;
    rc = 0;
done:
    free(h);
    free(t);
    free(A);
    free(B);
    return rc;
}
