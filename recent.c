/* Recent files: a short list of full paths, saved to a per-user text file. */
#ifndef _WIN32
    #define _XOPEN_SOURCE 700   /* realpath */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recent.h"

#define PATH_LEN 512

static char entries[MAX_RECENT][PATH_LEN];
static int count = 0;

static int same_path(const char *a, const char *b) {
#ifdef _WIN32
    return _stricmp(a, b) == 0;    /* Windows file names ignore case */
#else
    return strcmp(a, b) == 0;
#endif
}

/* Builds the path of the list file in the user's home folder.
   `name` is the file name inside that folder. */
static int home_file_path(const char *name, char *buf, size_t size) {
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) return 0;
    snprintf(buf, size, "%s/%s", home, name);
    return 1;
}

static int list_file_path(char *buf, size_t size) {
    return home_file_path(".velo_recent", buf, size);
}

/* Pre-rename list file, read once if the new one does not exist yet. */
static int old_list_file_path(char *buf, size_t size) {
    return home_file_path(".edit_recent", buf, size);
}

static void recent_save(void) {
    char path[PATH_LEN];
    if (!list_file_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < count; i++) {
        fprintf(f, "%s\n", entries[i]);
    }
    fclose(f);
}

void recent_load(void) {
    char path[PATH_LEN];
    count = 0;
    if (!list_file_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* First run after the rename: take over the old list */
        if (!old_list_file_path(path, sizeof(path))) return;
        f = fopen(path, "rb");
        if (!f) return;
    }
    char line[PATH_LEN];
    while (count < MAX_RECENT && fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;
        snprintf(entries[count], PATH_LEN, "%s", line);
        count++;
    }
    fclose(f);
}

void recent_add(const char *name) {
    char full[PATH_LEN];
#ifdef _WIN32
    if (!_fullpath(full, name, sizeof(full))) return;
#else
    if (!realpath(name, full)) return;
#endif

    /* Drop an older copy of the same file, if there is one */
    int at = count;
    for (int i = 0; i < count; i++) {
        if (same_path(entries[i], full)) { at = i; break; }
    }
    if (at == count) {
        if (count < MAX_RECENT) count++;
        at = count - 1;      /* list is full: the oldest entry falls off the end */
    }
    for (int i = at; i > 0; i--) {
        memcpy(entries[i], entries[i - 1], PATH_LEN);
    }
    snprintf(entries[0], PATH_LEN, "%s", full);
    recent_save();
}

int recent_count(void) {
    return count;
}

const char *recent_get(int i) {
    return entries[i];
}

void recent_remove(int i) {
    if (i < 0 || i >= count) return;
    for (int j = i; j < count - 1; j++) {
        memcpy(entries[j], entries[j + 1], PATH_LEN);
    }
    count--;
    recent_save();
}
