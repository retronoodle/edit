/* Tabs: see tabs.h. Invariant: the slot of the active tab is empty (zeroed);
   every other slot owns its document lines and history. */
#ifndef _WIN32
    #define _XOPEN_SOURCE 700   /* realpath */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include <windows.h>
#endif

#include "editor.h"
#include "buffer.h"
#include "undo.h"
#include "options.h"
#include "utf8.h"
#include "tabs.h"

#define TAB_NAME_MAX 20   /* cells of file name shown in the tab bar */

typedef struct {
    Editor ed;        /* document; zeroed while this tab is active */
    UndoState undo;   /* history; empty while this tab is active */
    int crlf;         /* line ending style, kept while inactive */
    int named;        /* 1 once the tab has a real file name */
} Tab;

static Tab tabs[MAX_TABS];
static int tab_n = 0;
static int cur = 0;
static int next_untitled = 1;
static int bar_first = 0;   /* first tab shown in the tab bar */

static void free_doc(Editor *d) {
    for (int i = 0; i < d->line_count; i++) free(d->lines[i].text);
    free(d->lines);
    d->lines = NULL;
    d->line_count = 0;
    d->line_cap = 0;
}

static void untitled_name(char *buf, size_t size) {
    if (next_untitled == 1) snprintf(buf, size, "untitled.txt");
    else snprintf(buf, size, "untitled%d.txt", next_untitled);
    next_untitled++;
}

/* Moves the active document and history into its slot (which then owns them). */
static void stash_active(void) {
    Tab *t = &tabs[cur];
    t->ed = editor;
    undo_take(&t->undo);
    t->crlf = options.crlf;
}

/* Makes tab i the active one. The active slot must already be stashed or empty.
   The menu fields are UI state, not document state, so they are kept. */
static void load_active(int i) {
    Tab *t = &tabs[i];
    int menu_active = editor.menu_active;
    int selected_menu = editor.selected_menu;
    int dropdown_open = editor.dropdown_open;
    int selected_item = editor.selected_item;

    editor = t->ed;
    editor.menu_active = menu_active;
    editor.selected_menu = selected_menu;
    editor.dropdown_open = dropdown_open;
    editor.selected_item = selected_item;
    memset(&t->ed, 0, sizeof t->ed);

    undo_put(&t->undo);
    options.crlf = t->crlf;
    cur = i;
}

void tabs_init(void) {
    memset(tabs, 0, sizeof tabs);
    tab_n = 1;
    cur = 0;
    bar_first = 0;
}

void tabs_free_all(void) {
    for (int i = 0; i < tab_n; i++) {
        if (i == cur) {
            free_all_lines();
            undo_clear();
        } else {
            free_doc(&tabs[i].ed);
            undo_state_free(&tabs[i].undo);
        }
    }
    tab_n = 0;
}

int tab_count(void) {
    return tab_n;
}

int tab_current(void) {
    return cur;
}

Editor *tab_doc(int i) {
    return i == cur ? &editor : &tabs[i].ed;
}

const char *tab_name(int i) {
    return tab_doc(i)->filename;
}

int tab_crlf(int i) {
    return i == cur ? options.crlf : tabs[i].crlf;
}

int tab_named(int i) {
    return tabs[i].named;
}

void tab_mark_named(void) {
    tabs[cur].named = 1;
}

/* Same file under two spellings (relative and absolute, say). Paths that
   resolve are compared resolved; anything else is compared as typed. */
static int same_file(const char *a, const char *b) {
    if (strcmp(a, b) == 0) return 1;
    char ra[4096], rb[4096];
#ifdef _WIN32
    if (!_fullpath(ra, a, sizeof(ra)) || !_fullpath(rb, b, sizeof(rb))) return 0;
    return _stricmp(ra, rb) == 0;
#else
    if (!realpath(a, ra) || !realpath(b, rb)) return 0;
    return strcmp(ra, rb) == 0;
#endif
}

int tab_find(const char *name) {
    for (int i = 0; i < tab_n; i++) {
        if (tabs[i].named && same_file(tab_name(i), name)) return i;
    }
    return -1;
}

int tab_is_blank(void) {
    return !tabs[cur].named && !editor.modified
        && editor.line_count == 1 && editor.lines[0].len == 0;
}

int tabs_first_modified(void) {
    for (int i = 0; i < tab_n; i++) {
        if (tab_doc(i)->modified) return i;
    }
    return -1;
}

int tab_new(void) {
    if (tab_n >= MAX_TABS) return 0;
    stash_active();

    int n = tab_n++;
    Tab *t = &tabs[n];
    memset(t, 0, sizeof *t);
    t->ed.insert_mode = 1;
    untitled_name(t->ed.filename, sizeof t->ed.filename);

    load_active(n);
    reset_document();     /* one empty line, cursor at the top */
    options.crlf = 0;     /* new files are saved with LF */
    return 1;
}

void tab_switch(int i) {
    if (i < 0 || i >= tab_n || i == cur) return;
    stash_active();
    load_active(i);
}

void tab_next(void) {
    if (tab_n > 1) tab_switch((cur + 1) % tab_n);
}

void tab_prev(void) {
    if (tab_n > 1) tab_switch((cur + tab_n - 1) % tab_n);
}

void tab_close_current(void) {
    if (tab_n == 1) {
        /* Last tab: start over with an untitled one */
        free_all_lines();
        undo_clear();
        memset(&tabs[0], 0, sizeof tabs[0]);   /* clears the name flag too */
        untitled_name(editor.filename, sizeof editor.filename);
        reset_document();
        options.crlf = 0;
        return;
    }

    int old = cur;
    int target = (cur + 1 < tab_n) ? cur + 1 : cur - 1;

    /* The active document is discarded. Its slot is empty, so load the
       neighbour first, then remove the old slot by shifting the rest down. */
    free_all_lines();
    undo_clear();
    load_active(target);

    memmove(&tabs[old], &tabs[old + 1], (tab_n - old - 1) * sizeof(Tab));
    tab_n--;
    memset(&tabs[tab_n], 0, sizeof(Tab));
    cur = (target > old) ? target - 1 : target;
    bar_first = 0;
}

/* ---- Tab bar ---- */

/* Label for tab i: its base file name, a '*' when modified, padded with a space each side. */
int tab_bar_label(int i, char *buf, int size) {
    const char *name = tab_name(i);
    const char *base = name;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    int n = (int)strlen(base);
    if (n > TAB_NAME_MAX) {
        n = TAB_NAME_MAX;
        while (n > 0 && ((unsigned char)base[n] & 0xC0) == 0x80) n--;   /* stay on a character */
    }
    snprintf(buf, size, " %.*s%s ", n, base, tab_doc(i)->modified ? "*" : "");
    return (int)strlen(buf);
}

static int label_cells(int i) {
    char buf[64];
    int n = tab_bar_label(i, buf, sizeof buf);
    return utf8_width(buf, n);
}

/* Scrolls the bar so the active tab is visible, then finds the tabs that fit. */
void tab_bar_range(int *first, int *end) {
    if (cur < bar_first) bar_first = cur;
    while (bar_first < cur) {
        int w = 0;
        for (int i = bar_first; i <= cur; i++) w += label_cells(i);
        if (w <= screen_w) break;
        bar_first++;
    }

    int x = 0, e = bar_first;
    while (e < tab_n) {
        int w = label_cells(e);
        if (x + w > screen_w) break;
        x += w;
        e++;
    }
    *first = bar_first;
    *end = e;
}

int tab_bar_hit(int x) {
    int first, end;
    tab_bar_range(&first, &end);
    int start = 0;
    for (int i = first; i < end; i++) {
        int w = label_cells(i);
        if (x >= start && x < start + w) return i;
        start += w;
    }
    return -1;
}
