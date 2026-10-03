#include "context_diagnostics.h"

namespace loom::context {
namespace {
void missing_rows(std::string& out, const Json& rows, std::string_view category) {
  if (!rows.is_array()) return;
  for (const auto& row : rows) {
    const auto status = json::get_string(row, "status");
    if (status == "selected") continue;
    out += "- [INCOMPLETE: " + std::string(category) + " " + json::get_string(row, "ref") + ": " + status + "]\n";
  }
}
}

std::string render_context_diagnostics(const model::ContextSet& set) {
  const auto& params = set.goal.params;
  std::string out;
  if (const auto* plan = json::find(params, "plan_trace")) {
    out += "\n## Retrieval plan coverage\n";
    out += "Candidate coverage is structural; it does not establish support or truth.\n";
    if (const auto* theses = json::find(*plan, "theses"); theses && theses->is_array()) {
      for (const auto& thesis : *theses) {
        out += "- " + json::get_string(thesis, "id") + ": " + json::get_string(thesis, "text") + "\n";
        if (const auto* gaps = json::find(thesis, "gaps"); gaps && !gaps->empty()) {
          out += "  [INCOMPLETE: " + json::dump(*gaps) + "]\n";
        }
      }
    }
  }
  if (const auto* claims = json::find(params, "claim_selection")) missing_rows(out, *claims, "requested claim");
  if (const auto* retrieval = json::find(params, "candidate_retrieval")) {
    if (json::get_string(*retrieval, "corpus_status") != "ok" || retrieval->value("possibly_truncated", false) ||
        retrieval->value("entity_label_query_errors", 0) > 0) {
      out += "\n[INCOMPLETE: candidate corpus query failed, reached its scan cap, or lacked entity labels; see candidate_retrieval.]\n";
    }
    if (const auto* channels = json::find(*retrieval, "channels"); channels && channels->is_array()) {
      for (const auto& channel : *channels) {
        const auto status = json::get_string(channel, "status");
        if (status != "ok") out += "[INCOMPLETE: candidate instrument " + json::get_string(channel, "id") + ": " + status + "]\n";
        if (channel.value("truncated", false)) out += "[INCOMPLETE: candidate instrument " + json::get_string(channel, "id") + " reached its result limit.]\n";
      }
    }
  }
  if (const auto* counters = json::find(params, "counter_evidence")) {
    out += "\nCounter-evidence coverage follows recorded links only; absence of a link does not establish absence of contradiction.\n";
    if (const auto* entries = json::find(*counters, "entries"); entries && entries->is_array()) {
      for (const auto& entry : *entries) {
        if (const auto* claims = json::find(entry, "claims")) missing_rows(out, *claims, "counter-claim");
        if (const auto* observations = json::find(entry, "observations")) missing_rows(out, *observations, "counter-observation");
        if (const auto* gaps = json::find(entry, "gaps"); gaps && !gaps->empty()) {
          out += "- [INCOMPLETE: counter-evidence for " + json::get_string(entry, "for_claim") + ": " + json::dump(*gaps) + "]\n";
        }
      }
    }
  }
  return out;
}
}
