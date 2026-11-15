/* Fuzzy matching for Quick Open: pure helpers, no I/O. See fuzzy.c. */
#ifndef FUZZY_H
#define FUZZY_H

/* Scores how well query matches path. Every query character must appear in
   path, in order, ignoring case. Higher is better; -1 means no match. An empty
   query matches everything with score 0. Matches inside the file name (after
   the last / or \) and at word starts score higher. */
int fuzzy_score(const char *query, const char *path);

#endif
