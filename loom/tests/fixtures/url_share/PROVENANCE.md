# ChatGPT share HTML structural fixtures — 2026-11-01

`chatgpt_next_data.html` and `chatgpt_turbo_stream.html` contain **fictional
messages and identifiers** in publicly evidenced provider structures. They are
not captures of the owner's chats, production data, or the existing ChatADHD
JSON contract. No conversation was fetched from a provider during this task.

Public evidence read on 2026-11-01:

- [OpenAI sharing documentation](https://help.openai.com/en/articles/7925741-sharing-conversations-and-scheduled-tasks-in-chatgpt):
  shared snapshots can contain a conversation or one response; access can be
  workspace restricted. This is evidence of sharing semantics, **not** an
  official versioned HTML/API schema.
- [Provider HTML parser maintained by its author](https://github.com/evanhu1/chatgpt-share-parser/blob/main/src/index.ts):
  `extractLoaderPayload`, `parseModernShare`, and `parseLegacyShare` document
  the enqueue string, loader route, `serverResponse.data`, and Next.js wrapper.
  `parseConversationData` reads `mapping`, `linear_conversation`, and
  `message.author.role`; content and attachment parsing identifies `parts`,
  `asset_pointer`, and metadata attachment fields. Inspected as public format
  evidence; no dependency installed or code copied.
- [turbo-stream v2.4.1 unflatten](https://github.com/jacob-ebey/turbo-stream/blob/v2.4.1/src/unflatten.ts)
  and [sentinels](https://github.com/jacob-ebey/turbo-stream/blob/v2.4.1/src/utils.ts):
  pool slots, reference-valued containers, indexed object keys, null/undefined.
  This is the format implementation's primary source. Our parser handles its
  JSON subset and refuses unavailable/deferred conversation structures.

Fixture construction: the Next.js file wraps a fictional five-node mapping
(one structural root, four messages, two assistant branches) plus an explicit
current node and linear path. The other file independently encodes the same
data into a scalar-deduplicating slot pool; strings inside two enqueue calls
split the pool across script boundaries. Object keys and container values are
references; primitive pool slots are literals. Neither file contains executable
behavior required by a test. Escaped `<` in JSON is decoded as data, never HTML.

The synthetic text exercises Unicode, literal markup, whitespace, an empty tool
message, a text URL, a private-address attachment URL and a provider asset
pointer. Both attachments must remain references; neither is fetched. JSON
pointer escaping is tested with provider node IDs containing `/` and `~`.

These fixtures establish reproducible parsing of the evidenced wire shapes.
They do not establish that every current ChatGPT share page uses those shapes,
that public TLS works in this environment, or that login/protection can be
bypassed. Modern deferred frames, future stream tags in conversation fields,
unrecognized routes, Claude HTML shells and ambiguous branches remain explicit
unsupported/partial cases with the existing file-import fallback.
