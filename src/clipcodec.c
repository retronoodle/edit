/* Pure helpers for the clipboard: base64 for OSC 52, and cleanup of pasted text. */
#include <stdlib.h>

#include "clipcodec.h"

char *clip_b64_encode(const char *text, int len) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((size_t)len + 2) / 3 * 4;
    char *enc = malloc(out_len + 1);
    if (!enc) return NULL;

    size_t o = 0;
    for (int i = 0; i < len; i += 3) {
        unsigned v = (unsigned char)text[i] << 16;
        if (i + 1 < len) v |= (unsigned char)text[i + 1] << 8;
        if (i + 2 < len) v |= (unsigned char)text[i + 2];
        enc[o++] = b64[(v >> 18) & 63];
        enc[o++] = b64[(v >> 12) & 63];
        enc[o++] = (i + 1 < len) ? b64[(v >> 6) & 63] : '=';
        enc[o++] = (i + 2 < len) ? b64[v & 63] : '=';
    }
    enc[o] = '\0';
    return enc;
}

int clip_clean_paste(char *text, int len) {
    int o = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r') {
            text[o++] = '\n';
            if (i + 1 < len && text[i + 1] == '\n') i++;   /* CRLF -> one newline */
        } else if (c == '\n' || c == '\t' || c >= 0x20) {
            text[o++] = (char)c;
        }
        /* other control bytes are dropped */
    }
    return o;
}

int clip_single_line(const char *text, int len, int *start) {
    int end = 0;
    while (end < len && text[end] != '\n') end++;   /* first line only */
    int s = 0;
    while (s < end && (text[s] == ' ' || text[s] == '\t')) s++;
    while (end > s && (text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
    *start = s;
    return end;
}
