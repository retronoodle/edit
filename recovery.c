/* Crash recovery: while there are unsaved changes, the document is copied to
   "<file>.recover" every BACKUP_INTERVAL seconds. A saved or discarded document
   removes the copy. If the editor dies, the next start offers to restore it. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "options.h"
#include "files.h"
#include "recovery.h"

#define BACKUP_INTERVAL 30      /* seconds between backups while changes are unsaved */
#define BACKUP_SUFFIX ".recover"
#define PATH_BUF 320            /* room for a 256-byte file name plus the suffixes */

static time_t last_backup = 0;

/* Fills path with the backup name and tmp with the name used while writing it.
   Returns 0 if the names would not fit. */
static int backup_paths(char *path, char *tmp) {
    int n = snprintf(path, PATH_BUF, "%s%s", editor.filename, BACKUP_SUFFIX);
    if (n <= 0 || n >= PATH_BUF) return 0;
    n = snprintf(tmp, PATH_BUF, "%s.tmp", path);
    return n > 0 && n < PATH_BUF;
}

/* Writes every line with the current line ending. Changes nothing in the editor. */
static void write_backup(void) {
    char path[PATH_BUF], tmp[PATH_BUF];
    if (!backup_paths(path, tmp)) return;

    FILE *file = fopen(tmp, "wb");   /* binary: the line ending comes from options */
    if (!file) return;
    const char *eol = options.crlf ? "\r\n" : "\n";
    for (int i = 0; i < editor.line_count; i++) {
        fwrite(editor.lines[i].text, 1, editor.lines[i].len, file);
        fputs(eol, file);
    }
    if (fclose(file) != 0) {
        remove(tmp);
        return;
    }
    /* Windows rename() does not replace an existing file, so remove the old copy first */
    remove(path);
    rename(tmp, path);
}

void recovery_tick(void) {
    if (!editor.modified) return;
    time_t now = time(NULL);
    if (last_backup != 0 && now - last_backup < BACKUP_INTERVAL) return;
    last_backup = now;
    write_backup();
}

void recovery_discard(void) {
    char path[PATH_BUF], tmp[PATH_BUF];
    if (!backup_paths(path, tmp)) return;
    remove(path);
    remove(tmp);
}

/* Offers to restore a backup left by an earlier run that did not exit cleanly.
   Y restores it (the document stays modified); N deletes it. There is no
   Esc: keeping the backup while typing new text would overwrite it anyway. */
void recovery_check_on_startup(void) {
    char path[PATH_BUF], tmp[PATH_BUF];
    if (!backup_paths(path, tmp)) return;

    FILE *in = fopen(path, "rb");
    if (!in) in = fopen(tmp, "rb");   /* a crash mid-write leaves only the .tmp copy */
    if (!in) return;

    gotoxy(0, screen_h - 2);
    printf("%-*s", screen_w - 1, "Unsaved changes found. Recover them? (Y)es / (N)o");
    fflush(stdout);

    while (1) {
        int c = getch_cp();
        if (c == 'y' || c == 'Y') {
            load_stream(in);
            fclose(in);
            editor.modified = 1;
            return;
        }
        if (c == 'n' || c == 'N') {
            fclose(in);
            recovery_discard();
            return;
        }
        if (c == -1) {          /* input closed: decide nothing, keep the backup */
            fclose(in);
            return;
        }
    }
}
