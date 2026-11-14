/* Entry point and main loop: wires menus and keys to the editor modules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "buffer.h"
#include "term.h"
#include "ui.h"
#include "files.h"
#include "undo.h"
#include "clipboard.h"

static int needs_redraw = 1;

/* Types text at the cursor. Over a selection, the selection is replaced in one undo step. */
static void type_chars(const char *s, int n) {
    if (sel_begin_replace()) {
        insert_text(s, n);
        undo_end_group();
        return;
    }
    for (int i = 0; i < n; i++) insert_char(s[i]);
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
            case KEY_SHIFT_UP: sel_extend(); move_cursor(0, -1); break;
            case KEY_SHIFT_DOWN: sel_extend(); move_cursor(0, 1); break;
            case KEY_SHIFT_LEFT: sel_extend(); move_cursor(-1, 0); break;
            case KEY_SHIFT_RIGHT: sel_extend(); move_cursor(1, 0); break;

            case KEY_UP: sel_clear(); move_cursor(0, -1); break;
            case KEY_DOWN: sel_clear(); move_cursor(0, 1); break;
            case KEY_LEFT: sel_clear(); move_cursor(-1, 0); break;
            case KEY_RIGHT: sel_clear(); move_cursor(1, 0); break;

            case KEY_F1: show_help(); break;

            case KEY_CTRL_N: sel_clear(); do_new(); break;
            case KEY_CTRL_O: sel_clear(); do_open(); break;
            case KEY_CTRL_S: do_save(); break;
            case KEY_CTRL_Q: if (do_exit()) running = 0; break;
            case KEY_CTRL_Z: sel_clear(); undo_undo(); break;
            case KEY_CTRL_Y: sel_clear(); undo_redo(); break;
            case KEY_CTRL_C: clip_copy(); break;
            case KEY_CTRL_X: clip_cut(); break;
            case KEY_CTRL_V: clip_paste(); break;

            case KEY_INSERT: editor.insert_mode = !editor.insert_mode; break;

            case KEY_DELETE:
                if (sel_begin_replace()) undo_end_group();
                else delete_char();
                break;

            case KEY_ENTER: {
                int replaced = sel_begin_replace();
                insert_newline();
                if (replaced) undo_end_group();
                break;
            }
            case KEY_BACKSPACE:
                if (sel_begin_replace()) undo_end_group();
                else backspace();
                break;
            case KEY_TAB:
                type_chars("    ", 4);
                break;

            default:
                if (key >= 32 && key <= 126) {
                    char c = (char)key;
                    type_chars(&c, 1);
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
    undo_clear();
    free_all_lines();
    return 0;
}
