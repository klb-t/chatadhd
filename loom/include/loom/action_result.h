// loom/action_result.h — the result of an action run lands in the graph (GRAF_HUB_KOD_SILNIK §5-§6).
//
// The graph is the hub, the code is the engine: when a run completes, each result part the run produced
// (text, image, audio, video) becomes a FORM node of the class that matches its modality (data/graph/
// classes.pack: text_form, image_form, audio_form, video_form), linked
//
//   form --expresses-->    content   ONE content node per source element, shared by every later run on the same
//                                    source (get-or-create, never rewritten); the weight is the fidelity of the
//                                    form (what the action preserved, condensed ...) and the link says what was
//                                    kept, lost or added; the source element is linked to the same node, so a
//                                    search for the content finds it in every form;
//   form --derived_from--> source    the element the action was applied to (message, conversation, node, blob);
//   form --produced_by-->  run       an `action_run` node: action id, run id, step, model / adapter, usage
//                                    receipts. References and hashes only: never a secret, never bytes;
//   form --stored_as-->    blob      the blob node that holds the bytes of a media result or of a text result
//                                    that was too large to keep in the run document.
//
// Where results go is a setting with presets (runtime profile `action_results`): `graph` (preset), `run_only`
// (the result stays in the run document, as before) and `conversation_message` (the text is also posted as a
// message of the conversation, and that message is the form node). The setting `action_result_target` is the
// user's level of a value that resolves through the scope chain like the rules do (R47): installation preset ->
// user -> a rule on a project or a conversation; the answer says which scope set it.
//
// Large text results (above `inline_max_bytes`, a preset) go to the blob store; the run document keeps a
// reference (blob_hash, size, preview) instead of the text. That holds for every target.
//
// A failure to store never fails the run: the model has already been paid for. It is reported in
// run.result.produced.issues. A run on a credential (sensitive) stores nothing anywhere.
#pragma once

#include <string>
#include <string_view>

#include "loom/action_rules.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class BlobStore;
class Config;
class Database;
class EventBus;
}  // namespace loom

namespace loom::actions {

inline constexpr std::string_view kProducedSchema = "loom.action_produced/1";
inline constexpr std::string_view kRunNodeSchema = "loom.action_run_node/1";
inline constexpr std::string_view kFormNodeSchema = "loom.action_result_form/1";

struct ResultServices {
  Database* db = nullptr;
  BlobStore* blobs = nullptr;
  const Config* config = nullptr;  // the user's level of the target (profile target.setting)
  const Engine* engine = nullptr;  // the scope chain; without it only the installation preset and the user's setting count
  EventBus* bus = nullptr;         // MSG_CREATED for a message the target posts
  Json profile;                    // values of the runtime profile `action_results`
};

// The scope a run is asked in: its request's scope, else the conversation of its context.
Json scope_of_run(const Json& run);

// The preset id that applies to this run (graph | run_only | conversation_message | a user preset), with the
// scope that set it.
Result<ScopedValue> result_target(const ResultServices& services, const Json& run);

// A completed run: big text to the blob store, the results into the graph / the conversation as the target
// says. Adds run["result"]["produced"] ({schema, target, items:[{form, content, ...}], run_node, issues}).
Status store_run_results(const ResultServices& services, Json& run);

// The text of a part of a stored run document: inline, or read from the blob store where it was moved.
Result<std::string> part_text(BlobStore* blobs, const Json& part);

}  // namespace loom::actions
