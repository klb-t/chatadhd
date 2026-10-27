// Layout fingerprint of an export (IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23 §2.3–2.4, package P2). Not a public contract.
//
// The dictionary of structural descriptors is closed here (what a fingerprint can look at); the layouts are open, in
// data (runtime/import_graph.pack "layout_fingerprint": claims with Beta reliabilities and dated observations, layout
// profiles = conjunctions of descriptors). Adding a provider or a new version of an export is a new entry of data
// (or of the person's overlay <data_dir>/profiles/import_graph.pack), never code.
//
// Descriptors (each names a claim of the registry; a descriptor without "optional": true is required — a failed
// required descriptor excludes the layout, it is still listed with what failed):
//   container(regex)            the container kind of the source ("zip", "file")
//   member_present(regex)       some member name matches        member_absent(regex)   no member name matches
//   member_count(regex,min,max) the number of matching members lies in [min, max] (max absent = no upper bound)
//   root_folder(regex)          the common root folder that was stripped from every name (empty when none)
//   ext_set([exts])             every member's extension is in the set
//   json_top(member, array|object|scalar)   the first matching member's JSON starts with that kind
//   json_keys(member, all|any|none, [keys]) object keys seen in the read prefix of the first matching member
//   js_marker(member, literal)  the literal occurs in the read prefix of a matching member
//   html_title(member, regex)   the <title> of a matching member matches
// An unknown descriptor name is an advisory (it contributes nothing), never an error.
//
// Scoring (presets in data): evidence(layout) = prior + Σ over satisfied descriptors of weight · max(0, logit(r)),
// r = alpha / (alpha + beta) of the descriptor's claim. Non-excluded layouts and a pseudo-candidate "unknown layout"
// (evidence = unknown_evidence) are normalised by softmax; a layout is chosen with confidence only when its share is
// at least confident_at. Shared evidence cancels out, so only what distinguishes layouts moves the result; a weak
// distinction stays "uncertain" and the alternatives are always returned with their shares.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "loom/util/json.h"

namespace loom::import_graph {

struct LayoutMember {
  std::string name;     // as in the source (relative, '/' separated); for a bare file: its file name
  std::string prefix;   // the first bytes, read only for members a content descriptor names (bounded by preset)
  bool has_prefix = false;
};

struct LayoutEvidence {
  std::string container;  // "zip" | "file" (open: whatever the reader saw)
  std::string root;       // the common root folder stripped from every name ("" = none)
  std::vector<LayoutMember> members;
  Json advisories = Json::array();
};

// Reads the evidence of a source: the member names of a ZIP archive (or the file itself) and the bounded prefixes
// of the members the configured layouts read. `display_name` names a bare file (the person's name of an upload).
LayoutEvidence layout_evidence(const std::filesystem::path& path, const std::string& display_name, const Json& config);

// Strips the common root folder of every member name (preset strip_common_root); called by layout_evidence and by
// tests that build evidence in memory.
void normalise_layout_evidence(LayoutEvidence& evidence, const Json& config);

// Scores every layout of `config` against `evidence`:
// {schema, state: confident|uncertain|unknown, chosen|null, class|null, candidates[], unknown_share, tie, advisories}.
Json fingerprint_layout(const LayoutEvidence& evidence, const Json& config);

}  // namespace loom::import_graph
