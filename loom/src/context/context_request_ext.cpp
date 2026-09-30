#include "context_request_ext.h"

#include <limits>
#include <set>

#include "loom/context_plan.h"

namespace loom::context {

Status parse_context_extensions(const Json& j, ContextRequest& r) {
  if (const auto* plan = json::find(j, "plan")) r.plan = *plan;
  LOOM_TRY(validate_context_plan(r.plan));
  if (const auto* targets = json::find(j, "claim_targets")) {
    if (!targets->is_array()) return Error(Errc::InvalidArgument, "claim_targets must be an array of nonempty claim ids");
    std::set<std::string> seen;
    for (const auto& target : *targets) {
      if (!target.is_string() || target.get_ref<const std::string&>().find_first_not_of(" \r\n\t") == std::string::npos) {
        return Error(Errc::InvalidArgument, "claim_targets must contain nonempty claim ids");
      }
      const auto id = target.get<std::string>();
      if (!seen.insert(id).second) return Error(Errc::InvalidArgument, "claim_targets contains duplicate id");
      r.claim_targets.push_back(id);
    }
  }
  for (const auto* name : {"include_counter_evidence", "lexical_shadow"}) {
    if (const auto* value = json::find(j, name)) {
      if (!value->is_boolean()) return Error(Errc::InvalidArgument, std::string(name) + " must be boolean");
      (std::string_view(name) == "lexical_shadow" ? r.lexical_shadow : r.include_counter_evidence) = value->get<bool>();
    }
  }
  if (const auto* channels = json::find(j, "candidate_channels")) {
    if (!channels->is_array()) return Error(Errc::InvalidArgument, "candidate_channels must be an array");
    std::set<std::string> seen;
    for (const auto& channel : *channels) {
      LOOM_TRY_ASSIGN(auto parsed, CandidateChannelRequest::from_json(channel));
      if (!seen.insert(parsed.id).second) return Error(Errc::InvalidArgument, "candidate_channels contains duplicate instrument id");
      r.candidate_channels.push_back(std::move(parsed));
    }
  }
  if (const auto* limit = json::find(j, "candidate_scan_limit")) {
    if (!limit->is_number_integer() || *limit <= 0 || *limit > std::numeric_limits<int>::max()) {
      return Error(Errc::InvalidArgument, "candidate_scan_limit must be a positive supported integer");
    }
    r.candidate_scan_limit = limit->get<int>();
  }
  return {};
}

void serialize_context_extensions(Json& j, const ContextRequest& r) {
  if (!r.plan.is_null()) j["plan"] = r.plan;
  if (!r.claim_targets.empty()) j["claim_targets"] = r.claim_targets;
  if (r.include_counter_evidence) j["include_counter_evidence"] = true;
  if (!r.candidate_channels.empty()) {
    j["candidate_channels"] = Json::array();
    for (const auto& channel : r.candidate_channels) j["candidate_channels"].push_back(channel.to_json());
  }
  if (r.candidate_scan_limit != 10000) j["candidate_scan_limit"] = r.candidate_scan_limit;
  if (r.lexical_shadow) j["lexical_shadow"] = true;
}

Status validate_context_extensions(const ContextRequest& r) {
  Json value = Json::object();
  serialize_context_extensions(value, r);
  ContextRequest checked;
  return parse_context_extensions(value, checked);
}
}
