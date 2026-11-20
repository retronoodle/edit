/* Non-blocking AI transport: curl runs as a child process whose output goes to a pipe
   that the main loop polls. Same temp-file scheme as aiask.c (key and text stay off the
   command line). Request/reply handling is in ai.c. */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "aiask.h"
#include "aiasync.h"
#include "options.h"

#define AI_TIMEOUT_S 30
#define AI_MAX_REPLY (1024 * 1024)

#ifdef _WIN32

int aiasync_start(const char *system, const char *input, int max_tokens, char *err, int errsize) {
    (void)system; (void)input; (void)max_tokens;
    snprintf(err, errsize, "AI autocomplete is not supported on Windows yet");
    return -1;
}
int aiasync_poll(char **out, char *err, int errsize) { (void)out; (void)err; (void)errsize; return 0; }
void aiasync_cancel(void) {}
int aiasync_busy(void) { return 0; }

#else

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static struct {
    pid_t pid;            /* 0 when idle */
    int fd;               /* read end of curl's output pipe */
    char *buf;
    size_t len, cap;
    char cfg_path[512], body_path[512];
} job;

static void set_err(char *err, int errsize, const char *msg) {
    if (err && errsize > 0) snprintf(err, errsize, "%s", msg);
}

static void job_reset(void) {
    if (job.fd > 0) close(job.fd);
    if (job.cfg_path[0]) remove(job.cfg_path);
    if (job.body_path[0]) remove(job.body_path);
    free(job.buf);
    memset(&job, 0, sizeof job);
}

void aiasync_cancel(void) {
    if (job.pid > 0) {
        kill(job.pid, SIGKILL);
        waitpid(job.pid, NULL, 0);
    }
    job_reset();
}

int aiasync_busy(void) {
    return job.pid > 0;
}

int aiasync_start(const char *system, const char *input, int max_tokens, char *err, int errsize) {
    aiasync_cancel();
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

    char *url = anthropic ? strdup(AI_ANTHROPIC_URL) : ai_openai_url(options.ai_base_url);
    char *body = anthropic ? ai_build_anthropic_body(options.ai_model, system, input, max_tokens)
                           : ai_build_openai_body(options.ai_model, system, input, max_tokens);
    char *cfg = url ? ai_curl_config(provider, url, key) : NULL;
    int rc = -1;
    int pfd[2] = {-1, -1};
    char to[16];

    if (!url || !body || !cfg) {
        set_err(err, errsize, "Out of memory");
        goto done;
    }
    if (!ai_write_temp(job.cfg_path, sizeof job.cfg_path, cfg) ||
        !ai_write_temp(job.body_path, sizeof job.body_path, body)) {
        set_err(err, errsize, "Could not write a temp file for the AI request");
        goto done;
    }
    if (pipe(pfd) != 0) {
        set_err(err, errsize, "Could not start the AI request");
        goto done;
    }

    char data_arg[sizeof job.body_path + 2];
    snprintf(data_arg, sizeof data_arg, "@%s", job.body_path);
    snprintf(to, sizeof to, "%d", AI_TIMEOUT_S);

    pid_t pid = fork();
    if (pid < 0) {
        set_err(err, errsize, "Could not start the AI request");
        goto done;
    }
    if (pid == 0) {   /* child: curl with stdout/stderr into the pipe */
        dup2(pfd[1], 1);
        dup2(pfd[1], 2);
        close(pfd[0]);
        close(pfd[1]);
        execlp("curl", "curl", "-sS", "--max-time", to, "-K", job.cfg_path,
               "--data-binary", data_arg, "-w", "\n%{http_code}", (char *)NULL);
        _exit(127);
    }
    close(pfd[1]);
    pfd[1] = -1;
    fcntl(pfd[0], F_SETFL, fcntl(pfd[0], F_GETFL) | O_NONBLOCK);
    job.pid = pid;
    job.fd = pfd[0];
    pfd[0] = -1;
    rc = 0;

done:
    if (pfd[0] >= 0) close(pfd[0]);
    if (pfd[1] >= 0) close(pfd[1]);
    free(url);
    free(body);
    free(cfg);
    if (rc != 0) job_reset();
    return rc;
}

int aiasync_poll(char **out, char *err, int errsize) {
    *out = NULL;
    if (job.pid <= 0) return 0;

    /* Drain whatever curl has written so far */
    int eof = 0;
    for (;;) {
        if (job.len + 4096 + 1 > job.cap) {
            size_t ncap = job.cap ? job.cap * 2 : 8192;
            char *nb = realloc(job.buf, ncap);
            if (!nb) {
                aiasync_cancel();
                set_err(err, errsize, "Out of memory");
                return -1;
            }
            job.buf = nb;
            job.cap = ncap;
        }
        ssize_t n = read(job.fd, job.buf + job.len, 4096);
        if (n > 0) {
            job.len += (size_t)n;
            if (job.len > AI_MAX_REPLY) {
                aiasync_cancel();
                set_err(err, errsize, "The AI reply was too large");
                return -1;
            }
            continue;
        }
        if (n == 0) eof = 1;
        else if (errno == EINTR) continue;
        break;   /* EAGAIN: nothing more yet */
    }
    if (!eof) return 0;

    /* curl closed the pipe: collect its exit status, then parse like ai_ask */
    int st = 0;
    waitpid(job.pid, &st, 0);
    job.pid = 0;
    int status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    job.buf[job.len] = '\0';
    char *buf = job.buf;
    job.buf = NULL;
    job_reset();

    int result = -1;
    if (status != 0) {
        const char *m = ai_curl_error(status);
        if (m) set_err(err, errsize, m);
        else {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "AI request failed (curl exit %d)", status);
            set_err(err, errsize, tmp);
        }
        goto out;
    }
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
        goto out;
    }
    *out = ai_parse_reply(options.ai_provider, buf, err, errsize);
    if (*out) result = 1;
out:
    free(buf);
    return result;
}

#endif
