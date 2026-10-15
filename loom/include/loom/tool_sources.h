// loom/tool_sources.h — tool sources as an adapter (PRZEGLAD_STRAZNIK_SRODOWISKA 3.3, step 8; S2/R34).
//
// A tool source is an external server whose tools the user wants as actions. The first kind is MCP (Model Context Protocol:
// JSON-RPC 2.0, initialize / tools/list / tools/call) over stdio (a spawned process) or Streamable HTTP. More kinds are one
// ToolSourceRegistry::add() plus the data in the profile `tool_sources`.
//
//   setting `tool_sources`   id -> {kind, transport, command/args | url, access: {secret: <vault key name>}, env: {NAME: {secret}},
//                            effects?, tools?: {name: {effects?, hidden?}}, enabled?, label?}.  A reference to the secrets vault,
//                            never secret bytes.  Nothing is contacted until the user adds a source and asks for it to be listed.
//   sync                     contacts the source(s), lists the tools and writes ONE action per tool into the user overlay
//                            profiles/actions.pack (never the shipped pack): id `tool.<source>.<tool>`, port `tool_source`, action
//                            `call:<source>:<tool>`, params from the input schema.  Effects are UNKNOWN (preset: every flag worst
//                            case + effects_unknown:true) so the shipped rule confirm:tool_source.unknown asks before each run.  The
//                            hints a server gives are kept as `hints` and never lower the preset.
//   trust                    the user relaxes (or tightens again) one tool or one source: a rule in the user overlay action_rules.pack.
//   remove                   takes the source, its actions, its rules and its graph nodes away.
//   call                     the executor behind the interpreter: ActionRunner::invoke_port("tool_source", "call:<source>:<tool>", args).
//                            Rules, x10, limits, receipts and the trace belong to the interpreter; this is only the port.
//   graph                    a source is an `environment:service:tool_source:<id>` node of class service_environment, a capability
//                            --provided_by--> the port node `port:tool_source`, and an access (a reference to a vault key) --stored_in-->
//                            `vault:secrets`.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "loom/adapter_registry.h"
#include "loom/result.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::tool_sources {

inline constexpr const char* kPort = "tool_source";
inline constexpr const char* kSyncSchema = "loom.tool_sources_sync/1";
inline constexpr const char* kListSchema = "loom.tool_sources_list/1";
inline constexpr const char* kCallSchema = "loom.tool_source_call/1";

struct ToolDecl {
  std::string name, title, description;
  Json input_schema = Json::object();
  Json annotations = Json::object();  // hints of the server; recorded, never trusted
};

struct ToolOutput {
  bool is_error = false;       // the tool says it failed (MCP isError)
  std::string text;            // the text blocks joined
  Json content = Json::array();
  Json structured;             // structuredContent when the server sends it (null otherwise)
};

// The port every kind implements. A source object is cheap; each operation opens its own session.
class ToolSource {
 public:
  virtual ~ToolSource() = default;
  virtual std::string kind() const = 0;
  virtual Result<std::vector<ToolDecl>> list_tools(const CancelToken* cancel) = 0;
  virtual Result<ToolOutput> call_tool(const std::string& name, const Json& arguments, const CancelToken* cancel) = 0;
};

struct ToolSourceEnv {
  Runtime* rt = nullptr;  // http transport, secrets (has/get_string), profile root
};

using ToolSourceRegistry = AdapterRegistry<ToolSource, ToolSourceEnv>;
// The built-in kinds (mcp). Selector key "kind".
ToolSourceRegistry& registry();

// loom_invoke port "tool_source": list | sync | add | remove | trust | call[:<source>:<tool>] | tools.
Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request, const CancelToken* cancel = nullptr);

// Action id of a tool (`tool.<source>.<tool>`, characters outside [A-Za-z0-9_.-] replaced by '_').
std::string action_id(Runtime& rt, const std::string& source, const std::string& tool);

}  // namespace loom::tool_sources
