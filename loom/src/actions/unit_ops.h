// M1-K2c-1: the units a conversation belongs to (projects, teams, organisations ... - the kinds are DATA, the views `unit_kinds`) as actions:
// units.define, units.list, units.forget, conversation.set_unit, conversation.clear_unit.
//
// A unit is a node `unit:<kind>/<id>` of the table `nodes` (kind "unit", metadata.unit_kind / unit_id); the assignment is
// conversations.metadata.units = {<kind>: <id>}, the field the scope chain already reads (views/scope_resolver.cpp), written through a MERGE: the
// other keys of the metadata (flow, settings, interview ...) are never touched. Pure Json in, a text and data out; the words come from the pack
// chat_ops (env.say). Nothing here asks the person: define, forget, set_unit and clear_unit are gated by the runner (confirm:<action>), exactly like
// profile.set; list only reads.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::actions::units {

struct Env {
  Database& db;
  std::filesystem::path root;  // the data directory: the unit kinds are read from the effective view (ROM + the person's overlay)
  std::string lang;            // "pl" | "en"
  std::string open_conv;       // the conversation the call is made from; empty when none
  std::function<std::string(const std::string&, const Json&)> say;  // a sentence of the pack in the language of the run
  std::function<std::string(const std::string&)> fold;              // letters folded for comparing the person's words with the kinds
};

struct Reply {
  std::string text;
  Json data;
};

// The id of a unit node: unit:<kind>/<id>.
std::string node_id(const std::string& kind, const std::string& id);

Result<Reply> define(const Env& env, const Json& args);       // {kind, id, label?} - gated
Result<Reply> list(const Env& env, const Json& args);         // {kind?} - read only
Result<Reply> forget(const Env& env, const Json& args);       // {kind, id} - gated
Result<Reply> set_unit(const Env& env, const Json& args);     // {conv_id?, kind, id} - gated
Result<Reply> clear_unit(const Env& env, const Json& args);   // {conv_id?, kind} - gated

}  // namespace loom::actions::units
