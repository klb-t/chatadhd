// loom/action_emergent.h — the catalogue grows from conversations (R55; GRAF_HUB_KOD_SILNIK §9). Always as proposals, never silently.
//
// Three things the router may offer, each a definition that the user accepts or drops:
//   from_command   an input that matches no action but is shaped like a command (a verb-like lead word, an element, no question) becomes
//                  the definition of a new action: a composite with one use_model step whose prompt is the user's own instruction, a name
//                  from the user's words, `requires` inferred from the traits of the element, the verb added to the lexicon; accepted, it
//                  is written (actions.define) and then run on the element it was said about;
//   from_sequence  the same chain of actions, each run on the result of the one before, on elements of one class, N times in a row
//                  (N is data, preset 3) becomes one composite whose steps are those actions;
//   from_group     a user-defined action used on several classes that share traits is offered a group (a named trait bundle) of what
//                  they share (groups.define).
// Accepting writes only user overlays (profiles/actions.pack, profiles/intent_routing.pack, profiles/graph_entry.pack); nothing touches the
// shipped data. Every proposal leaves a node (class emergent_proposal) with `derived_from` links to the messages it emerged from, and every
// definition can be taken back (actions.remove, groups.remove). Data: runtime/intent_routing.pack `emergent`; the setting emergent_actions
// switches it (off | propose).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::actions {
class Engine;
class ElementResolver;
}  // namespace loom::actions

namespace loom::actions::emergent {

struct Context {
  Runtime& rt;
  const Json& routing;          // runtime profile intent_routing (values)
  const Engine& engine;
  const ElementResolver& resolver;
};

// "propose" | "off": the setting emergent_actions over the profile's active preset ("off" for a name the profile does not list).
std::string preset(Runtime& rt, const Json& routing);

// A definition: {"kind": define_action | define_sequence | define_group, "writer": "actions.define" | "groups.define", "args": {...},
//   "id", "signature", "title", "sources": [message ids it emerged from, beyond the user's own message],
//   "summary": {fields the texts use}, "then_run"?: {"element": {"id": ...}}}.
// P30: the action whose phrases the (folded) verb equals, matches as a stem, or shares command_shape.existing_prefix_chars leading characters
// with; "" when it names none. from_command never offers such a verb as a new action.
std::string existing_action_for(const Context& c, const std::string& verb);
std::optional<Json> from_command(const Context& c, const std::string& text, const std::string& lang, const Json& element);
// `conv_id`: the conversation the run that has just finished belongs to.
std::optional<Json> from_sequence(const Context& c, const std::string& conv_id);
std::optional<Json> from_group(const Context& c, const std::string& action_just_run);

// The node that records where a proposal came from, with `derived_from` links to the messages (the user's own included). Returns its id.
Result<std::string> record(Runtime& rt, const Json& routing, const Json& definition, const std::string& conv_id, const std::vector<std::string>& message_ids);
// proposed -> accepted | rejected | undone | stale, with the time and a detail (what was written).
void settle(Runtime& rt, const std::string& node_id, const std::string& status, const Json& detail = Json::object());

}  // namespace loom::actions::emergent
