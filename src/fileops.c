/* File operations for the sidebar. See fileops.h. */
#ifndef _WIN32
    #define _XOPEN_SOURCE 700   /* realpath, lstat under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileops.h"

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
#else
    #include <dirent.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#define PATH_MAX_LEN 1024
#define MAX_COPY_DEPTH 40

int fileops_valid_name(const char *name) {
    if (!name || !*name) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') return 0;
#ifdef _WIN32
        if (strchr(":*?\"<>|", *p) || (unsigned char)*p < 32) return 0;
#endif
    }
    return 1;
}

static int exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

static int join(char *out, size_t size, const char *dir, const char *name) {
    int n = snprintf(out, size, "%s/%s", dir, name);
    return n > 0 && (size_t)n < size;
}

static int make_dir(const char *path, int mode) {
#ifdef _WIN32
    (void)mode;
    return _mkdir(path);
#else
    return mkdir(path, (mode_t)mode);
#endif
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
    if (make_dir(out, 0777) != 0) return "can't create the folder";
    return NULL;
}

/* Copies one regular file; never overwrites. `mode` is the source's permission bits (POSIX). */
static const char *copy_file(const char *src, const char *dst, unsigned mode) {
    FILE *in = fopen(src, "rb");
    if (!in) return "can't read the file";
#ifdef _WIN32
    (void)mode;
    FILE *o = exists(dst) ? NULL : fopen(dst, "wb");
    if (!o) {
        fclose(in);
        return "can't create the copy";
    }
#else
    int fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0600);   /* O_EXCL: never overwrite */
    FILE *o = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (!o) {
        if (fd >= 0) close(fd);
        fclose(in);
        return "can't create the copy";
    }
#endif
    char buf[8192];
    size_t n;
    const char *err = NULL;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, o) != n) { err = "write failed"; break; }
    }
    if (ferror(in)) err = "read failed";
    fclose(in);
    if (fclose(o) != 0 && !err) err = "write failed";
#ifndef _WIN32
    if (!err) chmod(dst, (mode_t)(mode & 0777));
#endif
    return err;
}

#ifdef _WIN32
/* 1 only for real symlinks and junctions. Other reparse points (OneDrive placeholders,
   dedup or compressed files) are ordinary content and get copied. */
static int win_is_link(const char *path) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
    return fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK || fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
}
#endif

static const char *copy_tree(const char *src, const char *dst, int depth) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(src);
    if (attr == INVALID_FILE_ATTRIBUTES) return "can't read the source";
    if (win_is_link(src)) return NULL;   /* symlinks and junctions are skipped */
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return copy_file(src, dst, 0);
    if (depth > MAX_COPY_DEPTH) return "folder nests too deep";
    if (make_dir(dst, 0700) != 0) return "can't create the folder";
    char pattern[PATH_MAX_LEN];
    if (!join(pattern, sizeof pattern, src, "*")) return "path too long";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return "can't read the folder";
    const char *err = NULL;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char s[PATH_MAX_LEN], t[PATH_MAX_LEN];
        if (!join(s, sizeof s, src, fd.cFileName) || !join(t, sizeof t, dst, fd.cFileName)) { err = "path too long"; break; }
        err = copy_tree(s, t, depth + 1);
    } while (!err && FindNextFileA(h, &fd));
    FindClose(h);
    return err;
#else
    struct stat st;
    if (lstat(src, &st) != 0) return "can't read the source";
    if (S_ISLNK(st.st_mode)) return NULL;               /* symlinks are skipped */
    if (S_ISREG(st.st_mode)) return copy_file(src, dst, st.st_mode);
    if (!S_ISDIR(st.st_mode)) return NULL;
    if (depth > MAX_COPY_DEPTH) return "folder nests too deep";
    if (make_dir(dst, 0700) != 0) return "can't create the folder";   /* owner-only until filled */
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
#endif
}

/* 1 if `inner` is `outer` or lies inside it (both resolved). */
static int inside(const char *inner, const char *outer) {
    char a[PATH_MAX_LEN], b[PATH_MAX_LEN];
#ifdef _WIN32
    if (!_fullpath(a, inner, sizeof a) || !_fullpath(b, outer, sizeof b)) return 0;
    for (char *p = a; *p; p++) if (*p == '\\') *p = '/';
    for (char *p = b; *p; p++) if (*p == '\\') *p = '/';
    size_t n = strlen(b);
    if (_strnicmp(a, b, n) != 0) return 0;
#else
    if (!realpath(inner, a) || !realpath(outer, b)) return 0;
    size_t n = strlen(b);
    if (strncmp(a, b, n) != 0) return 0;
#endif
    return a[n] == '\0' || a[n] == '/' || (n > 0 && b[n - 1] == '/');
}

static int is_dir_path(const char *path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

const char *fileops_copy(const char *src, const char *dstdir, char *out, size_t size) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(src);
    if (attr == INVALID_FILE_ATTRIBUTES) return "the source is gone";
    if (win_is_link(src)) return "links are not copied";
    int src_is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (lstat(src, &st) != 0) return "the source is gone";
    if (S_ISLNK(st.st_mode)) return "symlinks are not copied";
    int src_is_dir = S_ISDIR(st.st_mode);
#endif
    if (!is_dir_path(dstdir)) return "the target is not a folder";
    if (src_is_dir && inside(dstdir, src)) return "can't copy a folder into itself";

    const char *base = strrchr(src, '/');
#ifdef _WIN32
    const char *bs = strrchr(src, '\\');
    if (bs && (!base || bs > base)) base = bs;
#endif
    base = base ? base + 1 : src;
    if (!*base) return "invalid source";
    char name[256];
    fileops_unique_name(dstdir, base, name, sizeof name);
    if (!join(out, size, dstdir, name)) return "path too long";
    return copy_tree(src, out, 0);
}
