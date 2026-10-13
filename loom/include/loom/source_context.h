// Explicit native preparation of transient source observations. This is an
// in-memory result, not a persisted chat request or a disclosure capability.
#pragma once

#include "loom/context_engine.h"

namespace loom {

struct SourceFragmentRequest {
  std::string conversation_id;
  std::string unit_id;
  std::vector<std::size_t> message_indices;
  std::optional<std::string> expected_version;
};

struct SourceContextRequest {
  context::ContextRequest context;
  std::vector<SourceFragmentRequest> fragments;
  std::string profile_user; // existing native R40 profile, never implicitly opened
  Json read_options = Json::object();
  Json source_profile_overrides = Json::object();
};

struct PreparedSourceContext {
  model::ContextSet context_set; // includes text; memory only
  std::string prompt;           // includes text; memory only
  Json receipt;                // strict metadata projection; no source/query text
};

} // namespace loom
