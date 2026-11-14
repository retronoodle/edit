/* Terminal layer: frame buffer, console/raw mode and key input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <termios.h>
    #include <unistd.h>
#endif

#include "term.h"

/* Frame buffer: all screen output is built here and written in one go,
   so the terminal never shows a half-drawn frame (fixes flicker). */
#define FRAME_SIZE 16384
static char frame_buf[FRAME_SIZE];
static int frame_len = 0;

void fb_reset(void) {
    frame_len = 0;
}

void fb_write(const char *s, int n) {
    if (n > FRAME_SIZE - frame_len) n = FRAME_SIZE - frame_len;
    if (n > 0) {
        memcpy(frame_buf + frame_len, s, n);
        frame_len += n;
    }
}

void fb_puts(const char *s) {
    fb_write(s, (int)strlen(s));
}

void fb_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int room = FRAME_SIZE - frame_len;
    if (room > 1) {
        int n = vsnprintf(frame_buf + frame_len, room, fmt, ap);
        if (n > 0) frame_len += (n < room) ? n : room - 1;
    }
    va_end(ap);
}

void fb_gotoxy(int x, int y) {
    fb_printf("\x1b[%d;%dH", y + 1, x + 1);
}

#ifdef _WIN32
void gotoxy(int x, int y) {
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void clear_screen() {
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

void set_cursor_visibility(int visible) {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = visible;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
}

int kbhit() {
    return _kbhit();
}

int getch_impl() {
    return _getch();
}

static DWORD g_orig_out_mode = 0;
void configure_console() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(hOut, &g_orig_out_mode)) return;
    DWORD mode = g_orig_out_mode;
    mode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) {
        /* Older consoles lack VT support; fall back to the original mode */
        SetConsoleMode(hOut, g_orig_out_mode & ~ENABLE_WRAP_AT_EOL_OUTPUT);
    }
}

void restore_console() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    if (g_orig_out_mode) {
        SetConsoleMode(hOut, g_orig_out_mode);
    }
}

#else
struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= (CS8);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void gotoxy(int x, int y) {
    printf("\x1b[%d;%dH", y + 1, x + 1);
}

void clear_screen() {
    printf("\x1b[2J");
    printf("\x1b[H");
}

void set_cursor_visibility(int visible) {
    if (visible) {
        printf("\x1b[?25h");
    } else {
        printf("\x1b[?25l");
    }
}

void set_line_wrap(int enable) {
    if (enable) {
        printf("\x1b[?7h");
    } else {
        printf("\x1b[?7l");
    }
}

int kbhit() {
    int ch = getchar();
    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

int getch_impl() {
    int ch = getchar();
    return ch;
}
#endif

/* Unified key codes and key reader to normalize Windows and POSIX input.
   Fixes:
   - Backspace handling on terminals that send 127 (DEL) instead of 8.
   - Function keys on POSIX (which begin with ESC) no longer trigger immediate exit.
*/

#ifdef _WIN32
int read_key() {
    int ch = getch_impl();
    if (ch == 0 || ch == 224) {
        int ch2 = getch_impl();
        switch (ch2) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            case 59: return KEY_F1;
            case 60: return KEY_F2;
            case 61: return KEY_F3;
            case 62: return KEY_F4;
            case 63: return KEY_F5;
            case 68: return KEY_F10;
            case 82: return KEY_INSERT;
            case 83: return KEY_DELETE;
            case 33: return KEY_ALT_F;  // Alt+F
            case 18: return KEY_ALT_E;  // Alt+E
            case 31: return KEY_ALT_S;  // Alt+S
            case 24: return KEY_ALT_O;  // Alt+O
            default: return -1;
        }
    } else {
        switch (ch) {
            case 27:
                // Some Windows terminals send Alt+letter as ESC followed by the letter
                if (kbhit()) {
                    int ch3 = getch_impl();
                    switch (ch3) {
                        case 'f': case 'F': return KEY_ALT_F;
                        case 'e': case 'E': return KEY_ALT_E;
                        case 's': case 'S': return KEY_ALT_S;
                        case 'o': case 'O': return KEY_ALT_O;
                        default: _ungetch(ch3); return KEY_ESC;
                    }
                }
                return KEY_ESC;
            case 13: return KEY_ENTER;
            case 8:
            case 127: return KEY_BACKSPACE;
            case 9: return KEY_TAB;
            case 14: return KEY_CTRL_N;
            case 15: return KEY_CTRL_O;
            case 17: return KEY_CTRL_Q;
            case 19: return KEY_CTRL_S;
            case 25: return KEY_CTRL_Y;
            case 26: return KEY_CTRL_Z;
            default: return ch;
        }
    }
}
#else
int read_key() {
    int ch = getch_impl(); // getchar() in POSIX branch
    if (ch == EOF) return -1;

    if (ch == 127 || ch == 8) return KEY_BACKSPACE; // DEL or BS
    if (ch == 9) return KEY_TAB;
    if (ch == 13 || ch == 10) return KEY_ENTER;
    if (ch == 14) return KEY_CTRL_N;
    if (ch == 15) return KEY_CTRL_O;
    if (ch == 17) return KEY_CTRL_Q;
    if (ch == 19) return KEY_CTRL_S;
    if (ch == 25) return KEY_CTRL_Y;
    if (ch == 26) return KEY_CTRL_Z;

    if (ch != 27) { // Not an escape sequence
        return ch;
    }

    // Begin parsing escape sequences
    int a = getch_impl();
    if (a == EOF) return KEY_ESC; // Bare ESC

    if (a == '[') {
        int b = getch_impl();
        if (b == EOF) return KEY_ESC;

        if (b >= '0' && b <= '9') {
            // CSI with digits, possibly ending with '~'
            char digits[4];
            int idx = 0;
            digits[idx++] = (char)b;
            int c;
            while (idx < 3) {
                c = getch_impl();
                if (c == EOF) break;
                if (c >= '0' && c <= '9') {
                    digits[idx++] = (char)c;
                } else {
                    if (c == '~') {
                        digits[idx] = '\0';
                        int num = atoi(digits);
                        switch (num) {
                            case 2:  return KEY_INSERT; // ESC[2~
                            case 3:  return KEY_DELETE; // ESC[3~
                            case 11: return KEY_F1;     // ESC[11~
                            case 12: return KEY_F2;     // ESC[12~
                            case 13: return KEY_F3;     // ESC[13~
                            case 14: return KEY_F4;     // ESC[14~
                            case 15: return KEY_F5;     // ESC[15~
                            case 21: return KEY_F10;    // ESC[21~
                            default: return -1;
                        }
                    }
                    return -1;
                }
            }
            return -1;
        } else {
            // Single-letter CSI
            switch (b) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                default:  return -1;
            }
        }
    } else if (a == 'O') {
        // SS3 sequences (F1-F4 commonly)
        int b = getch_impl();
        switch (b) {
            case 'P': return KEY_F1; // ESCOP
            case 'Q': return KEY_F2; // ESCOQ
            case 'R': return KEY_F3; // ESCOR
            case 'S': return KEY_F4; // ESCOS
            default:  return -1;
        }
    } else {
        // Alt+letter combinations (ESC followed by letter)
        switch (a) {
            case 'f':
            case 'F': return KEY_ALT_F;
            case 'e':
            case 'E': return KEY_ALT_E;
            case 's':
            case 'S': return KEY_ALT_S;
            case 'o':
            case 'O': return KEY_ALT_O;
            default: return KEY_ESC; // Treat as ESC if not recognized
        }
    }

    return -1;
}
#endif

/* Sends the whole frame to the terminal in one write. */
void fb_flush(void) {
    fwrite(frame_buf, 1, frame_len, stdout);
    fflush(stdout);
}
