// Private to src/tool_sources: the factory of the MCP kind and the shared helpers.
#pragma once

#include <memory>
#include <string>

#include "loom/result.h"
#include "loom/tool_sources.h"

namespace loom::tool_sources {

// The `mcp` kind: id, the source's settings object, the environment.
Result<std::unique_ptr<ToolSource>> make_mcp(const std::string& id, const Json& cfg, const ToolSourceEnv& env);

// The profile `tool_sources` of the data directory (values).
Result<Json> profile_values(Runtime& rt);

}  // namespace loom::tool_sources
