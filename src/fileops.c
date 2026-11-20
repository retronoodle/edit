/* File operations for the sidebar. See fileops.h. */
#ifndef _WIN32
    #define _XOPEN_SOURCE 700   /* realpath, lstat under -std=c99 */
#endif

#include <stdio.h>
#include <string.h>

#include "fileops.h"

int fileops_valid_name(const char *name) {
    if (!name || !*name) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') return 0;
    return 1;
}

#ifdef _WIN32

void fileops_unique_name(const char *d, const char *n, char *o, size_t s) { (void)d; snprintf(o, s, "%s", n); }
const char *fileops_new_file(const char *d, const char *n, char *o, size_t s) { (void)d; (void)n; (void)o; (void)s; return "not available on Windows"; }
const char *fileops_new_dir(const char *d, const char *n, char *o, size_t s) { (void)d; (void)n; (void)o; (void)s; return "not available on Windows"; }
const char *fileops_copy(const char *a, const char *d, char *o, size_t s) { (void)a; (void)d; (void)o; (void)s; return "not available on Windows"; }

#else

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_MAX_LEN 1024
#define MAX_COPY_DEPTH 40

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

static int join(char *out, size_t size, const char *dir, const char *name) {
    int n = snprintf(out, size, "%s/%s", dir, name);
    return n > 0 && (size_t)n < size;
}

void fileops_unique_name(const char *dir, const char *name, char *out, size_t size) {
    char path[PATH_MAX_LEN];
    snprintf(out, size, "%s", name);
    if (!join(path, sizeof path, dir, out) || !exists(path)) return;

    /* Split at the last dot, but a leading dot is part of the stem (".env") */
    const char *dot = strrchr(name, '.');
    int stem = (dot && dot != name) ? (int)(dot - name) : (int)strlen(name);
    const char *ext = name + stem;
    for (int k = 1; k < 10000; k++) {
        if (k == 1) snprintf(out, size, "%.*s copy%s", stem, name, ext);
        else snprintf(out, size, "%.*s copy %d%s", stem, name, k, ext);
        if (!join(path, sizeof path, dir, out) || !exists(path)) return;
    }
}

const char *fileops_new_file(const char *dir, const char *name, char *out, size_t size) {
    if (!fileops_valid_name(name)) return "invalid name";
    if (!join(out, size, dir, name)) return "path too long";
    if (exists(out)) return "already exists";
    FILE *f = fopen(out, "wb");
    if (!f) return "can't create the file";
    fclose(f);
    return NULL;
}

const char *fileops_new_dir(const char *dir, const char *name, char *out, size_t size) {
    if (!fileops_valid_name(name)) return "invalid name";
    if (!join(out, size, dir, name)) return "path too long";
    if (exists(out)) return "already exists";
    if (mkdir(out, 0777) != 0) return "can't create the folder";
    return NULL;
}

static const char *copy_file(const char *src, const char *dst, mode_t mode) {
    FILE *in = fopen(src, "rb");
    if (!in) return "can't read the file";
    int fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0600);   /* O_EXCL: never overwrite */
    FILE *o = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (!o) {
        if (fd >= 0) close(fd);
        fclose(in);
        return "can't create the copy";
    }
    char buf[8192];
    size_t n;
    const char *err = NULL;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, o) != n) { err = "write failed"; break; }
    }
    if (ferror(in)) err = "read failed";
    fclose(in);
    if (fclose(o) != 0 && !err) err = "write failed";
    if (!err) chmod(dst, mode & 0777);
    return err;
}

static const char *copy_tree(const char *src, const char *dst, int depth) {
    struct stat st;
    if (lstat(src, &st) != 0) return "can't read the source";
    if (S_ISLNK(st.st_mode)) return NULL;               /* symlinks are skipped */
    if (S_ISREG(st.st_mode)) return copy_file(src, dst, st.st_mode);
    if (!S_ISDIR(st.st_mode)) return NULL;
    if (depth > MAX_COPY_DEPTH) return "folder nests too deep";
    if (mkdir(dst, 0700) != 0) return "can't create the folder";   /* owner-only until filled */
    DIR *d = opendir(src);
    if (!d) return "can't read the folder";
    const char *err = NULL;
    struct dirent *e;
    while (!err && (e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char s[PATH_MAX_LEN], t[PATH_MAX_LEN];
        if (!join(s, sizeof s, src, e->d_name) || !join(t, sizeof t, dst, e->d_name)) { err = "path too long"; break; }
        err = copy_tree(s, t, depth + 1);
    }
    closedir(d);
    if (!err) chmod(dst, st.st_mode & 0777);   /* same permissions as the source folder */
    return err;
}

/* 1 if `inner` is `outer` or lies inside it (both resolved). */
static int inside(const char *inner, const char *outer) {
    char a[PATH_MAX_LEN], b[PATH_MAX_LEN];
    if (!realpath(inner, a) || !realpath(outer, b)) return 0;
    size_t n = strlen(b);
    if (strncmp(a, b, n) != 0) return 0;
    return a[n] == '\0' || a[n] == '/' || (n > 0 && b[n - 1] == '/');
}

const char *fileops_copy(const char *src, const char *dstdir, char *out, size_t size) {
    struct stat st;
    if (lstat(src, &st) != 0) return "the source is gone";
    if (S_ISLNK(st.st_mode)) return "symlinks are not copied";
    if (stat(dstdir, &st) != 0 || !S_ISDIR(st.st_mode)) return "the target is not a folder";
    lstat(src, &st);
    if (S_ISDIR(st.st_mode) && inside(dstdir, src)) return "can't copy a folder into itself";

    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    if (!*base) return "invalid source";
    char name[256];
    fileops_unique_name(dstdir, base, name, sizeof name);
    if (!join(out, size, dstdir, name)) return "path too long";
    return copy_tree(src, out, 0);
}

#endif
