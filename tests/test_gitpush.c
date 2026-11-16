/* Unit tests for gitpush.c. Build and run with: make test */
#include <stdio.h>
#include <string.h>
#include "gitpush.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("ok   %s\n", name); \
    else { printf("FAIL %s\n", name); failures++; } \
} while (0)

#define RN 64

int main(void) {
    char remote[RN], branch[RN];

    /* The exact output git gave for a push on a new branch */
    const char *hint =
        "fatal: The current branch feat/twilio-sms-foundations has no upstream branch.\n"
        "To push the current branch and set the remote as upstream, use\n"
        "\n"
        "    git push --set-upstream origin feat/twilio-sms-foundations\n"
        "\n"
        "To have this happen automatically for branches without a tracking\n"
        "upstream, see 'push.autoSetupRemote' in 'git help config'.\n";
    strcpy(remote, "x");
    strcpy(branch, "x");
    CHECK(gitpush_parse_upstream(hint, remote, RN, branch, RN) == 1, "real hint matches");
    CHECK(strcmp(remote, "origin") == 0, "real hint: remote is origin");
    CHECK(strcmp(branch, "feat/twilio-sms-foundations") == 0, "real hint: branch parsed");

    /* CRLF line endings (Windows git) */
    const char *crlf = "    git push --set-upstream origin main\r\n";
    CHECK(gitpush_parse_upstream(crlf, remote, RN, branch, RN) == 1, "CRLF line matches");
    CHECK(strcmp(branch, "main") == 0, "CRLF line: branch has no CR");

    /* Other push failures get no offer */
    const char *rejected =
        " ! [rejected]        main -> main (non-fast-forward)\n"
        "error: failed to push some refs to 'github.com:x/y.git'\n"
        "hint: Updates were rejected because the tip of your current branch is behind\n";
    CHECK(gitpush_parse_upstream(rejected, remote, RN, branch, RN) == 0, "non-fast-forward is not offered");
    CHECK(gitpush_parse_upstream("fatal: Authentication failed for 'x'\n", remote, RN, branch, RN) == 0,
          "auth failure is not offered");
    CHECK(gitpush_parse_upstream("", remote, RN, branch, RN) == 0, "empty output is not offered");
    CHECK(gitpush_parse_upstream(NULL, remote, RN, branch, RN) == 0, "NULL output is not offered");

    /* Unsafe or malformed suggestions are refused */
    const char *bad[] = {
        "git push --set-upstream origin main; rm -rf ~\n",
        "git push --set-upstream origin $(whoami)\n",
        "git push --set-upstream origin `id`\n",
        "git push --set-upstream origin a|b\n",
        "git push --set-upstream origin main extra\n",
        "git push --set-upstream origin\n",
        "git push --set-upstream -x main\n",
        "git push --set-upstream origin -main\n",
        "git push --set-upstream origin a..b\n",
        "git push --force origin main\n",
        "git push --set-upstream origin ma'in\n",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char name[64];
        snprintf(name, sizeof name, "refused: %.40s", bad[i]);
        for (char *q = name; *q; q++) if (*q == '\n') *q = ' ';
        CHECK(gitpush_parse_upstream(bad[i], remote, RN, branch, RN) == 0, name);
    }

    /* Too long for the caller's buffers */
    CHECK(gitpush_parse_upstream("git push --set-upstream origin abcdef\n", remote, 4, branch, RN) == 0,
          "branch too long for buffer is refused");

    printf("%s\n", failures ? "gitpush: FAILURES" : "gitpush: all passed");
    return failures ? 1 : 0;
}
