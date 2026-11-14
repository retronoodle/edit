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
int getch_cp(void);     /* next typed character as a Unicode code point, 0 for non-text keys, -1 on EOF */

#ifdef _WIN32
void configure_console(void);
void restore_console(void);
#else
void enable_raw_mode(void);
void set_line_wrap(int enable);
#endif

/* Key codes live above the Unicode range so that any code point < KEY_BASE
   is a plain character. */
#define KEY_BASE 0x110000
#define KEY_ESC (KEY_BASE + 1000)
#define KEY_ENTER (KEY_BASE + 1001)
#define KEY_BACKSPACE (KEY_BASE + 1002)
#define KEY_TAB (KEY_BASE + 1003)
#define KEY_UP (KEY_BASE + 1010)
#define KEY_DOWN (KEY_BASE + 1011)
#define KEY_LEFT (KEY_BASE + 1012)
#define KEY_RIGHT (KEY_BASE + 1013)
#define KEY_SHIFT_UP (KEY_BASE + 1014)
#define KEY_SHIFT_DOWN (KEY_BASE + 1015)
#define KEY_SHIFT_LEFT (KEY_BASE + 1016)
#define KEY_SHIFT_RIGHT (KEY_BASE + 1017)
#define KEY_INSERT (KEY_BASE + 1020)
#define KEY_DELETE (KEY_BASE + 1021)
#define KEY_F1 (KEY_BASE + 1101)
#define KEY_F2 (KEY_BASE + 1102)
#define KEY_F3 (KEY_BASE + 1103)
#define KEY_F4 (KEY_BASE + 1104)
#define KEY_F5 (KEY_BASE + 1105)
#define KEY_F6 (KEY_BASE + 1106)
#define KEY_F7 (KEY_BASE + 1107)
#define KEY_F10 (KEY_BASE + 1110)
#define KEY_CTRL_N (KEY_BASE + 1201)
#define KEY_CTRL_O (KEY_BASE + 1202)
#define KEY_CTRL_S (KEY_BASE + 1203)
#define KEY_CTRL_Q (KEY_BASE + 1204)
#define KEY_CTRL_Y (KEY_BASE + 1205)
#define KEY_CTRL_Z (KEY_BASE + 1206)
#define KEY_CTRL_C (KEY_BASE + 1207)
#define KEY_CTRL_V (KEY_BASE + 1208)
#define KEY_CTRL_X (KEY_BASE + 1209)
#define KEY_CTRL_F (KEY_BASE + 1210)
#define KEY_CTRL_R (KEY_BASE + 1211)
#define KEY_CTRL_G (KEY_BASE + 1212)
#define KEY_CTRL_A (KEY_BASE + 1213)
#define KEY_CTRL_W (KEY_BASE + 1214)
#define KEY_CTRL_E (KEY_BASE + 1215)
#define KEY_ALT_F (KEY_BASE + 1301)
#define KEY_ALT_E (KEY_BASE + 1302)
#define KEY_ALT_S (KEY_BASE + 1303)
#define KEY_ALT_O (KEY_BASE + 1304)
#define KEY_RESIZE (KEY_BASE + 1400)   /* the console window changed size */
#define KEY_MOUSE (KEY_BASE + 1401)    /* a mouse event; read it with term_get_mouse() */

/* Mouse events. x/y are 0-based screen cells. Only the left button, plus the wheel. */
typedef enum {
    MEV_PRESS, MEV_DRAG, MEV_RELEASE, MEV_WHEEL_UP, MEV_WHEEL_DOWN
} MouseKind;

typedef struct {
    int x, y;
    MouseKind kind;
    int shift;      /* Shift held */
} MouseEvent;

int read_key(void);
int read_key_wait(int ms);             /* waits up to ms (-1 = forever); returns -1 if no key came in time */
void term_get_mouse(MouseEvent *ev);   /* the event behind the last KEY_MOUSE */
void term_update_size(void);           /* sets screen_w/screen_h from the console */

#endif
