/* Reads git push's "no upstream" hint. Pure, no I/O. Unit tests in tests/test_gitpush.c (make test). */
#ifndef GITPUSH_H
#define GITPUSH_H

#include <stddef.h>

/* Looks for the line "git push --set-upstream <remote> <branch>" in git's output
   (leading indentation and a trailing CR or spaces are allowed). Remote and branch
   must be plain ref-name tokens, so the caller can run them without a shell.
   On a match, copies them into remote/branch (NUL-terminated, within the sizes) and
   returns 1. Otherwise returns 0 and leaves them untouched. */
int gitpush_parse_upstream(const char *out, char *remote, size_t rn, char *branch, size_t bn);

/* Returns 1 when git refused the push because the remote has commits we lack
   (a "[rejected]" line marked "(fetch first)" or "(non-fast-forward)"). Pure, no I/O.
   Upstream hints, auth errors and NULL or empty output return 0. */
int gitpush_is_rejected_behind(const char *out);

#endif
