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
- `menu.c/.h`: menu tables (`menus[]`, items with label/hint/mnemonic/action), menu-mode keys (`menu_open_key`, `menu_handle_key`), and the `ACT_*` action codes
- `options.c/.h`: `options` (tab width, line endings, color scheme), `options_cycle`, `options_label`, `options_base_sgr` (the scheme's SGR; use it instead of a plain `\x1b[0m`), `options_chrome_sgr(ChromeKind)` (bars, tabs, status line, borders, per scheme)
- `gitinfo.c/.h`: cwd (home shortened to `~`) and git branch for the status line, read from `.git` files (no git process); cached, refreshed every 2 s
- `gitmodal.c/.h`: Alt+G git panel: status of the cwd's repo, commit (stages all), push, B branch list (switch / + new branch), M merge a branch, A abort merge. Runs git as a child process (`popen`), unlike gitinfo
- `ui.c/.h`: status line, menu bar, dropdown, editor area, `refresh_screen`, `show_message`, `prompt_line`, `show_help`
- `tabs.c/.h`: open documents as tabs. The active document is the global `editor` (plus undo.c's active history); other tabs hold their own copy in a slot. `tab_switch()` moves data between them. Tab bar drawing and hit-testing live here too (`tab_bar_range`, `tab_bar_hit`)
- `files.c/.h`: save/save-as/new tab/open (`open_path`)/close tab/exit (asks about each modified tab), the folder browser (`browse`, shared by Open and Save As) and the recent files list (`do_open_recent`)
- `recent.c/.h`: last 10 files opened or saved (full paths), kept in `~/.edit_recent` (`USERPROFILE` or `HOME`)
- `syntax.c/.h`: keyword/comment/string coloring. One scanner per language (chosen by file extension), each line scanned with the parser state carried from the line above. `draw_editor_area()` (ui.c) rescans from the top of the file each redraw to get that state. Options > Syntax turns it off. Unit tests in `test_syntax.c` (`make test`)
- `clipboard.c/.h`: selection (Shift+arrows, Ctrl+A select all), internal clipboard, cut/copy/paste (Ctrl+X/C/V)
- `undo.c/.h`: undo/redo operation log (`undo_record_*` called from buffer.c, `undo_undo`/`undo_redo`, Ctrl+Z/Ctrl+Y)
- `search.c/.h`: Find, Find Next, Replace, Go to Line (Ctrl+F, F3, Ctrl+R, Ctrl+G; Search menu)
- `utf8.c/.h`: UTF-8 decode/encode, character boundaries, display width (`cp_width`, `utf8_col`, `utf8_byte_at_col`). `test_utf8.c` holds its unit tests (`make test`)
- `recovery.c/.h`: crash recovery. `recovery_tick()` (main loop) writes `<file>.recover` for every modified tab every 30 s; `recovery_discard_name()` runs after save, on discard and on tab close; `recovery_check_on_startup()` offers to restore the active tab
- `mouse.c/.h`: mouse clicks, drag selection, wheel scrolling and menu-bar clicks (`mouse_handle`). term.c reads the events (`KEY_MOUSE`, `term_get_mouse`); `read_key()` skips them, so only the main loop sees mouse input
- `edit.c`: `main`, main loop, `run_action()` (runs a menu action or key command)

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

Screen size comes from the console and follows window resizes (`term_update_size()` sets `screen_w`/`screen_h`; 80x25 is the fallback, 40x10 the minimum). Rows are relative to `screen_h`:
- Line 0: Menu bar showing "File Edit Search Options" (all four are working dropdowns)
- Line 1 (`TAB_ROW`): Tab bar, one label per open file; the active tab is reverse video, `*` marks unsaved changes
- Lines `TEXT_TOP` (2) to screen_h-3: Editable text area (`TEXT_ROWS` rows; use these constants, not literal row numbers)
- Line screen_h-2: Message and prompt row (blank when drawing; messages are printed over it)
- Line screen_h-1: Status line with function key hints, cursor position, mode (INS/OVR), modified flag (*), filename

### Main Loop

The main loop in `main()` (edit.c) reads a key. Menu activation and menu-mode keys go to `menu.c`, which returns an `ACT_*` code; `run_action()` in edit.c runs it. Otherwise the key goes to the editing switch. Menu items and key shortcuts share the same functions.

## Development Notes

- Line storage grows on demand (`line_reserve`, `lines_reserve`, `insert_line_at`, `delete_line_at` in buffer.c); `reset_document()` clears the buffer
- Build is `make` (compiles all `.c` files listed in the Makefile). Add new `.c` files to `SOURCES` and their headers to `HEADERS`
- The file browser still caps listings at MAX_DIR_ENTRIES=512
- Clipboard is internal only (no system clipboard yet)
- Tab key inserts `options.tab_width` spaces (2, 4 or 8; default 4)
- Files are saved with LF by default. Opening a file with CRLF switches saving to CRLF; the Options menu can change it. Files are written and read in binary mode, so the line ending is controlled by `options.crlf`
- Options are not saved between runs yet
- The main loop uses `read_key_wait(ms)` (1 s timeout) so backups run while idle. Only the main loop should use it; prompts and browsers use `read_key()`, which blocks
- `files.c:load_stream()` reads a stream into the buffer. Recovery uses it, so keep it free of filename or modified-flag changes
- Per-document state (lines, cursor, filename, modified, selection, undo history, CRLF) must move with its tab. Add new per-document fields to `Editor` or `undo.c`'s `UndoState`, and check `tabs.c` (`stash_active`/`load_active`) still carries them. Global state stays global (clipboard, search text, options)
- The tab limit is `MAX_TABS` (16). Tabs are closed with Ctrl+W; F6/F7 move between them
