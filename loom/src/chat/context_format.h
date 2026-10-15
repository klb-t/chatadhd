// Source-private: the two orthogonal chat formats (P27). Everything here is a function of data in the runtime profile guardian
// (context_format): the preset names, the recipes, the reply schema, the flows by model class, the texts.
//
//   chat_context_format  conversation (preset) | graph | both   how the context the selector chose is given to the model
//   chat_reply_format    text (preset) | graph                   how the model is asked to answer (loom.graph_reply/2)
//
// Both are values of the scope chain (scopes::Resolver): conversation -> units -> user -> installation. The presets leave the request
// bytes unchanged. The model of the conversation never decides a setting; the flow of a graph reply follows the class of the model
// (cheap | mid | frontier | unknown) as data.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Database;
}  // namespace loom

namespace loom::chat::formats {

// What the flow resolved to for one request.
struct Axes {
  std::string flow = "plain";      // name of the flow (data: context_io.flows)
  std::string renderer = "chat_turns";
  std::string output = "text";
  bool turns = true;               // the selection is rendered as chat turns
  bool packet = false;             // the selection is rendered as a graph packet
  bool reply_graph = false;        // the answer is asked for as loom.graph_reply/2
  std::string mode;                // response_format | prompt_only (only for a graph reply)
  std::string model_class;         // cheap | mid | frontier | unknown
  std::string reason;              // why this flow / mode
  Json section = nullptr;          // profile guardian, context_io
  bool plain() const { return turns && !packet && !reply_graph; }
};

// The flow: an override of the request, else the scope chain (setting chat_flow), else the flow of the model's class (data). Errors are
// the profile's; the caller degrades to plain, visibly (log).
Result<Axes> resolve(const std::filesystem::path& root, const Config& cfg, Database* db, const std::string& conv_id,
                     const std::string& model, const std::optional<std::string>& override_flow, bool reply_allowed);

struct Entry {
  Message msg;                  // the row, whole
  std::string text;             // the text of the selection (raw, or a lossy level chosen by the selector)
  bool selected_level = false;
};

// loom.context_graph/1: nodes (messages with provenance) and links (follows, reply_to), packet_id = sha256 of the rest.
// The SELECTION: every node and graph link keeps its source (table, id, position, the whole row) so that the embedder can put it back where it came
// from. `inputs` records what the selector read (representation, flow, settings). packet_id = sha256 of the rest.
Json packet(const Json& section, const std::string& conv_id, const std::vector<Entry>& entries, const std::vector<Link>& graph_links, const Json& inputs);
// A renderer may drop parts of the selection (renderers.<id>.drop, e.g. "source"); the selection itself never does.
Json render_packet(const Json& section, const std::string& renderer, const Json& packet);
// The system block that carries the packet: recipe + the packet as JSON.
std::string packet_block(const Json& section, const Json& packet);
// The instruction of a graph reply (recipe + the id the reply echoes + the schema when the flow has no response format).
std::string reply_instruction(const Json& section, const std::string& mode, const std::string& packet_id);
// Adds what the flow puts in the payload (response_format, provider constraints).
void apply_reply_request(Json& payload, const Json& section, const std::string& mode);

struct Reply {
  bool ok = false;
  std::string error;         // machine reason when !ok
  Json graph = nullptr;      // the validated loom.graph_reply/2
  std::string text;          // the rendered public text (root)
  bool fence_stripped = false;
  std::map<std::string, std::string> rendered;  // local node id -> its rendered text
};
// Parse + mechanical validation + the deterministic renderer (ordered exact concatenation of the children).
Reply parse_reply(const Json& section, const std::string& model_text, const std::string& packet_id, const std::set<std::string>& prior_ids);
// The visible note for an invalid reply (data text).
std::string invalid_note(const Json& section, const std::string& lang, const std::string& reason);

// PLACEMENT (an analysis-like step, not part of the embedder): where each new element of a reply goes. Cascade as data; here explicit refs and
// deterministic keys. Returns {nodes: local->id, links: [{from,to,predicate,placed_by}], unplaced: [...], cascade}.
Json plan_placement(const Json& section, const std::string& message_id, const Reply& reply, const std::set<std::string>& prior_ids);
// OUTPUT node embedder (inputs."loom.graph_reply/2".embedder = reply_graph): PURE STRUCTURE, no model, no analysis. It applies a placement plan: writes
// the nodes and links of a valid reply into the graph with provenance to the message. Deterministic ids and upserting links make it idempotent.
Result<Json> embed(Database& db, const Json& section, const std::string& message_id, const std::string& conv_id,
                   const std::string& model, const Reply& reply, const Json& plan);

// INPUT node embedder (inputs."loom.context_graph/1".embedder = packet_messages): the inverse of the selector on its image. Every message of the
// packet is matched to the graph (by id, else by an earlier embedding of the same packet node); a match changes NOTHING (a lossy text_form is
// never written over the raw text); what is new is added to the conversation as a message with provenance metadata.embedded = {packet_id, node,
// origin}. So select(embed(select(G))) == select(G) and embedding twice adds nothing. Parsers that need a model are not part of this guarantee.
Result<Json> embed_packet(Database& db, const Json& section, const std::string& conv_id, const Json& packet);

}  // namespace loom::chat::formats
