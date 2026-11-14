# Future Improvements

What the editor needs to move from a DOS-style toy to something that feels current, in rough priority order.

## Priority Order (Core Functionality)

### 1. ~~Remove hard limits~~ (done)
- Line array and per-line buffers now grow on demand. `MAX_LINES` and `MAX_LINE_LENGTH` are removed.

### 2. ~~Undo/redo~~ (done)
- Ctrl+Z undoes, Ctrl+Y redoes. Typing and deleting runs merge into one step; cursor moves end a step. Lives in `undo.c`.

### 3. Clipboard
- Selection via Shift+arrows.
- Cut/Copy/Paste on Ctrl+X/C/V. Internal clipboard first, system clipboard later.

### 4. Search and replace
- The Search menu is displayed but has no implementation.
- Find (Ctrl+F), Find Next (F3), Replace (Ctrl+H), Go to Line.

### 5. UTF-8 support
- Currently ASCII only. Non-ASCII input is mangled and can corrupt a line.
- Store text as UTF-8 and handle multi-byte cursor movement and display width.
- Probably the single biggest step toward "not the stone age."

### 6. Interactive menu bar
- Only File has a working dropdown. Edit, Search and Options are displayed but do nothing.
- File: New, Open, Save, Save As, Exit
- Edit: Undo, Redo, Cut, Copy, Paste, Select All
- Search: Find, Find Next, Replace, Go to Line
- Options: tab width, word wrap, line numbers, line endings, color scheme

### 7. Line wrapping or horizontal scrolling
- Currently limited to the visible width.

### 8. Resize-aware layout
- Screen is fixed at 80x25. Read the real console size and adjust on resize.

### 9. Auto-save and crash recovery
- Write a periodic backup file so unsaved work survives a crash.

## Usability

- **Mouse support**: click to place the cursor, drag to select, scroll wheel.
- **Line numbers**: optional display in the left margin.
- **Multiple file support**: tabs or buffers for editing several files.
- **Better file operations**: recent files, file browser, save-as dialog.
- **Syntax highlighting**: at least basic keyword and comment coloring.

## Nice to Have (Polish)

- **Config file**: save preferences (tab size, colors, keybindings). Tabs are hard-coded to 4 spaces today.
- **Auto-indent**: match the indentation of the previous line.
- **Selection highlighting**: visual block selection.
- **Word wrap mode**: optional soft wrapping.

## Current Constraints

- Files are always saved with Unix line endings (`\n`).
- No clipboard or search exists yet.
- Undo history has no depth limit and the modified flag is not save-point aware (undo after a save still shows `*`).
