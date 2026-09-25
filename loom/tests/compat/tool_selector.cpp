// Compat command for SelectorEngine (core/selector.py port).
#include "compat_registry.h"
#include "loom/selector.h"

using namespace loom;
using namespace loom::compat;

// {"tier": 2|3, "corpus":[...], "ids":[...]?, "queries":[{"query":str,"top_k":n}]}
// -> per query: [{"id","text","score"}, ...]
LOOM_COMPAT_COMMAND(cmd_selector_search, "selector-search", "@req.json : index + search, per-query hit lists") {
  if (args.empty()) return fail("usage: selector-search @req.json");
  auto req = arg_json(args[0]);
  if (!req || !req->is_object()) return fail("expected a JSON object");
  int tier = static_cast<int>(json::get_int(*req, "tier", SelectorEngine::kTierTfIdf));
  SelectorEngine sel(tier);

  std::vector<std::string> corpus;
  if (const Json* c = json::find(*req, "corpus"); c && c->is_array()) {
    for (const auto& v : *c) corpus.push_back(v.get<std::string>());
  }
  std::vector<std::string> ids;
  if (const Json* i = json::find(*req, "ids"); i && i->is_array()) {
    for (const auto& v : *i) ids.push_back(v.get<std::string>());
  }
  if (auto st = sel.index(corpus, ids); !st) return fail(st.error().to_string());

  Json out = Json::array();
  if (const Json* qs = json::find(*req, "queries"); qs && qs->is_array()) {
    for (const auto& q : *qs) {
      std::string query = json::get_string(q, "query");
      int top_k = static_cast<int>(json::get_int(q, "top_k", 5));
      Json hits = Json::array();
      for (const auto& h : sel.search(query, top_k)) hits.push_back(h.to_json());
      out.push_back(hits);
    }
  }
  print_json(out);
  return 0;
}
