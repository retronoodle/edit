/* File operations: save, save-as, new, open (with a folder browser), exit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <direct.h>
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
#include "undo.h"

/* ---- File operations. Each returns 1 on success, 0 if cancelled/failed. ---- */

static int write_file(const char *name) {
    /* Binary mode: Windows must not add its own CR, the line ending comes from options */
    FILE *file = fopen(name, "wb");
    if (!file) {
        show_message("Error: could not save file.");
        return 0;
    }
    const char *eol = options.crlf ? "\r\n" : "\n";
    for (int i = 0; i < editor.line_count; i++) {
        fwrite(editor.lines[i].text, 1, editor.lines[i].len, file);
        fputs(eol, file);
    }
    fclose(file);
    editor.modified = 0;

    char msg[SCREEN_WIDTH];
    snprintf(msg, sizeof(msg), "Saved %s", name);
    show_message(msg);
    return 1;
}

int do_save_as(void);

int do_save(void) {
    if (strlen(editor.filename) == 0 || strcmp(editor.filename, "untitled.txt") == 0) {
        return do_save_as();
    }
    return write_file(editor.filename);
}

int do_save_as(void) {
    char name[256];
    if (!prompt_line("Save as: ", name, sizeof(name))) return 0;
    if (!write_file(name)) return 0;
    strcpy(editor.filename, name);
    return 1;
}

/* Asks to save unsaved changes. Returns 1 to go ahead (saved or discarded),
   0 if the user cancelled with Esc or the save failed. */
static int confirm_discard(void) {
    if (!editor.modified) return 1;

    gotoxy(0, SCREEN_HEIGHT - 2);
    printf("%-79s", "Save changes? (Y)es / (N)o / Esc = cancel");
    fflush(stdout);

    while (1) {
        int c = getch_cp();
        if (c == 'y' || c == 'Y') return do_save();
        if (c == 'n' || c == 'N') return 1;
        if (c == 27) return 0;
    }
}

int do_new(void) {
    if (!confirm_discard()) return 0;
    strcpy(editor.filename, "untitled.txt");
    reset_document();
    return 1;
}

/* ---- File browser (used by Open) ---- */

#define MAX_DIR_ENTRIES 512
#define BROWSER_ROWS 20          /* list rows on screen rows 2..21 */

typedef struct {
    char name[256];
    int is_dir;
} DirEntry;

static DirEntry dir_entries[MAX_DIR_ENTRIES];
static int dir_count = 0;

static void add_dir_entry(const char *name, int is_dir) {
    snprintf(dir_entries[dir_count].name, sizeof(dir_entries[0].name), "%s", name);
    dir_entries[dir_count].is_dir = is_dir;
    dir_count++;
}

/* Folders first, then names, case-insensitive. Index 0 ("..") is excluded. */
static int dir_entry_cmp(const void *a, const void *b) {
    const DirEntry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    const unsigned char *p = (const unsigned char *)x->name;
    const unsigned char *q = (const unsigned char *)y->name;
    while (*p && tolower(*p) == tolower(*q)) { p++; q++; }
    return tolower(*p) - tolower(*q);
}

static void load_dir_entries(void) {
    dir_count = 0;
    add_dir_entry("..", 1);

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (dir_count >= MAX_DIR_ENTRIES) break;
            add_dir_entry(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(".");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if (dir_count >= MAX_DIR_ENTRIES) break;
            struct stat st;
            int is_dir = (stat(de->d_name, &st) == 0 && S_ISDIR(st.st_mode));
            add_dir_entry(de->d_name, is_dir);
        }
        closedir(d);
    }
#endif

    qsort(dir_entries + 1, dir_count - 1, sizeof(DirEntry), dir_entry_cmp);
}

static void current_dir(char *buf, size_t size) {
#ifdef _WIN32
    if (!_getcwd(buf, (int)size)) buf[0] = '\0';
#else
    if (!getcwd(buf, size)) buf[0] = '\0';
#endif
}

static int change_dir(const char *path) {
#ifdef _WIN32
    return _chdir(path) == 0;
#else
    return chdir(path) == 0;
#endif
}

static void draw_browser(int sel, int top) {
    char cwd[512];
    char line[512];   /* large enough for any path; fb_printf clips to 79 cols */
    current_dir(cwd, sizeof(cwd));

    fb_reset();
    fb_puts("\x1b[?25l");

    snprintf(line, sizeof(line), " Open: %s", cwd);
    fb_gotoxy(0, 1);
    fb_printf("\x1b[7m%-79.79s%s", line, options_base_sgr());

    for (int row = 0; row < BROWSER_ROWS; row++) {
        int idx = top + row;
        fb_gotoxy(0, 2 + row);
        if (idx < dir_count) {
            int hl = (idx == sel);
            if (hl) fb_puts("\x1b[7m");
            snprintf(line, sizeof(line), "  %s%s",
                     dir_entries[idx].is_dir ? "[DIR] " : "      ",
                     dir_entries[idx].name);
            fb_printf("%-79.79s", line);
            if (hl) fb_puts(options_base_sgr());
        } else {
            fb_printf("%-79s", "");
        }
    }

    fb_gotoxy(0, 22);
    fb_printf("\x1b[7m%-79s%s", " Enter=open  Esc=cancel  T=type name  Up/Down=move", options_base_sgr());
    fb_gotoxy(0, 23);
    fb_printf("%-79s", "");

    fb_flush();
}

/* Shows a file picker over the editor area. Entering a folder changes the
   working directory to it, so the chosen file name is relative to it.
   Returns 1 with the file name in out, or 0 if cancelled. */
static int browse_for_file(char *out, size_t size) {
    int sel = 0, top = 0;
    load_dir_entries();

    while (1) {
        if (sel < top) top = sel;
        if (sel >= top + BROWSER_ROWS) top = sel - BROWSER_ROWS + 1;
        draw_browser(sel, top);

        int key = read_key();
        switch (key) {
            case KEY_UP:
                if (sel > 0) sel--;
                break;
            case KEY_DOWN:
                if (sel < dir_count - 1) sel++;
                break;
            case KEY_ESC:
                return 0;
            case KEY_ENTER:
                if (dir_entries[sel].is_dir) {
                    if (change_dir(dir_entries[sel].name)) {
                        load_dir_entries();
                        sel = 0;
                        top = 0;
                    } else {
                        show_message("Cannot open that folder.");
                    }
                } else {
                    snprintf(out, size, "%s", dir_entries[sel].name);
                    return 1;
                }
                break;
            case 't':
            case 'T':
                if (prompt_line("Open: ", out, (int)size)) return 1;
                break;
        }
    }
}

int do_open(void) {
    char name[256];
    if (!browse_for_file(name, sizeof(name))) return 0;

    /* Binary mode keeps CR so the line ending style can be detected */
    FILE *in = fopen(name, "rb");
    if (!in) {
        show_message("File not found.");
        return 0;
    }

    // Only discard the current document once we know the new file opens
    if (!confirm_discard()) {
        fclose(in);
        return 0;
    }

    /* Read in chunks; a line longer than the chunk spans several reads */
    free_all_lines();
    undo_clear();
    char chunk[4096];
    int need_line = 1;
    int first_eol = -1;   /* -1 = no line ending seen yet, 1 = CRLF, 0 = LF */
    while (fgets(chunk, sizeof(chunk), in)) {
        int n = (int)strlen(chunk);
        int ends_line = (n > 0 && chunk[n - 1] == '\n');
        if (ends_line) {
            n--;
            if (first_eol < 0) first_eol = (n > 0 && chunk[n - 1] == '\r');
            if (n > 0 && chunk[n - 1] == '\r') n--;   /* CRLF files */
        }
        if (need_line) {
            insert_line_at(editor.line_count, "", 0);
            need_line = 0;
        }
        Line *ln = &editor.lines[editor.line_count - 1];
        line_reserve(ln, ln->len + n + 1);
        memcpy(ln->text + ln->len, chunk, n);
        ln->len += n;
        ln->text[ln->len] = '\0';
        if (ends_line) need_line = 1;
    }
    fclose(in);

    if (editor.line_count == 0) {
        insert_line_at(0, "", 0);
    }

    /* Keep saving in the style the file already uses */
    if (first_eol >= 0) options.crlf = first_eol;

    strcpy(editor.filename, name);
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.modified = 0;
    return 1;
}

/* Returns 1 if the editor should quit. */
int do_exit(void) {
    return confirm_discard();
}
