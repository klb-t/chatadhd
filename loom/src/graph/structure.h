// W11a-1: the structure of a node (window „Struktura projektu”, action graph.structure) - an overlay on graph.navigate, nothing stored.
//
// A structure template (loom.structure_template/1, runtime/structure_templates.pack + the person's overlay) says what we expect under a
// node with a trait: {match, expects[{id, label, relation, child_match, card, confidence, source}], extends?[], label, description,
// example?, derived_from?}. Every template shipped is an example; the list is open. For a root and the templates that fit it, every
// expectation gets a state from the ordered state rules of the data (presets: declined, missing, excess, inferred, present, empty), and a
// state whose rule says ghost:true is drawn as a ghost node (a synthetic address, never written anywhere).
//
// Nothing here throws to the caller and nothing is refused for being unusual: a malformed template, an unknown predicate operator, a
// cycle of extends, an empty or duplicated expects, an inverted cardinality - each gives a non-blocking advisory (code + fingerprint for
// "don't ask next time", words pl + en) and the rest of the answer is still computed. A root that does not exist gives an answer with
// found:false and the reason.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "graph/projection.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::structure {

// The fields of a template and of an expectation this file reads (the rule: a field without a reader is an error). A field outside these
// lists in a template is not rejected: it raises the advisory unknown_field.
const std::vector<std::string>& template_fields();
const std::vector<std::string>& expect_fields();

// The predicate language of match / child_match (minimal, open): {all:[p]}, {any:[p]}, {not:p}, {prefix:"unit:"} (address), {kind:"x"} or
// {kind:["x","y"]}, {field:"attrs.k", equals:v | in:[v] | prefix:"s" | exists:bool}, {true:bool}; several keys in one object = all of them.
// null / absent = every node. An unknown operator matches nothing and is added to `unknown_ops`.
bool matches(const Json& predicate, const Json& node, std::set<std::string>& unknown_ops);

// The pure part: the templates of `data` (the values of the structure_templates profile) applied to `focus` and its `children` (rows of
// graph.navigate). request: {templates?:[ids] (absent = every template that fits), declined?:["template/expect"], show_expected?:bool,
// lang?}. Returns {templates_applied, expectations, ghosts, children (each with expected_by[]), counts, advisories}.
Json evaluate(const Json& data, const Json& focus, const Json& children, const Json& request);

// graph.structure {root?, templates?, show_expected?, depth?, budget?, declined?, lang, conv_id?} -> {schema loom.structure/1, found,
// root, ...evaluate, navigate{counts, breadcrumbs, limit}} | {found:false, reason, reason_human, suggestions?}. Read only.
Result<Json> structure(const projection::Env& env, const Json& request);

}  // namespace loom::structure
