/* Shared editor types and the single global editor state. */
#ifndef EDITOR_H
#define EDITOR_H

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

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
} Editor;

/* The document being edited. Defined in buffer.c. */
extern Editor editor;

#endif
