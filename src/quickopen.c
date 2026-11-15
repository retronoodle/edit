/* Quick Open (Ctrl+P): scans every file under the current folder once, then
   filters the list as the user types. Matching lives in fuzzy.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "options.h"
#include "files.h"
#include "recent.h"
#include "utf8.h"
#include "fuzzy.h"
#include "quickopen.h"

#define QO_MAX_FILES 20000   /* the scan stops here and the list says so */
#define QO_MAX_DEPTH 8       /* folders deeper than this are not entered */
#define QO_PATH 512
#define QO_QUERY 256

#ifdef _WIN32
    #define QO_SEP '\\'
#else
    #define QO_SEP '/'
#endif

typedef struct {
    char *rel;     /* path relative to the folder Quick Open started in */
    int recent;    /* 1: this file is in the recent list (shown first with no query) */
    int score;     /* fuzzy score for the current query, -1 = no match */
} QFile;

static QFile *files = NULL;
static int file_count = 0, file_cap = 0;
static int truncated = 0;
static int *shown = NULL;      /* indexes into files, best match first */
static int shown_count = 0;

static void free_files(void) {
    for (int i = 0; i < file_count; i++) free(files[i].rel);
    free(files);
    free(shown);
    files = NULL;
    shown = NULL;
    file_count = file_cap = shown_count = 0;
    truncated = 0;
}

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void add_file(const char *rel) {
    if (file_count >= QO_MAX_FILES) { truncated = 1; return; }
    if (file_count == file_cap) {
        int cap = file_cap ? file_cap * 2 : 1024;
        QFile *grown = realloc(files, (size_t)cap * sizeof(QFile));
        if (!grown) { truncated = 1; return; }
        files = grown;
        file_cap = cap;
    }
    char *copy = dup_str(rel);
    if (!copy) { truncated = 1; return; }
    files[file_count].rel = copy;
    files[file_count].recent = 0;
    files[file_count].score = -1;
    file_count++;
}

/* Folders that are never worth searching. Hidden folders (.git, .vs, ...) are skipped too. */
static int skip_dir(const char *name) {
    static const char *skip[] = {
        "node_modules", "vendor", "__pycache__", "build", "dist", "bin", "obj", NULL
    };
    if (name[0] == '.') return 1;
    for (int i = 0; skip[i]; i++) {
        if (strcmp(name, skip[i]) == 0) return 1;
    }
    return 0;
}

/* Builds "dir\name" (or "name" at the top level) into out. Returns 0 if it does not fit. */
static int make_child(char *out, size_t size, const char *dir, const char *name) {
    int n = dir[0] ? snprintf(out, size, "%s%c%s", dir, QO_SEP, name)
                   : snprintf(out, size, "%s", name);
    return n > 0 && (size_t)n < size;
}

/* Walks dir (relative to the cwd, "" = the cwd) and adds every file found. */
static void scan_dir(const char *dir, int depth) {
    char child[QO_PATH];
#ifdef _WIN32
    char pattern[QO_PATH];
    if (dir[0]) snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    else snprintf(pattern, sizeof(pattern), "*");

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (truncated) break;
        const char *name = fd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   /* junctions and links */
        if (!make_child(child, sizeof(child), dir, name)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < QO_MAX_DEPTH && !skip_dir(name)) scan_dir(child, depth + 1);
        } else {
            add_file(child);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (truncated) break;
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!make_child(child, sizeof(child), dir, name)) continue;
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < QO_MAX_DEPTH && !skip_dir(name)) scan_dir(child, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            add_file(child);
        }
    }
    closedir(d);
#endif
}

/* Sorts by name, ignoring case. Gives the empty-query list a stable order. */
static int cmp_rel(const void *a, const void *b) {
    const QFile *x = a, *y = b;
    const unsigned char *p = (const unsigned char *)x->rel;
    const unsigned char *q = (const unsigned char *)y->rel;
    while (*p && tolower(*p) == tolower(*q)) { p++; q++; }
    return tolower(*p) - tolower(*q);
}

static int char_eq_norm(char a, char b) {
    if (a == '\\') a = '/';
    if (b == '\\') b = '/';
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

/* True if the recent path ends with rel as whole path parts. Recent entries
   are full paths, the scan gives relative ones, so this is a best guess. */
static int path_ends_with(const char *full, const char *rel) {
    size_t n = strlen(full), m = strlen(rel);
    if (m == 0 || m > n) return 0;
    if (m < n && full[n - m - 1] != '\\' && full[n - m - 1] != '/') return 0;
    for (size_t i = 0; i < m; i++) {
        if (!char_eq_norm(full[n - m + i], rel[i])) return 0;
    }
    return 1;
}

static void mark_recent(void) {
    for (int r = 0; r < recent_count(); r++) {
        const char *full = recent_get(r);
        for (int i = 0; i < file_count; i++) {
            if (!files[i].recent && path_ends_with(full, files[i].rel)) files[i].recent = 1;
        }
    }
}

/* Best score first; equal scores keep the name order. */
static int cmp_shown(const void *a, const void *b) {
    int i = *(const int *)a, j = *(const int *)b;
    if (files[i].score != files[j].score) return files[j].score - files[i].score;
    return i - j;
}

/* Rebuilds the visible list for the query. Empty query: recent files first, then all. */
static void refilter(const char *query) {
    shown_count = 0;
    for (int i = 0; i < file_count; i++) {
        int s = query[0] ? fuzzy_score(query, files[i].rel)
                         : (files[i].recent ? 1 : 0);
        files[i].score = s;
        if (s >= 0) shown[shown_count++] = i;
    }
    qsort(shown, (size_t)shown_count, sizeof(int), cmp_shown);
}

/* Scans the cwd. Returns the number of files found (0 = nothing to show). */
static int scan_files(void) {
    scan_dir("", 0);
    if (file_count == 0) return 0;
    qsort(files, (size_t)file_count, sizeof(QFile), cmp_rel);
    mark_recent();
    shown = malloc((size_t)file_count * sizeof(int));
    if (!shown) return 0;
    return file_count;
}

static int qo_rows(void) {
    return screen_h - 5;   /* same layout as the Open browser */
}

static void draw_qo(const char *query, int sel, int top, int scanning) {
    char line[QO_PATH + QO_QUERY + 64];
    fb_reset();
    fb_puts("\x1b[?25l");

    snprintf(line, sizeof(line), " Quick open: %s_", query);
    fb_gotoxy(0, 1);
    fb_printf("\x1b[7m%-*.*s%s", screen_w - 1, screen_w - 1, line, options_base_sgr());

    for (int row = 0; row < qo_rows(); row++) {
        int idx = top + row;
        fb_gotoxy(0, 2 + row);
        if (!scanning && idx < shown_count) {
            int hl = (idx == sel);
            if (hl) fb_puts("\x1b[7m");
            snprintf(line, sizeof(line), "  %s", files[shown[idx]].rel);
            fb_printf("%-*.*s", screen_w - 1, screen_w - 1, line);
            if (hl) fb_puts(options_base_sgr());
        } else {
            fb_printf("%-*s", screen_w - 1, "");
        }
    }

    fb_gotoxy(0, screen_h - 3);
    fb_printf("\x1b[7m%-*.*s%s", screen_w - 1, screen_w - 1,
              " Enter=open  Esc=cancel  Up/Down=move  (type to filter)", options_base_sgr());

    fb_gotoxy(0, screen_h - 2);
    if (scanning) {
        snprintf(line, sizeof(line), " Scanning...");
    } else {
        snprintf(line, sizeof(line), " %d of %d files%s", shown_count, file_count,
                 truncated ? "  (list capped at 20000 files)" : "");
    }
    fb_printf("%-*.*s", screen_w - 1, screen_w - 1, line);

    fb_flush();
}

/* Same rules as the Save As name field in files.c. */
static int is_text_key(int key) {
    return key >= 32 && key != 127 && (key < 0x80 || key >= 0xA0) && key < KEY_BASE;
}

static void query_append(char *q, size_t size, int key) {
    char enc[4];
    int n = utf8_encode(key, enc);
    size_t len = strlen(q);
    if (len + (size_t)n + 1 > size) return;   /* query is full */
    memcpy(q + len, enc, (size_t)n);
    q[len + (size_t)n] = '\0';
}

static void query_backspace(char *q) {
    size_t len = strlen(q);
    if (len == 0) return;
    len--;
    while (len > 0 && ((unsigned char)q[len] & 0xC0) == 0x80) len--;   /* whole UTF-8 character */
    q[len] = '\0';
}

/* Pasted text goes into the query without its line breaks or control bytes. */
static void query_append_text(char *q, size_t size, const char *text, int len) {
    size_t l = strlen(q);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 32 && c != 127 && l + 1 < size) q[l++] = (char)c;
    }
    q[l] = '\0';
}

/* Key loop. Mouse wheel scrolls the list; other mouse events are ignored. */
static int quick_open_loop(void) {
    char query[QO_QUERY] = "";
    int sel = 0, top = 0;

    while (1) {
        if (sel >= shown_count) sel = shown_count - 1;
        if (sel < 0) sel = 0;
        if (sel < top) top = sel;
        if (sel >= top + qo_rows()) top = sel - qo_rows() + 1;
        if (top < 0) top = 0;
        draw_qo(query, sel, top, 0);

        int key = read_key_wait(-1);
        if (key == -1) continue;
        if (key == KEY_MOUSE) {
            MouseEvent ev;
            term_get_mouse(&ev);
            if (ev.kind == MEV_WHEEL_UP) sel -= 3;
            if (ev.kind == MEV_WHEEL_DOWN) sel += 3;
            continue;
        }
        if (key == KEY_PASTE) {
            int len = 0;
            char *text = term_take_paste(&len);
            if (text) {
                query_append_text(query, sizeof(query), text, len);
                free(text);
            }
            refilter(query);
            sel = 0;
            top = 0;
            continue;
        }

        switch (key) {
            case KEY_UP:
                if (sel > 0) sel--;
                break;
            case KEY_DOWN:
                if (sel < shown_count - 1) sel++;
                break;
            case KEY_RESIZE:
                term_update_size();
                clear_screen();
                break;
            case KEY_ESC:
                return 0;
            case KEY_BACKSPACE:
                if (query[0] == '\0') break;
                query_backspace(query);
                refilter(query);
                sel = 0;
                top = 0;
                break;
            case KEY_ENTER: {
                if (shown_count == 0) {
                    show_message("No matching files.");
                    break;
                }
                char name[QO_PATH];   /* copy: freeing the list drops the entry */
                snprintf(name, sizeof(name), "%s", files[shown[sel]].rel);
                return open_path(name, 0);
            }
            default:
                if (is_text_key(key)) {
                    query_append(query, sizeof(query), key);
                    refilter(query);
                    sel = 0;
                    top = 0;
                }
                break;
        }
    }
}

int do_quick_open(void) {
    int opened = 0;
    free_files();
    draw_qo("", 0, 0, 1);   /* show the panel while the folder is scanned */
    if (!scan_files()) {
        free_files();
        show_message("No files found under this folder.");
        return 0;
    }
    refilter("");
    opened = quick_open_loop();
    free_files();
    return opened;
}
