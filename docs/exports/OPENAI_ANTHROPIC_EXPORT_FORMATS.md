# OpenAI (ChatGPT) and Anthropic (Claude) data exports — format reference

Status: research reference for Loom's export registry (`loom/data/exports/formats.json`).
Retrieval date of every external source: **2026-09-29**. Written for R14, R17, R18, R20, R21
(`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md`).

**No real export has been seen by this project.** Neither vendor publishes a contract for the
data-export ZIP. Everything below comes from open-source parsers, their self-written specs and vendor
help pages, and is only as good as those sources. Read the confidence column before trusting a row.

Confidence legend (used in every table):

| Mark | Meaning |
|---|---|
| **V** | *Verified in a real parser's code* — the field/structure is read or typed by working third-party code (file cited). Still not proof that every export carries it. |
| **D** | *Documented* — stated in a project's own spec/README or a vendor help page, but not confirmed by code. |
| **I** | *Inferred* — from Loom's own code, from an adjacent API (backend API, Anthropic Messages API), from names, or recalled without a fetched source this session. |
| **U** | *Unknown* — no source found; the registry treats the structure as open (kept verbatim). |

## 1. Sources

Shorthand used below (all fetched 2026-09-29 unless noted):

| Id | Source | Kind | Used for |
|---|---|---|---|
| CV-msg | `mohamed-chs/convoviz` `convoviz/models/message.py`, `models/conversation.py`, `models/node.py`, `message_logic.py` — https://raw.githubusercontent.com/mohamed-chs/convoviz/main/convoviz/models/message.py (same dir for the others; `message_logic.py` at `convoviz/message_logic.py`) | working Python parser (pydantic models) | ChatGPT conversation/node/message/content, hidden-message rules, canvas, citations |
| CV-io | same repo, `convoviz/io/loaders.py`, `convoviz/io/assets.py` | working parser | zip layout, shards, top-level wrapper, asset resolution |
| CV-spec | same repo, `docs/dev/chatgpt-spec.md` (v3.0, 2026-02-05), `chatgpt-export-discovery.md`, `data-schema.md` | author's *unofficial* reverse-engineered spec | root file list, new 2025/26 fields, naming patterns |
| PZ-api | `pionxzh/chatgpt-exporter` `src/api.ts` — https://raw.githubusercontent.com/pionxzh/chatgpt-exporter/master/src/api.ts | TypeScript types for the **backend API** (the export ZIP is reported to hold the same conversation objects) | content-type union, message metadata, citations, content_references, audio/image pointers |
| JD | `jack-driscoll/chatgpt-dataexport` README (page fetched through WebFetch) | README of a data-export toolkit | `.dat` files, root files, single-line JSON |
| OC | gist `ocombe/1d7604bd29a91ceb716304ef8b5aa4b5` | script using backend endpoints | attachment ids, `file-service://` / `sediment://` pointers |
| CL-zod | `osteele/claude-chat-viewer` `src/schemas/chat.ts` — https://raw.githubusercontent.com/osteele/claude-chat-viewer/main/src/schemas/chat.ts | Zod schema of a working viewer for Claude exports (zip, `conversations.json`, single chats) | every Claude conversation/message/content/attachment/file field |
| CL-proj | `Brads777/ClaudeProjectExport` `claude_export_extractor.py` — https://raw.githubusercontent.com/Brads777/ClaudeProjectExport/main/claude_export_extractor.py | working extractor on official exports | `projects.json` fields, `memories.json` / `users.json` existence, no project id on conversations |
| CL-help | Claude Help Center, "Export your Claude data" https://support.claude.com/en/articles/9450526-export-your-claude-data | vendor page | export = "conversation data and the user data", link valid 24 h; **no file list** |
| OA-help | OpenAI Help Center, "Exporting your ChatGPT history and data" https://help.openai.com/en/articles/7260999 | vendor page (only seen as a search hit; content not fetched) | (nothing beyond existence) |
| WS | web search result summaries (takeoutday.org, memoryplugin.com, xtrace.ai, exportreader.com, …) | secondary blog summaries | weak corroboration only; every use is marked **D** at best |

Blocked by the network egress policy (HTTP 403 / EGRESS_BLOCKED) and **not** routed around:
`chatgpt.com`, `deepwiki.com`, `ai-chat-importer.com`, `community.openai.com`. The GitHub REST API and
GitHub search are also unavailable in this session (only raw file fetches and repository page views).
So the OpenAI community thread "Decoding Exported Data by Parsing conversations.json and/or chat.html" and
the vendor's own help text could not be read.

## 2. OpenAI — ChatGPT export

### 2.1 Package layout (ZIP members)

Delivered by e-mail after Settings → Data controls → Export data (CV-spec §1). Layout as observed by the sources:

| Member | Content | Conf. | Source |
|---|---|---|---|
| `conversations.json` | all conversations; usually **one minified line**, can be hundreds of MB | V | CV-io loaders, JD |
| `conversations-000.json`, `conversations-001.json`, … | **sharded** form of the same array; `conversations.json` absent | V (regex `^conversations-\d+\.json$`) | CV-io |
| top level of a conversations file | either a JSON **array** of conversations or an **object** `{"conversations":[…]}` | V | CV-io `load_collection_from_json` |
| `user.json` | account profile: `id`, `email`, `chatgpt_plus_user`, `phone_number` (CV-spec); "ID, email, user type, birth year" (JD) — the two lists disagree | D (fields U) | CV-spec §8.1, JD |
| `message_feedback.json` | thumbs up/down records with optional text | D (fields U) | CV-spec, JD |
| `shared_conversations.json` | metadata of publicly shared links | D (fields U) | CV-spec, JD |
| `model_comparisons.json` | A/B "which response is better" data, usually empty | D | CV-spec |
| `group_chats.json` (`{"chats":[]}`), `shopping.json` (`[]`), `sora.json` (`{}`) | 2025 additions, mostly empty | D | CV-spec |
| `chat.html` | offline viewer of all conversations (large, JS + embedded JSON) | D | CV-spec, JD |
| `textdocs/…json` | Canvas ("canmore") documents as separate JSON files | D (schema U) | CV-spec §1.1, §8.3 |
| `file-<alnum>-<original name>` (root) | user uploads, original extension kept | D | CV-spec §1.2 |
| `file_<hex>-sanitized.<ext>` (root) | sanitised/processed uploads | D | CV-spec §1.2 |
| `dalle-generations/file-<alnum>-<uuid>.webp` | DALL·E images | D (dir handled: V) | CV-spec, CV-io assets |
| `user-<user_id>/file_<hex>-<uuid>.png` | newer generated images | D (dir handled: V) | CV-spec, CV-io assets |
| `*.dat` | assets with a misleading extension; **PNG bytes with C2PA metadata** | D (single, older source) | JD |
| `export_manifest.json` | **not confirmed by any source**; a search summary says standard exports typically do not include it | U | WS |

Consequence for the reader: the asset file names are not stable across export generations, so
attachment linking has to be done by id *prefix* and by content, not by a fixed path (§2.8).

### 2.2 Conversation object (element of the array)

| Field | Type / notes | Conf. |
|---|---|---|
| `id`, `conversation_id` | UUIDs, usually equal (`conversation_id` is what chatgpt.com/c/<id> uses) | V (`conversation_id` required in CV-msg) / D (`id`) |
| `title` | string **or null** (new/empty chats) | V |
| `create_time`, `update_time` | Unix epoch seconds, float | V |
| `mapping` | object: node id → node (see §2.3) | V |
| `current_node` | id of the leaf of the branch shown in the UI ("golden path") | V |
| `is_starred` (bool/null), `voice` (string **or object**), `plugin_ids` (list) | | V (CV-msg types) |
| `is_archived`, `moderation_results[]`, `safe_urls[]` | | V (PZ-api `ApiConversation`) |
| `default_model_slug`, `model_slug`, `gizmo_id`, `gizmo_type`, `pinned_time`, `is_study_mode`, `is_do_not_remember`, `owner`, `memory_scope`, `context_scopes`, `blocked_urls[]`, `disabled_tool_ids[]`, `async_status`, `conversation_origin`, `sugar_item_id`, `sugar_item_visible` | 2025/26 additions | D (CV-spec §2.1); `is_temporary_chat`, `gizmo_id`, `conversation_origin`, `pinned_time`, `is_do_not_remember` also V in PZ-api list items |
| anything else | must be assumed to appear ("OpenAI frequently changes the internal schema … without updating documentation") | D |

`gizmo_id` is the link from a conversation to a custom GPT **or a project** (PZ-api comments: "Non-null when the
conversation belongs to a custom GPT or project"). There is no project *object* in the export package that
was reported by any source (U).

### 2.3 Tree: `mapping` nodes and branches

Node: `{"id": str, "message": Message|null, "parent": str|null, "children": [str]}` — V (CV-msg node.py).
Root node: `parent` null, `message` usually null (D). Several `children` ⇒ an edit of a user turn or a
regeneration of an assistant turn. Reading the visible thread = walk `parent` from `current_node` to the root
and reverse (V, CV-io/CV-msg `ordered_nodes`). The other leaves are the *alternative branches*; nothing in the
node says which alternative was the older one except `create_time` on the messages (convoviz sorts by
`create_time`, then node id).
Failure modes to expect (I): `current_node` pointing at an id that is missing from `mapping`; `children`
listing ids that are absent; cycles (defensive `seen` set in CV-msg); `message: null` interior nodes.

### 2.4 Message

| Field | Notes | Conf. |
|---|---|---|
| `id` | string (often equals the node id) | V |
| `author` | `{"role": "user"|"assistant"|"system"|"tool"|"function", "name": str|null, "metadata": {}}` | V |
| `create_time`, `update_time` | epoch seconds, may be null | V |
| `content` | polymorphic, see §2.5 | V |
| `status` | `finished_successfully` (default), `in_progress`, `error`, …; **may be omitted** in exports "from ~July 2026" | V (CV-msg comment) |
| `end_turn` | bool/null | V |
| `weight` | float (0 marks discarded/hidden nodes in some exports, I); may be omitted | V |
| `recipient` | `all` (to the user), or a tool name: `browser`, `python`, `dalle.text2im`, `canmore.create_textdoc`, `canmore.update_textdoc`, `web.run`, `bio`, … | V |
| `channel` | string/null (e.g. analysis vs final channel of reasoning models, I) | V (PZ-api field exists; values U) |
| `metadata` | see §2.6 | V |

### 2.5 `content` — polymorphic by `content_type`

| `content_type` | Shape | Conf. | Source |
|---|---|---|---|
| `text` | `parts: [string]` | V | CV-msg, PZ-api |
| `multimodal_text` | `parts: [string | image_asset_pointer | audio_asset_pointer | real_time_user_audio_video_asset_pointer | audio_transcription | …]` | V | PZ-api |
| `code` | `language`, `text` (assistant input to the code interpreter or a canvas payload; `recipient` tells) | V | PZ-api, CV-msg |
| `execution_output` | `text`; images arrive in `metadata.aggregate_result.messages[]` | V | PZ-api |
| `tether_quote` | `domain?`, `text`, `title`, `url?` | V | PZ-api, CV-msg |
| `tether_browsing_display` | `result`, `summary?` | V | PZ-api |
| `tether_browsing_code` | fields unknown ("unknown" in the types) | V (existence) / U (fields) | PZ-api |
| `sonic_webpage` | `url`, `domain`, `title`, `text`, `snippet`, `pub_timestamp` | D | CV-spec §4.2 E |
| `system_error` | tool execution error text (`name`, `text`) | D (type name V as a filter in CV-msg) | CV-spec |
| `user_editable_context` | `user_profile`, `user_instructions` — **custom instructions** | V | PZ-api |
| `model_editable_context` | `model_set_context` — **memory** as given to the model | V | PZ-api |
| `thoughts` | `thoughts: [{summary, content, chunks[], finished}]` — reasoning | V | PZ-api, CV-msg |
| `reasoning_recap` | `content: "Thought for 12s"` (+ `metadata.finished_duration_sec`, `reasoning_title`) | V | PZ-api |
| new/unknown | any other string; must be kept verbatim | — | — |

Non-string `parts` items (all V unless marked):

| Part | Fields |
|---|---|
| `image_asset_pointer` | `asset_pointer` (`file-service://<id>` legacy or `sediment://<id>`), `size_bytes`, `width`, `height`, `fovea`, `metadata.dalle{gen_id,prompt,seed,serialization_title}` (CV-msg strips both URL prefixes; PZ-api) |
| `audio_asset_pointer` | `asset_pointer`, `expiry_datetime`, `format`, `size_bytes`, `metadata{start_timestamp,end_timestamp,pretokenized_vq}` |
| `real_time_user_audio_video_asset_pointer` | `expiry_datetime`, `frames_asset_pointers[]`, `video_container_asset_pointer`, `audio_asset_pointer{…}`, `audio_start_timestamp` |
| `audio_transcription` | `decoding_id`, `direction` (`in`/`out`), `text` |
| `{"content_type":"text","text":…}` part | accepted by Loom's Python importer (I) |
| `search_result_group` / `search_result` (key `type`, not `content_type`) | `entries[]` with `ref_id{ref_type:"search",turn_index,ref_index}`, `title`, `url` (CV-msg `internal_citation_map`) |
| canvas payload as a **string part** (JSON `{"name","type","content"}`) when `recipient == "canmore.create_textdoc"` | V (CV-msg) |

### 2.6 Message `metadata`

V from PZ-api `MessageMeta` and CV-msg `MessageMetadata`:
`model_slug`, `parent_id`, `finish_details{type:"stop"|"interrupted", stop_tokens[]}`, `is_complete`,
`timestamp_` (`"absolute"`), `command` (`click|search|quote|quote_lines|scroll`), `args`,
`citations[]`, `_cite_metadata{citation_format{name}, metadata_list[{title,url,text}]}`,
`content_references[]`, `search_result_groups[]`, `attachments[{id,name,mime_type,size}]`,
`aggregate_result{code,final_expression_output,start_time,end_time,jupyter_messages[],messages[],run_id,status,update_time}`,
`invoked_plugin{namespace,…}`, `is_user_system_message`, `user_context_message_data` (dict of strings — custom
instructions text), `is_visually_hidden_from_conversation`, `is_thinking_preamble_message`,
`finished_duration_sec`, `reasoning_title`.
Recalled from memory of real exports and **not verified this session (I)**: `request_id`, `message_type`,
`default_model_slug`, `message_source`, `serialization_metadata`, `rebase_system_message`, `jit_plugin_data`,
`selected_sources`. The registry must therefore treat `metadata` as open: mapped keys are consumed, all others kept.

### 2.7 Citations and references

* Legacy `citations[]`: `{start_ix, end_ix, citation_format_type:"tether_og", metadata:{title,url,text,type:"webpage",extra{cited_message_idx,evidence_text}}}` — V (PZ-api). Index range refers to the message text.
* Newer `content_references[]`: `{type: grouped_webpages | sources_footnote | nav_list | alt_text | webpage | image_group | file …, matched_text, start_idx, end_idx, alt, items[], sources[], fallback_items[], safe_urls[], refs[], name, images[]}` — V (PZ-api; `type` is an open string).
* Inline markers in the text: `citeturn0search3` (with invisible private-use delimiters around it) — `matched_text` example V (PZ-api), "invisible or special Unicode placeholders" D (CV-spec §11). Key `turn{turn_index}search{ref_index}` joins them to `search_result_group.entries[].ref_id` (V, CV-msg).
* A conversation can have `search_result_groups` but **empty citations** ("ghost citations", D, CV-spec discovery guide).

### 2.8 Attachments and asset resolution

* User uploads: `metadata.attachments[{id, name, mime_type, size}]` on the *user* message; `id` looks like `file-<alnum>` (V, PZ-api/OC). Images uploaded in the same turn also show up as `image_asset_pointer` parts.
* Generated/inline images: `asset_pointer` = `file-service://<id>` (legacy) or `sediment://<id>` (current) — V (CV-msg). The stripped `<id>` is `file-XXXX…` (legacy) or `file_000000…` (sediment) — D.
* Resolution rule that a working parser uses (V, CV-io assets.py): (1) exact file name = id, (2) first file in the root whose name starts with the id, (3) same in `dalle-generations/`, (4) same in every `user-*/` directory. The prefix key is the name up to the first `_`/`.` (so `file-abc-original.png` and `file-abc-uuid.webp` both start with `file-abc`).
* Generated files can lack any stable link (the pointer and the file name only share a prefix); a byte hash is the only strong identity across copies (I).
* File names can carry the *original* upload name (`file-<alnum>-<original name>`), which allows a name-based fallback when the id has no file (I).

### 2.9 Authors, tools, hidden messages

`author.name` values seen in code/docs: `browser`, `python`, `dalle.text2im`, `bio` (memory tool), `web.run`, `web.search`, `file_search`, `canmore.*` (Canvas), plus plugin names (V/D). Messages that are internal
plumbing and that working viewers hide (V, CV-msg `is_message_hidden`): empty content; `is_visually_hidden_from_conversation`;
system messages except custom instructions (`is_user_system_message`); tool output except `tether_quote`; assistant messages whose
`recipient` is not `all`/`python`/null; `content_type` in `code`, `sonic_webpage`, `system_error`, `tether_browsing_display`, `thoughts`,
`reasoning_recap`. **A lossless import must keep them all**; "hidden" is a display attribute, not a reason to drop.

### 2.10 Canvas, memory, custom instructions

* Canvas: assistant `code`/`text` message with `recipient == "canmore.create_textdoc"`; payload is a JSON string `{"name","type","content"}` (V, CV-msg + convoviz `canvas.py`); updates use `canmore.update_textdoc` with a patch (D). Some exports add `textdocs/` (D).
* Custom instructions: `user_editable_context{user_profile,user_instructions}` and/or a system message with `metadata.is_user_system_message` and `user_context_message_data` (V).
* Memory: `model_editable_context.model_set_context` (V) and `bio` tool calls/outputs (D). A separate memory list is **not known** to be exported (U).

## 3. Anthropic — Claude export

### 3.1 Package layout

Requested in Settings → Privacy → Export data; e-mail link **valid 24 h** (CL-help, D). The help page lists no files.
Sources agree on:

| Member | Content | Conf. |
|---|---|---|
| `conversations.json` | JSON array of conversations | V (CL-zod, CL-proj) |
| `projects.json` | JSON array of projects (knowledge docs, prompt template) | V (CL-proj) |
| `users.json` | account details only | D (CL-proj, WS); fields U |
| `memories.json` | saved memory; in some exports only | D (CL-proj lists it; WS is contradictory: "memory is not included" vs "memories.json sits in the first batch zip") — structure U |
| several "batch" zips | very large accounts may be delivered in more than one zip | D (single WS mention) |
| uploaded files | **not known to be inside the ZIP**; messages carry only metadata (+ `extracted_content` of text attachments) | I |

Loom's catalog code already assumes a memories object with `conversations_memory` (string) and `project_memories`
(object of strings). That is an inference by an earlier session, not a verified structure (I).

### 3.2 Conversation object

V (CL-zod `ConversationItemSchema` / `IndividualChatSchema`; both `passthrough`):

| Field | Notes |
|---|---|
| `uuid`, `name`, `created_at`, `updated_at` | ISO-8601 strings (with `Z` or fractional seconds) |
| `account{uuid}` | owner (in `conversations.json`) |
| `summary` | optional string (newer) |
| `settings{preview_feature_uses_artifacts, preview_feature_uses_latex, preview_feature_uses_citations?, enabled_artifacts_attachments, enabled_turmeric?}` | optional |
| `is_starred`, `current_leaf_message_uuid`, `conversation_id`, `model`, `project_uuid`, `project`, `workspace_id` | optional, mostly in single-chat exports made by other tools; CL-proj states the official export has **no project id on conversations** (D) |
| `chat_messages[]` | see below |

### 3.3 Message

V (CL-zod): `uuid`, `index` (default 0), `sender` (`"human"` | `"assistant"`), `text` (may be absent or a flattened rendering), `content[]` (blocks),
`created_at`, `updated_at`, `truncated` (bool), `attachments[]`, `files[]`, `files_v2[]`, `sync_sources[]`, `parent_message_uuid`.
Branching: `parent_message_uuid` chains messages into a tree; `current_leaf_message_uuid` marks the shown leaf (both optional — older
exports are a plain linear array, I). The first message of a conversation has a null/zero-uuid parent in real data (I; not confirmed).

### 3.4 Content blocks (`content[]`)

| `type` | Fields | Conf. |
|---|---|---|
| `text` | `text`, `start_timestamp`, `stop_timestamp`, `citations[]` (element shape U) | V |
| `thinking` | `thinking`, `summaries[{summary}]`, `cut_off`, `start_timestamp`, `stop_timestamp` | V |
| `voice_note` | `title`, `text` | V |
| `tool_use` | `name`, `input{…}`, `message`, `integration_name`, `integration_icon_url`, `context`, `display_content`, `approval_options`, `approval_key`, `start_timestamp`, `stop_timestamp` | V |
| `tool_result` | `name`, `content[{type?,text?,uuid?,…}]` (inner types include `knowledge`, `rag_reference`), `is_error`, `message`, `integration_name`, `integration_icon_url`, `display_content`, timestamps | V |
| `image`, `document`, others | not seen in any export parser; the viewer turns unknown types into a JSON dump placeholder. The Anthropic Messages API has `image`/`document` blocks, so they are plausible | I |

### 3.5 Attachments, files, artifacts

* `attachments[]` (text uploads): `id?`, `file_name`, `file_size`, `file_type`, `extracted_content` (the full extracted text!), `created_at` — V. Keys are optional in newer formats; unknown keys appear.
* `files[]`/`files_v2[]` (images and other binaries): `file_kind`, `file_uuid`, `file_name`, `created_at`, `thumbnail_url`, `preview_url`, `thumbnail_asset{url,file_variant,primary_color,image_width,image_height}`, `preview_asset{…}` — V. The URLs point to claude.ai, not into the ZIP (I).
* Artifacts: (a) new format — `tool_use` block with `input{id,type,title,command,content,language,version_uuid,source,md_citations[]}` (`command` = create/update/rewrite, I) — V for the input keys; (b) older format — `<antArtifact identifier=… type=… title=…>…</antArtifact>` tags inside the message `text` — I (recalled, not verified this session). Versions of one artifact share `id`; each has a `version_uuid` (V).

### 3.6 Projects (`projects.json`)

V (CL-proj): array (or an object with `projects`) of `{uuid, name, description, prompt_template, created_at, docs:[{filename, content}]}`.
Recalled and **not verified (I)**: `updated_at`, `is_private`, `is_starred`, `creator{uuid,full_name}`, `docs[].uuid`, `docs[].created_at`.
Conversations are **not linked to projects by id** in the official export (D, CL-proj) — a project↔conversation link can only be *inferred*
(name/time/keywords) unless a conversation carries `project_uuid`.

### 3.7 `users.json`, `memories.json`

Unknown structure (U). Registry treats both as open objects: every leaf kept verbatim, `account`/`memory` entities created best-effort from key names.

## 4. Drift the sources report (design constraint)

* "OpenAI does not publish an official contract … the format evolves silently" (CV-spec) — new content types (`sonic_webpage`, `thoughts`, `reasoning_recap`), asset protocol (`file-service://` → `sediment://`), new root files (2025), shards (`conversations-NNN.json`), omission of `status`/`weight` (~July 2026), `voice` becoming an object, `title` null.
* Claude: fields "made optional for newer export formats" (CL-zod comments), unknown content types, `passthrough` everywhere.
* Therefore: unknown key ⇒ kept verbatim; unknown `content_type`/block `type` ⇒ kept verbatim and *guessed* from names (`*_asset_pointer` ⇒ media pointer, `text` key ⇒ text, `thinking|thought|reasoning` ⇒ reasoning), always marked `inferred`.

## 5. What only a real export can settle (checklist for the owner)

1. Real member list of the ZIP (names of every file, sizes) — especially whether `export_manifest.json`, `memories.json`, `textdocs/`, `projects*.json` or per-conversation folders exist.
2. Fields of `user.json`, `message_feedback.json`, `shared_conversations.json`, `users.json`, `memories.json`, `projects.json` (all `U`/`D` above).
3. Whether `conversations.json` is a bare array or wrapped; whether shards are used; the largest single conversation (memory bound).
4. Which `content_type` values and which `metadata` keys occur (`jq` one-liners are in CV-spec discovery guide; Loom prints the same census with `loom import --audit`).
5. How attachments are named in *your* ZIP versus the ids in `metadata.attachments` / `asset_pointer`: unlinked assets and unlinked pointers are reported by the audit.
6. Claude: do `files[]` bytes exist anywhere in the ZIP; do `citations` and `image`/`document` blocks occur; is `parent_message_uuid` filled; are project docs duplicated across projects.
7. Timestamp forms (epoch float vs ISO string; time zones), duplicate message ids across conversations, lone surrogates / invalid UTF-8 in strings.

## 6. How provider-inspired interface profiles can consume the registry (not built here)

The registry (`loom/data/exports/formats.json`) names, for every provider structure, the abstract role it plays
(conversation, branch, message, content block, attachment, citation, tool call, reasoning block, project, memory, custom
instruction, account). A later profile layer can therefore be **pure data on top of the same vocabulary**:
a profile lists which block kinds render as what widget (e.g. `reasoning` ⇒ collapsible "Thought for Ns", `tool_call` ⇒ tool chip,
`attachment` ⇒ file chip linking to the ZIP member), how branches are navigated (`branch` ⇒ "< 2/3 >" switcher driven by
`loom_x_links(rel='alternative')`), which conversation list grouping to use (project, starred, archived), and which
hidden-by-default classes the original app hides (registry `visibility` flags). Because attachments are stored as locators into
the ZIP (or copies) and every entity carries its `origin` provider/version, a profile can render an archived export the way the
source app did without any provider-specific code. Profiles must not copy the vendors' graphics or text (R17 copyright note).

## 7. Implementation notes

See `docs/exports/EXPORT_REGISTRY_AND_AUDIT.md` (written with the implementation) for the registry schema, the lossless
accounting rule, the coverage table (before/after) and the CLI.
