#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
    #include <conio.h>
    #include <direct.h>
    #include <windows.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <dirent.h>
#endif

#define MAX_LINES 1000
#define MAX_LINE_LENGTH 256
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LENGTH];
    int line_count;
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

Editor editor;

/* Frame buffer: all screen output is built here and written in one go,
   so the terminal never shows a half-drawn frame (fixes flicker). */
#define FRAME_SIZE 16384
static char frame_buf[FRAME_SIZE];
static int frame_len = 0;
static int needs_redraw = 1;

static void fb_reset(void) {
    frame_len = 0;
}

static void fb_write(const char *s, int n) {
    if (n > FRAME_SIZE - frame_len) n = FRAME_SIZE - frame_len;
    if (n > 0) {
        memcpy(frame_buf + frame_len, s, n);
        frame_len += n;
    }
}

static void fb_puts(const char *s) {
    fb_write(s, (int)strlen(s));
}

static void fb_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int room = FRAME_SIZE - frame_len;
    if (room > 1) {
        int n = vsnprintf(frame_buf + frame_len, room, fmt, ap);
        if (n > 0) frame_len += (n < room) ? n : room - 1;
    }
    va_end(ap);
}

static void fb_gotoxy(int x, int y) {
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
#define KEY_ALT_F 1301
#define KEY_ALT_E 1302
#define KEY_ALT_S 1303
#define KEY_ALT_O 1304

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

void init_editor() {
    editor.line_count = 1;
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.insert_mode = 1;
    editor.modified = 0;
    editor.menu_active = 0;
    editor.selected_menu = 0;
    editor.dropdown_open = 0;
    editor.selected_item = 0;
    strcpy(editor.filename, "untitled.txt");

    for (int i = 0; i < MAX_LINES; i++) {
        editor.lines[i][0] = '\0';
    }
}

void draw_status_line() {
    char buf[SCREEN_WIDTH + 1];
    memset(buf, ' ', SCREEN_WIDTH);
    buf[SCREEN_WIDTH] = '\0';

    char left[256];
    snprintf(left, sizeof(left), "F1=Help | Line:%d Col:%d %s %s",
             editor.cursor_y + 1,
             editor.cursor_x + 1,
             editor.insert_mode ? "INS" : "OVR",
             editor.modified ? "*" : "");

    int left_len = (int)strlen(left);
    if (left_len > SCREEN_WIDTH) left_len = SCREEN_WIDTH;
    memcpy(buf, left, left_len);

    int fname_only_len = (int)strlen(editor.filename);
    int fname_len_with_space = fname_only_len + 1;
    if (fname_len_with_space > SCREEN_WIDTH) fname_len_with_space = SCREEN_WIDTH;
    int start = SCREEN_WIDTH - fname_len_with_space;
    if (start < 0) start = 0;

    if (start < SCREEN_WIDTH) {
        buf[start] = ' ';
        int copy = SCREEN_WIDTH - start - 1;
        if (copy > fname_only_len) copy = fname_only_len;
        memcpy(&buf[start + 1], editor.filename, copy);
    }

    fb_gotoxy(0, SCREEN_HEIGHT - 1);
    fb_write(buf, SCREEN_WIDTH);
}

void draw_menu_bar() {
    const char *menus[] = {"File", "Edit", "Search", "Options"};
    int menu_positions[] = {0, 6, 12, 20};  // X positions of each menu

    fb_gotoxy(0, 0);

    // Draw entire menu bar with spaces
    char blank[SCREEN_WIDTH];
    memset(blank, ' ', SCREEN_WIDTH);
    fb_write(blank, SCREEN_WIDTH);

    // Draw each menu item
    for (int i = 0; i < 4; i++) {
        fb_gotoxy(menu_positions[i], 0);

        if (editor.menu_active && editor.selected_menu == i) {
            // Highlighted (inverse video): black on white
            fb_printf("\x1b[7m%s\x1b[0m", menus[i]);
        } else {
            fb_printf("%s", menus[i]);
        }
    }

    // Draw "EDIT" on the right side
    fb_gotoxy(SCREEN_WIDTH - 4, 0);
    fb_puts("EDIT");
}

/* File menu items. Index order matters: it's used by run_file_item(). */
#define FILE_ITEM_COUNT 5
static const char *file_items[FILE_ITEM_COUNT] = {"New", "Open...", "Save", "Save As...", "Exit"};
static const char *file_hints[FILE_ITEM_COUNT] = {"Ctrl+N", "Ctrl+O", "Ctrl+S", "", "Ctrl+Q"};
static const char file_mnemonics[] = "NOSAX";   // Letter shortcut for each item

void draw_dropdown() {
    if (!editor.dropdown_open || editor.selected_menu != 0) return;

    for (int i = 0; i < FILE_ITEM_COUNT; i++) {
        fb_gotoxy(0, 1 + i);

        if (i == editor.selected_item) fb_printf("\x1b[7m"); // highlight
        fb_printf(" %-10s %-7s ", file_items[i], file_hints[i]);
        if (i == editor.selected_item) fb_printf("\x1b[0m");
    }
}

void draw_editor_area() {
    char row[SCREEN_WIDTH];
    for (int screen_row = 1; screen_row < SCREEN_HEIGHT - 1; screen_row++) {
        fb_gotoxy(0, screen_row);

        int file_line = screen_row - 1 + editor.scroll_y;

        if (file_line < editor.line_count) {
            char *line = editor.lines[file_line];
            int line_len = (int)strlen(line);

            for (int col = 0; col < SCREEN_WIDTH; col++) {
                int file_col = col + editor.scroll_x;
                row[col] = (file_col < line_len) ? line[file_col] : ' ';
            }
        } else {
            memset(row, ' ', SCREEN_WIDTH);
        }
        fb_write(row, SCREEN_WIDTH);
    }
}

void refresh_screen() {
    fb_reset();
    fb_puts("\x1b[?25l");   // hide cursor while drawing

    draw_menu_bar();
    draw_editor_area();
    draw_dropdown();
    draw_status_line();

    int screen_x = editor.cursor_x - editor.scroll_x;
    int screen_y = editor.cursor_y - editor.scroll_y + 1;
    fb_gotoxy(screen_x, screen_y);
    fb_puts("\x1b[?25h");   // show cursor

    // One write for the whole frame
    fwrite(frame_buf, 1, frame_len, stdout);
    fflush(stdout);
}

void adjust_scroll() {
    if (editor.cursor_x < editor.scroll_x) {
        editor.scroll_x = editor.cursor_x;
    } else if (editor.cursor_x >= editor.scroll_x + SCREEN_WIDTH) {
        editor.scroll_x = editor.cursor_x - SCREEN_WIDTH + 1;
    }

    if (editor.cursor_y < editor.scroll_y) {
        editor.scroll_y = editor.cursor_y;
    } else if (editor.cursor_y >= editor.scroll_y + SCREEN_HEIGHT - 2) {
        editor.scroll_y = editor.cursor_y - SCREEN_HEIGHT + 3;
    }
}

void move_cursor(int dx, int dy) {
    if (dy != 0) {
        editor.cursor_y += dy;
        if (editor.cursor_y < 0) editor.cursor_y = 0;
        if (editor.cursor_y >= editor.line_count) editor.cursor_y = editor.line_count - 1;

        int line_len = strlen(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > line_len) {
            editor.cursor_x = line_len;
        }
    }

    if (dx != 0) {
        editor.cursor_x += dx;
        if (editor.cursor_x < 0) editor.cursor_x = 0;

        int line_len = strlen(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > line_len) {
            editor.cursor_x = line_len;
        }
    }

    adjust_scroll();
}

void insert_char(char ch) {
    char *line = editor.lines[editor.cursor_y];
    int line_len = strlen(line);

    if (line_len < MAX_LINE_LENGTH - 1) {
        if (editor.insert_mode) {
            memmove(&line[editor.cursor_x + 1], &line[editor.cursor_x],
                    line_len - editor.cursor_x + 1);
        }

        line[editor.cursor_x] = ch;
        if (!editor.insert_mode && editor.cursor_x == line_len) {
            line[editor.cursor_x + 1] = '\0';
        }

        editor.cursor_x++;
        editor.modified = 1;
    }
}

void delete_char() {
    char *line = editor.lines[editor.cursor_y];
    int line_len = strlen(line);

    if (editor.cursor_x < line_len) {
        memmove(&line[editor.cursor_x], &line[editor.cursor_x + 1],
                line_len - editor.cursor_x);
        editor.modified = 1;
    }
}

void backspace() {
    if (editor.cursor_x > 0) {
        editor.cursor_x--;
        delete_char();
    } else if (editor.cursor_y > 0) {
        int prev_line_len = strlen(editor.lines[editor.cursor_y - 1]);
        if (prev_line_len + strlen(editor.lines[editor.cursor_y]) < MAX_LINE_LENGTH - 1) {
            strcat(editor.lines[editor.cursor_y - 1], editor.lines[editor.cursor_y]);

            for (int i = editor.cursor_y; i < editor.line_count - 1; i++) {
                strcpy(editor.lines[i], editor.lines[i + 1]);
            }
            editor.line_count--;

            editor.cursor_y--;
            editor.cursor_x = prev_line_len;
            editor.modified = 1;
        }
    }
}

void insert_newline() {
    if (editor.line_count < MAX_LINES - 1) {
        for (int i = editor.line_count; i > editor.cursor_y; i--) {
            strcpy(editor.lines[i], editor.lines[i - 1]);
        }

        char *current_line = editor.lines[editor.cursor_y];
        strcpy(editor.lines[editor.cursor_y + 1], &current_line[editor.cursor_x]);
        current_line[editor.cursor_x] = '\0';

        editor.line_count++;
        editor.cursor_y++;
        editor.cursor_x = 0;
        editor.modified = 1;
    }
}

/* ---- Status messages and prompts (bottom row, SCREEN_HEIGHT-2) ---- */

/* Shows a message briefly; the next redraw clears it. */
static void show_message(const char *msg) {
    char line[SCREEN_WIDTH + 1];
    snprintf(line, sizeof(line), "%-79s", msg);
    gotoxy(0, SCREEN_HEIGHT - 2);
    printf("%s", line);
    fflush(stdout);
#ifdef _WIN32
    Sleep(900);
#else
    sleep(1);
#endif
}

/* Reads a line of text on the prompt row. Enter accepts, Esc cancels.
   Reads char-by-char so it works in raw mode (POSIX) and cooked mode alike.
   Returns 1 if a non-empty line was entered. */
static int prompt_line(const char *msg, char *buf, int size) {
    int len = 0;
    buf[0] = '\0';

    gotoxy(0, SCREEN_HEIGHT - 2);
    printf("%-79s", "");
    gotoxy(0, SCREEN_HEIGHT - 2);
    printf("%s", msg);
    fflush(stdout);
    set_cursor_visibility(1);

    while (1) {
        int c = getch_impl();
        if (c == EOF) continue;
        if (c == 13 || c == 10) break;
        if (c == 27) {                       // Esc: cancel
            buf[0] = '\0';
            return 0;
        }
        if (c == 8 || c == 127) {            // Backspace
            if (len > 0) {
                len--;
                buf[len] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (c >= 32 && c <= 126 && len < size - 1) {
            buf[len++] = (char)c;
            buf[len] = '\0';
            putchar(c);
            fflush(stdout);
        }
    }
    buf[len] = '\0';
    return len > 0;
}

/* ---- File operations. Each returns 1 on success, 0 if cancelled/failed. ---- */

static int write_file(const char *name) {
    FILE *file = fopen(name, "w");
    if (!file) {
        show_message("Error: could not save file.");
        return 0;
    }
    for (int i = 0; i < editor.line_count; i++) {
        fprintf(file, "%s\n", editor.lines[i]);
    }
    fclose(file);
    editor.modified = 0;

    char msg[SCREEN_WIDTH];
    snprintf(msg, sizeof(msg), "Saved %s", name);
    show_message(msg);
    return 1;
}

static int do_save_as(void);

static int do_save(void) {
    if (strlen(editor.filename) == 0 || strcmp(editor.filename, "untitled.txt") == 0) {
        return do_save_as();
    }
    return write_file(editor.filename);
}

static int do_save_as(void) {
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
        int c = getch_impl();
        if (c == 'y' || c == 'Y') return do_save();
        if (c == 'n' || c == 'N') return 1;
        if (c == 27) return 0;
    }
}

static int do_new(void) {
    if (!confirm_discard()) return 0;
    strcpy(editor.filename, "untitled.txt");
    editor.line_count = 1;
    editor.lines[0][0] = '\0';
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.modified = 0;
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
    fb_printf("\x1b[7m%-79.79s\x1b[0m", line);

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
            if (hl) fb_puts("\x1b[0m");
        } else {
            fb_printf("%-79s", "");
        }
    }

    fb_gotoxy(0, 22);
    fb_printf("\x1b[7m%-79s\x1b[0m", " Enter=open  Esc=cancel  T=type name  Up/Down=move");
    fb_gotoxy(0, 23);
    fb_printf("%-79s", "");

    fwrite(frame_buf, 1, frame_len, stdout);
    fflush(stdout);
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

static int do_open(void) {
    char name[256];
    if (!browse_for_file(name, sizeof(name))) return 0;

    FILE *in = fopen(name, "r");
    if (!in) {
        show_message("File not found.");
        return 0;
    }

    // Only discard the current document once we know the new file opens
    if (!confirm_discard()) {
        fclose(in);
        return 0;
    }

    editor.line_count = 0;
    char buffer[MAX_LINE_LENGTH];
    while (editor.line_count < MAX_LINES - 1 && fgets(buffer, sizeof(buffer), in)) {
        int n = (int)strlen(buffer);
        if (n > 0 && buffer[n - 1] == '\n') {
            buffer[n - 1] = '\0';
        }
        strcpy(editor.lines[editor.line_count], buffer);
        editor.line_count++;
    }
    fclose(in);

    if (editor.line_count == 0) {
        editor.line_count = 1;
        editor.lines[0][0] = '\0';
    }

    strcpy(editor.filename, name);
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_x = 0;
    editor.scroll_y = 0;
    editor.modified = 0;
    return 1;
}

/* Returns 1 if the editor should quit. */
static int do_exit(void) {
    return confirm_discard();
}

/* Runs a File menu item by index. Returns 1 if the editor should quit. */
static int run_file_item(int item) {
    switch (item) {
        case 0: do_new(); break;
        case 1: do_open(); break;
        case 2: do_save(); break;
        case 3: do_save_as(); break;
        case 4: return do_exit();
    }
    return 0;
}

static void show_help(void) {
    clear_screen();
    printf("EDIT Help\n\n");
    printf("Menu Access:\n");
    printf("  F10 or Alt      - Activate menu bar\n");
    printf("  Alt+F           - Open File menu\n");
    printf("  Alt+E           - Open Edit menu\n");
    printf("  Alt+S           - Open Search menu\n");
    printf("  Alt+O           - Open Options menu\n");
    printf("  Arrow Keys      - Navigate menus\n");
    printf("  Enter           - Select menu item\n");
    printf("  N/O/S/A/X       - Pick File menu item (New/Open/Save/Save As/Exit)\n");
    printf("  Open (O)        - Browse folders (Enter=open, T=type a name)\n");
    printf("  ESC             - Close menu\n\n");
    printf("Keyboard Shortcuts:\n");
    printf("  Ctrl+N          - New file\n");
    printf("  Ctrl+O          - Open file\n");
    printf("  Ctrl+S          - Save file\n");
    printf("  Ctrl+Q          - Exit\n\n");
    printf("Editing:\n");
    printf("  Arrow Keys      - Move cursor\n");
    printf("  Insert          - Toggle insert/overwrite mode\n");
    printf("  Delete          - Delete character\n");
    printf("  Backspace       - Backspace\n");
    printf("  Enter           - New line\n");
    printf("  Tab             - Insert 4 spaces\n\n");
    printf("Press any key to continue...");
    fflush(stdout);
    getch_impl();
    clear_screen();
}

int main(int argc, char *argv[]) {
    int running = 1;

    init_editor();

    if (argc > 1) {
        strcpy(editor.filename, argv[1]);
    }

#ifndef _WIN32
    enable_raw_mode();
#endif

#ifdef _WIN32
    configure_console();
#else
    set_line_wrap(0);
#endif
    clear_screen();
    set_cursor_visibility(1);

    while (running) {
        adjust_scroll();
        if (needs_redraw) {
            refresh_screen();
            needs_redraw = 0;
        }

        int key = read_key();
        if (key == -1) {
            continue;
        }
        needs_redraw = 1;

        // Handle menu activation keys
        if (key == KEY_F10 || key == KEY_ALT_F || key == KEY_ALT_E ||
            key == KEY_ALT_S || key == KEY_ALT_O) {
            editor.menu_active = 1;
            editor.dropdown_open = 0;
            editor.selected_item = 0;

            if (key == KEY_ALT_E) editor.selected_menu = 1;
            else if (key == KEY_ALT_S) editor.selected_menu = 2;
            else if (key == KEY_ALT_O) editor.selected_menu = 3;
            else {
                // Alt+F drops the File menu straight open; F10 just highlights it
                editor.selected_menu = 0;
                if (key == KEY_ALT_F) editor.dropdown_open = 1;
            }
            continue;
        }

        // Menu mode handling
        if (editor.menu_active) {
            if (!editor.dropdown_open) {
                // Menu bar navigation
                switch (key) {
                    case KEY_LEFT:
                        editor.selected_menu = (editor.selected_menu + 3) % 4;
                        break;
                    case KEY_RIGHT:
                        editor.selected_menu = (editor.selected_menu + 1) % 4;
                        break;
                    case KEY_DOWN:
                    case KEY_ENTER:
                        if (editor.selected_menu == 0) { // Only File menu implemented
                            editor.dropdown_open = 1;
                            editor.selected_item = 0;
                        }
                        break;
                    case KEY_ESC:
                        editor.menu_active = 0;
                        break;
                }
            } else {
                // Dropdown navigation (only File has items so far)
                int item_count = (editor.selected_menu == 0) ? FILE_ITEM_COUNT : 0;
                int item = -1;

                switch (key) {
                    case KEY_LEFT:
                        editor.selected_menu = (editor.selected_menu + 3) % 4;
                        editor.selected_item = 0;
                        break;
                    case KEY_RIGHT:
                        editor.selected_menu = (editor.selected_menu + 1) % 4;
                        editor.selected_item = 0;
                        break;
                    case KEY_UP:
                        if (item_count) editor.selected_item = (editor.selected_item + item_count - 1) % item_count;
                        break;
                    case KEY_DOWN:
                        if (item_count) editor.selected_item = (editor.selected_item + 1) % item_count;
                        break;
                    case KEY_ENTER:
                        if (item_count) item = editor.selected_item;
                        break;
                    case KEY_ESC:
                        editor.dropdown_open = 0;
                        break;
                    default:
                        // Letter shortcut (case-insensitive): N, O, S, A, X
                        if (item_count && key >= 32 && key <= 126) {
                            int up = (key >= 'a' && key <= 'z') ? key - 32 : key;
                            const char *m = strchr(file_mnemonics, up);
                            if (m) item = (int)(m - file_mnemonics);
                        }
                        break;
                }

                if (item >= 0) {
                    editor.menu_active = 0;
                    editor.dropdown_open = 0;
                    if (run_file_item(item)) running = 0;
                }
            }
            continue;
        }

        // Normal editing mode
        switch (key) {
            case KEY_UP: move_cursor(0, -1); break;
            case KEY_DOWN: move_cursor(0, 1); break;
            case KEY_LEFT: move_cursor(-1, 0); break;
            case KEY_RIGHT: move_cursor(1, 0); break;

            case KEY_F1: show_help(); break;

            case KEY_CTRL_N: do_new(); break;
            case KEY_CTRL_O: do_open(); break;
            case KEY_CTRL_S: do_save(); break;
            case KEY_CTRL_Q: if (do_exit()) running = 0; break;

            case KEY_INSERT: editor.insert_mode = !editor.insert_mode; break;
            case KEY_DELETE: delete_char(); break;

            case KEY_ENTER: insert_newline(); break;
            case KEY_BACKSPACE: backspace(); break;
            case KEY_TAB:
                for (int i = 0; i < 4; i++) insert_char(' ');
                break;

            default:
                if (key >= 32 && key <= 126) {
                    insert_char((char)key);
                }
                break;
        }
    }

    clear_screen();
#ifdef _WIN32
    restore_console();
#else
    set_line_wrap(1);
#endif
    set_cursor_visibility(1);
    return 0;
}
