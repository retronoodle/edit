/* Git panel (Alt+G): branch and changed files of the current folder's repo,
   with commit (stages everything) and push. git runs as a child process. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* popen/pclose under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    FILE *_popen(const char *command, const char *mode);
    int _pclose(FILE *stream);
#endif

#include "editor.h"
#include "term.h"
#include "ui.h"
#include "tabs.h"
#include "options.h"
#include "gitmodal.h"

#define MSG_MAX 256
#define CMD_MAX 1536   /* room for a full temp path inside the git commit command */
#define COMMIT_MAX 256

static char *text_buf = NULL;   /* git status output; header and files point into it */
static char **files = NULL;     /* changed-file lines (porcelain) */
static int nfiles = 0;
static const char *header = "";  /* "master...origin/master [ahead 1]" */
static int in_repo = 0;
static int top = 0;             /* first visible file row */
static char msg[MSG_MAX];       /* result of the last action, or a git error */

static void set_msg(const char *s) {
    snprintf(msg, sizeof msg, "%s", s);
}

/* Shows the last (or first) non-empty line of git's output in the message row. */
static void msg_from_output(const char *text, int want_last) {
    char pick[MSG_MAX] = "";
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t l = eol ? (size_t)(eol - p) : strlen(p);
        if (l > 0 && p[l - 1] == '\r') l--;
        if (l > 0) {
            snprintf(pick, sizeof pick, "%.*s", (int)l, p);
            if (!want_last) break;
        }
        if (!eol) break;
        p = eol + 1;
    }
    set_msg(pick[0] ? pick : "(no output)");
}

static int list_rows(void) {
    int r = screen_h - 3;   /* rows 1 .. screen_h-3; then message and footer rows */
    return r < 1 ? 1 : r;
}

static void clamp_top(void) {
    int max = nfiles - list_rows();
    if (max < 0) max = 0;
    if (top > max) top = max;
    if (top < 0) top = 0;
}

/* Runs a shell command with stderr merged in. Output goes to *out (malloc'd,
   may be NULL on failure). Returns the exit status, or -1 if it could not run. */
static int run_cmd(const char *cmd, char **out) {
    char full[CMD_MAX];
    snprintf(full, sizeof full, "%s 2>&1", cmd);
    *out = NULL;

#ifdef _WIN32
    FILE *p = _popen(full, "r");
#else
    FILE *p = popen(full, "r");
#endif
    if (!p) return -1;

    size_t cap = 1024, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
#ifdef _WIN32
        _pclose(p);
#else
        pclose(p);
#endif
        return -1;
    }

    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, p)) > 0) {
        if (len + n + 1 > cap) {
            while (len + n + 1 > cap) cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
#ifdef _WIN32
                _pclose(p);
#else
                pclose(p);
#endif
                return -1;
            }
            buf = nb;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    buf[len] = '\0';

#ifdef _WIN32
    int rc = _pclose(p);
#else
    int rc = pclose(p);
#endif
    *out = buf;
    return rc;
}

static void clear_status(void) {
    free(text_buf);
    free(files);
    text_buf = NULL;
    files = NULL;
    nfiles = 0;
    header = "";
    in_repo = 0;
}

/* Reads git status. Not being in a repo makes git exit non-zero, which is how
   we detect it. */
static void load_status(void) {
    clear_status();
    top = 0;

    char *out = NULL;
    int rc = run_cmd("git status --porcelain=v1 -b", &out);
    if (rc != 0 || !out) {
        msg_from_output(out ? out : "", 0);
        free(out);
        return;
    }
    in_repo = 1;
    text_buf = out;

    int cap = 1;
    for (char *q = out; *q; q++) if (*q == '\n') cap++;
    files = malloc(sizeof(char *) * cap);
    if (!files) { clear_status(); set_msg("Out of memory."); return; }

    char *p = out;
    while (*p) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        size_t l = strlen(p);
        if (l > 0 && p[l - 1] == '\r') p[l - 1] = '\0';

        if (strncmp(p, "## ", 3) == 0) header = p + 3;
        else if (*p) files[nfiles++] = p;

        if (!eol) break;
        p = eol + 1;
    }
}

/* Draws one full-width row. Text is cut at a character boundary so it never
   runs into the last column. */
static void put_row(int y, const char *sgr, const char *text) {
    int w = screen_w - 1;
    if (w < 0) w = 0;
    int n = text ? (int)strlen(text) : 0;
    if (n > w) {
        n = w;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    }
    fb_gotoxy(0, y);
    fb_puts(sgr);
    if (n > 0) fb_write(text, n);
    for (int i = n; i < w; i++) fb_write(" ", 1);
    fb_puts(options_base_sgr());
}

static void draw(void) {
    char title[600];
    char line[700];
    int rows = list_rows();

    fb_reset();

    snprintf(title, sizeof title, " Git: %s", in_repo ? header : "not a git repository");
    put_row(0, options_chrome_sgr(CH_BAR), title);

    for (int i = 0; i < rows; i++) {
        const char *t = "";
        if (!in_repo) {
            if (i == 0) t = "  Open a file inside a git repository to see its status.";
        } else if (nfiles == 0) {
            if (i == 0) t = "  Working tree clean.";
        } else if (top + i < nfiles) {
            snprintf(line, sizeof line, "  %s", files[top + i]);
            t = line;
        }
        put_row(1 + i, options_base_sgr(), t);
    }

    const char *note = "";
    if (msg[0]) note = msg;
    else if (tabs_first_modified() >= 0) note = "Unsaved tabs are not included until you save them.";
    put_row(screen_h - 2, options_base_sgr(), note);

    put_row(screen_h - 1, options_chrome_sgr(CH_STATUS),
            in_repo ? " C Commit  P Push  R Refresh  Up/Down Scroll  Esc Close"
                    : " Esc Close");
    fb_flush();
}

/* Asks for a message, stages everything with git add -A, then commits. */
static void do_commit(void) {
    char message[COMMIT_MAX] = "";
    if (!prompt_line("Commit message: ", message, sizeof message)) {
        set_msg("Commit cancelled.");
        return;
    }

    /* git reads the message from a file, which avoids shell quoting problems. */
    const char *dir = getenv("TEMP");
    if (!dir) dir = getenv("TMPDIR");
    if (!dir) dir = "/tmp";
    char path[1024];
    snprintf(path, sizeof path, "%s/edit_commit_msg.txt", dir);

    FILE *f = fopen(path, "wb");
    if (!f) {
        set_msg("Could not write the commit message file.");
        return;
    }
    fputs(message, f);
    fputs("\n", f);
    fclose(f);

    char *out = NULL;
    int rc = run_cmd("git add -A", &out);
    if (rc != 0) {
        msg_from_output(out ? out : "", 1);
        free(out);
        remove(path);
        return;
    }
    free(out);
    out = NULL;

    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd, "git commit -F \"%s\"", path);
    rc = run_cmd(cmd, &out);
    remove(path);
    /* success: first line is "[branch hash] message"; failure: the reason is usually last */
    msg_from_output(out ? out : "", rc != 0);
    free(out);
    load_status();
}

/* Pushes. Prompts are switched off so a missing login fails instead of hanging. */
static void do_push(void) {
#ifdef _WIN32
    const char *cmd = "set GIT_TERMINAL_PROMPT=0&& git push";
#else
    const char *cmd = "GIT_TERMINAL_PROMPT=0 git push";
#endif
    char *out = NULL;
    run_cmd(cmd, &out);
    msg_from_output(out ? out : "", 1);
    free(out);
    load_status();
}

void git_modal(void) {
    int done = 0;
    msg[0] = '\0';

    set_cursor_visibility(0);
    clear_screen();
    load_status();

    while (!done) {
        clamp_top();
        draw();
        int key = read_key();
        if (key == -1) continue;   /* unmapped key (e.g. Alt released): ignore, don't close */

        switch (key) {
            case KEY_ESC:
            case KEY_ALT_G:  done = 1; break;
            case KEY_UP:     top--; break;
            case KEY_DOWN:   top++; break;
            case 'c':
            case 'C':        if (in_repo) do_commit(); break;
            case 'p':
            case 'P':        if (in_repo) do_push(); break;
            case 'r':
            case 'R':        load_status(); break;
            case KEY_RESIZE: term_update_size(); clear_screen(); break;
            default: break;
        }
    }

    clear_status();
    set_cursor_visibility(1);
    clear_screen();
}
