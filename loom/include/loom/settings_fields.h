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
//  "human":{"label":{pl,en},"why":{pl,en},"effect":{pl,en},"levels":[...],"group","subgroup"?,"placed_from"?,"role"?,"role_status"?,"flows"?,"technical_only"?},
//  "values"?:[{"value","meaning"?:{pl,en},"preset"?,"status"?,"by"?,"reason"?:{pl,en},"on_set"?:"refuse"|"warn","from_profile"?,"status_superseded"?}],
//  "values_from"?:{"profile","pointer","keys"?},"values_from_problem"?,"values_open"?,
//  "on_unlisted"?:"refuse"|"warn","status"?:{"state","by"?,"reader"?,"reason":{pl,en},"today"?:{pl,en}}}],
//  "groups":{id:{"label":{pl,en},"why":{pl,en},"order","subgroups":[...],"origin","provenance":[...],"hidden"?,"hidden_by"?,"disabled"?,"empty_reason"?}},
//  "groups_issues"?:[...], "level_evidence":{"chain":[keys],"turn":[keys],"installation":[keys and <profile><pointer>]},
//  "profile_values":[{"profile","pointer","human":{...},"value"|"problem"}]}
// `human`, `groups` and `profile_values` are data of the pack (M1-D0): an overlay may regroup, rename or hide. Membership is patched per
// key by the pack's `placement` (field key or <profile><pointer> -> {group, subgroup?}; a user overlay moves one field without copying
// the fields array): the entry's human.group/subgroup become the placement's and human.placed_from keeps the preset; a placement
// without a group or naming nothing is a `groups_issues` entry.
// `groups` is the effective list of the view kind `settings_groups` (graph_views.pack): the shipped pack's /groups is the ROM layer,
// the user's profile overlay (profiles/settings_fields.pack) is its own layer (origin overlay:profile:settings_fields, never "rom") and
// the view overlays of `data_dir` (views/*.json) are composed on top with the kind's policy, each group carrying origin and provenance; a hidden group stays listed with hidden:true and hidden_by, so the
// fields in it are never orphaned; bad overlay ops are `groups_issues`, and so is a views directory that cannot be read (a file that
// is not JSON or has another schema, of any view): then the shipped groups are shown without view overlays, never an error. `status` (field level) says a key has a preset but no
// reader yet (shown as "planned (reason)"); `level_evidence` names the keys whose readers honour project/conversation (scope
// chain) and turn (per-request option) — the only keys that may declare those levels.
// `values_from` makes a field's values its reader's data: the runtime profile's value at the pointer (object keys with keys:true), read
// with the user's profile overlay, so a write refuses only what the effective profile does not know; the pack's `values` entries are
// annotations over that list (see resolve_values_from in settings_fields.cpp).
// A profile value is read from its runtime profile (with the user's overlay) at the JSON pointer; when it cannot be
// read the entry carries `problem` instead of `value` (never an exception: a pointer is user data). `data_dir` selects the user
// overlay profile (profiles/settings_fields.pack) and the view overlays; empty = builtin.
Result<Json> settings_fields(const std::filesystem::path& data_dir = {});

// Mismatches between the metadata and the real defaults/secret keys, and of the human layer: every field and
// profile value has `human` with label/why/effect in pl and en, a group of `groups` (and a subgroup of it when
// named), levels inside the scope chain (graph/scopes.pack through the rules engine of `data_dir`; empty levels only
// on a read_only field, which is settable nowhere and whose rule carries the reason; project/conversation only for a key of
// `level_evidence.chain`, turn only for a key of `level_evidence.turn`, installation only for a key or profile value of
// `level_evidence.installation`, person never on a profile value), `flows` that are actions of the catalogue (actions.pack with the
// user's overlay), a `role` known to ports.pack or carrying `role_status`,
// a `status` with state and reason pl+en, no `groups_issues`; every group holds something or says why it is empty
// (`empty_reason`); every profile value resolves. Empty = consistent.
// Used by the completeness test and available to tooling; each entry is a reason the owner can read.
std::vector<std::string> settings_fields_problems(const Json& fields_doc, const std::filesystem::path& data_dir = {});

// Edit rule of a key ("" when the key is not described). Open string.
std::string settings_edit_rule(std::string_view scope, std::string_view key);

// Rejects a config write when the key's rule is enforced (read_only). Unknown keys are writable.
Status check_config_write(std::string_view key, const std::filesystem::path& data_dir = {});

// The `values` a field declares, applied to a value about to be written. The data says what a write does (never a rule in code):
// a value whose entry carries a `status` (planned) is refused when the entry says `on_set: "refuse"` (its reader rejects it, e.g.
// chat_context_order=stability) and stored with a warning otherwise (`"warn"` or absent: its reader degrades, e.g.
// layout_mode=windows); a value outside a closed list (no `values_open`) is refused when the field says `on_unlisted: "refuse"`
// and stored with a warning otherwise. null (clearing the key: back to the preset) is always accepted.
// Ok(null) = nothing to say; Ok(warning) = {"value","status":"planned"|"unlisted","reason"?,"values"?,"message"} to report with the
// stored value; Error = the refusal with the data's reason.
Result<Json> check_declared_value(const Json& field, const Json& value);

// One rule for every raw config write path (loom_set_config, PATCH /api/config = loom_set_config_json, `loom config set`, a role's
// model setting): for each key of `patch` the read_only rule (check_config_write) and the declared values (check_declared_value),
// with one read of the settings document. Ok = the warnings ([{"key", ...warning}], empty = none); Error = the first refusal, and
// nothing is written by the caller. Metadata unavailable: Ok (never blocks a write; the reason is logged).
Result<Json> check_config_patch(const Json& patch, const std::filesystem::path& data_dir = {});

}  // namespace loom
