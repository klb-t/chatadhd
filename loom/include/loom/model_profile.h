#pragma once

#include <string>
#include <string_view>

#include "loom/runtime_profile.h"

namespace loom::model {

// Creation policy is separate from historic decoding. The producer supplies
// identity, type, origin and actual evidence. Metadata is a companion receipt,
// not part of the model record: an inserted confidence is a creation prior,
// never a calibrated measurement or evidence of validation.
struct CreationRecord {
  Json record;
  Json applied_defaults = Json::object();  // JSON pointer -> inserted value
  std::string profile_hash;
};

// Supported record kinds and trusted decoder/nesting capabilities. No code is
// loaded from profile data. Explicit fields override priors; explicit null for
// a policy field is invalid, rather than a request for a historic fallback.
Json creation_capabilities();
Result<CreationRecord> normalize_creation(std::string_view kind, const Json& record);
Result<CreationRecord> normalize_creation(std::string_view kind, const Json& record,
                                          const RuntimeProfile& profile);

}  // namespace loom::model
