/* Reads git push's "no upstream" hint. Pure, no I/O. */
#include <string.h>
#include "gitpush.h"

static const char SUGGEST[] = "git push --set-upstream ";

/* A plain ref token: letters, digits and . _ / -, not starting with '-', no "..". */
static int plain_token(const char *s, size_t n) {
    if (n == 0 || s[0] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '/' || c == '-')) return 0;
        if (c == '.' && i + 1 < n && s[i + 1] == '.') return 0;
    }
    return 1;
}

int gitpush_parse_upstream(const char *out, char *remote, size_t rn, char *branch, size_t bn) {
    if (!out || !remote || !branch || rn == 0 || bn == 0) return 0;

    const char *p = out;
    while (*p) {
        const char *eol = strchr(p, '\n');
        const char *end = eol ? eol : p + strlen(p);

        /* Trim indentation and trailing whitespace (CR included). */
        const char *s = p, *e = end;
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;

        size_t plen = sizeof SUGGEST - 1;
        if ((size_t)(e - s) > plen && strncmp(s, SUGGEST, plen) == 0) {
            const char *a = s + plen;
            const char *sp = memchr(a, ' ', (size_t)(e - a));
            if (sp && !memchr(sp + 1, ' ', (size_t)(e - sp - 1))) {
                size_t rl = (size_t)(sp - a);
                size_t bl = (size_t)(e - (sp + 1));
                if (plain_token(a, rl) && plain_token(sp + 1, bl) &&
                    rl < rn && bl < bn) {
                    memcpy(remote, a, rl);
                    remote[rl] = '\0';
                    memcpy(branch, sp + 1, bl);
                    branch[bl] = '\0';
                    return 1;
                }
            }
        }

        if (!eol) break;
        p = eol + 1;
    }
    return 0;
}

int gitpush_is_rejected_behind(const char *out) {
    if (!out) return 0;
    const char *p = out;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[512];
        if (len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            if (strstr(line, "[rejected]") &&
                (strstr(line, "(fetch first)") || strstr(line, "(non-fast-forward)")))
                return 1;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return 0;
}
