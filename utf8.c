/* UTF-8 helpers. Invalid bytes are treated as single one-column characters
   so they round-trip through the buffer unchanged. */
#include "utf8.h"

int utf8_decode(const char *s, int len, int *cp) {
    unsigned char c = (unsigned char)s[0];
    int need, min;
    int v;

    if (c < 0x80) { *cp = c; return 1; }
    if (c >= 0xC2 && c <= 0xDF)      { need = 1; min = 0x80;    v = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { need = 2; min = 0x800;   v = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { need = 3; min = 0x10000; v = c & 0x07; }
    else { *cp = -1; return 1; }

    if (len < need + 1) { *cp = -1; return 1; }
    for (int i = 1; i <= need; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) { *cp = -1; return 1; }
        v = (v << 6) | (cc & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
        *cp = -1;
        return 1;
    }
    *cp = v;
    return need + 1;
}

int utf8_encode(int cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

typedef struct { int lo, hi; } Range;

/* Zero-width: combining marks, ZWJ/ZWNJ, variation selectors. */
static const Range zero_width[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F},
    {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xE0100, 0xE01EF}
};

/* Wide: East Asian wide/fullwidth and common emoji blocks. */
static const Range wide[] = {
    {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
    {0xFE30, 0xFE4F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1F300, 0x1F64F},
    {0x1F900, 0x1F9FF}, {0x20000, 0x3FFFD}
};

static int in_ranges(int cp, const Range *r, int n) {
    for (int i = 0; i < n; i++) {
        if (cp >= r[i].lo && cp <= r[i].hi) return 1;
    }
    return 0;
}

int cp_width(int cp) {
    if (cp < 0) return 1;                      /* invalid byte shown as '?' */
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 1;
    if (in_ranges(cp, zero_width, sizeof zero_width / sizeof zero_width[0])) return 0;
    if (in_ranges(cp, wide, sizeof wide / sizeof wide[0])) return 2;
    return 1;
}

/* Length of the character starting at i (base plus following zero-width marks). */
static int char_len(const char *text, int len, int i) {
    int cp;
    int n = utf8_decode(text + i, len - i, &cp);
    int j = i + n;
    while (j < len) {
        int cp2;
        int m = utf8_decode(text + j, len - j, &cp2);
        if (cp2 < 0 || cp_width(cp2) != 0) break;
        j += m;
    }
    return j - i;
}

int utf8_next(const char *text, int len, int i) {
    if (i >= len) return len;
    return i + char_len(text, len, i);
}

int utf8_prev(const char *text, int len, int i) {
    if (i <= 0) return 0;
    if (i > len) i = len;
    /* Walk forward from the line start; the last boundary before i wins.
       Lines are short, so this is cheap enough for per-keypress use. */
    int start = 0, pos = 0;
    while (pos < i) {
        start = pos;
        pos = utf8_next(text, len, pos);
    }
    return start;
}

int utf8_col(const char *text, int len, int i) {
    int col = 0, pos = 0;
    if (i > len) i = len;
    while (pos < i) {
        int nxt = utf8_next(text, len, pos);
        int cp;
        utf8_decode(text + pos, len - pos, &cp);
        col += cp_width(cp);
        pos = nxt;
    }
    return col;
}

int utf8_byte_at_col(const char *text, int len, int col) {
    int c = 0, pos = 0;
    while (pos < len) {
        int nxt = utf8_next(text, len, pos);
        int cp;
        utf8_decode(text + pos, len - pos, &cp);
        int w = cp_width(cp);
        if (c + w > col) break;
        c += w;
        pos = nxt;
    }
    return pos;
}

int utf8_width(const char *text, int len) {
    return utf8_col(text, len, len);
}
