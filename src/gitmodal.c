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

#ifndef _WIN32
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
#endif

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
#include "diffcap.h"
#include "gitpush.h"
#include "msgedit.h"
#include "utf8.h"

#define MSG_MAX 256
#define CMD_MAX 1536   /* room for a full temp path inside the git commit command */
#define COMMIT_MAX 256
#define DIFF_MAX 30000   /* diff bytes sent to the AI for a commit draft */
#define SUMMARY_MAX 4096   /* file list bytes sent to the AI ahead of the diff */
#define COMMIT_BODY_MAX 4096   /* longest drafted message kept from the AI */
#define VIEW_DIFF_MAX 200000   /* diff bytes shown in the viewer (Enter / click on a file) */

static const char COMMIT_SYSTEM[] =
    "Write a git commit message for the changes below: a file list, then the diff (which may be "
    "cut short, but every file is in the list). Format: a subject line in imperative mood, max 72 "
    "characters, no prefix, no trailing period; then a blank line; then one '- ' bullet per "
    "distinct change, each under 72 characters, covering every file in the list. No quotes, no "
    "code fences. Output only the message.";
#define NAME_MAX_LEN 128   /* longest branch name we accept */

static char *text_buf = NULL;   /* git status output; header and files point into it */
static char **files = NULL;     /* changed-file lines (porcelain) */
static int nfiles = 0;
static const char *header = "";  /* "master...origin/master [ahead 1]" */
static int unpushed = 0;         /* commits not on any remote (from header, or rev-list without an upstream) */
static int in_repo = 0;
static int have_origin = 0;      /* a remote named origin exists (set by load_status) */
static int merging = 0;          /* a merge is stopped (MERGE_HEAD exists) */
static int top = 0;             /* first visible file row */
static int sel = 0;             /* highlighted file (Up/Down move it, Space stages it) */
static char msg[MSG_MAX];       /* result of the last action, or a git error */
/* Commit message box (D drafts into it, Ctrl+S commits, Esc hides it). The draft is kept
   until it is committed or the panel closes. */
#define BOX_TEXT 5        /* most text rows the box shows */
#define BOX_MIN_LIST 2    /* file rows kept on screen while the box is open */
static MsgEdit cmsg;
static int box_open = 0;
static int dtop = 0, dleft = 0;   /* box scroll: first line, first display column */

/* Text rows of the box, fewer on short screens so the file list keeps BOX_MIN_LIST rows. */
static int box_text_rows(void) {
    int r = screen_h - 3 - BOX_MIN_LIST - 1;
    if (r > BOX_TEXT) r = BOX_TEXT;
    return r < 1 ? 1 : r;
}

/* Screen rows the box takes: a header plus its text rows (0 when hidden). */
static int box_rows(void) { return box_open ? 1 + box_text_rows() : 0; }

/* Output panel: the last git action, replaced by each new one. */
#define OUT_ROWS 4        /* header line + 3 tail lines of output */
#define OUT_MIN_H 10      /* below this screen height there is no panel */
typedef enum { OUT_NONE, OUT_RUNNING, OUT_OK, OUT_FAILED, OUT_INFO } OutState;  /* OUT_INFO: failed, but a push offer is pending */
static OutState out_state = OUT_NONE;
static char out_label[32] = "";  /* e.g. "git push" */
static int out_rc = 0;
static char *out_text = NULL;    /* full output of that action (malloc'd) */

static int out_shown(void) { return screen_h >= OUT_MIN_H && !box_open; }

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
    int r = screen_h - 3 - box_rows() - (out_shown() ? OUT_ROWS : 0);
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

/* pclose() and waitpid() return a wait status, not the exit code: git's exit 128
   shows up as 128 << 8 = 32768. Decode it; a signal gives 128 + signal number. */
static int exit_code(int st) {
#ifdef _WIN32
    return st;
#else
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
#endif
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
    return exit_code(rc);
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
    have_origin = 0;
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

    char *ro = NULL;
    have_origin = (run_cmd("git remote get-url origin", &ro) == 0);
    free(ro);

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
static const char *kind_sgr[4][FS_COUNT] = {
    /* Default */
    { "\x1b[0m", "\x1b[1;31m", "\x1b[35m", "\x1b[32m", "\x1b[31m", "\x1b[36m", "\x1b[33m" },
    /* Blue */
    { "\x1b[0m", "\x1b[1;91m", "\x1b[95m", "\x1b[92m", "\x1b[91m", "\x1b[96m", "\x1b[93m" },
    /* Light */
    { "\x1b[0m", "\x1b[1;31m", "\x1b[38;5;90m", "\x1b[38;5;28m", "\x1b[31m", "\x1b[38;5;30m", "\x1b[38;5;130m" },
    /* Slate */
    { "\x1b[0m", "\x1b[1;38;5;203m", "\x1b[38;5;176m", "\x1b[38;5;114m", "\x1b[38;5;203m", "\x1b[38;5;80m", "\x1b[38;5;215m" },
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
        case OUT_INFO:
            snprintf(head, sizeof head, "  $ %s  no upstream", out_label);
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

static int have_draft(void) { return cmsg.len > 0; }

static void clear_draft(void) {
    msgedit_clear(&cmsg);
    box_open = 0;
    dtop = 0;
    dleft = 0;
}

/* Scrolls the box so the cursor stays visible. */
static void box_clamp(void) {
    int cl, cc;
    msgedit_cursor(&cmsg, &cl, &cc);
    int th = box_text_rows();
    int w = screen_w - 1;
    if (w < 1) w = 1;
    if (cl < dtop) dtop = cl;
    if (cl >= dtop + th) dtop = cl - th + 1;
    if (cc < dleft) dleft = cc;
    if (cc >= dleft + w) dleft = cc - w + 1;
    if (dtop < 0) dtop = 0;
    if (dleft < 0) dleft = 0;
}

/* One text row of the box: the part of the line visible from column dleft. cb is the cursor's
   byte offset in the line when it is on this row, else -1; the cursor is a reversed cell. */
static void box_row(int y, const char *lt, int len, int cb) {
    int w = screen_w - 1;
    if (w < 0) w = 0;
    int b0 = utf8_byte_at_col(lt, len, dleft);
    int b1 = b0 + utf8_byte_at_col(lt + b0, len - b0, w);
    int cells = utf8_width(lt + b0, b1 - b0);

    fb_gotoxy(0, y);
    fb_puts(options_base_sgr());
    if (cb >= b0 && cb < b1) {              /* cursor on a character */
        int nx = utf8_next(lt, len, cb);
        fb_write(lt + b0, cb - b0);
        fb_puts(options_chrome_sgr(CH_BAR));
        fb_write(lt + cb, nx - cb);
        fb_puts(options_base_sgr());
        fb_write(lt + nx, b1 - nx);
    } else if (cb == b1) {                  /* cursor after the last visible character */
        fb_write(lt + b0, b1 - b0);
        fb_puts(options_chrome_sgr(CH_BAR));
        fb_write(" ", 1);
        fb_puts(options_base_sgr());
        cells++;
    } else {
        fb_write(lt + b0, b1 - b0);
    }
    for (int i = cells; i < w; i++) fb_write(" ", 1);
    fb_puts(options_base_sgr());
}

/* The box: a header row, then its text rows just above the message row. */
static void draw_box(void) {
    if (!box_open) return;
    box_clamp();
    int top = 1 + list_rows();
    int th = box_text_rows();
    int nl = msgedit_nlines(&cmsg);
    char head[80];
    snprintf(head, sizeof head, "  Commit message, %d line%s", nl, nl == 1 ? "" : "s");
    put_row(top, options_chrome_sgr(CH_BAR), head);

    int cl, cc;
    msgedit_cursor(&cmsg, &cl, &cc);
    for (int r = 0; r < th; r++) {
        int line = dtop + r;
        int start, len;
        if (msgedit_line(&cmsg, line, &start, &len)) {
            int here = (line == cl) ? cmsg.cur - start : -1;
            box_row(top + 1 + r, cmsg.text + start, len, here);
        } else {
            box_row(top + 1 + r, "", 0, -1);
        }
    }
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
            if (i == 0) t = "  Not a git repository. Press I to create one here.";
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

    draw_box();
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
    if (box_open) foot = " Type to edit  Ctrl+S Commit  Esc Hide (keeps draft)";
    else if (!in_repo) foot = " I Init repo  Esc Close";
    else if (merging) foot = " Space Stage  Enter Diff  S/U All  C Commit  D Draft  A Abort merge  R Refresh  Esc Close";
    else if (!have_origin) foot = " Space Stage  Enter Diff  S/U All  C Commit  D Draft  N New remote  B Branch  M Merge  R Refresh  Esc Close";
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
    int rc = run_cmd("git branch --sort=-committerdate", &out);
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
    put_row(screen_h - 1, options_chrome_sgr(CH_STATUS), " Enter/Click Pick  Up/Down/Wheel Move  Esc Back");
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

#ifndef _WIN32
/* Runs a program from argv with no shell, capturing stdout and stderr like run_cmd.
   Used for the push retry, where remote and branch come from git's output. */
static int run_argv(const char *label, char *const argv[]) {
    snprintf(out_label, sizeof out_label, "%s", label);
    free(out_text);
    out_text = NULL;
    out_state = OUT_RUNNING;
    msg[0] = '\0';
    if (picking) draw_pick();
    else draw();

    int fds[2];
    if (pipe(fds) != 0) { out_rc = -1; out_state = OUT_FAILED; return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        out_rc = -1;
        out_state = OUT_FAILED;
        return -1;
    }
    if (pid == 0) {
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        setenv("GIT_SSH_COMMAND", "ssh -o BatchMode=yes", 1);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);

    size_t cap = 1024, len = 0;
    char *buf = malloc(cap);
    char chunk[512];
    ssize_t n;
    while (buf && (n = read(fds[0], chunk, sizeof chunk)) > 0) {
        if (len + (size_t)n + 1 > cap) {
            size_t nc = cap;
            while (len + (size_t)n + 1 > nc) nc *= 2;
            char *nb = realloc(buf, nc);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
            cap = nc;
        }
        memcpy(buf + len, chunk, (size_t)n);
        len += (size_t)n;
    }
    close(fds[0]);
    if (buf) buf[len] = '\0';

    int st = 0;
    waitpid(pid, &st, 0);
    int rc = buf ? exit_code(st) : -1;
    out_text = buf;
    out_rc = rc;
    out_state = rc == 0 ? OUT_OK : OUT_FAILED;
    return rc;
}
#endif

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
   text (may be NULL) is the message; NULL asks for one on a line. The message goes through a
   temp file, which avoids shell quoting problems. A commit clears the draft; a failure keeps it. */
static void do_commit_with(const char *text) {
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
    if (!text) {
        if (!prompt_line("Commit message: ", message, sizeof message)) {
            set_msg("Commit cancelled.");
            return;
        }
        text = message;
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
    fputs(text, f);
    fputs("\n", f);
    fclose(f);

    if (stage_all && run_git("git add", "git add -A") != 0) {
        remove(path);
        load_status();
        return;
    }

    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd, "git commit -F \"%s\"", path);
    int committed = run_git("git commit", cmd) == 0;
    remove(path);
    if (committed) clear_draft();   /* text may point into cmsg, which is not used after this */
    load_status();
}

/* 1 when the draft has no text but whitespace. */
static int draft_blank(void) {
    for (int i = 0; i < cmsg.len; i++) {
        if (!isspace((unsigned char)cmsg.text[i])) return 0;
    }
    return 1;
}

/* Commits the draft (Ctrl+S in the box). The box closes so git's result shows in the panel. */
static void commit_draft(void) {
    box_open = 0;
    if (draft_blank()) {
        set_msg("Commit message is empty.");
        return;
    }
    do_commit_with(cmsg.text);
}

/* C: commits the draft when there is one (after a y), else asks for a message.
   Answering n drops the draft and asks for a message instead. */
static void do_commit(void) {
    if (have_draft()) {
        char ans[8] = "";
        if (!prompt_line("Commit with the drafted message? (y/n): ", ans, sizeof ans)) {
            set_msg("Commit cancelled.");
            return;
        }
        if (ans[0] == 'y' || ans[0] == 'Y') {
            commit_draft();
            return;
        }
        clear_draft();
    }
    do_commit_with(NULL);
}

/* 1 if s is empty or only whitespace (or NULL). */
static int is_blank(const char *s) {
    if (!s) return 1;
    while (*s && isspace((unsigned char)*s)) s++;
    return *s == '\0';
}

/* Appends s to *buf, which grows as needed. 0 when out of memory. */
static int cat_str(char **buf, size_t *len, const char *s) {
    size_t add = strlen(s);
    char *nb = realloc(*buf, *len + add + 1);
    if (!nb) return 0;
    memcpy(nb + *len, s, add + 1);
    *buf = nb;
    *len += add;
    return 1;
}

/* Trims surrounding whitespace in place. */
static void trim_ws(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

/* Gets what a commit draft reads: a file list (git's stat, plus untracked files when nothing
   is staged, since git diff never shows them), then the diff. The staged changes when something
   is staged, otherwise the working tree against HEAD. Each part is capped (diffcap.c), so every
   file keeps at least its first lines.
   Returns 0 with *out set (malloc'd, caller frees), or -1 with msg set. */
static int get_diff(char **out) {
    int staged = count_staged() > 0;
    char *text = NULL, *stat = NULL, *summary = NULL, *capped = NULL, *trimmed = NULL, *input = NULL;
    size_t slen = 0;
    int rc, ok = 1;

    if (staged) {
        rc = run_cmd("git diff --cached", &text);
        if (rc == 0) run_cmd("git diff --cached --stat", &stat);
    } else {
        rc = run_cmd("git diff HEAD", &text);
        if (rc != 0) {   /* no commits yet: HEAD does not exist */
            free(text);
            text = NULL;
            rc = run_cmd("git diff", &text);
            if (rc == 0) run_cmd("git diff --stat", &stat);
        } else {
            run_cmd("git diff HEAD --stat", &stat);
        }
    }
    if (rc != 0 || !text) {
        free(text);
        free(stat);
        set_msg("Could not read the diff.");
        return -1;
    }

    if (stat) ok = cat_str(&summary, &slen, stat);
    if (ok && !staged) {
        for (int i = 0; ok && i < nfiles; i++) {
            if (file_kind(files[i]) != FS_UNTRACKED) continue;
            ok = cat_str(&summary, &slen, "new file (untracked): ") &&
                 cat_str(&summary, &slen, files[i] + 3) &&
                 cat_str(&summary, &slen, "\n");
        }
    }

    if (is_blank(text) && is_blank(summary)) {
        free(text);
        free(stat);
        free(summary);
        if (!merging) {
            set_msg("Nothing to draft.");
            return -1;
        }
        /* A stopped merge can have no changes listed; say so rather than refusing */
        input = malloc(64);
        if (!input) {
            set_msg("Out of memory.");
            return -1;
        }
        snprintf(input, 64, "A merge is in progress with no file changes listed.\n");
        *out = input;
        return 0;
    }

    capped = diffcap_head(summary ? summary : "", SUMMARY_MAX, "[... more files not listed]\n");
    trimmed = diffcap_trim(text, DIFF_MAX);
    if (!capped || !trimmed) ok = 0;
    if (ok) {
        size_t total = strlen(capped) + strlen(trimmed) + 2;
        input = malloc(total);
        if (input) snprintf(input, total, "%s\n%s", capped, trimmed);
    }

    free(text);
    free(stat);
    free(summary);
    free(capped);
    free(trimmed);
    if (!input) {
        set_msg("Out of memory.");
        return -1;
    }
    *out = input;
    return 0;
}

/* Turns an AI reply into a commit message: LF only, no surrounding whitespace, code fences or
   quotes, capped at COMMIT_BODY_MAX. Malloc'd (NULL when out of memory); "" when nothing is left. */
static char *tidy_draft(const char *reply) {
    size_t n = strlen(reply);
    char *s = malloc(n + 1);
    if (!s) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) if (reply[i] != '\r') s[k++] = reply[i];
    s[k] = '\0';
    trim_ws(s);

    int fenced = strncmp(s, "```", 3) == 0;
    if (fenced) {   /* drop the opening fence line and a closing fence */
        char *nl = strchr(s, '\n');
        if (nl) memmove(s, nl + 1, strlen(nl + 1) + 1);
        else s[0] = '\0';
        trim_ws(s);
        n = strlen(s);
        if (n >= 3 && strcmp(s + n - 3, "```") == 0) {
            s[n - 3] = '\0';
            trim_ws(s);
        }
    }

    n = strlen(s);
    if (n >= 2 && strchr("\"'`", s[0]) && s[n - 1] == s[0]) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
        trim_ws(s);
    }

    char *capped = diffcap_head(s, COMMIT_BODY_MAX, "");
    free(s);
    return capped;
}

/* Drafts a commit message with the AI (D) into the commit message box. Ctrl+S commits it. */
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

    char *draft = tidy_draft(reply);
    free(reply);
    if (!draft) {
        set_msg("Out of memory.");
        return;
    }
    if (draft[0] == '\0') {
        free(draft);
        set_msg("The AI returned an empty message.");
        return;
    }

    msgedit_set(&cmsg, draft);
    free(draft);
    dtop = 0;
    dleft = 0;
    box_open = 1;
    set_msg("Edit the message. Ctrl+S commits, Esc hides it.");
}

static void push_log(const char *line);

/* Asks before running git's own suggestion, "git push --set-upstream <remote> <branch>".
   Nothing runs without a yes. The result shows in the output panel like any push. */
static void offer_set_upstream(char *remote, char *branch) {
    char q[320], ans[8] = "";
    snprintf(q, sizeof q, "Branch has no upstream. Run 'git push --set-upstream %s %s'? [y/N]: ",
             remote, branch);
    if (!prompt_line(q, ans, sizeof ans) || (ans[0] != 'y' && ans[0] != 'Y')) {
        set_msg("Push not retried. Upstream not set.");
        return;
    }

    int rc;
#ifdef _WIN32
    char cmd[CMD_MAX];
    snprintf(cmd, sizeof cmd,
             "set GIT_TERMINAL_PROMPT=0&& set GIT_SSH_COMMAND=ssh -o BatchMode=yes&& "
             "git push --set-upstream %s %s <NUL", remote, branch);
    rc = run_git("git push --set-upstream", cmd);
#else
    char *argv[] = { "git", "push", "--set-upstream", remote, branch, NULL };
    rc = run_argv("git push --set-upstream", argv);
#endif

    char rcline[64];
    snprintf(rcline, sizeof rcline, "set-upstream exit status: %d", rc);
    push_log(rcline);
    push_log(out_text && *out_text ? out_text : "(no output)");
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

/* Runs git push once and logs it. Returns the exit status; the output is in out_text. */
static int run_push(void) {
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
    return rc;
}

/* Reports the open merge after a failed pull, same wording as do_merge. */
static void report_merge_conflicts(void) {
    char tmp[MSG_MAX];
    snprintf(tmp, sizeof tmp, "Merge conflicts in %d file(s). Fix them, then C Commit, or A Abort merge.", count_conflicts());
    set_msg(tmp);
}

/* Asks before pulling (merge, no editor) when the remote has commits we lack, then pushes again.
   Nothing runs without a yes. A pull that stops on conflicts is not followed by a push. */
static void offer_pull_then_push(void) {
    char ans[8] = "";
    if (!prompt_line("Remote has new commits. Pull (merge) and push again? [y/N]: ", ans, sizeof ans) ||
        (ans[0] != 'y' && ans[0] != 'Y')) {
        set_msg("Push not retried. Pull first to merge the remote commits.");
        return;
    }

    int rc;
    push_log("--- pull start ---");
#ifdef _WIN32
    const char *pcmd = "set GIT_TERMINAL_PROMPT=0&& set GIT_SSH_COMMAND=ssh -o BatchMode=yes&& git pull --no-rebase --no-edit <NUL";
    rc = run_git("git pull", pcmd);
#else
    char *pargv[] = { "git", "pull", "--no-rebase", "--no-edit", NULL };
    rc = run_argv("git pull", pargv);
#endif
    char rcline[64];
    snprintf(rcline, sizeof rcline, "pull exit status: %d", rc);
    push_log(rcline);
    push_log(out_text && *out_text ? out_text : "(no output)");
    push_log("--- pull end ---");
    load_status();

    if (rc != 0) {
        if (merging) report_merge_conflicts();
        else set_msg("Pull failed. Push not retried.");
        return;
    }
    /* One retry only: if this push is refused too, its red output stays. */
    int prc = run_push();
    load_status();
    if (prc == 0) add_reload_hint();
}

static void do_push(void) {
    int rc = run_push();

    /* Only git's own hints are offered; other failures stay red. */
    char remote[NAME_MAX_LEN], branch[NAME_MAX_LEN];
    if (rc != 0 && gitpush_parse_upstream(out_text, remote, sizeof remote, branch, sizeof branch)) {
        out_state = OUT_INFO;
        offer_set_upstream(remote, branch);
    } else if (rc != 0 && gitpush_is_rejected_behind(out_text)) {
        out_state = OUT_INFO;
        offer_pull_then_push();
    }
    push_log("--- push end ---");
    load_status();
}

/* ---- Init and new GitHub remote ---- */

static void do_init(void) {
    char ans[8] = "";
    if (!prompt_line("Create a git repo in this folder? (y/N): ", ans, sizeof ans)
        || (ans[0] != 'y' && ans[0] != 'Y')) {
        set_msg("Cancelled.");
        return;
    }
    if (run_git("git init", "git init") == 0) set_msg("Repo created. Press N to add a GitHub remote.");
    load_status();
}

#ifndef _WIN32
/* GitHub repo names: letters, digits, '.', '_' and '-'. */
static int valid_repo_name(const char *s) {
    if (!*s || s[0] == '-' || s[0] == '.' || strlen(s) > 100) return 0;
    for (const char *q = s; *q; q++) {
        if (!(isalnum((unsigned char)*q) || *q == '.' || *q == '_' || *q == '-')) return 0;
    }
    return 1;
}
#endif

static void do_new_remote(void) {
#ifdef _WIN32
    set_msg("Creating a GitHub remote is not supported on Windows yet.");
#else
    char cwd[1024] = "";
    char name[101] = "";
    if (getcwd(cwd, sizeof cwd)) {
        const char *base = strrchr(cwd, '/');
        snprintf(name, sizeof name, "%s", base ? base + 1 : cwd);
    }
    if (!prompt_line_prefill("GitHub repo name: ", name, sizeof name)) {
        set_msg("Cancelled.");
        return;
    }
    if (!valid_repo_name(name)) { set_msg("Repo name not allowed (letters, digits, . _ -)."); return; }

    char vis[16] = "";
    if (!prompt_line_opt("Visibility, private or public (Enter = private): ", vis, sizeof vis)) {
        set_msg("Cancelled.");
        return;
    }
    const char *flag;
    if (vis[0] == '\0' || strcmp(vis, "private") == 0) flag = "--private";
    else if (strcmp(vis, "public") == 0) flag = "--public";
    else { set_msg("Cancelled. Type private or public."); return; }

    char *head = NULL;
    int has_head = (run_cmd("git rev-parse --verify -q HEAD", &head) == 0);
    free(head);
    if (!has_head) { set_msg("Commit something first, then press N."); return; }

    char *argv[] = { "gh", "repo", "create", name, (char *)flag, "--source=.",
                     "--remote=origin", "--push", NULL };
    int rc = run_argv("gh repo create", argv);
    load_status();
    if (rc == 127) {
        set_msg("GitHub CLI (gh) not found. Install it and run gh auth login.");
    } else if (rc == 0) {
        set_msg("Remote created and pushed.");
    } else if (have_origin) {
        set_msg("Remote created, but the push failed. Press P to retry.");
    } else {
        set_msg("gh repo create failed. See the output panel.");
    }
#endif
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
        report_merge_conflicts();
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
            key = read_key_wait(-1);   /* not read_key(): the branch list takes mouse clicks */
            if (key == -1) continue;

            switch (key) {
                case KEY_ESC:    picking = 0; msg[0] = '\0'; break;
                case KEY_UP:     psel--; break;
                case KEY_DOWN:   psel++; break;
                case KEY_ENTER:  pick_activate(); break;
                case KEY_MOUSE: {
                    MouseEvent ev;
                    term_get_mouse(&ev);
                    if (ev.kind == MEV_WHEEL_UP) psel--;
                    else if (ev.kind == MEV_WHEEL_DOWN) psel++;
                    else if (ev.kind == MEV_PRESS && ev.y >= 1 && ev.y - 1 < list_rows()
                             && ptop + ev.y - 1 < npick) {
                        psel = ptop + ev.y - 1;
                        pick_activate();
                    }
                    break;
                }
                case KEY_RESIZE: term_update_size(); clear_screen(); break;
                default: break;
            }
            continue;
        }

        if (box_open) {
            draw();
            key = read_key();
            if (key == -1) continue;

            switch (key) {
                case KEY_ESC:       box_open = 0; set_msg("Draft kept. E opens it again."); break;
                case KEY_CTRL_S:    commit_draft(); break;
                case KEY_ENTER:     msgedit_insert_cp(&cmsg, '\n'); break;
                case KEY_BACKSPACE: msgedit_backspace(&cmsg); break;
                case KEY_DELETE:    msgedit_delete(&cmsg); break;
                case KEY_LEFT:      msgedit_left(&cmsg); break;
                case KEY_RIGHT:     msgedit_right(&cmsg); break;
                case KEY_UP:        msgedit_up(&cmsg); break;
                case KEY_DOWN:      msgedit_down(&cmsg); break;
                case KEY_RESIZE:    term_update_size(); clear_screen(); break;
                case KEY_PASTE: {
                    int len;
                    char *text = term_take_paste(&len);
                    if (text) {
                        msgedit_paste(&cmsg, text, len);
                        free(text);
                    }
                    break;
                }
                default:
                    if (key >= 0 && key < KEY_BASE) msgedit_insert_cp(&cmsg, key);
                    break;
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
            case 'e':
            case 'E':        if (in_repo && have_draft()) box_open = 1; break;
            case 'p':
            case 'P':        if (in_repo) do_push(); break;
            case 'i':
            case 'I':        if (!in_repo) do_init(); break;
            case 'n':
            case 'N':        if (in_repo && !have_origin) do_new_remote(); break;
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
    clear_draft();
    set_cursor_visibility(1);
    clear_screen();
}
