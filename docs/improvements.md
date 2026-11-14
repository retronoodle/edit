# Future Improvements

What the editor needs to move from a DOS-style toy to something that feels current, in rough priority order.

## Priority Order (Core Functionality)

### 1. ~~Remove hard limits~~ (done)
- Line array and per-line buffers now grow on demand. `MAX_LINES` and `MAX_LINE_LENGTH` are removed.

### 2. ~~Undo/redo~~ (done)
- Ctrl+Z undoes, Ctrl+Y redoes. Typing and deleting runs merge into one step; cursor moves end a step. Lives in `undo.c`.

### 3. ~~Clipboard~~ (done, internal only)
- Shift+arrows select (shown in reverse video). Ctrl+X/C/V cut/copy/paste; typing or Delete replaces a selection. Each cut, paste or replace is one undo step. Lives in `clipboard.c`.
- Still open: system clipboard. Windows Terminal may take Ctrl+C/Ctrl+V for itself; if so, its paste arrives as typed text.

### 4. ~~Search and replace~~ (done)
- Find (Ctrl+F, case-insensitive), Find Next (F3, wraps), Replace (Ctrl+R, Y/N/A/Esc, from cursor to end, one undo step), Go to Line (Ctrl+G). All in the Search menu too. Lives in `search.c`.
- Ctrl+H was not used for Replace: terminals send it as Backspace.

### 5. ~~UTF-8 support~~ (done)
- Text is stored as UTF-8. Typing, paste, load and save handle non-ASCII; the cursor steps by whole characters; wide characters (CJK, emoji) take two columns. Lives in `utf8.c`.
- Limits: no grapheme clustering (ZWJ emoji and flags split apart), no right-to-left, search case-folding is ASCII only, and tabs or control characters in a file show as `?`.
- Non-ASCII file names (command line, folder browser) still go through the ANSI code page on Windows.

### 6. ~~Interactive menu bar~~ (done)
- All four menus open and run their items. Menu tables and menu keys live in `menu.c`; Options settings in `options.c`.
- File, Edit, Search: every item works (Edit adds Select All, Ctrl+A).
- Options: tab width (2/4/8), line endings (LF/CRLF), color scheme (Default/Blue/Light). Toggles update in place and the menu stays open.
- Still open: word wrap (needs the feature first). Options are not saved between runs (see Config file).

### 7. Line wrapping or horizontal scrolling
- Currently limited to the visible width.

### 8. ~~Resize-aware layout~~ (done)
- The size is read at startup and on window resize (`term_update_size` in `term.c`). Drawing, scrolling, prompts and the file browser all use it. Minimum 40x10; smaller windows are laid out as 40x10. The frame buffer grows to fit.

### 9. ~~Auto-save and crash recovery~~ (done)
- While there are unsaved changes, the document is copied to `<file>.recover` every 30 seconds. Save or discarding changes removes it. If the editor dies, the next start asks to recover (Y restores, N deletes). Lives in `recovery.c`; the main loop wakes every second so backups run while idle.
- Limits: backups are not made while a prompt, menu or the file browser is open. Closing the console window or logging off does not write a final backup. Untitled documents back up to `untitled.txt.recover` in the working folder.

## Usability

- ~~**Mouse support**~~ (done): click places the cursor, drag selects, Shift+click extends, wheel scrolls 3 rows, and clicks open and pick menu items. Lives in `mouse.c`; terminal reporting in `term.c`.
  - Limits: left button only, no double-click word select. Windows console QuickEdit is turned off while the editor runs (restored on exit), so console text can't be selected by mouse.
- ~~**Line numbers**~~ (done): Options > Line numbers shows dim numbers in a left gutter. The gutter is at least 3 digits and widens as the file grows. Lives in `ui.c` (`gutter_w`, `text_w`).
- ~~**Multiple file support**~~ (done): tabs. Each open file is a tab on row 1. Command-line files each get a tab. New tab (Ctrl+N), Open (Ctrl+O; a file already open just gets its tab selected), Close Tab (Ctrl+W), next/previous tab (F6/F7 or click the tab). Undo, cursor, selection and line endings are per tab. Exit asks about each modified tab. Lives in `tabs.c`.
  - Limits: 16 tabs at most; the tab bar scrolls to keep the active tab visible but shows no overflow marker; crash-recovery restore at startup only offers the first tab; a tab label is cut to 20 characters; two tabs can end up on the same file if you Save As onto a name open in another tab.
- **Better file operations**: recent files, file browser, save-as dialog.
- **Syntax highlighting**: at least basic keyword and comment coloring.
- **Statusline** current dir, current branch (if any) line and col # 


## Nice to Have (Polish)

- **Config file**: save preferences (tab size, colors, keybindings). Tabs are hard-coded to 4 spaces today.
- **Auto-indent**: match the indentation of the previous line.
- **Selection highlighting**: visual block selection.
- **Word wrap mode**: optional soft wrapping.

## Current Constraints

- Files are saved with LF by default. A CRLF file keeps CRLF on save. Changed via Options > Line endings.
- The clipboard is internal only.
- Undo history has no depth limit and the modified flag is not save-point aware (undo after a save still shows `*`).
