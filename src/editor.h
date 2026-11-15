/* Shared editor types and the single global editor state. */
#ifndef EDITOR_H
#define EDITOR_H

/* Console size in cells. Read at startup and on resize (term_update_size). */
#define SCREEN_DEFAULT_W 80
#define SCREEN_DEFAULT_H 25
#define SCREEN_MIN_W 40
#define SCREEN_MIN_H 10
extern int screen_w;
extern int screen_h;

/* Rows: 0 menu bar, TAB_ROW tab bar, text from TEXT_TOP down to screen_h-3,
   screen_h-2 message/prompt, screen_h-1 status line. */
#define TAB_ROW 1
#define TEXT_TOP 2
#define TEXT_ROWS (screen_h - TEXT_TOP - 2)   /* visible text rows */

typedef struct {
    char *text;     /* always NUL-terminated */
    int len;        /* strlen(text), kept in sync */
    int cap;        /* bytes allocated for text, including the NUL */
} Line;

typedef struct {
    Line *lines;
    int line_count;
    int line_cap;   /* allocated slots in lines[] */
    int cursor_x, cursor_y;
    int scroll_x, scroll_y;
    int insert_mode;
    char filename[256];
    int modified;
    int menu_active;
    int selected_menu;     // 0=File, 1=Edit, 2=Search, 3=Options
    int dropdown_open;
    int selected_item;
    int sel_active;         /* selection anchor is (sel_x, sel_y); the cursor is the other end */
    int sel_x, sel_y;
    long disk_mtime;        /* file's mtime and size when we last loaded or saved it (watch.c) */
    long disk_size;
} Editor;

/* The document being edited. Defined in buffer.c. */
extern Editor editor;

#endif
