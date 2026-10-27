// loom/platforms.h — data-driven table of export platforms (B-MOD-05, R42).
//
// Names such as "chatgpt"/"claude"/"openai"/"anthropic" live only in the
// compiled import preset (data/presets/import.pack, section "platforms").
// Code asks the table: which platform does this element belong to, which
// provider handlers parse it, which walker reads its messages, what unit kind
// does it yield. Adding a platform = one preset entry (+ a walker/provider
// adapter only if its structure is new).
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "loom/util/json.h"

namespace loom {

struct PlatformSpec {
  std::string id;          // stored unit kind / sniff result ("chatgpt", "claude_projects", ...)
  std::string vendor;      // catalog "platform" attribute
  std::string provider;    // export ProviderHandlers name ("" = no lossless provider mapping)
  std::string unit_kind;   // "conversation" | "project" | "memory" | ...
  std::string walker;      // message walker adapter ("" = not a chat); names the element's message container key
  std::string id_field;    // element field holding the external id
  std::string id_literal;  // fixed external id (overrides id_field)
  std::string shard_prefix;                        // "<prefix>NNN.json" conversation shards
  std::string shard_pattern;                       // regex of a shard name ("" = any name starting with shard_prefix)
  std::vector<std::vector<std::string>> match;     // AND over groups, each group an OR of present keys
  std::map<std::string, std::string> shape;        // key -> "object" | "array" (type required for lossless parse)

  bool is_chat() const noexcept { return !walker.empty(); }
  bool matches(const Json& element) const;
  bool shape_ok(const Json& element) const;
  std::string ext_id(const Json& element) const;
};

// Table in preset order (= sniff order). Throws std::runtime_error when the
// compiled preset's platforms section is malformed.
const std::vector<PlatformSpec>& import_platforms();
const PlatformSpec* find_platform(std::string_view id);
const PlatformSpec* sniff_platform(const Json& element);  // first match, nullptr if none / not an object
const PlatformSpec* platform_for_unit(std::string_view unit_kind, std::string_view vendor);
const PlatformSpec* platform_for_shard(std::string_view base);
const PlatformSpec* platform_for_provider(std::string_view provider);

}  // namespace loom
