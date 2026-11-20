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

/* Returns the raw JSON text of the value at a dot path (object, array, string with its
   quotes, number...), malloc'd, or NULL when the path is missing. */
char *ai_json_get_raw(const char *json, const char *path);

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

/* Autocomplete helpers (pure). */

/* User message for inline completion: the text before and after the cursor. Returns
   malloc'd text (free it), or NULL on allocation failure. */
char *ai_build_complete_input(const char *before, const char *after);

/* Cleans a completion reply: strips a wrapping code fence, keeps at most max_lines
   lines and drops trailing blank space. Returns malloc'd text (free it), or NULL on
   allocation failure. */
char *ai_trim_completion(const char *reply, int max_lines);

/* Chat with read-only tools (pure). History is a list of provider-format message
   JSON objects (strings); a string may hold several messages joined by commas. */

typedef struct {
    char *id;     /* provider's call id */
    char *name;   /* tool name */
    char *args;   /* arguments as a JSON object (malloc'd) */
} AiToolCall;

/* {"role":role,"content":text} (same for both providers). ai_chat_user_msg is role "user". */
char *ai_chat_text_msg(const char *role, const char *text);
char *ai_chat_user_msg(const char *text);

/* The assistant message to keep in history, taken from a response body (content blocks
   for Anthropic, choices.0.message for OpenAI), so tool calls are echoed back. NULL if absent. */
char *ai_chat_assistant_msg(const char *provider, const char *body);

/* Message(s) answering n tool calls: one user message of tool_result blocks (Anthropic) or
   n comma-joined "tool" messages (OpenAI). */
char *ai_chat_tool_results_msg(const char *provider, const char *const *ids,
                               const char *const *texts, int n);

/* Request body for a chat: system prompt, history msgs[0..n), optional tools_json (from
   ai_chat_tools_json; NULL or "" for none). */
char *ai_build_chat_body(const char *provider, const char *model, const char *system,
                         const char *const *msgs, int n, const char *tools_json,
                         int max_tokens);

/* The tool definitions for list_dir, read_file, grep and open_tabs, in the provider's format. */
char *ai_chat_tools_json(const char *provider);

/* Same, but only the read-only tools (list_dir, read_file, grep, open_tabs): plan mode. */
char *ai_chat_tools_json_ro(const char *provider);

/* Fills out[0..max) with the tool calls in a response body; returns the count. The caller
   frees them with ai_chat_free_calls. */
int ai_chat_parse_calls(const char *provider, const char *body, AiToolCall *out, int max);
void ai_chat_free_calls(AiToolCall *c, int n);

/* Finds the first complete fenced block in a reply. span = { start of the opening fence line,
   first byte of the body, end of the body (no final newline), first byte after the closing
   fence line }, all byte offsets. Returns 0 when there is none. */
int ai_chat_find_fence(const char *reply, int span[4]);

/* First fenced block of a reply (the proposed whole-file replacement), without the fence
   lines. NULL when the reply has no complete fenced block. A fence of N backticks closes
   only on a line of at least N backticks. */
char *ai_chat_extract_file(const char *reply);

/* The user message for one chat turn: the text, the active file name, the selection and
   the (already capped) file contents. truncated notes that the contents are partial. */
char *ai_build_chat_input(const char *message, const char *filename, const char *selection,
                          const char *content, int truncated);

#endif
