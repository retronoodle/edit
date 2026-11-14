# Future Improvements

This document outlines potential improvements to make the editor more functional.

## High Priority (Core Functionality)
1. **Clipboard support** - Copy/cut/paste is essential for any real editor
2. **Undo/redo** - Critical for practical editing
3. **Find/Replace** - The Search menu exists but has no implementation
4. **File menu operations** - The File/Edit/Search/Options menu bar is displayed but not interactive
5. **Line wrapping or horizontal scrolling** - Currently limited to visible width
6. **Remove hard limits** - Dynamic line storage instead of MAX_LINES=1000

## Medium Priority (Usability)
7. **Mouse support** - For clicking to position cursor
8. **Syntax highlighting** - At least basic keyword/comment coloring
9. **Line numbers** - Optional display in left margin
10. **Multiple file support** - Tabs or buffers for editing multiple files
11. **Better file operations** - Recent files, file browser, save-as dialog

## Nice to Have (Polish)
12. **Config file** - Save preferences (tab size, colors, keybindings)
13. **Auto-indent** - Match indentation of previous line
14. **Selection/highlighting** - Visual block selection
15. **Word wrap mode** - Optional soft wrapping
16. **UTF-8 support** - Currently ASCII only

## Current Menu Bar Issue

The menu bar at the top (File Edit Search Options) is currently just static text. It should be made interactive to allow:
- File: New, Open, Save, Save As, Exit
- Edit: Cut, Copy, Paste, Undo, Redo
- Search: Find, Replace, Go to Line
- Options: Tab size, word wrap, line numbers, etc.
