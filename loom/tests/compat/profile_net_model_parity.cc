// Frozen old-API probe: synthetic records and scripted HTTP only, no Runtime.
// Compile this same source against the baseline and the data-driven revision.
#include <algorithm>
#include <iostream>
#include <type_traits>

#include "loom/kb.h"
#include "loom/model.h"
#include "loom/net/http.h"

using namespace loom;

namespace {
Json history(const std::vector<model::StatusValue>& statuses) {
  std::vector<model::StatusRecord> records;
  for (std::size_t i = 0; i < statuses.size(); ++i) {
    model::StatusRecord row;
    row.id = "synthetic-status-" + std::to_string(i);
    row.entity = "synthetic-component";
    row.branch = "synthetic-branch";
    row.version = "1.0." + std::to_string(i);
    row.date = "2026-01-01";
    row.status = statuses[i];
    records.push_back(std::move(row));
  }
  std::reverse(records.begin(), records.end());
  Json out = Json::array();
  for (const auto& row : model::order_status_history(std::move(records))) out.push_back(row.to_json());
  return out;
}
}

int main() {
  static_assert(std::is_aggregate_v<net::HttpRequest>);
  static_assert(noexcept(model::authority_rank(model::Origin::User)));
  Json out = Json::object();
  Json ranks = Json::object();
  for (const auto origin : model::all<model::Origin>()) {
    ranks[std::string(model::to_string(origin))] = model::authority_rank(origin);
  }
  ranks["unknown"] = model::authority_rank(static_cast<model::Origin>(999));
  out["authority_ranks"] = ranks;

  Json histories = Json::array();
  for (const auto a : model::all<model::StatusValue>()) {
    for (const auto b : model::all<model::StatusValue>()) {
      for (const auto c : model::all<model::StatusValue>()) histories.push_back(history({a, b, c}));
    }
  }
  histories.push_back(history({model::StatusValue::Implemented, model::StatusValue::Lost, model::StatusValue::Restored,
                               model::StatusValue::Lost, model::StatusValue::Partial, model::StatusValue::Lost,
                               model::StatusValue::Implemented}));
  histories.push_back(history({model::StatusValue::Planned, model::StatusValue::Abandoned, model::StatusValue::Lost,
                               model::StatusValue::Superseded, model::StatusValue::Restored, model::StatusValue::Lost,
                               model::StatusValue::Implemented}));
  out["status_histories"] = histories;

  const auto pack = kb::Pack::load_builtin();
  if (!pack) { std::cerr << pack.error().to_string() << '\n'; return 1; }
  const auto anchors = model::anchoring_morphisms(**pack);
  if (!anchors) { std::cerr << anchors.error().to_string() << '\n'; return 1; }
  out["anchoring_morphisms"] = Json::array();
  for (const auto& morphism : *anchors) out["anchoring_morphisms"].push_back(morphism.to_json());

  const std::vector<Json> fixtures{
      Json{{"url", "https://example.test/synthetic"}},
      Json{{"method", "POST"}, {"url", "https://example.test/synthetic"}, {"body", "synthetic α🙂"},
           {"headers", Json{{"X-Synthetic", "fixture"}, {"Content-Type", "text/plain"}}}, {"timeout_ms", 1234}, {"stream", true}},
      Json{{"url", "http://example.test/synthetic"}, {"timeout_ms", 0}},
      Json{{"url", "http://example.test/synthetic"}, {"timeout_ms", -10}},
      Json{{"url", "http://example.test/synthetic"}, {"timeout_ms", 12.75}},
      Json{{"url", "http://example.test/synthetic"}, {"timeout_ms", "use preset"}},
      Json{{"url", "http://example.test/synthetic"}, {"body_base64", "AAEC/w=="}}};
  net::ScriptedTransport transport;
  transport.set_fallback(net::ScriptedTransport::Reply::text(200, "saved synthetic response"));
  out["http_requests"] = Json::array();
  for (const auto& fixture : fixtures) {
    const auto request = net::HttpRequest::from_json(fixture);
    if (!request) { std::cerr << request.error().to_string() << '\n'; return 1; }
    const auto response = transport.send(*request);
    if (!response) { std::cerr << response.error().to_string() << '\n'; return 1; }
    out["http_requests"].push_back(request->to_json());
  }
  out["aggregate_request"] = net::HttpRequest{"POST", "https://example.test/synthetic", {}, "synthetic", 4321, true}.to_json();
  out["default_request"] = net::HttpRequest{}.to_json();
  out["default_transport_name"] = net::make_default_transport()->name();
  std::cout << out.dump() << '\n';
}
