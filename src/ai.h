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

/* Transport helpers (pure). */

/* curl config text (malloc'd, free it): url, plus the headers for the provider
   ("anthropic" uses x-api-key and anthropic-version, anything else a Bearer token).
   Values are quoted and escaped for curl's config parser, so the key stays out of
   the command line. Returns NULL on allocation failure. */
char *ai_curl_config(const char *provider, const char *url, const char *key);

/* Reply text from a response body (malloc'd, free it). Anthropic reads content.0.text,
   other providers choices.0.message.content. On failure returns NULL and fills err
   (size errsize) with the API error message or "The AI returned no text". */
char *ai_parse_reply(const char *provider, const char *body, char *err, int errsize);

/* Short message for a curl exit code, or NULL when there is no special message. */
const char *ai_curl_error(int curl_exit);

/* Ask AI helpers (pure). */

/* Removes one wrapping ```lang ... ``` fence when the whole reply is fenced; any other
   reply is copied unchanged. Returns malloc'd text (free it), or NULL on allocation failure. */
char *ai_strip_fences(const char *s);

/* User message for Ask AI: the instruction, then the text before, the selection (or a
   note that the reply goes at the cursor), and the text after. Empty sections are left
   out. Returns malloc'd text (free it), or NULL on allocation failure. */
char *ai_build_ask_input(const char *instruction, const char *selection,
                         const char *before, const char *after);

#endif
