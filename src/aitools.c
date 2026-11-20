/* Read-only project tools for the AI chat. See aitools.h. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* realpath, lstat under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "aitools.h"

#define MAX_PATH_LEN 1024
#define MAX_DEPTH 8
#define MAX_GREP_FILES 3000
#define MAX_FILE_BYTES (1024 * 1024)
#define LINE_SHOW 160

#ifdef _WIN32

void aitools_set_root(void) {}
char *aitools_list_dir(const char *rel) { (void)rel; return strdup("Error: tools are not available on Windows"); }
char *aitools_read_file(const char *rel, int s, int m) { (void)rel; (void)s; (void)m; return strdup("Error: tools are not available on Windows"); }
char *aitools_grep(const char *p, const char *rel) { (void)p; (void)rel; return strdup("Error: tools are not available on Windows"); }

#else

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[MAX_PATH_LEN];   /* realpath of the start folder */

void aitools_set_root(void) {
    char cwd[MAX_PATH_LEN];
    if (getcwd(cwd, sizeof cwd) && realpath(cwd, root)) return;
    root[0] = '\0';
}

#endif /* _WIN32 */

/* Growable output, capped at AITOOLS_MAX_OUT. */
typedef struct {
    char *p;
    int len;
    int full;   /* set when something did not fit */
} Out;

static void out_put(Out *o, const char *s, int n) {
    if (!o->p || o->full) return;
    if (o->len + n > AITOOLS_MAX_OUT) {
        n = AITOOLS_MAX_OUT - o->len;
        o->full = 1;
    }
    memcpy(o->p + o->len, s, n);
    o->len += n;
}

static void out_str(Out *o, const char *s) { out_put(o, s, (int)strlen(s)); }

/* Appends a short note (under 100 bytes) even when the output is full: the buffer has slack. */
static void out_force(Out *o, const char *s) {
    int n = (int)strlen(s);
    if (!o->p || n > 100) return;
    memcpy(o->p + o->len, s, n);
    o->len += n;
}

static int out_init(Out *o) {
    o->len = 0;
    o->full = 0;
    o->p = malloc(AITOOLS_MAX_OUT + 256);
    return o->p != NULL;
}

/* Closes the output: adds a note when cut, NUL-terminates, returns the text. */
static char *out_done(Out *o) {
    if (!o->p) return strdup("Error: out of memory");
    if (o->full) {
        const char *note = "\n[output truncated]\n";
        memcpy(o->p + o->len, note, strlen(note));
        o->len += (int)strlen(note);
    }
    o->p[o->len] = '\0';
    return o->p;
}

static char *err_text(const char *msg) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "Error: %s", msg);
    return strdup(tmp);
}

static int skip_name(const char *name) {
    static const char *skip[] = {
        "node_modules", "vendor", "__pycache__", "build", "dist", "bin", "obj", NULL
    };
    if (name[0] == '.') return 1;
    for (int i = 0; skip[i]; i++)
        if (strcmp(name, skip[i]) == 0) return 1;
    return 0;
}

int aitools_path_ok(const char *rel) {
    if (!rel) return 0;
    if (rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':') || strchr(rel, '\\')) return 0;
    const char *p = rel;
    while (*p) {
        const char *e = strchr(p, '/');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0) {
            char part[256];
            if (n >= (int)sizeof part) return 0;
            memcpy(part, p, n);
            part[n] = '\0';
            if (strcmp(part, ".") != 0 && skip_name(part)) return 0;   /* includes ".." and hidden */
        }
        if (!e) break;
        p = e + 1;
    }
    return 1;
}

#ifndef _WIN32

/* Resolves rel under the root to a real path in out. Returns NULL on success, else an
   error text (static). Symlinks that leave the root are refused. */
static const char *resolve(const char *rel, char *out) {
    if (!root[0]) return "no project folder";
    if (!aitools_path_ok(rel)) return "path not allowed";
    char joined[MAX_PATH_LEN * 2];
    int n = rel[0] ? snprintf(joined, sizeof joined, "%s/%s", root, rel)
                   : snprintf(joined, sizeof joined, "%s", root);
    if (n < 0 || n >= (int)sizeof joined) return "path too long";
    char real[MAX_PATH_LEN];
    if (!realpath(joined, real)) return "no such file or folder";
    size_t rl = strlen(root);
    if (strncmp(real, root, rl) != 0 || (real[rl] != '\0' && real[rl] != '/')) return "path not allowed";
    snprintf(out, MAX_PATH_LEN, "%s", real);
    return NULL;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

char *aitools_list_dir(const char *rel) {
    char real[MAX_PATH_LEN];
    const char *e = resolve(rel ? rel : "", real);
    if (e) return err_text(e);
    DIR *d = opendir(real);
    if (!d) return err_text("not a folder");
    char *names[AITOOLS_MAX_ENTRIES];
    int n = 0, more = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (skip_name(de->d_name)) continue;
        if (n >= AITOOLS_MAX_ENTRIES) { more = 1; break; }
        char child[MAX_PATH_LEN * 2];
        struct stat st;
        snprintf(child, sizeof child, "%s/%s", real, de->d_name);
        if (stat(child, &st) != 0) continue;
        size_t len = strlen(de->d_name);
        int isdir = S_ISDIR(st.st_mode);
        char *nm = malloc(len + 2);
        if (!nm) break;
        memcpy(nm, de->d_name, len);
        if (isdir) nm[len++] = '/';
        nm[len] = '\0';
        names[n++] = nm;
    }
    closedir(d);
    qsort(names, n, sizeof names[0], cmp_str);
    Out o;
    if (!out_init(&o)) {
        for (int i = 0; i < n; i++) free(names[i]);
        return err_text("out of memory");
    }
    for (int i = 0; i < n; i++) {
        out_str(&o, names[i]);
        out_str(&o, "\n");
        free(names[i]);
    }
    if (n == 0) out_str(&o, "(empty)\n");
    if (more) out_str(&o, "[more entries not shown]\n");
    return out_done(&o);
}

/* 1 if the first bytes of f contain a NUL (binary). Rewinds f. */
static int is_binary(FILE *f) {
    unsigned char buf[4096];
    size_t n = fread(buf, 1, sizeof buf, f);
    rewind(f);
    return memchr(buf, 0, n) != NULL;
}

char *aitools_read_file(const char *rel, int start_line, int max_lines) {
    char real[MAX_PATH_LEN];
    const char *e = resolve(rel ? rel : "", real);
    if (e) return err_text(e);
    struct stat st;
    if (stat(real, &st) != 0 || !S_ISREG(st.st_mode)) return err_text("not a file");
    if (st.st_size > MAX_FILE_BYTES) return err_text("file too large (use grep on it)");
    FILE *f = fopen(real, "rb");
    if (!f) return err_text("cannot open file");
    if (is_binary(f)) {
        fclose(f);
        return err_text("binary file");
    }
    if (start_line < 1) start_line = 1;
    if (max_lines < 1) max_lines = 200;
    if (max_lines > 500) max_lines = 500;

    Out o;
    if (!out_init(&o)) {
        fclose(f);
        return err_text("out of memory");
    }
    char line[4096];
    int lineno = 0, shown = 0, next = 0;   /* next: first line not shown, 0 = reached the end */
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        int ends = len > 0 && line[len - 1] == '\n';
        if (!ends) {   /* a very long line: keep its first part, drop the rest */
            char skip[4096];
            while (fgets(skip, sizeof skip, f)) {
                size_t l = strlen(skip);
                if (l > 0 && skip[l - 1] == '\n') break;
            }
        }
        lineno++;
        if (lineno < start_line) continue;
        if (shown >= max_lines) {
            next = lineno;
            break;
        }
        char prefix[16];
        snprintf(prefix, sizeof prefix, "%d: ", lineno);
        out_str(&o, prefix);
        out_put(&o, line, (int)len);
        if (o.full) {
            next = lineno;   /* cut by the size cap: resume at this line */
            break;
        }
        if (!ends) out_str(&o, "\n");
        shown++;
    }
    fclose(f);
    if (next) {
        char note[96];
        snprintf(note, sizeof note, "\n[more lines follow: call again with start_line=%d]\n", next);
        o.full = 0;
        out_force(&o, note);
    } else if (shown == 0) {
        out_str(&o, lineno ? "(start_line is past the end of the file)\n" : "(empty file)\n");
    }
    return out_done(&o);
}

typedef struct {
    const char *pat;
    Out *out;
    int hits, files;
} Grep;

static void grep_file(Grep *g, const char *real, const char *rel) {
    struct stat st;
    if (stat(real, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > MAX_FILE_BYTES) return;
    FILE *f = fopen(real, "rb");
    if (!f) return;
    if (is_binary(f)) {
        fclose(f);
        return;
    }
    char line[2048];
    int lineno = 0;
    while (g->hits < AITOOLS_MAX_HITS && fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        int cont = len > 0 && line[len - 1] != '\n';
        lineno++;
        if (strstr(line, g->pat)) {
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
            if (len > LINE_SHOW) line[LINE_SHOW] = '\0';
            char prefix[MAX_PATH_LEN + 24];
            snprintf(prefix, sizeof prefix, "%s:%d: ", rel, lineno);
            out_str(g->out, prefix);
            out_str(g->out, line);
            out_str(g->out, "\n");
            g->hits++;
        }
        while (cont && fgets(line, sizeof line, f)) {
            len = strlen(line);
            cont = len > 0 && line[len - 1] != '\n';
        }
    }
    fclose(f);
}

static void grep_dir(Grep *g, const char *real, const char *rel, int depth) {
    DIR *d = opendir(real);
    if (!d) return;
    struct dirent *de;
    while (g->hits < AITOOLS_MAX_HITS && g->files < MAX_GREP_FILES && (de = readdir(d)) != NULL) {
        if (skip_name(de->d_name)) continue;
        char creal[MAX_PATH_LEN * 2], crel[MAX_PATH_LEN * 2];
        snprintf(creal, sizeof creal, "%s/%s", real, de->d_name);
        if (rel[0]) snprintf(crel, sizeof crel, "%s/%s", rel, de->d_name);
        else snprintf(crel, sizeof crel, "%s", de->d_name);
        if (strlen(creal) >= MAX_PATH_LEN || strlen(crel) >= MAX_PATH_LEN) continue;
        /* Links are not followed in a walk: they could lead out of the root. */
        struct stat st;
        if (lstat(creal, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_DEPTH) grep_dir(g, creal, crel, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            g->files++;
            grep_file(g, creal, crel);
        }
    }
    closedir(d);
}

char *aitools_grep(const char *pattern, const char *rel) {
    if (!pattern || !*pattern) return err_text("pattern is empty");
    char real[MAX_PATH_LEN];
    const char *e = resolve(rel ? rel : "", real);
    if (e) return err_text(e);
    struct stat st;
    if (stat(real, &st) != 0) return err_text("no such file or folder");
    Out o;
    if (!out_init(&o)) return err_text("out of memory");
    Grep g = { pattern, &o, 0, 0 };
    if (S_ISDIR(st.st_mode)) grep_dir(&g, real, rel ? rel : "", 0);
    else grep_file(&g, real, rel);
    if (g.hits == 0) out_str(&o, "(no matches)\n");
    if (g.hits >= AITOOLS_MAX_HITS) out_str(&o, "[match limit reached]\n");
    return out_done(&o);
}

#endif /* !_WIN32 */

static int arg_int(const char *args, const char *key) {
    char *raw = ai_json_get_raw(args, key);
    int v = raw ? atoi(raw) : 0;
    free(raw);
    return v;
}

char *aitools_run(const char *name, const char *args) {
    if (!name || !args) return NULL;
    char *path = ai_json_get_string(args, "path");
    char *out = NULL;
    if (strcmp(name, "list_dir") == 0) {
        out = aitools_list_dir(path ? path : "");
    } else if (strcmp(name, "read_file") == 0) {
        out = aitools_read_file(path ? path : "", arg_int(args, "start_line"), arg_int(args, "max_lines"));
    } else if (strcmp(name, "grep") == 0) {
        char *pat = ai_json_get_string(args, "pattern");
        out = aitools_grep(pat, path ? path : "");
        free(pat);
    }
    free(path);
    return out;
}
