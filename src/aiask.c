/* The AI transport: runs curl as a child process with the request body and headers
   in 0600 temp files, so the key and the text stay off the command line. Request and
   reply parsing is in ai.c. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L   /* mkstemp, fdopen, popen under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <fcntl.h>
    #include <io.h>
    #include <share.h>
    #include <sys/stat.h>
    #include <time.h>
    FILE *_popen(const char *command, const char *mode);
    int _pclose(FILE *stream);
#else
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "ai.h"
#include "aiask.h"
#include "options.h"

#define AI_MAX_TOKENS 4096
#define AI_TIMEOUT_S  60
#define AI_MAX_REPLY  (8 * 1024 * 1024)

static void set_err(char *err, int errsize, const char *msg) {
    if (err && errsize > 0) snprintf(err, errsize, "%s", msg);
}

/* Creates a new 0600 temp file in the temp dir and opens it for writing. Writes its
   path to path. Returns NULL on failure. */
static FILE *open_temp(char *path, size_t n) {
#ifdef _WIN32
    const char *dir = getenv("TEMP");
    if (!dir || !*dir) dir = ".";
    snprintf(path, n, "%s\\velo-ai-%08lx%08lx.tmp", dir,
             (unsigned long)time(NULL), (unsigned long)rand());
    int fd;
    if (_sopen_s(&fd, path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _SH_DENYRW,
                 _S_IREAD | _S_IWRITE) != 0)
        return NULL;
    FILE *f = _fdopen(fd, "wb");
    if (!f) {
        _close(fd);
        remove(path);
    }
    return f;
#else
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    if (snprintf(path, n, "%s/velo-ai-XXXXXX", dir) >= (int)n) return NULL;
    int fd = mkstemp(path);   /* creates the file with mode 0600 */
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        remove(path);
    }
    return f;
#endif
}

/* Writes data to a new temp file and sets path. Returns 1 on success. */
static int write_temp(char *path, size_t n, const char *data) {
    FILE *f = open_temp(path, n);
    if (!f) return 0;
    size_t len = strlen(data);
    int ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

/* Runs the curl command and reads all of its output into *buf (malloc'd).
   Returns the exit status, or -1 if it could not run. *too_big is set if the
   output passed AI_MAX_REPLY. */
static int run_curl(const char *cmd, char **buf, size_t *len, int *too_big) {
#ifdef _WIN32
    FILE *p = _popen(cmd, "r");
#else
    FILE *p = popen(cmd, "r");
#endif
    *buf = NULL;
    *len = 0;
    *too_big = 0;
    if (!p) return -1;

    size_t cap = 4096;
    char *b = malloc(cap);
    char chunk[4096];
    size_t n;
    if (!b) {
#ifdef _WIN32
        _pclose(p);
#else
        pclose(p);
#endif
        return -1;
    }
    while ((n = fread(chunk, 1, sizeof chunk, p)) > 0) {
        if (*len + n + 1 > AI_MAX_REPLY) {
            *too_big = 1;   /* stop reading; curl gets SIGPIPE and exits */
            break;
        }
        if (*len + n + 1 > cap) {
            while (*len + n + 1 > cap) cap *= 2;
            char *nb = realloc(b, cap);
            if (!nb) {
                free(b);
#ifdef _WIN32
                _pclose(p);
#else
                pclose(p);
#endif
                return -1;
            }
            b = nb;
        }
        memcpy(b + *len, chunk, n);
        *len += n;
    }
    b[*len] = '\0';
    *buf = b;

#ifdef _WIN32
    return _pclose(p);
#else
    int st = pclose(p);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

int ai_ask(const char *system, const char *input, char **out, char *err, int errsize) {
    char cfg_path[512] = "", body_path[512] = "";
    char *url = NULL, *body = NULL, *cfg = NULL, *buf = NULL;
    char cmd[1600];
    int rc = -1;

    *out = NULL;
    set_err(err, errsize, "");

    const char *provider = options.ai_provider;
    int anthropic = strcmp(provider, "anthropic") == 0;
    if (!anthropic && strcmp(provider, "openai") != 0) {
        set_err(err, errsize, "AI is not set up (Options > AI)");
        return -1;
    }
    if (!options.ai_model[0]) {
        set_err(err, errsize, "AI model is not set (Options > AI)");
        return -1;
    }
    const char *key = options_ai_key();
    if (!key || !*key) {
        set_err(err, errsize, "No AI key set (Options > AI)");
        return -1;
    }

    url = anthropic ? strdup(AI_ANTHROPIC_URL) : ai_openai_url(options.ai_base_url);
    body = anthropic
        ? ai_build_anthropic_body(options.ai_model, system, input, AI_MAX_TOKENS)
        : ai_build_openai_body(options.ai_model, system, input, AI_MAX_TOKENS);
    cfg = url ? ai_curl_config(provider, url, key) : NULL;
    if (!url || !body || !cfg) {
        set_err(err, errsize, "Out of memory");
        goto done;
    }

    /* The config holds the key and the body holds the user's text: both go in
       0600 temp files, removed at done. */
    if (!write_temp(cfg_path, sizeof cfg_path, cfg) ||
        !write_temp(body_path, sizeof body_path, body)) {
        set_err(err, errsize, "Could not write a temp file for the AI request");
        goto done;
    }

    snprintf(cmd, sizeof cmd,
#ifdef _WIN32
             "curl.exe"
#else
             "curl"
#endif
             " -sS --max-time %d -K \"%s\" --data-binary \"@%s\" -w \"\\n%%{http_code}\" 2>&1",
             AI_TIMEOUT_S, cfg_path, body_path);

    size_t len;
    int too_big;
    int status = run_curl(cmd, &buf, &len, &too_big);
    if (!buf) {
        set_err(err, errsize, "Could not run curl (is it installed?)");
        goto done;
    }
    if (too_big) {
        set_err(err, errsize, "The AI reply was too large");
        goto done;
    }
    if (status != 0) {
        const char *m = ai_curl_error(status);
        if (m) set_err(err, errsize, m);
        else {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "AI request failed (curl exit %d)", status);
            set_err(err, errsize, tmp);
        }
        goto done;
    }

    /* curl's -w adds "\n<http code>" after the body. Split it off. */
    int code = 0;
    char *nl = strrchr(buf, '\n');
    if (nl && strlen(nl + 1) == 3 && strspn(nl + 1, "0123456789") == 3) {
        code = atoi(nl + 1);
        *nl = '\0';
    }
    if (code < 200 || code >= 300) {
        char *m = ai_json_error(buf);
        char tmp[320];
        if (m) snprintf(tmp, sizeof tmp, "HTTP %d: %s", code, m);
        else snprintf(tmp, sizeof tmp, "HTTP %d", code);
        free(m);
        set_err(err, errsize, tmp);
        goto done;
    }

    {
        char *reply = ai_parse_reply(provider, buf, err, errsize);
        if (reply) {
            *out = reply;
            rc = 0;
        }
    }

done:
    if (cfg_path[0]) remove(cfg_path);
    if (body_path[0]) remove(body_path);
    free(url);
    free(body);
    free(cfg);
    free(buf);
    return rc;
}
