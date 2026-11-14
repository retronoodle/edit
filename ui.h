/* Screen drawing: status line, menus, editor area, messages and help. */
#ifndef UI_H
#define UI_H

void draw_status_line(void);
void draw_menu_bar(void);
void draw_dropdown(void);
void dropdown_rect(int which, int *x, int *w);   /* column and width of a menu's dropdown */
void draw_editor_area(void);
void refresh_screen(void);
void show_message(const char *msg);
int prompt_line(const char *msg, char *buf, int size);      /* 1 only if non-empty */
int prompt_line_opt(const char *msg, char *buf, int size);  /* 1 on Enter, even if empty */
void show_help(void);

#endif
