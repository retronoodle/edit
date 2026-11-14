/* Screen drawing: status line, menus, editor area, messages and help. */
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include "editor.h"
#include "term.h"
#include "clipboard.h"
#include "ui.h"

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
static const char *file_items[FILE_ITEM_COUNT] = {"New", "Open...", "Save", "Save As...", "Exit"};
static const char *file_hints[FILE_ITEM_COUNT] = {"Ctrl+N", "Ctrl+O", "Ctrl+S", "", "Ctrl+Q"};
const char file_mnemonics[] = "NOSAX";   // Letter shortcut for each item

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
    int has_sel, sy1 = 0, sx1 = 0, sy2 = 0, sx2 = 0;
    has_sel = sel_range(&sy1, &sx1, &sy2, &sx2);

    for (int screen_row = 1; screen_row < SCREEN_HEIGHT - 1; screen_row++) {
        fb_gotoxy(0, screen_row);

        int file_line = screen_row - 1 + editor.scroll_y;
        char *line = NULL;
        int line_len = 0;
        if (file_line < editor.line_count) {
            line = editor.lines[file_line].text;
            line_len = editor.lines[file_line].len;
        }

        /* Selected text is drawn in reverse video. */
        int highlighted = 0;
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            int file_col = col + editor.scroll_x;
            char ch = (line && file_col < line_len) ? line[file_col] : ' ';

            int selected = 0;
            if (has_sel && line && file_col < line_len &&
                file_line >= sy1 && file_line <= sy2) {
                int from = (file_line == sy1) ? sx1 : 0;
                int to = (file_line == sy2) ? sx2 : line_len;
                selected = file_col >= from && file_col < to;
            }
            if (selected != highlighted) {
                fb_puts(selected ? "\x1b[7m" : "\x1b[0m");
                highlighted = selected;
            }
            fb_write(&ch, 1);
        }
        if (highlighted) fb_puts("\x1b[0m");
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
    fb_flush();
}

/* ---- Status messages and prompts (bottom row, SCREEN_HEIGHT-2) ---- */

/* Shows a message briefly; the next redraw clears it. */
void show_message(const char *msg) {
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
int prompt_line(const char *msg, char *buf, int size) {
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

void show_help(void) {
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
    printf("  Ctrl+Z / Ctrl+Y - Undo / Redo\n");
    printf("  Ctrl+X/C/V      - Cut / Copy / Paste\n");
    printf("  Ctrl+Q          - Exit\n\n");
    printf("Editing:\n");
    printf("  Arrow Keys      - Move cursor\n");
    printf("  Shift+Arrows    - Select text\n");
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
