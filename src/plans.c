/* Saved AI chat plans (see plans.h). */
#ifndef _WIN32
    #define _XOPEN_SOURCE 700
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
    #include <dirent.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "plans.h"
#include "recent.h"

#define SLUG_MAX 40

void plans_slug(const char *text, char *out, size_t size) {
    size_t n = 0;
    int dash = 0;
    if (size == 0) return;
    const char *p = text ? text : "";
    while (*p) {   /* first line with a letter or digit */
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        int has = 0;
        for (size_t i = 0; i < len; i++)
            if (isalnum((unsigned char)p[i])) { has = 1; break; }
        if (has) {
            for (size_t i = 0; i < len && n < SLUG_MAX && n + 1 < size; i++) {
                unsigned char c = (unsigned char)p[i];
                if (isalnum(c)) {
                    if (dash && n > 0) out[n++] = '-';
                    dash = 0;
                    if (n + 1 < size) out[n++] = (char)tolower(c);
                } else {
                    dash = 1;
                }
            }
            break;
        }
        p = e ? e + 1 : p + len;
    }
    if (n == 0) {
        snprintf(out, size, "plan");
        return;
    }
    out[n] = '\0';
}

#ifdef _WIN32
int plans_save(const char *text, char *path, size_t size) {
    (void)text; (void)path; (void)size;
    return 0;
}
#else
static int cmp_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

/* Only files plans_save wrote: yyyymmdd-hhmmss-<slug>.md */
static int is_plan_name(const char *n) {
    for (int i = 0; i < 15; i++) {
        if (i == 8 ? n[i] != '-' : !isdigit((unsigned char)n[i])) return 0;
    }
    return n[15] == '-';
}

static void prune(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    size_t cap = 64, n = 0;
    char (*names)[256] = malloc(cap * sizeof *names);
    struct dirent *de;
    while (names && (de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l < 4 || strcmp(de->d_name + l - 3, ".md") != 0) continue;
        if (!is_plan_name(de->d_name)) continue;
        if (n == cap) {
            char (*t)[256] = realloc(names, cap * 2 * sizeof *names);
            if (!t) break;
            names = t;
            cap *= 2;
        }
        snprintf(names[n++], sizeof *names, "%s", de->d_name);
    }
    closedir(d);
    if (names && n > PLANS_KEEP) {
        qsort(names, n, sizeof *names, cmp_names);   /* timestamp prefix: oldest first */
        for (size_t i = 0; i < n - PLANS_KEEP; i++) {
            char full[1024];
            snprintf(full, sizeof full, "%s/%s", dir, names[i]);
            unlink(full);
        }
    }
    free(names);
}

int plans_save(const char *text, char *path, size_t size) {
    char dir[512];
    if (!text || !home_file_path(".velo_plans", dir, sizeof dir)) return 0;
    if (mkdir(dir, 0700) != 0 && access(dir, W_OK) != 0) return 0;
    char slug[SLUG_MAX + 8], stamp[32];
    plans_slug(text, slug, sizeof slug);
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
    int fd = -1;
    for (int i = 0; i < 100 && fd < 0; i++) {   /* same-second clash: add a counter */
        int w = i == 0 ? snprintf(path, size, "%s/%s-%s.md", dir, stamp, slug)
                       : snprintf(path, size, "%s/%s-%s-%d.md", dir, stamp, slug, i);
        if (w < 0 || (size_t)w >= size) return 0;
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }
    if (fd < 0) return 0;
    size_t len = strlen(text), off = 0;
    int ok = 1;
    while (off < len) {
        ssize_t w = write(fd, text + off, len - off);
        if (w <= 0) { ok = 0; break; }
        off += (size_t)w;
    }
    if (ok && (len == 0 || text[len - 1] != '\n') && write(fd, "\n", 1) != 1) ok = 0;
    if (close(fd) != 0) ok = 0;
    if (!ok) {
        unlink(path);
        return 0;
    }
    prune(dir);
    return 1;
}
#endif
