// Shared helpers for the context+materialize area (context_engine.cpp,
// materialize.cpp). Not a public header: included by src/context/** and
// src/materialize/** only (both are this area's own files).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/knowledge_store.h"
#include "loom/model.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::ctx {

// "" = latest run with status "done" (KnowledgeStore::list_runs is newest
// first). NotFound when `run` is empty and no run has finished yet, or when
// a given run id does not exist.
Result<std::string> resolve_run(kb::KnowledgeStore& store, std::string_view run);

// t[lang] if present, else t["en"], else the first value (map order), else "".
std::string pick_text(const model::Text& t, std::string_view lang);

// Parses the "YYYY-MM-DD" prefix of an ISO date into days-since-epoch
// (proleptic Gregorian, civil_from_days/days_from_civil). nullopt when the
// prefix is not a valid date.
std::optional<long> parse_date_days(std::string_view iso);

// Recency in [0, 1]: 1.0 at `anchor`, halving every `half_life_days` before
// it (dates after the anchor also score 1.0). 0.6 (neutral-low) when either
// date is missing/unparseable, so undated items are neither favoured nor
// zeroed out.
double freshness_score(std::string_view date, std::string_view anchor, double half_life_days = 365.0);

// model::authority_rank(o) / 5.0 (rank in {1..5}), so it composes with the
// other [0, 1] factors by multiplication (LOOM_CONCEPTUAL_MODEL §4).
double authority_score(model::Origin o) noexcept;

// Wraps `value_text` with the evidence marker of policy/evidence_encoding.json
// ("markdown" template of evidence.<class>, placeholders {value},
// {confidence}, {expected_property}, {basis}, {query}), then appends the
// origin marker (origin.<origin>.markdown; "" for archive/system). Never
// upgrades evidence (I2/I3): the wrapping is purely textual.
std::string evidence_markdown(const kb::Pack& pack, model::EvidenceClass ev, model::Origin origin, double confidence,
                              std::string_view value_text, const std::optional<kb::ExpectedProperty>& expected,
                              std::string_view basis, const Json& fill_query);

// Plain-text badge for a principle (which has no EvidenceClass of its own):
// "[<validation>, <level>/<form>]".
std::string principle_badge(model::ValidationStatus validation, model::PrincipleLevel level, model::PrincipleForm form);

// Every element of goals/goal_types.json (foundation only exposes
// model::goal_type(pack,id) for a single id; the classifier needs the list).
Result<std::vector<model::GoalType>> list_goal_types(const kb::Pack& pack);

}  // namespace loom::ctx
