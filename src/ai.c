/* Pure JSON helpers for the AI module. See ai.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "utf8.h"

#define AI_MAX_DEPTH 64

/* Growable NUL-terminated byte buffer. */
typedef struct {
    char *p;
    int len;
    int cap;
} Buf;

static int buf_init(Buf *b) {
    b->cap = 64;
    b->len = 0;
    b->p = malloc(b->cap);
    if (!b->p) return 0;
    b->p[0] = '\0';
    return 1;
}

static int buf_put(Buf *b, const char *s, int n) {
    if (b->len + n + 1 > b->cap) {
        int ncap = b->cap;
        while (ncap < b->len + n + 1) ncap *= 2;
        char *np = realloc(b->p, ncap);
        if (!np) return 0;
        b->p = np;
        b->cap = ncap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
    return 1;
}

static int buf_putc(Buf *b, char c) {
    return buf_put(b, &c, 1);
}

char *ai_json_escape(const char *text, int len) {
    Buf b;
    if (!buf_init(&b)) return NULL;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        int ok;
        switch (c) {
        case '"':  ok = buf_put(&b, "\\\"", 2); break;
        case '\\': ok = buf_put(&b, "\\\\", 2); break;
        case '\b': ok = buf_put(&b, "\\b", 2); break;
        case '\f': ok = buf_put(&b, "\\f", 2); break;
        case '\n': ok = buf_put(&b, "\\n", 2); break;
        case '\r': ok = buf_put(&b, "\\r", 2); break;
        case '\t': ok = buf_put(&b, "\\t", 2); break;
        default:
            if (c < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof tmp, "\\u%04x", c);
                ok = buf_put(&b, tmp, 6);
            } else {
                ok = buf_putc(&b, (char)c);
            }
        }
        if (!ok) {
            free(b.p);
            return NULL;
        }
    }
    return b.p;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Reads the four hex digits of a \uXXXX escape whose 'u' is at s[i]. Returns the
   code unit, or -1 if the digits are missing or not hex. */
static int read_u4(const char *s, int n, int i) {
    if (i + 4 >= n) return -1;
    int v = 0;
    for (int k = 1; k <= 4; k++) {
        int h = hex_val(s[i + k]);
        if (h < 0) return -1;
        v = v * 16 + h;
    }
    return v;
}

char *ai_json_unescape(const char *s, int len) {
    Buf b;
    if (!buf_init(&b)) return NULL;
    for (int i = 0; i < len; i++) {
        if (s[i] != '\\') {
            if (!buf_putc(&b, s[i])) goto fail;
            continue;
        }
        if (++i >= len) goto fail;
        switch (s[i]) {
        case '"':
        case '\\':
        case '/':
            if (!buf_putc(&b, s[i])) goto fail;
            break;
        case 'b': if (!buf_putc(&b, '\b')) goto fail; break;
        case 'f': if (!buf_putc(&b, '\f')) goto fail; break;
        case 'n': if (!buf_putc(&b, '\n')) goto fail; break;
        case 'r': if (!buf_putc(&b, '\r')) goto fail; break;
        case 't': if (!buf_putc(&b, '\t')) goto fail; break;
        case 'u': {
            int cu = read_u4(s, len, i);
            if (cu < 0) goto fail;
            i += 4;
            int cp = cu;
            if (cu >= 0xD800 && cu <= 0xDBFF) {
                /* High surrogate: pair it with a following low surrogate escape. */
                int lo = -1;
                if (i + 2 < len && s[i + 1] == '\\' && s[i + 2] == 'u') lo = read_u4(s, len, i + 2);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cu - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cu >= 0xDC00 && cu <= 0xDFFF) {
                cp = 0xFFFD;
            }
            char enc[4];
            int n = utf8_encode(cp, enc);
            if (!buf_put(&b, enc, n)) goto fail;
            break;
        }
        default:
            goto fail;
        }
    }
    return b.p;
fail:
    free(b.p);
    return NULL;
}

/* Cursor over a JSON document: s[0..n), position i. */
typedef struct {
    const char *s;
    int n;
    int i;
} Cur;

static void skip_ws(Cur *c) {
    while (c->i < c->n && (c->s[c->i] == ' ' || c->s[c->i] == '\t' ||
                           c->s[c->i] == '\n' || c->s[c->i] == '\r'))
        c->i++;
}

/* Skips a string whose opening quote is at c->i. Returns 1 if it closes. */
static int skip_string(Cur *c) {
    c->i++;
    while (c->i < c->n) {
        char ch = c->s[c->i++];
        if (ch == '\\') {
            if (c->i >= c->n) return 0;
            c->i++;
        } else if (ch == '"') {
            return 1;
        }
    }
    return 0;
}

/* Skips one complete value at c->i. Returns 1 on success. */
static int skip_value(Cur *c, int depth) {
    if (depth > AI_MAX_DEPTH) return 0;
    skip_ws(c);
    if (c->i >= c->n) return 0;
    char ch = c->s[c->i];
    if (ch == '"') return skip_string(c);
    if (ch == '{' || ch == '[') {
        char close = ch == '{' ? '}' : ']';
        c->i++;
        skip_ws(c);
        if (c->i < c->n && c->s[c->i] == close) {
            c->i++;
            return 1;
        }
        for (;;) {
            if (ch == '{') {
                skip_ws(c);
                if (c->i >= c->n || c->s[c->i] != '"' || !skip_string(c)) return 0;
                skip_ws(c);
                if (c->i >= c->n || c->s[c->i] != ':') return 0;
                c->i++;
            }
            if (!skip_value(c, depth + 1)) return 0;
            skip_ws(c);
            if (c->i >= c->n) return 0;
            if (c->s[c->i] == ',') {
                c->i++;
                continue;
            }
            if (c->s[c->i] == close) {
                c->i++;
                return 1;
            }
            return 0;
        }
    }
    /* Literal or number: a run of characters up to a delimiter. */
    int start = c->i;
    while (c->i < c->n && !strchr(",]} \t\r\n", c->s[c->i])) c->i++;
    return c->i > start;
}

/* Moves c to the value of key seg (seglen bytes) in the object at c->i.
   Returns 1 with c on the value, or 0 if the key is absent. */
static int seek_key(Cur *c, const char *seg, int seglen) {
    if (c->i >= c->n || c->s[c->i] != '{') return 0;
    c->i++;
    skip_ws(c);
    if (c->i < c->n && c->s[c->i] == '}') return 0;
    for (;;) {
        skip_ws(c);
        if (c->i >= c->n || c->s[c->i] != '"') return 0;
        int ks = c->i + 1;
        if (!skip_string(c)) return 0;
        int ke = c->i - 1;
        int match = 0;
        char *dec = ai_json_unescape(c->s + ks, ke - ks);
        if (dec) {
            match = strlen(dec) == (size_t)seglen && memcmp(dec, seg, seglen) == 0;
            free(dec);
        }
        skip_ws(c);
        if (c->i >= c->n || c->s[c->i] != ':') return 0;
        c->i++;
        skip_ws(c);
        if (match) return 1;
        if (!skip_value(c, 1)) return 0;
        skip_ws(c);
        if (c->i >= c->n || c->s[c->i] != ',') return 0;
        c->i++;
    }
}

/* Moves c to element idx of the array at c->i. Returns 1 on success, 0 if absent. */
static int seek_index(Cur *c, int idx) {
    if (c->i >= c->n || c->s[c->i] != '[') return 0;
    c->i++;
    skip_ws(c);
    if (c->i < c->n && c->s[c->i] == ']') return 0;
    for (int k = 0;; k++) {
        skip_ws(c);
        if (k == idx) return 1;
        if (!skip_value(c, 1)) return 0;
        skip_ws(c);
        if (c->i >= c->n || c->s[c->i] != ',') return 0;
        c->i++;
    }
}

/* Parses a path segment of digits as an array index. Returns -1 if not all digits. */
static int parse_index(const char *p, int len) {
    if (len <= 0 || len > 6) return -1;
    int v = 0;
    for (int k = 0; k < len; k++) {
        if (!isdigit((unsigned char)p[k])) return -1;
        v = v * 10 + (p[k] - '0');
    }
    return v;
}

char *ai_json_get_string(const char *json, const char *path) {
    if (!json || !path) return NULL;
    Cur c;
    c.s = json;
    c.n = (int)strlen(json);
    c.i = 0;
    const char *p = path;
    for (;;) {
        const char *dot = strchr(p, '.');
        int seglen = dot ? (int)(dot - p) : (int)strlen(p);
        if (seglen == 0) return NULL;
        skip_ws(&c);
        if (c.i >= c.n) return NULL;
        int ok;
        if (c.s[c.i] == '[') {
            int idx = parse_index(p, seglen);
            ok = idx >= 0 && seek_index(&c, idx);
        } else {
            ok = seek_key(&c, p, seglen);
        }
        if (!ok) return NULL;
        if (!dot) break;
        p = dot + 1;
    }
    skip_ws(&c);
    if (c.i >= c.n || c.s[c.i] != '"') return NULL;
    int ks = c.i + 1;
    if (!skip_string(&c)) return NULL;
    return ai_json_unescape(json + ks, c.i - 1 - ks);
}

char *ai_json_error(const char *json) {
    char *m = ai_json_get_string(json, "error.message");
    if (m) return m;
    m = ai_json_get_string(json, "error");
    if (m) return m;
    return ai_json_get_string(json, "message");
}

/* Request building helpers. */

static int put_lit(Buf *b, const char *s) {
    return buf_put(b, s, (int)strlen(s));
}

/* Appends s as an escaped JSON string, with quotes. */
static int put_json_str(Buf *b, const char *s) {
    char *e = ai_json_escape(s, (int)strlen(s));
    if (!e) return 0;
    int ok = buf_putc(b, '"') && put_lit(b, e) && buf_putc(b, '"');
    free(e);
    return ok;
}

/* Appends "key":"value". */
static int put_field(Buf *b, const char *key, const char *val) {
    return buf_putc(b, '"') && put_lit(b, key) && put_lit(b, "\":") && put_json_str(b, val);
}

static char *finish_body(Buf *b, int ok) {
    if (!ok) {
        free(b->p);
        return NULL;
    }
    return b->p;
}

char *ai_build_anthropic_body(const char *model, const char *system,
                              const char *input, int max_tokens) {
    Buf b;
    if (!buf_init(&b)) return NULL;
    char num[16];
    snprintf(num, sizeof num, "%d", max_tokens);
    int ok = buf_putc(&b, '{') &&
             put_field(&b, "model", model ? model : "") &&
             put_lit(&b, ",\"max_tokens\":") && put_lit(&b, num);
    if (ok && system && *system)
        ok = buf_putc(&b, ',') && put_field(&b, "system", system);
    ok = ok && put_lit(&b, ",\"messages\":[{") &&
         put_field(&b, "role", "user") && buf_putc(&b, ',') &&
         put_field(&b, "content", input ? input : "") &&
         put_lit(&b, "}]}");
    return finish_body(&b, ok);
}

char *ai_build_openai_body(const char *model, const char *system,
                           const char *input, int max_tokens) {
    Buf b;
    if (!buf_init(&b)) return NULL;
    char num[16];
    snprintf(num, sizeof num, "%d", max_tokens);
    int ok = buf_putc(&b, '{') &&
             put_field(&b, "model", model ? model : "") &&
             put_lit(&b, ",\"max_tokens\":") && put_lit(&b, num) &&
             put_lit(&b, ",\"messages\":[");
    if (ok && system && *system)
        ok = put_lit(&b, "{") && put_field(&b, "role", "system") && buf_putc(&b, ',') &&
             put_field(&b, "content", system) && put_lit(&b, "},");
    ok = ok && put_lit(&b, "{") && put_field(&b, "role", "user") && buf_putc(&b, ',') &&
         put_field(&b, "content", input ? input : "") &&
         put_lit(&b, "}]}");
    return finish_body(&b, ok);
}

char *ai_openai_url(const char *base_url) {
    static const char SUFFIX[] = "/chat/completions";
    const char *base = base_url ? base_url : "";
    int s = 0;
    int e = (int)strlen(base);
    while (s < e && isspace((unsigned char)base[s])) s++;
    while (e > s && (isspace((unsigned char)base[e - 1]) || base[e - 1] == '/')) e--;
    if (e == s) {
        base = AI_OPENAI_DEFAULT_BASE;
        s = 0;
        e = (int)strlen(base);
    }
    int suffix_len = (int)sizeof SUFFIX - 1;
    int has_suffix = e - s >= suffix_len && memcmp(base + e - suffix_len, SUFFIX, suffix_len) == 0;
    int n = (e - s) + (has_suffix ? 0 : suffix_len);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, base + s, e - s);
    if (!has_suffix) memcpy(out + (e - s), SUFFIX, suffix_len);
    out[n] = '\0';
    return out;
}

/* Transport helpers (pure). */

/* Appends s escaped for a double-quoted curl config value. */
static int put_cfg_str(Buf *b, const char *s) {
    if (!buf_putc(b, '"')) return 0;
    for (; *s; s++) {
        int ok;
        switch (*s) {
        case '\\': ok = put_lit(b, "\\\\"); break;
        case '"':  ok = put_lit(b, "\\\""); break;
        case '\n': ok = put_lit(b, "\\n"); break;
        case '\r': ok = put_lit(b, "\\r"); break;
        case '\t': ok = put_lit(b, "\\t"); break;
        default:   ok = buf_putc(b, *s); break;
        }
        if (!ok) return 0;
    }
    return buf_putc(b, '"');
}

/* Appends header = "name: value" as one curl config line. */
static int put_cfg_header(Buf *b, const char *name, const char *value) {
    char *line = malloc(strlen(name) + strlen(value) + 3);
    if (!line) return 0;
    sprintf(line, "%s: %s", name, value);
    int ok = put_lit(b, "header = ") && put_cfg_str(b, line) && buf_putc(b, '\n');
    free(line);
    return ok;
}

char *ai_curl_config(const char *provider, const char *url, const char *key) {
    Buf b;
    if (!buf_init(&b)) return NULL;
    int anthropic = provider && strcmp(provider, "anthropic") == 0;
    int ok = put_lit(&b, "url = ") && put_cfg_str(&b, url ? url : "") && buf_putc(&b, '\n') &&
             put_cfg_header(&b, "content-type", "application/json");
    if (anthropic) {
        ok = ok && put_cfg_header(&b, "x-api-key", key ? key : "") &&
             put_cfg_header(&b, "anthropic-version", "2023-06-01");
    } else {
        char *auth = NULL;
        if (key) {
            auth = malloc(strlen(key) + 8);
            if (!auth) {
                free(b.p);
                return NULL;
            }
            sprintf(auth, "Bearer %s", key);
        }
        ok = ok && put_cfg_header(&b, "authorization", auth ? auth : "Bearer ");
        free(auth);
    }
    return finish_body(&b, ok);
}

char *ai_parse_reply(const char *provider, const char *body, char *err, int errsize) {
    int anthropic = provider && strcmp(provider, "anthropic") == 0;
    char *text = ai_json_get_string(body ? body : "",
                                    anthropic ? "content.0.text" : "choices.0.message.content");
    if (text) {
        if (*text) return text;
        free(text);
        if (err && errsize > 0) snprintf(err, errsize, "The AI returned no text");
        return NULL;
    }
    char *msg = ai_json_error(body ? body : "");
    if (err && errsize > 0) {
        if (msg) snprintf(err, errsize, "AI error: %s", msg);
        else snprintf(err, errsize, "Unexpected reply from the AI");
    }
    free(msg);
    return NULL;
}

const char *ai_curl_error(int curl_exit) {
    switch (curl_exit) {
    case 6:  return "Could not resolve the AI server host";
    case 7:  return "Could not connect to the AI server";
    case 28: return "The AI request timed out";
    case 35: /* fall through */
    case 60: return "TLS error talking to the AI server";
    case 22: return "The AI server returned an HTTP error";
    default: return NULL;
    }
}
