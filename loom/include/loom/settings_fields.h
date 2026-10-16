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
//  "default":"present|absent","default_value"?,"edit","description","sensitive"?,
//  "human":{"label":{pl,en},"why":{pl,en},"effect":{pl,en},"levels":[...],"group","subgroup"?,"role"?,"role_status"?,"flows"?,"technical_only"?},
//  "values"?:[{"value","meaning"?,"status"?,"reason"?}],"values_open"?,"status"?:{"state","by"?,"reason":{pl,en}}}],
//  "groups":{id:{"label":{pl,en},"why":{pl,en},"order","subgroups":[...],"origin","provenance":[...],"hidden"?,"hidden_by"?,"disabled"?,"empty_reason"?}},
//  "groups_issues"?:[...], "level_evidence":{"chain":[keys],"turn":[keys]},
//  "profile_values":[{"profile","pointer","human":{...},"value"|"problem"}]}
// `human`, `groups` and `profile_values` are data of the pack (M1-D0): an overlay may regroup, rename or hide.
// `groups` is the effective list of the view kind `settings_groups` (graph_views.pack): the pack's /groups (with the user's profile
// overlay, which may add a group) is the ROM layer and the view overlays of `data_dir` (views/*.json) are composed on top with the
// kind's policy, each group carrying origin and provenance; a hidden group stays listed with hidden:true and hidden_by, so the
// fields in it are never orphaned; bad overlay ops are `groups_issues`. `status` (field level) says a key has a preset but no
// reader yet (shown as "planned (reason)"); `level_evidence` names the keys whose readers honour project/conversation (scope
// chain) and turn (per-request option) — the only keys that may declare those levels.
// A profile value is read from its runtime profile (with the user's overlay) at the JSON pointer; when it cannot be
// read the entry carries `problem` instead of `value` (never an exception: a pointer is user data). `data_dir` selects the user
// overlay profile (profiles/settings_fields.pack) and the view overlays; empty = builtin.
Result<Json> settings_fields(const std::filesystem::path& data_dir = {});

// Mismatches between the metadata and the real defaults/secret keys, and of the human layer: every field and
// profile value has `human` with label/why/effect in pl and en, a group of `groups` (and a subgroup of it when
// named), levels inside the scope chain (graph/scopes.pack through the rules engine of `data_dir`; empty levels only
// on a read_only field, which is settable nowhere and whose rule carries the reason; project/conversation only for a key of
// `level_evidence.chain`, turn only for a key of `level_evidence.turn`), a `role` known to ports.pack or carrying `role_status`,
// a `status` with state and reason pl+en, no `groups_issues`; every group holds something or says why it is empty
// (`empty_reason`); every profile value resolves. Empty = consistent.
// Used by the completeness test and available to tooling; each entry is a reason the owner can read.
std::vector<std::string> settings_fields_problems(const Json& fields_doc, const std::filesystem::path& data_dir = {});

// Edit rule of a key ("" when the key is not described). Open string.
std::string settings_edit_rule(std::string_view scope, std::string_view key);

// Rejects a config write when the key's rule is enforced (read_only). Unknown keys are writable.
Status check_config_write(std::string_view key, const std::filesystem::path& data_dir = {});

}  // namespace loom
