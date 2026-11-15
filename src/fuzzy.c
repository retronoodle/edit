/* Fuzzy matching for Quick Open. Pure: no I/O, no globals. */
#include <ctype.h>
#include <string.h>

#include "fuzzy.h"

#define FUZZY_BASE 1000   /* keeps every real match above the -1 "no match" value */

static int is_sep(char c) {
    return c == '/' || c == '\\';
}

int fuzzy_score(const char *query, const char *path) {
    if (!query || !path) return -1;
    if (query[0] == '\0') return 0;

    size_t plen = strlen(path);
    size_t base_start = 0;   /* index of the file name (after the last separator) */
    for (size_t i = 0; i < plen; i++) {
        if (is_sep(path[i])) base_start = i + 1;
    }

    int score = FUZZY_BASE;
    size_t p = 0;             /* position in path */
    int prev_match = -2;      /* index of the previous matched path char */
    int first_match = -1;

    for (const char *q = query; *q; q++) {
        int qc = tolower((unsigned char)*q);
        if (qc == '\\') qc = '/';   /* treat either separator as the same */

        int found = 0;
        while (p < plen) {
            int pc = tolower((unsigned char)path[p]);
            if (pc == '\\') pc = '/';
            if (pc == qc) {
                found = 1;
                break;
            }
            p++;
        }
        if (!found) return -1;

        if (first_match < 0) first_match = (int)p;

        /* Bonuses */
        if (prev_match >= 0 && (int)p == prev_match + 1) score += 5;          /* consecutive */
        if (p == 0 || is_sep(path[p - 1])) score += 8;                        /* segment start */
        else if (!isalnum((unsigned char)path[p - 1]) && path[p - 1] != '_') score += 4; /* word start */
        if (p >= base_start) score += 3;                                      /* inside the file name */
        if (p == base_start) score += 6;                                      /* name starts with a matched char */

        prev_match = (int)p;
        p++;
    }

    /* Prefer shorter paths, and matches that start early */
    score -= (int)(plen / 8);
    score -= first_match / 4;
    return score < 0 ? 0 : score;
}
