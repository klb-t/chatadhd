// loom/settings_fields.h — field metadata of every settings key (config.json and secrets.json).
//
// Data lives in data/runtime/settings_fields.pack (RuntimeProfile domain "settings_fields").
// Per field: default presence ("present"/"absent"), the default value when present (read from
// the real defaults, never copied into the pack) and an open-string edit label derived from
// general rules {scope,target,action:"edit",effect}; no rule = allow = "optional"
// (optional / required / read_only / requires_confirmation / any other string).
// Secret values are never part of the result.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "loom/config.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

// {"schema":"loom.settings_fields/1","edit_rules":[...],"fields":[{"key","scope","group","type",
//  "default":"present|absent","default_value"?,"edit","description","sensitive"?}]}
// `data_dir` selects the user overlay profile (profiles/settings_fields.pack); empty = builtin.
Result<Json> settings_fields(const std::filesystem::path& data_dir = {});

// Mismatches between the metadata and the real defaults/secret keys; empty = consistent.
// Used by the completeness test and available to tooling.
std::vector<std::string> settings_fields_problems(const Json& fields_doc);

// Edit rule of a key ("" when the key is not described). Open string.
std::string settings_edit_rule(std::string_view scope, std::string_view key);

// Rejects a config write when the key's rule is enforced (read_only). Unknown keys are writable.
Status check_config_write(std::string_view key, const std::filesystem::path& data_dir = {});

}  // namespace loom
