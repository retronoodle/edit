# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a simple text editor written in C, similar to MS-DOS EDIT. It runs in the terminal with a full-screen interface and supports basic editing operations.

## Build and Run

- **Build**: `make` (produces `edit.exe` on Windows)
- **Clean**: `make clean`
- **Run**: `./edit.exe [filename]`

## Architecture

### Module Layout

- `editor.h`: shared types (`Line`, `Editor`), screen size, and the global `extern Editor editor`
- `term.c/.h`: frame buffer (`fb_*`, `fb_flush`), console/raw mode, `KEY_*` codes, `read_key()`
- `buffer.c/.h`: `Editor editor` definition, line storage, editing ops (`insert_char`, `backspace`, ...), `move_cursor`/`adjust_scroll`, `init_editor`
- `ui.c/.h`: status line, menu bar, dropdown, editor area, `refresh_screen`, `show_message`, `prompt_line`, `show_help`
- `files.c/.h`: save/save-as/new/open/exit and the folder browser
- `undo.c/.h`: undo/redo operation log (`undo_record_*` called from buffer.c, `undo_undo`/`undo_redo`, Ctrl+Z/Ctrl+Y)
- `edit.c`: `main`, main loop, key/menu dispatch

New features should go in their own module (e.g. `search.c`) rather than growing `edit.c`.

Edit functions in buffer.c (`insert_char`, `backspace`, ...) mutate text via `raw_*` helpers and then record the change for undo. Undo/redo use the `raw_*` helpers directly so they are not recorded again. Any code that changes lines outside these functions (e.g. file load) must call `undo_clear()`.

### Core Structure

The editor uses a single `Editor` struct (editor.h) that maintains:
- Growable array of `Line` structs (each holds a heap-allocated, NUL-terminated `text` plus `len`/`cap`); no fixed line or length limit
- Cursor position (cursor_x, cursor_y)
- Scroll offset (scroll_x, scroll_y)
- Insert/overwrite mode flag
- Modified flag for tracking unsaved changes

### Platform Abstraction

The code uses conditional compilation (#ifdef _WIN32) to handle platform differences:
- **Windows**: Uses conio.h for _getch(), Windows console API for cursor/screen control
- **POSIX**: Uses termios for raw mode, ANSI escape sequences for cursor/screen control

Key abstracted functions:
- `read_key()`: Normalizes keyboard input to unified KEY_* codes (term.c)
- `gotoxy()`, `clear_screen()`, `set_cursor_visibility()`: Platform-specific screen control

### Keyboard Handling

The editor normalizes all keyboard input through `read_key()` which returns unified key codes:
- Standard keys (KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, etc.)
- Function keys (KEY_F1-F10)
- Special keys (KEY_ESC, KEY_ENTER, KEY_BACKSPACE, KEY_TAB, KEY_INSERT, KEY_DELETE)

This fixes cross-platform issues like:
- Backspace sending DEL (127) vs BS (8) on different terminals
- Function keys sending escape sequences that could trigger exit

### Screen Layout

Fixed 80x25 character screen:
- Line 0: Menu bar showing "File Edit Search Options"
- Lines 1-23: Editable text area (23 visible lines)
- Line 24: Status line with function key hints, cursor position, mode (INS/OVR), modified flag (*), filename

### Main Loop

The main loop in `main()` (edit.c) reads a key and dispatches it: menu keys, then File menu items via `run_file_item()`, then editing keys. Help (F1), New, Open, Save and Exit are handled in the editing switch.

## Development Notes

- Line storage grows on demand (`line_reserve`, `lines_reserve`, `insert_line_at`, `delete_line_at` in buffer.c); `reset_document()` clears the buffer
- Build is `make` (compiles all `.c` files listed in the Makefile). Add new `.c` files to `SOURCES` and their headers to `HEADERS`
- The file browser still caps listings at MAX_DIR_ENTRIES=512
- No clipboard/copy-paste support
- Tab key inserts 4 spaces
- Files are always saved with Unix line endings (\n)
