/* Pure JSON helpers for the AI module: string escape and unescape, a path
   lookup for the reply text, and API error extraction. No I/O, so tests can use them. */
#ifndef AI_H
#define AI_H

/* Escapes text[0..len) as the inside of a JSON string (no quotes). Returns a malloc'd
   NUL-terminated string (free it), or NULL on allocation failure. */
char *ai_json_escape(const char *text, int len);

/* Decodes the JSON string body s[0..len) (no quotes) to UTF-8. Returns a malloc'd
   string (free it), or NULL on a bad escape. */
char *ai_json_unescape(const char *s, int len);

/* Finds a string value by a dot path such as "content.0.text" (object keys and
   array indexes). Returns the decoded value (malloc'd), or NULL when the path is
   missing or the value is not a string. */
char *ai_json_get_string(const char *json, const char *path);

/* Pulls the API error message from a response body: "error.message", "error" when
   it is a plain string, or top-level "message". Returns malloc'd text or NULL. */
char *ai_json_error(const char *json);

/* Request building (pure). Each returns a malloc'd string (free it), or NULL on
   allocation failure. An empty or NULL system prompt is left out of the body. A NULL
   input is sent as "". */
#define AI_ANTHROPIC_URL "https://api.anthropic.com/v1/messages"
#define AI_OPENAI_DEFAULT_BASE "https://api.openai.com/v1"

/* Anthropic Messages API body: model, max_tokens, optional system, one user message. */
char *ai_build_anthropic_body(const char *model, const char *system,
                              const char *input, int max_tokens);

/* OpenAI-compatible chat completions body: model, max_tokens, optional system
   message, then the user message. */
char *ai_build_openai_body(const char *model, const char *system,
                           const char *input, int max_tokens);

/* Chat completions URL for an OpenAI-compatible base URL. An empty or NULL base uses
   AI_OPENAI_DEFAULT_BASE. Trailing slashes and spaces are trimmed, and
   "/chat/completions" is appended unless the base already ends with it. */
char *ai_openai_url(const char *base_url);

#endif
