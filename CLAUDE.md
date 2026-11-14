# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a simple text editor written in C, similar to MS-DOS EDIT. It runs in the terminal with a full-screen interface and supports basic editing operations.

## Build and Run

- **Build**: `make` (produces `edit.exe` on Windows)
- **Clean**: `make clean`
- **Run**: `./edit.exe [filename]`

## Architecture

### Core Structure

The editor uses a single `Editor` struct (edit.c:19-27) that maintains:
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
- `read_key()`: Normalizes keyboard input to unified KEY_* codes (edit.c:143-260)
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

The main loop (edit.c:494-621) uses goto labels for major operations:
- `help`: Display help screen
- `save`: Save current file
- `open`: Prompt for and load a file
- `exit`: Exit with save prompt if modified

## Development Notes

- Line storage grows on demand (`line_reserve`, `lines_reserve`, `insert_line_at`, `delete_line_at`); `reset_document()` clears the buffer
- The file browser still caps listings at MAX_DIR_ENTRIES=512
- No undo/redo functionality
- No clipboard/copy-paste support
- Tab key inserts 4 spaces
- Files are always saved with Unix line endings (\n)
