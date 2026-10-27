// loom/view_engine.h — the visualization engine: a view declaration (graph data) is evaluated into a layout-independent view model (P28, step 1).
//
// A declaration (effective view `view_declarations`: ROM presets + user overlays, with provenance) is a list of ELEMENTS, each {id, kind, ...}:
//   source       {perspective, weight, depth, role?, direction?}   a perspective of P24 with a weight (main/side) and a depth; several = a mix
//   measure      {measure, params?}                                a value computed on the elements (kinds: effective view `measure_kinds`)
//   coordinates  {system, bind: {axis: measure id}, ...}           a coordinate system (effective view `coordinate_systems`)
//   encoding     {channel, measure, scale?, combine?}              a visual channel <- a measure (effective view `view_channels`)
//                scale (G3, all optional, open values): range, domain, invert, exponent, reference (a point of the output space: the
//                value is a signed distance from it), direction (a number or {measure, reference?, magnitude?, map?, otherwise?}: per
//                element, from data — its sign reverses the order or the side of the reference), clamp. combine: how the value joins
//                earlier encodings of the same channel on the same element (replace = default | add | multiply | min | max | mean);
//                every encoding is applied, none is dropped, the contributions are in the element's channel_parts.
//   composition  {composition: sectors | layers | lanes | small_multiples, ...}
//   rules        {budget?, promote_top?, disable?: [rule ids]}     overrides of the rules layer for this view
// The list of element kinds is open, and so are the kinds inside it (measure kinds, coordinate systems, channels, compositions, combination
// rules): one that the engine has no evaluator for is passed through into `unsupported` with a visible reason, never dropped and never fatal.
//
// The rules layer (effective view `view_rules`) is separate from composition and never blocks: it ranks channels per data type (Mackinlay
// style table), checks interference pairs (an interfering dimension must grow with importance, checked on the data of the view by rank
// correlation) and holds the node budget with the collapse order. Its output is, per encoding, `promoted` or `allowed` with the reasons.
//
// Output of evaluate(): {schema: "loom.view_model/1", declaration, focus, sources, elements: [{id, label, kind, measures, sector?, coords,
// channels, sources}], edges, groups, measure_stats, coordinate_systems, encodings: [{id, channel, measure, status, rank?, reasons}],
// unsupported: [{element, kind, reason}], budget, issues, advisories}. Nothing in it depends on a renderer.
// `advisories` (G3, R58; shape loom.advisory/1 of loom/advisory.h): non-blocking records when a configuration looks of little use or
// the result is degenerate (nothing_visible, marks_identical, values_outside_valid, duplicate_encoding, encoding_overridden,
// direction_missing, combine_unknown — an open list; texts in view_rules `messages`), each with evidence, a question, choices and
// whether to ask, from the remembered answers (<data_dir>/advisory_ack.json) and the preset view_rules `advisory_policy` (a view
// overrides it with rules.advisories). request.now (ISO UTC) fixes the clock of remember_days (tests); default = now.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "loom/effective_view.h"
#include "loom/perspectives.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::views {

class ViewEngine {
 public:
  static Result<ViewEngine> open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs = {});

  // {schema, declarations: [{id, entry, origin, provenance, ...}], measure_kinds, coordinate_systems, channels, composition_kinds,
  // combination_rules, rules, renderers (each the effective list of that kind; renderers = how a client draws the model, P28 step 3), issues}
  Json list() const;

  // request: {focus: node id (required), declaration?: inline declaration object (instead of the id), sources?: {source id: {weight?, depth?}},
  // budget?: number (0 or null = none)}
  Result<Json> evaluate(const RenderContext& ctx, const std::string& declaration_id, const Json& request) const;
  Result<Json> evaluate_declaration(const RenderContext& ctx, const Json& declaration, const Json& request) const;

 private:
  Perspectives persp_;
  Effective declarations_, measures_, systems_, channels_, compositions_, combines_, rules_, renderers_;
  friend struct Evaluator;
};

}  // namespace loom::views
