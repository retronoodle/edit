# PRD: AI integration

## Summary

Add AI help to velo, modelled on the AI features in `~/electron/veledit`. One AI (one provider, model and key) is configured once and used by every AI feature. There are no per-feature AI settings.

## Goals

- Rewrite or generate text from a plain-language instruction, in place, as one undo step.
- Explain the selected code.
- Draft a commit message from the staged/working diff in the Alt+G git panel.
- One global AI setting, shared by all features.
- Keep the plain `make` build with no new library dependencies.

## Non-goals

- Per-feature provider or model settings.
- Autocomplete (the main loop blocks during a request, so it would freeze typing).
- Chat panel with tools or an agent loop (possible later, out of scope here).
- Usage and cost tracking.
- Streaming tokens into the buffer. Requests are blocking, with a "Thinking..." message.

## Design

### Providers

Two request formats cover everything:

1. **Anthropic Messages API** (`https://api.anthropic.com/v1/messages`).
2. **OpenAI-compatible chat completions.** One format for OpenRouter, Kilo, Ollama and any similar service. Only the base URL and key differ.

### Settings

Stored in `~/.velo_config` as `key=value` lines, alongside the existing options:

| Key | Meaning |
|---|---|
| `ai_provider` | `anthropic` or `openai` (OpenAI-compatible) |
| `ai_model` | model name sent in the request |
| `ai_base_url` | base URL for the `openai` provider (e.g. an Ollama or OpenRouter URL); ignored for `anthropic` |
| `ai_key` | API key (optional if an env var is set) |

Env var fallback for the key: `ANTHROPIC_API_KEY` for `anthropic`, `OPENAI_API_KEY` for `openai`. An env var wins over a stored key. An Options > AI item prompts for provider, model, base URL and key.

### Core module: `ai.c/.h`

One function does all the work:

```c
/* Sends one request using the global AI settings. On success returns 0 and
   fills *out with a malloc'd reply (caller frees). On failure returns -1 and
   fills err (size errsize) with a short message. */
int ai_ask(const char *system, const char *input, char **out, char *err, int errsize);
```

- Builds the JSON body by hand (fixed shape) with a JSON string escaper.
- Runs `curl` through `popen`, as `gitmodal.c` already does with `git`. On Windows it uses `_popen` and `curl.exe` (shipped with Windows 10+).
- The key never appears on the command line, so it is not visible in `ps`. The request headers go to curl through a temp config file (mode 0600) that is deleted afterwards.
- A small extractor pulls the reply text out of the response JSON (`content[0].text` for Anthropic, `choices[0].message.content` for OpenAI-compatible) and decodes the JSON escapes. API error bodies are turned into a short message.
- Pure helpers (JSON escape and unescape, field extraction, request building) are kept free of I/O so they can be unit tested in `tests/test_ai.c`.

### Features built on `ai_ask`

Each feature is only a system prompt plus glue code.

- **Ask AI** (Alt+I, Edit menu): prompts for an instruction on the prompt row. Sends the instruction, the selection (or empty for insert at cursor) and some surrounding text. The reply replaces the selection (or is inserted) as a single undo step.
- **Explain** (Edit menu): sends the selection and shows the reply in a read-only scrollable modal.
- **Commit message** (in the Alt+G panel): sends the diff and puts the reply in the commit message prompt for editing before commit.

### Constraints

- Requests block the UI. Show "Thinking..." before the call and a clear message on failure. A timeout (`curl --max-time`) bounds the wait.
- Content sent to the API is the user's selection or diff, and only when they trigger a feature. Nothing is sent in the background.
- Text edits go through the existing edit functions in `buffer.c` so undo records them. See the module notes in `CLAUDE.md`.

## Tasks

### Phase 1: AI settings

- [x] 1.1 Read `docs/PRD-ai.md`
- [x] 1.2 Add `ai_provider`, `ai_model`, `ai_base_url`, `ai_key` to `Options` and to `options_load`/`options_save`/`options_parse_line`
- [x] 1.3 Add key lookup with the env var fallback (env wins)
- [x] 1.4 Extend `tests/test_options.c` for the new keys and bad values

### Phase 2: Options > AI setup

- [x] 2.1 Add `ACT_AI_SETUP` to `menu.h` and an "AI..." item to the Options menu
- [x] 2.2 Add `ai_setup()` that prompts for provider, model, base URL and key, and saves
- [x] 2.3 Show the key masked (or as "set" / "from env") and never echo it in messages
- [x] 2.4 Wire the action into `run_action()` in `velo.c`

### Phase 3: JSON helpers

- [x] 3.1 Create `ai.c/.h` and add them to `SOURCES`/`HEADERS` in the Makefile
- [x] 3.2 Add JSON string escape and unescape (including `\uXXXX` and surrogate pairs)
- [x] 3.3 Add a field extractor for the reply text and the API error message
- [x] 3.4 Add `tests/test_ai.c` for the helpers and a `make test` target entry

### Phase 4: Request building

- [x] 4.1 Build the Anthropic request body from system, input, model and max tokens
- [x] 4.2 Build the OpenAI-compatible request body and URL from the base URL
- [x] 4.3 Unit tests for both request builders

### Phase 5: `ai_ask` transport

- [ ] 5.1 Write the headers to a 0600 temp curl config and delete it afterwards
- [ ] 5.2 Run curl through `popen`/`_popen` with `--max-time`, and read the whole response
- [ ] 5.3 Map curl failures and API error bodies to short messages
- [ ] 5.4 Check callers: no key and no model give a clear "set up AI" message

### Phase 6: Ask AI on a selection

- [ ] 6.1 Add `ACT_AI_ASK` (Alt+I) and an Edit menu item
- [ ] 6.2 Prompt for the instruction, build the input from the selection and surrounding text
- [ ] 6.3 Replace the selection (or insert at cursor) through the `buffer.c` edit functions as one undo step
- [ ] 6.4 Strip code fences from the reply

### Phase 7: Explain selection

- [ ] 7.1 Add `ACT_AI_EXPLAIN` and an Edit menu item
- [ ] 7.2 Add a read-only scrollable modal for long replies (reuse the `show_help` pattern)

### Phase 8: Commit message draft

- [ ] 8.1 Get the diff from the git panel's repo (capped in size) in `gitmodal.c`
- [ ] 8.2 Add a key in the Alt+G panel that drafts a message and fills the commit prompt for editing

### Phase 9: Docs

- [ ] 9.1 Update `CLAUDE.md` (module list, settings, Options menu) and the in-app help
