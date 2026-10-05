// R28: a caller-supplied product plan is a structural retrieval query.
// This projection neither compiles ActiveTaskSpec nor creates a second store.
#pragma once

#include "loom/context_engine.h"

namespace loom::context {

// null disables plan selection. Otherwise:
// {id, source_ref?: opaque JSON provenance, theses: [{id, text,
//   targets?: [entity ids], claims?: [claim ids], relation_hops?: integer >= 0,
//   detail_resolution?: null|label|summary|full|raw,
//   require_counter_evidence?: bool (default true), budget_weight?: number > 0}]}
// Unknown fields and duplicate ids are rejected. Omitted targets, claims,
// scope and detail inherit the enclosing ContextRequest; an empty id array
// explicitly clears it. A null detail also inherits the enclosing detail.
Status validate_context_plan(const Json& plan);

// Offline, per-thesis selection through the existing engine. Item budgets
// are apportioned by weight in plan order; unused/duplicate item capacity
// passes forward. Zero allocations never invoke the legacy default budget.
// Exact per-thesis diagnostics remain in goal.params.plan_trace; identical
// safe representations share an item with factors.thesis_ids membership.
Result<model::ContextSet> select_context_plan(ContextEngine& engine, kb::KnowledgeStore& store,
                                            const ContextRequest& request);

}  // namespace loom::context
