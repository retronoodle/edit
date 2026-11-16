/* Git panel (Alt+G): branch and changed files of the current folder's repo,
   with stage/unstage per file (or all), commit (staged files only), push,
   branch switch/new, merge and merge abort. git runs as a child process. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* popen/pclose under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    FILE *_popen(const char *command, const char *mode);
    int _pclose(FILE *stream);
#endif

#include "editor.h"
#include "term.h"
#include "ui.h"
#include "tabs.h"
#include "options.h"
#include "aiask.h"
#include "viewer.h"
#include "syntax.h"
#include "gitmodal.h"

#define MSG_MAX 256
#define CMD_MAX 1536   /* room for a full temp path inside the git commit command */
#define COMMIT_MAX 256
#define DIFF_MAX 20000   /* diff bytes sent to the AI for a commit draft */
#define VIEW_DIFF_MAX 200000   /* diff bytes shown in the viewer (Enter / click on a file) */

static const char COMMIT_SYSTEM[] =
    "Write a git commit message for this diff. One line, imperative mood, max 72 characters. "
    "No quotes, no prefix, no trailing period. Output only the message.";
#define NAME_MAX_LEN 128   /* longest branch name we accept */

static char *text_buf = NULL;   /* git status output; header and files point into it */
static char **files = NULL;     /* changed-file lines (porcelain) */
static int nfiles = 0;
static const char *header = "";  /* "master...origin/master [ahead 1]" */
static int unpushed = 0;         /* commits not on any remote (from header, or rev-list without an upstream) */
static int in_repo = 0;
static int merging = 0;          /* a merge is stopped (MERGE_HEAD exists) */
static int top = 0;             /* first visible file row */
static int sel = 0;             /* highlighted file (Up/Down move it, Space stages it) */
static char msg[MSG_MAX];       /* result of the last action, or a git error */

/* Output panel: the last git action, replaced by each new one. */
#define OUT_ROWS 4        /* header line + 3 tail lines of output */
#define OUT_MIN_H 10      /* below this screen height there is no panel */
typedef enum { OUT_NONE, OUT_RUNNING, OUT_OK, OUT_FAILED } OutState;
static OutState out_state = OUT_NONE;
static char out_label[32] = "";  /* e.g. "git push" */
static int out_rc = 0;
static char *out_text = NULL;    /* full output of that action (malloc'd) */

static int out_shown(void) { return screen_h >= OUT_MIN_H; }

/* Branch list (B = switch, M = merge). */
static int picking = 0;          /* 1 = the branch list is showing */
static int pick_merge = 0;       /* 1 = list picks a merge source; 0 = picks a branch to switch to */
static char cur_branch[NAME_MAX_LEN] = "";
static char **pick_name = NULL;  /* NULL entry = the "+ New branch..." row (switch mode only) */
static int npick = 0;
static int psel = 0;            /* highlighted row */
static int ptop = 0;            /* first visible row */

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
    /* rows 1 ..; then the output panel (if shown), message and footer rows */
    int r = screen_h - 3 - (out_shown() ? OUT_ROWS : 0);
    return r < 1 ? 1 : r;
}

/* Keeps the selected file in range and scrolls the list so it stays visible. */
static void clamp_top(void) {
    if (sel >= nfiles) sel = nfiles - 1;
    if (sel < 0) sel = 0;

    int max = nfiles - list_rows();
    if (max < 0) max = 0;
    if (top > max) top = max;
    if (top < 0) top = 0;

    if (sel < top) top = sel;
    if (sel >= top + list_rows()) top = sel - list_rows() + 1;
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
    unpushed = 0;
    in_repo = 0;
    merging = 0;
}

static void clear_output(void) {
    free(out_text);
    out_text = NULL;
    out_state = OUT_NONE;
    out_label[0] = '\0';
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

    /* -q: no output, exit 0 only when a merge is in progress */
    char *mo = NULL;
    merging = (run_cmd("git rev-parse -q --verify MERGE_HEAD", &mo) == 0);
    free(mo);

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

    /* Commits not pushed yet. With an upstream, git's header says "[ahead N]";
       without one, count commits no remote has. */
    if (strstr(header, "...")) {
        const char *ah = strstr(header, "ahead ");
        unpushed = ah ? atoi(ah + 6) : 0;
    } else {
        char *uo = NULL;
        if (run_cmd("git rev-list --count HEAD --not --remotes", &uo) == 0 && uo) unpushed = atoi(uo);
        free(uo);
    }
}

/* Number of changed files that are in conflict (porcelain XY with a U, or both added/deleted). */
static int count_conflicts(void) {
    int n = 0;
    for (int i = 0; i < nfiles; i++) {
        char x = files[i][0], y = files[i][1];
        if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) n++;
    }
    return n;
}

/* Status of one changed file, from its porcelain XY code. */
typedef enum {
    FS_OTHER, FS_CONFLICT, FS_UNTRACKED, FS_NEW, FS_DELETED, FS_RENAMED, FS_MODIFIED, FS_COUNT
} FileKind;

static FileKind file_kind(const char *line) {
    char x = line[0], y = line[1];
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) return FS_CONFLICT;
    if (x == '?') return FS_UNTRACKED;
    if (x == 'A') return FS_NEW;
    if (x == 'D' || y == 'D') return FS_DELETED;
    if (x == 'R' || x == 'C') return FS_RENAMED;
    if (x == 'M' || y == 'M' || x == 'T' || y == 'T') return FS_MODIFIED;
    return FS_OTHER;
}

/* 1 if the file has something in the index (a conflict is never staged here). */
static int file_staged(const char *line) {
    char x = line[0];
    return x != ' ' && x != '?' && file_kind(line) != FS_CONFLICT;
}

static int count_staged(void) {
    int n = 0;
    for (int i = 0; i < nfiles; i++) if (file_staged(files[i])) n++;
    return n;
}

static const char *kind_label(FileKind k) {
    switch (k) {
        case FS_CONFLICT:  return "conflict";
        case FS_UNTRACKED: return "untracked";
        case FS_NEW:       return "new";
        case FS_DELETED:   return "deleted";
        case FS_RENAMED:   return "renamed";
        case FS_MODIFIED:  return "modified";
        default:           return "other";
    }
}

/* Foreground-only colors per scheme (the row background comes from the scheme).
   Light has a white background and Blue a blue one, so their colors differ. */
static const char *kind_sgr[3][FS_COUNT] = {
    /* Default */
    { "\x1b[0m", "\x1b[1;31m", "\x1b[35m", "\x1b[32m", "\x1b[31m", "\x1b[36m", "\x1b[33m" },
    /* Blue */
    { "\x1b[0m", "\x1b[1;91m", "\x1b[95m", "\x1b[92m", "\x1b[91m", "\x1b[96m", "\x1b[93m" },
    /* Light */
    { "\x1b[0m", "\x1b[1;31m", "\x1b[38;5;90m", "\x1b[38;5;28m", "\x1b[31m", "\x1b[38;5;30m", "\x1b[38;5;130m" },
};

/* Counts of each kind, written as "3 modified, 1 new". Empty when there are no changes. */
static void summary_text(char *buf, size_t size) {
    int c[FS_COUNT] = {0};
    buf[0] = '\0';
    for (int i = 0; i < nfiles; i++) c[file_kind(files[i])]++;
    size_t n = 0;
    for (int k = FS_CONFLICT; k < FS_COUNT; k++) {
        if (!c[k]) continue;
        int w = snprintf(buf + n, size - n, "%s%d %s", n ? ", " : "", c[k], kind_label(k));
        if (w < 0 || (size_t)w >= size - n) break;
        n += (size_t)w;
    }
    int st = count_staged();
    if (st > 0 && n < size) snprintf(buf + n, size - n, "%s%d staged", n ? ", " : "", st);
}

static void put_row(int y, const char *sgr, const char *text);

/* Output panel above the message row: a header (label and state), then the last
   three non-empty lines of output, where git puts the reason. */
static void draw_panel(void) {
    if (!out_shown()) return;
    int y0 = screen_h - 2 - OUT_ROWS;
    char head[sizeof out_label + 64];
    char row_sgr[64];
    const char *hsgr = options_base_sgr();

    switch (out_state) {
        case OUT_RUNNING:
            snprintf(head, sizeof head, "  $ %s  running...", out_label);
            hsgr = options_chrome_sgr(CH_BAR);
            break;
        case OUT_OK:
            snprintf(head, sizeof head, "  $ %s  done", out_label);
            snprintf(row_sgr, sizeof row_sgr, "%s%s", options_base_sgr(), kind_sgr[options.scheme][FS_NEW]);
            hsgr = row_sgr;
            break;
        case OUT_FAILED:
            snprintf(head, sizeof head, "  $ %s  failed (exit %d)", out_label, out_rc);
            snprintf(row_sgr, sizeof row_sgr, "%s%s", options_base_sgr(), kind_sgr[options.scheme][FS_CONFLICT]);
            hsgr = row_sgr;
            break;
        default:
            head[0] = '\0';
            break;
    }
    put_row(y0, hsgr, head);

    /* Last three non-empty lines, kept in a small ring. */
    char tl[OUT_ROWS - 1][300];
    int nt = 0;
    const char *p = out_text ? out_text : "";
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t l = eol ? (size_t)(eol - p) : strlen(p);
        if (l > 0 && p[l - 1] == '\r') l--;
        if (l > 0) {
            snprintf(tl[nt % (OUT_ROWS - 1)], sizeof tl[0], "%.*s", (int)l, p);
            nt++;
        }
        if (!eol) break;
        p = eol + 1;
    }
    int start = nt > OUT_ROWS - 1 ? nt - (OUT_ROWS - 1) : 0;
    for (int i = 0; i < OUT_ROWS - 1; i++) {
        const char *t = (start + i < nt) ? tl[(start + i) % (OUT_ROWS - 1)] : "";
        put_row(y0 + 1 + i, options_base_sgr(), t);
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

    char count[64] = "";
    if (unpushed > 0) snprintf(count, sizeof count, "  |  %d unpushed commit%s", unpushed, unpushed == 1 ? "" : "s");
    snprintf(title, sizeof title, " Git: %s%s", in_repo ? header : "not a git repository", count);
    put_row(0, options_chrome_sgr(CH_BAR), title);

    for (int i = 0; i < rows; i++) {
        const char *t = "";
        const char *sgr = options_base_sgr();
        char row_sgr[64];
        if (!in_repo) {
            if (i == 0) t = "  Open a file inside a git repository to see its status.";
        } else if (nfiles == 0) {
            if (i == 0) t = "  Working tree clean.";
        } else if (top + i < nfiles) {
            FileKind k = file_kind(files[top + i]);
            snprintf(line, sizeof line, "  [%c] %-10s %s", file_staged(files[top + i]) ? 'x' : ' ',
                     kind_label(k), files[top + i] + 3);
            t = line;
            if (top + i == sel) {
                sgr = options_chrome_sgr(CH_BAR);
            } else {
                snprintf(row_sgr, sizeof row_sgr, "%s%s", options_base_sgr(), kind_sgr[options.scheme][k]);
                sgr = row_sgr;
            }
        }
        put_row(1 + i, sgr, t);
    }

    draw_panel();

    char sum[160] = "";
    if (nfiles > 0) summary_text(sum, sizeof sum);
    int unsaved = tabs_first_modified() >= 0;

    char note_buf[300];
    const char *note = "";
    if (msg[0]) note = msg;
    else if (sum[0] && unsaved) {
        snprintf(note_buf, sizeof note_buf, "%s. Unsaved tabs are not included.", sum);
        note = note_buf;
    } else if (sum[0]) note = sum;
    else if (unsaved) note = "Unsaved tabs are not included until you save them.";
    put_row(screen_h - 2, options_base_sgr(), note);

    const char *foot;
    if (!in_repo) foot = " Esc Close";
    else if (merging) foot = " Space Stage  Enter Diff  S/U All  C Commit  D Draft  A Abort merge  R Refresh  Esc Close";
    else foot = " Space Stage  Enter Diff  S/U All  C Commit  D Draft  P Push  B Branch  M Merge  R Refresh  Esc Close";
    put_row(screen_h - 1, options_chrome_sgr(CH_STATUS), foot);
    fb_flush();
}

/* ---- Branch list ---- */

static void clear_branches(void) {
    for (int i = 0; i < npick; i++) free(pick_name[i]);
    free(pick_name);
    pick_name = NULL;
    npick = 0;
}

/* Adds a branch name (NULL = the "+ New branch..." row). */
static void add_pick(const char *name) {
    char **nb = realloc(pick_name, sizeof(char *) * (npick + 1));
    if (!nb) return;
    pick_name = nb;
    if (name) {
        char *d = malloc(strlen(name) + 1);
        if (!d) return;
        strcpy(d, name);
        pick_name[npick++] = d;
    } else {
        pick_name[npick++] = NULL;
    }
}

/* Reads local branches from `git branch` (plain output: "* name" marks the current one). */
static void load_branches(void) {
    clear_branches();
    cur_branch[0] = '\0';
    psel = 0;
    ptop = 0;
    if (!pick_merge) add_pick(NULL);

    char *out = NULL;
    int rc = run_cmd("git branch", &out);
    if (rc != 0 || !out) {
        msg_from_output(out ? out : "", 0);
        free(out);
        return;
    }

    char *p = out;
    while (*p) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        size_t l = strlen(p);
        if (l > 0 && p[l - 1] == '\r') p[--l] = '\0';

        if (l >= 3 && p[0] != '(') {
            int is_cur = (p[0] == '*');
            char *name = p + 2;
            if (is_cur) snprintf(cur_branch, sizeof cur_branch, "%s", name);
            if (!(pick_merge && is_cur)) add_pick(name);
        }

        if (!eol) break;
        p = eol + 1;
    }
    free(out);
}

static void draw_pick(void) {
    char title[300];
    char line[300];
    int rows = list_rows();

    fb_reset();

    if (pick_merge) snprintf(title, sizeof title, " Merge into %s: pick a branch", cur_branch[0] ? cur_branch : "(detached)");
    else snprintf(title, sizeof title, " Switch branch (current: %s)", cur_branch[0] ? cur_branch : "(detached)");
    put_row(0, options_chrome_sgr(CH_BAR), title);

    for (int i = 0; i < rows; i++) {
        int idx = ptop + i;
        const char *sgr = options_base_sgr();
        const char *t = "";
        if (npick == 0 && i == 0) {
            t = pick_merge ? "  No other local branches." : "";
        } else if (idx < npick) {
            if (pick_name[idx] == NULL) {
                t = "  + New branch...";
            } else {
                snprintf(line, sizeof line, "  %s%s", cur_branch[0] && strcmp(cur_branch, pick_name[idx]) == 0 && !pick_merge ? "* " : "", pick_name[idx]);
                t = line;
            }
            if (idx == psel) sgr = options_chrome_sgr(CH_DROP_SEL);
        }
        put_row(1 + i, sgr, t);
    }

    draw_panel();
    put_row(screen_h - 2, options_base_sgr(), msg[0] ? msg : "");
    put_row(screen_h - 1, options_chrome_sgr(CH_STATUS), " Enter Pick  Up/Down Move  Esc Back");
    fb_flush();
}

static void pick_clamp(void) {
    int rows = list_rows();
    if (npick == 0) psel = 0;
    if (psel < 0) psel = 0;
    if (psel >= npick) psel = npick > 0 ? npick - 1 : 0;
    if (psel < ptop) ptop = psel;
    if (psel >= ptop + rows) ptop = psel - rows + 1;
    if (ptop < 0) ptop = 0;
}

/* Branch names go through the shell, so only plain names are allowed. */
static int valid_branch_name(const char *s) {
    if (!*s || s[0] == '-' || strlen(s) >= NAME_MAX_LEN) return 0;
    for (const char *q = s; *q; q++) {
        if (!(isalnum((unsigned char)*q) || *q == '.' || *q == '_' || *q == '/' || *q == '-')) return 0;
    }
    return 1;
}

/* Runs a git action for the output panel. RUNNING is drawn before the blocking
   call so a slow push shows it is in progress. Clears msg. Returns the exit status. */
static int run_git(const char *label, const char *cmd) {
    snprintf(out_label, sizeof out_label, "%s", label);
    free(out_text);
    out_text = NULL;
    out_state = OUT_RUNNING;
    msg[0] = '\0';
    if (picking) draw_pick();
    else draw();

    int rc = run_cmd(cmd, &out_text);
    out_rc = rc;
    out_state = rc == 0 ? OUT_OK : OUT_FAILED;
    return rc;
}

/* Appends the reload reminder to a success message (open files are not reloaded). */
static void add_reload_hint(void) {
    char tmp[MSG_MAX];
    snprintf(tmp, sizeof tmp, "%.200s  Reopen files to see changes.", msg);
    set_msg(tmp);
}

/* Paths go through the shell, so quotes, backticks, $ and backslashes are not allowed
   (git's quoted names start with a quote, so those are refused too). */
static int valid_path(const char *s) {
    if (!*s || *s == '"') return 0;
    for (const char *q = s; *q; q++) {
        if (strchr("\"`$\\\n\r", *q)) return 0;
    }
    return 1;
}

/* Stages the selected file, or takes it out of the index when it is fully staged.
   Renames show as "old -> new"; unstaging a rename restores both sides. */
static void do_toggle(void) {
    if (!in_repo || nfiles == 0) return;
    const char *line = files[sel];
    char y = line[1];
    int unstage = file_staged(line) && y == ' ';

    char old_p[512] = "", new_p[512] = "";
    const char *arrow = strstr(line + 3, " -> ");
    if (arrow) {
        snprintf(old_p, sizeof old_p, "%.*s", (int)(arrow - (line + 3)), line + 3);
        snprintf(new_p, sizeof new_p, "%s", arrow + 4);
    } else {
        snprintf(new_p, sizeof new_p, "%s", line + 3);
    }
    if (!valid_path(new_p) || (arrow && !valid_path(old_p))) {
        set_msg("That file name has characters git actions cannot handle here.");
        return;
    }

    char cmd[CMD_MAX];
    if (unstage && arrow) {
        snprintf(cmd, sizeof cmd, "git restore --staged -- \":/%s\" \":/%s\"", old_p, new_p);
        run_git("git restore --staged", cmd);
    } else if (unstage) {
        snprintf(cmd, sizeof cmd, "git restore --staged -- \":/%s\"", new_p);
        run_git("git restore --staged", cmd);
    } else {
        snprintf(cmd, sizeof cmd, "git add -- \":/%s\"", new_p);
        run_git("git add", cmd);
    }
    load_status();
}

/* Shows the selected file's diff in the viewer. Tracked files diff against HEAD
   (staged and unstaged changes together); untracked files diff against an empty file.
   A repo with no commits yet has no HEAD, so it falls back to the staged diff. */
static void do_file_diff(void) {
    if (!in_repo || nfiles == 0) return;
    const char *line = files[sel];
    int untracked = file_kind(line) == FS_UNTRACKED;

    const char *arrow = strstr(line + 3, " -> ");
    char old_p[512] = "", path[512];
    if (arrow) snprintf(old_p, sizeof old_p, "%.*s", (int)(arrow - (line + 3)), line + 3);
    snprintf(path, sizeof path, "%s", arrow ? arrow + 4 : line + 3);
    if (!valid_path(path) || (arrow && !valid_path(old_p))) {
        set_msg("That file name has characters git actions cannot handle here.");
        return;
    }

    char cmd[CMD_MAX];
    if (untracked) {
#ifdef _WIN32
        snprintf(cmd, sizeof cmd, "git diff --no-index -- NUL \"%s\"", path);
#else
        snprintf(cmd, sizeof cmd, "git diff --no-index -- /dev/null \"%s\"", path);
#endif
    } else if (arrow) {   /* both sides of a rename, so git pairs them and shows the edit */
        snprintf(cmd, sizeof cmd, "git diff HEAD -M -- \":/%s\" \":/%s\"", old_p, path);
    } else {
        snprintf(cmd, sizeof cmd, "git diff HEAD -- \":/%s\"", path);
    }

    /* git diff --no-index exits 1 when the files differ, so its status is not checked. */
    char *text = NULL;
    int rc = run_cmd(cmd, &text);
    if (!untracked && rc != 0) {   /* no HEAD yet */
        free(text);
        text = NULL;
        snprintf(cmd, sizeof cmd, "git diff --cached -- \":/%s\"", path);
        rc = run_cmd(cmd, &text);
    }
    if (!text || (rc != 0 && !untracked)) {
        free(text);
        set_msg("Could not read the diff.");
        return;
    }

    const char *p = text;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        free(text);
        set_msg("No changes to show.");
        return;
    }

    size_t n = strlen(text);
    if (n > VIEW_DIFF_MAX) {
        size_t cut = VIEW_DIFF_MAX;
        while (cut > 0 && text[cut - 1] != '\n') cut--;
        if (cut == 0) cut = VIEW_DIFF_MAX;
        text[cut] = '\0';
        set_msg("Diff is too large; showing the start.");
    }

    view_diff(path, text, options.syntax ? syntax_lang_for(path) : SYN_PLAIN);
    free(text);
    clear_screen();
}

static void do_stage_all(void) {
    run_git("git add", "git add -A");
    load_status();
}

static void do_unstage_all(void) {
    run_git("git reset", "git reset -q");
    load_status();
}

/* Commits what is staged. With nothing staged it asks whether to stage everything first.
   draft (may be NULL) is an AI message that the commit prompt starts with, for editing.
   The message is read from a file, which avoids shell quoting problems. */
static void do_commit_with(const char *draft) {
    int stage_all = 0;
    if (count_staged() == 0) {
        if (nfiles == 0 && !merging) {
            set_msg("Nothing to commit.");
            return;
        }
        char ans[8] = "";
        if (!prompt_line("Nothing staged. Stage all and commit? (y/n): ", ans, sizeof ans)) {
            set_msg("Commit cancelled.");
            return;
        }
        if (ans[0] != 'y' && ans[0] != 'Y') {
            set_msg("Commit cancelled.");
            return;
        }
        stage_all = 1;
    }

    char message[COMMIT_MAX] = "";
    if (draft) snprintf(message, sizeof message, "%s", draft);
    int ok = draft ? prompt_line_prefill("Commit message: ", message, sizeof message)
                   : prompt_line("Commit message: ", message, sizeof message);
    if (!ok) {
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

    if (stage_all && run_git("git add", "git add -A") != 0) {
        remove(path);
        load_status();
        return;
    }

    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd, "git commit -F \"%s\"", path);
    run_git("git commit", cmd);
    remove(path);
    load_status();
}

static void do_commit(void) { do_commit_with(NULL); }

/* Gets the diff for a commit draft: the staged changes when something is staged,
   otherwise the working tree against HEAD. Capped at DIFF_MAX bytes on a line boundary.
   Returns 0 with *out set (malloc'd, caller frees), or -1 with msg set. */
static int get_diff(char **out) {
    char *text = NULL;
    int rc;
    if (count_staged() > 0) {
        rc = run_cmd("git diff --cached", &text);
    } else {
        rc = run_cmd("git diff HEAD", &text);
        if (rc != 0) {   /* no commits yet: HEAD does not exist */
            free(text);
            text = NULL;
            rc = run_cmd("git diff", &text);
        }
    }
    if (rc != 0 || !text) {
        free(text);
        set_msg("Could not read the diff.");
        return -1;
    }

    const char *p = text;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        free(text);
        set_msg("Nothing to draft.");
        return -1;
    }

    size_t n = strlen(text);
    if (n > DIFF_MAX) {
        size_t cut = DIFF_MAX;
        while (cut > 0 && text[cut - 1] != '\n') cut--;
        if (cut == 0) cut = DIFF_MAX;
        char *capped = malloc(cut + 32);
        if (!capped) {
            free(text);
            set_msg("Out of memory.");
            return -1;
        }
        memcpy(capped, text, cut);
        strcpy(capped + cut, "[diff truncated]\n");
        free(text);
        text = capped;
    }
    *out = text;
    return 0;
}

/* Drafts a commit message with the AI (D), then opens the commit prompt with it for editing. */
static void do_draft_commit(void) {
    if (nfiles == 0 && !merging) {
        set_msg("Nothing to draft.");
        return;
    }
    char *diff = NULL;
    if (get_diff(&diff) != 0) return;

    set_msg("Thinking...");
    draw();

    char *reply = NULL;
    char err[256];
    int rc = ai_ask(COMMIT_SYSTEM, diff, &reply, err, sizeof err);
    free(diff);
    if (rc != 0) {
        set_msg(err);
        return;
    }

    /* First line only, without surrounding whitespace, quotes or backticks. */
    char draft[COMMIT_MAX];
    const char *s = reply;
    while (*s && isspace((unsigned char)*s)) s++;
    size_t i = 0;
    while (s[i] && s[i] != '\n' && i < sizeof draft - 1) {
        draft[i] = s[i];
        i++;
    }
    draft[i] = '\0';
    free(reply);
    while (i > 0 && isspace((unsigned char)draft[i - 1])) draft[--i] = '\0';
    if (i >= 2 && strchr("\"'`", draft[0]) && draft[i - 1] == draft[0]) {
        memmove(draft, draft + 1, i - 2);
        i -= 2;
        draft[i] = '\0';
    }
    if (draft[0] == '\0') {
        set_msg("The AI returned an empty message.");
        return;
    }

    msg[0] = '\0';
    do_commit_with(draft);
}

/* Pushes. Prompts are switched off so a missing login fails instead of hanging.
   ssh gets BatchMode and stdin is cut off, since ssh would otherwise ask on the
   terminal we are running in and velo would wait forever. */
/* Appends one line to ~/.velo_push.log so a push can be followed step by step. */
static void push_log(const char *line) {
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/.velo_push.log", home);
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

static void do_push(void) {
#ifdef _WIN32
    const char *cmd = "set GIT_TERMINAL_PROMPT=0&& set GIT_SSH_COMMAND=ssh -o BatchMode=yes&& git push <NUL";
#else
    const char *cmd = "GIT_TERMINAL_PROMPT=0 GIT_SSH_COMMAND='ssh -o BatchMode=yes' git push </dev/null";
#endif
    push_log("--- push start ---");
    push_log(cmd);
    int rc = run_git("git push", cmd);
    char rcline[64];
    snprintf(rcline, sizeof rcline, "exit status: %d", rc);
    push_log(rcline);
    push_log(out_text && *out_text ? out_text : "(no output)");
    push_log("--- push end ---");
    load_status();
}

/* Checks out an existing branch. Git refuses if local changes would be overwritten; its message is shown. */
static void do_switch(const char *name) {
    if (!valid_branch_name(name)) { set_msg("Branch name not allowed."); return; }
    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd, "git checkout %s", name);
    if (run_git("git checkout", cmd) == 0) add_reload_hint();
    load_status();
}

static void do_new_branch(void) {
    char name[NAME_MAX_LEN] = "";
    if (!prompt_line("New branch name: ", name, sizeof name)) {
        set_msg("Cancelled.");
        return;
    }
    if (!valid_branch_name(name)) { set_msg("Branch name not allowed (letters, digits, . _ / -)."); return; }
    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd, "git checkout -b %s", name);
    if (run_git("git checkout -b", cmd) == 0) set_msg("Created and switched to a new branch.");
    load_status();
}

/* Merges a branch into the current one. On conflicts the merge stays open until fixed or aborted. */
static void do_merge(const char *name) {
    if (!valid_branch_name(name)) { set_msg("Branch name not allowed."); return; }
    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd, "git merge --no-edit %s", name);
    int rc = run_git("git merge", cmd);
    load_status();
    if (rc == 0) {
        add_reload_hint();
    } else if (merging) {
        char tmp[MSG_MAX];
        snprintf(tmp, sizeof tmp, "Merge conflicts in %d file(s). Fix them, then C Commit, or A Abort merge.", count_conflicts());
        set_msg(tmp);
    }
}

static void do_merge_abort(void) {
    run_git("git merge --abort", "git merge --abort");
    load_status();
}

/* Runs the highlighted row of the branch list, then returns to the status view. */
static void pick_activate(void) {
    if (npick == 0) return;
    const char *name = pick_name[psel];
    picking = 0;
    if (!name) do_new_branch();
    else if (pick_merge) do_merge(name);
    else do_switch(name);
}

static void open_pick(int merge) {
    pick_merge = merge;
    picking = 1;
    msg[0] = '\0';
    load_branches();
}

void git_modal(void) {
    int done = 0;
    msg[0] = '\0';
    sel = 0;

    set_cursor_visibility(0);
    clear_screen();
    load_status();

    while (!done) {
        int key;
        if (picking) {
            pick_clamp();
            draw_pick();
            key = read_key();
            if (key == -1) continue;

            switch (key) {
                case KEY_ESC:    picking = 0; msg[0] = '\0'; break;
                case KEY_UP:     psel--; break;
                case KEY_DOWN:   psel++; break;
                case KEY_ENTER:  pick_activate(); break;
                case KEY_RESIZE: term_update_size(); clear_screen(); break;
                default: break;
            }
            continue;
        }

        clamp_top();
        draw();
        key = read_key_wait(-1);   /* not read_key(): the file list takes mouse clicks */
        if (key == -1) continue;   /* unmapped key (e.g. Alt released): ignore, don't close */

        switch (key) {
            case KEY_ESC:
            case KEY_ALT_G:  done = 1; break;
            case KEY_UP:     sel--; break;
            case KEY_DOWN:   sel++; break;
            case KEY_ENTER:  do_file_diff(); break;
            case KEY_MOUSE: {
                MouseEvent ev;
                term_get_mouse(&ev);
                if (!in_repo || nfiles == 0) break;
                if (ev.kind == MEV_WHEEL_UP) sel--;
                else if (ev.kind == MEV_WHEEL_DOWN) sel++;
                else if (ev.kind == MEV_PRESS && ev.y >= 1 && ev.y - 1 < list_rows()
                         && top + ev.y - 1 < nfiles) {
                    sel = top + ev.y - 1;
                    do_file_diff();
                }
                break;
            }
            case KEY_PASTE: {
                int len;
                free(term_take_paste(&len));
                break;
            }
            case ' ':        if (in_repo) do_toggle(); break;
            case 's':
            case 'S':        if (in_repo) do_stage_all(); break;
            case 'u':
            case 'U':        if (in_repo) do_unstage_all(); break;
            case 'c':
            case 'C':        if (in_repo) do_commit(); break;
            case 'd':
            case 'D':        if (in_repo) do_draft_commit(); break;
            case 'p':
            case 'P':        if (in_repo) do_push(); break;
            case 'b':
            case 'B':        if (in_repo) open_pick(0); break;
            case 'm':
            case 'M':        if (in_repo) open_pick(1); break;
            case 'a':
            case 'A':        if (in_repo && merging) do_merge_abort(); break;
            case 'r':
            case 'R':        load_status(); break;
            case KEY_RESIZE: term_update_size(); clear_screen(); break;
            default: break;
        }
    }

    clear_branches();
    clear_status();
    clear_output();
    set_cursor_visibility(1);
    clear_screen();
}
