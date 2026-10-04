# Application profile source audit — 2026-10-04

The two versioned profiles target **LibreChat itself at 0.8.8** and **NextChat
itself at 2.16.1**. These are third-party applications with their own behavior.
Their release numbers do not identify versions of ChatGPT, Claude or Gemini.
The bundled vendor-inspired examples continue to have an unknown original-app
version; they do not inherit fidelity from these clone references.

This audit read files through the GitHub connector at the full commits below.
It did not copy third-party implementation code into this repository, launch
either reference application, call a model or measure visual equivalence. Both
new profiles declare `evidence.status = partial` and include their omissions.

## Exact source identity

| Profile | Release | Full source commit | License at that commit |
|---|---|---|---|
| `librechat-0.8.8` | [v0.8.8](https://github.com/LibreChat-AI/LibreChat/releases/tag/v0.8.8) | [e8f3be08623663d4ad7f7241e693c94469b63bb0](https://github.com/LibreChat-AI/LibreChat/commit/e8f3be08623663d4ad7f7241e693c94469b63bb0) | [MIT](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/LICENSE) |
| `nextchat-2.16.1` | [v2.16.1](https://github.com/ChatGPTNextWeb/NextChat/releases/tag/v2.16.1) | [557a2cce357749c6fb3176d42e03ff6f7de4d355](https://github.com/ChatGPTNextWeb/NextChat/commit/557a2cce357749c6fb3176d42e03ff6f7de4d355) | [MIT](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/LICENSE) |

`profile_revision = 1` versions the local mapping independently of the source
release. A pin establishes which code was inspected; it does not establish
successful execution or exact rendering. UI choices, context assembly, model,
provider, tools and permissions remain independently selectable.

## LibreChat mapping

| Feature | Pinned primary source | Observed behavior and local mapping |
|---|---|---|
| Composer default | [settings.ts](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/store/settings.ts) | `enterToSend` starts true. The profile uses `enter`. |
| Composer execution | [shortcuts.ts](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/utils/shortcuts.ts), [useTextarea.ts](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/hooks/Input/useTextarea.ts) | Submission depends on composition, shortcuts and run state. Shift ordinarily retains a newline; Ctrl/Meta, custom overrides and during-run actions have additional semantics. Only the simpler composer mapping is implemented. |
| Message layout | [MessageRow.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Chat/Messages/ui/MessageRow.tsx) | User turns align right in a rounded surface; ordinary assistant bodies are plain. `user-bubble` preserves that role distinction. Source avatars, timestamps, system rows and parallel content remain richer. |
| Content width | [MessageRow.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Chat/Messages/ui/MessageRow.tsx), [ChatForm.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Chat/Input/ChatForm.tsx) | Ordinary content uses responsive `md:max-w-3xl` and `xl:max-w-4xl`, with wider parallel/full-width paths. The local 896px choice projects the XL 56rem width at 16px/rem; it is not a responsive replica. |
| Sidebar | [constants.ts](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/UnifiedSidebar/constants.ts), [UnifiedSidebar.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/UnifiedSidebar/UnifiedSidebar.tsx) | Expanded minimum is 360px; absent saved width, initialization uses that minimum. Collapsed width is 52px; resize and mobile drawer paths exist. The profile selects a left 360px sidebar only. |
| New chat / stop | [NewChat.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Chat/Menus/NewChat.tsx), [StopButton.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Chat/Input/StopButton.tsx) | Controls exist in the source. They map to Loom `chat.create` and `chat.cancel`, without claiming the same server/run lifecycle. |
| Rename | [RenameForm.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Conversations/RenameForm.tsx) | The form submits a title. `chat.rename` is the local equivalent; source focus and inline-form behavior are not reproduced. |
| Edit / rerun | [EditMessage.tsx](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/components/Chat/Messages/Content/EditMessage.tsx) | Save and rerun are separate actions; edited user reruns and assistant reruns take different paths. `message.edit` maps only editing. Complete rerun/branch semantics remain a gap. |
| Dark palette | [style.css](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/client/src/style.css) | Profile colors project dark primary background, tertiary user surface, primary/secondary text, purple brand accent and light border. Component surfaces are folded into six tokens. |

## NextChat mapping

| Feature | Pinned primary source | Observed behavior and local mapping |
|---|---|---|
| Composer default | [config.ts](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/store/config.ts) | Default is `SubmitKey.Enter`; Alt/Ctrl/Shift/Meta alternatives are distinct values. The profile selects `unmodified-enter`. |
| Submission / hints | [chat.tsx](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/chat.tsx), [en.ts](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/locales/en.ts) | Default Enter rejects modifier chords and composition. The source additionally handles Safari keyCode 229, prompt hints, colon commands and last-input recall. The local placeholder is generic because those features are not wired. |
| Message layout | [chat.module.scss](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/chat.module.scss) | User rows reverse direction. Both roles use rounded bordered message items, with a distinct user surface. `bubble` preserves the broad shape; exact surfaces, avatar headers and measurements remain partial. |
| Sidebar / sizing | [constant.ts](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/constant.ts), [home.module.scss](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/home.module.scss), [sidebar.tsx](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/sidebar.tsx) | Default sidebar width is 300px; source supports dragging/narrow mode and mobile layouts. Local content width 900px is a desktop projection from 1200px minus 300px, not an observed fixed source canvas. |
| New / select | [sidebar.tsx](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/sidebar.tsx), [chat-list.tsx](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/chat-list.tsx) | New sessions and selection have source handlers. Local adapters use Loom conversations and stable IDs rather than source session indices. |
| Stop / rename / edit | [chat.tsx](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/chat.tsx) | Source Stop stops a pending controller pool; rename updates session topic; text edit replaces stored content. Local stop is scoped to its request, while Loom edits preserve prior versions. These are explicit differences. |
| Retry | [chat.tsx](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/components/chat.tsx) | Source `onResend` removes the selected user/assistant rows and resends. No local Retry capability or transition is advertised; mapping it to ordinary Send would misstate behavior. |
| Dark palette | [globals.scss](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/app/styles/globals.scss) | Colors project source dark background, user surface, text and primary accent. Muted reuses text; the source white border alpha 0.192 is rounded to hex alpha `31`. This selects a dark projection, not the source default theme. |

## Workflow provenance and engine boundary

The shared `conversation-review` FSM is a **Loom extension** combining a
source-style new conversation with Loom knowledge inspection and a return to
the created conversation. It is labelled as such in both evidence gaps and
the `Inspect Loom knowledge` action. It is not extracted as a native workflow
from either clone.

Creation binds an explicit title payload and saves only the successful result's
`/id` as workflow variable `conversation_id`. Inspection passes an empty object.
Return binds that saved ID to `chat.select`; the user need not manually copy a
UUID between steps. Failure must not advance the FSM. Workflow variables and
their trace identify this local execution, not a source application's session.

Normal create/select/send/stop/rename/edit controls invoke the existing Loom
adapters. Selection of this UI profile neither creates a source account nor
installs the clone backend. Source authentication, storage, context compression,
agent execution, artifacts, tools, provider protocol details and recovery rules
require separately tested adapters. The profile cannot reproduce unavailable
service features simply by drawing their buttons.

The profile colors, sizes and composer keys are replaceable data. None of the
source differences authorizes deleting imported or prior Loom history, changing
the selected model or silently replacing the owner's context policy.

## Wider references and limits of the clone claim

- [LibreChat README](https://github.com/LibreChat-AI/LibreChat/blob/e8f3be08623663d4ad7f7241e693c94469b63bb0/README.md) describes an interface inspired by ChatGPT and a platform with its own integrations. It does not supply an original ChatGPT version catalogue.
- [NextChat README](https://github.com/ChatGPTNextWeb/NextChat/blob/557a2cce357749c6fb3176d42e03ff6f7de4d355/README.md) describes its own cross-platform application, model integrations and prompt masks. A model API integration is not a native-app backend.
- [Chatbot UI](https://github.com/mckaywrigley/chatbot-ui) explicitly preserves its own 1.0 implementation on `legacy` alongside 2.0. This is useful version evidence for that project, not ChatGPT version evidence; its current branch was reviewed, not pinned for a bundled profile.
- [Gemini-Clone](https://github.com/GourangaDasSamrat/Gemini-Clone) describes an educational Gemini API application. Its "Gemini 2.0 flash" label refers to the model. No original-app version mapping was established, so it is not used as a verified versioned target.
- [Open WebUI license documentation](https://docs.openwebui.com/license/) distinguishes BSD-3-Clause through v0.6.5 from branding conditions beginning v0.6.6. Any future reuse must record the actual selected revision and its license rather than assume public GitHub code has identical terms.

The reviewed projects establish that useful references exist. They do not
establish that every app, every historical version or every native workflow has
a complete clone. New target apps and versions therefore need their own source
pins, observed workflow map, capability gaps and conformance evidence.
