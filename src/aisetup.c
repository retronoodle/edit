/* Options > AI setup: prompts for the AI provider, model, base URL and key. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "aisetup.h"
#include "options.h"
#include "ui.h"

/* Copies val into dst if it fits. Returns 0 when it does not. */
static int fill(char *dst, size_t size, const char *val) {
    if (strlen(val) >= size) return 0;
    strcpy(dst, val);
    return 1;
}

/* Prompts for one field. Returns 0 on Esc (cancel). Enter keeps the current
   value, "-" clears it, anything else replaces it. */
static int ask_field(const char *label, char *value, size_t size) {
    char msg[256], buf[300];
    snprintf(msg, sizeof(msg), "%s [%s] (- clears): ", label, value[0] ? value : "none");
    buf[0] = '\0';
    if (!prompt_line_opt(msg, buf, sizeof(buf))) return 0;   /* Esc */
    if (strcmp(buf, "-") == 0) value[0] = '\0';
    else if (buf[0] && !fill(value, size, buf)) {
        show_message("Value is too long");
        return 0;
    }
    return 1;
}

void ai_setup(void) {
    /* Work on copies, so Esc at any step leaves the saved settings untouched. */
    char provider[sizeof(options.ai_provider)];
    char model[sizeof(options.ai_model)];
    char base_url[sizeof(options.ai_base_url)];
    char key[sizeof(options.ai_key)];
    char buf[300];

    strcpy(provider, options.ai_provider);
    strcpy(model, options.ai_model);
    strcpy(base_url, options.ai_base_url);
    strcpy(key, options.ai_key);

    /* Provider: Enter keeps the current one; only anthropic or openai is accepted */
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "AI provider (anthropic/openai) [%s]: ",
                 provider[0] ? provider : "none");
        buf[0] = '\0';
        if (!prompt_line_opt(msg, buf, sizeof(buf))) return;   /* Esc */
    }
    if (buf[0]) {
        if (strcasecmp(buf, "anthropic") == 0) strcpy(provider, "anthropic");
        else if (strcasecmp(buf, "openai") == 0) strcpy(provider, "openai");
        else {
            show_message("Provider must be anthropic or openai");
            return;
        }
    }
    if (!provider[0]) {
        show_message("AI setup cancelled: no provider chosen");
        return;
    }

    if (!ask_field("Model", model, sizeof(model))) return;

    /* Base URL is only used by the openai provider */
    if (strcmp(provider, "openai") == 0 && !ask_field("Base URL", base_url, sizeof(base_url)))
        return;

    /* Key: shows where the key comes from, never its value */
    {
        const char *env_name = strcmp(provider, "openai") == 0 ? "OPENAI_API_KEY" : "ANTHROPIC_API_KEY";
        const char *env = getenv(env_name);
        const char *state = (env && *env) ? "from env" : (key[0] ? "set" : "not set");
        char msg[128];
        snprintf(msg, sizeof(msg), "API key [%s] (- clears): ", state);
        buf[0] = '\0';
        if (!prompt_line_secret(msg, buf, sizeof(buf))) return;   /* Esc */
        if (strcmp(buf, "-") == 0) key[0] = '\0';
        else if (buf[0] && !fill(key, sizeof(key), buf)) {
            show_message("API key is too long");
            return;
        }
    }

    /* Apply and save once */
    strcpy(options.ai_provider, provider);
    strcpy(options.ai_model, model);
    strcpy(options.ai_base_url, base_url);
    strcpy(options.ai_key, key);
    options_save();
    show_message("AI settings saved");
}
