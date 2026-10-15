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

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
class Database;
}  // namespace loom

namespace loom::chat::formats {

// What the axes resolved to for one request.
struct Axes {
  std::string context_format = "conversation";
  std::string reply_format = "text";
  bool turns = true;           // the context goes as chat turns
  bool packet = false;         // the context goes as a graph packet
  bool reply_graph = false;    // the model is asked for loom.graph_reply/2
  std::string model_class;     // cheap | mid | frontier | unknown (only for a graph reply)
  std::string flow;            // json_schema | prompt_only (only for a graph reply)
  std::string flow_reason;     // why this flow (class flow, capability, ...)
  Json section = nullptr;      // profile guardian, context_format
  bool plain() const { return turns && !packet && !reply_graph; }
};

// The values come from the scope chain; `override_*` (a per-send option) win over it. Errors are the profile's; the caller degrades to plain.
Result<Axes> resolve(const std::filesystem::path& root, const Config& cfg, Database* db, const std::string& conv_id,
                     const std::string& model, const std::optional<std::string>& override_context,
                     const std::optional<std::string>& override_reply, bool reply_allowed);

struct Entry {
  std::string id, role, text, parent_id;
  double weight = 1.0;
  bool selected_level = false;  // the text is a lossy level chosen by the selector, not the raw one
};

// loom.context_graph/1: nodes (messages with provenance) and links (follows, reply_to), packet_id = sha256 of the rest.
Json packet(const Json& section, const std::string& conv_id, const std::vector<Entry>& entries);
// The system block that carries the packet: recipe + the packet as JSON.
std::string packet_block(const Json& section, const Json& packet);
// The instruction of a graph reply (recipe + the id the reply echoes + the schema when the flow has no response format).
std::string reply_instruction(const Json& section, const std::string& flow, const std::string& packet_id);
// Adds what the flow puts in the payload (response_format, provider constraints).
void apply_reply_request(Json& payload, const Json& section, const std::string& flow);

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

// Writes the nodes and links of a valid reply into the graph with provenance to the message. Returns a summary (counts, ids).
Result<Json> embed(Database& db, const Json& section, const std::string& message_id, const std::string& conv_id,
                   const std::string& model, const Reply& reply);

}  // namespace loom::chat::formats
