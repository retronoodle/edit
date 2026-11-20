/* Project instruction files (AGENTS.md, SMALL_AGENTS.md) added to AI system prompts. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* strdup under -std=c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif
#include "aiinstr.h"

static char root[1024];

void aiinstr_set_root(void) {
    if (!getcwd(root, sizeof root)) root[0] = '\0';
}

char *aiinstr_join(const char *base, const char *name, const char *content, size_t cap) {
    if (!base) base = "";
    if (!content || !*content) return strdup(base);
    size_t clen = strlen(content);
    int cut = clen > cap;
    if (cut) {
        clen = cap;
        while (clen > 0 && ((unsigned char)content[clen] & 0xC0) == 0x80) clen--; /* don't split a UTF-8 char */
    }
    static const char note[] = "\n[truncated]";
    size_t total = strlen(base) + strlen(name) + clen + 64 + sizeof note;
    char *out = malloc(total);
    if (!out) return NULL;
    int n = snprintf(out, total, "%s\n\nProject instructions from %s:\n", base, name);
    memcpy(out + n, content, clen);
    out[n + clen] = '\0';
    if (cut) strcat(out, note);
    return out;
}

char *aiinstr_system(const char *base, const char *file, size_t cap) {
    char *content = NULL;
    if (root[0]) {
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", root, file);
        struct stat st;
        FILE *f = (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? fopen(path, "rb") : NULL;
        if (f) {
            content = malloc(cap + 1);
            if (content) {
                size_t n = fread(content, 1, cap + 1, f); /* one extra byte detects overflow */
                content[n] = '\0';
            }
            fclose(f);
        }
    }
    char *out = aiinstr_join(base, file, content, cap);
    free(content);
    return out ? out : strdup(base);
}
