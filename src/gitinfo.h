/* Working directory and git branch for the status line. */
#ifndef GITINFO_H
#define GITINFO_H

#define GIT_DIR_MAX 512
#define GIT_BRANCH_MAX 128

typedef struct {
    char dir[GIT_DIR_MAX];      /* cwd, with the home folder shortened to ~ */
    char branch[GIT_BRANCH_MAX]; /* branch name, short hash when detached, "" outside a repo */
} GitInfo;

/* Current info. Re-read when the cwd changes or every 2 seconds. */
const GitInfo *gitinfo_get(void);

#endif
