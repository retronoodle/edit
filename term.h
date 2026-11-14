/* Terminal layer: screen output, console/raw mode and key input. */
#ifndef TERM_H
#define TERM_H

/* Frame buffer: build a frame with fb_*, then send it with fb_flush(). */
void fb_reset(void);
void fb_write(const char *s, int n);
void fb_puts(const char *s);
void fb_printf(const char *fmt, ...);
void fb_gotoxy(int x, int y);
void fb_flush(void);

void gotoxy(int x, int y);
void clear_screen(void);
void set_cursor_visibility(int visible);
int kbhit(void);
int getch_impl(void);

#ifdef _WIN32
void configure_console(void);
void restore_console(void);
#else
void enable_raw_mode(void);
void set_line_wrap(int enable);
#endif

#define KEY_ESC 1000
#define KEY_ENTER 1001
#define KEY_BACKSPACE 1002
#define KEY_TAB 1003
#define KEY_UP 1010
#define KEY_DOWN 1011
#define KEY_LEFT 1012
#define KEY_RIGHT 1013
#define KEY_INSERT 1020
#define KEY_DELETE 1021
#define KEY_F1 1101
#define KEY_F2 1102
#define KEY_F3 1103
#define KEY_F4 1104
#define KEY_F5 1105
#define KEY_F10 1110
#define KEY_CTRL_N 1201
#define KEY_CTRL_O 1202
#define KEY_CTRL_S 1203
#define KEY_CTRL_Q 1204
#define KEY_CTRL_Y 1205
#define KEY_CTRL_Z 1206
#define KEY_ALT_F 1301
#define KEY_ALT_E 1302
#define KEY_ALT_S 1303
#define KEY_ALT_O 1304

int read_key(void);

#endif
