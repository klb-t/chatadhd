// loom/graph_entry.h — the graph's entry view (R45, R52, R53): what a
// parameterless graph query returns. Read-only; never writes the database.
//
// The graph has no root, so the entry view is the zero-context set of "area"
// nodes, each with counts taken from live registries and an `expand` query
// descriptor. The area space is GENERATED from axes data (about x kind x time):
// every cell that has a calibration area or at least one provider binding
// appears. The shipped areas/bindings are calibration examples (data/graph/*.pack,
// runtime profile domain "graph_entry"); users reorder, hide and add through
// the profile overlay (<data_dir>/profiles/graph_entry.pack).
//
// Mechanism vs data: this file owns only the provider registry (provider id ->
// function). Which areas exist, where they sit, their labels and which provider
// fills them is data. A count whose registry is unavailable is
// {"known":false,"reason":...}; an unavailable count is never rendered as zero.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "loom/db.h"
#include "loom/kb.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Config;
}

namespace loom::graph_entry {

struct Env {
  Database* db = nullptr;                // nullptr: every database count is unknown
  const Config* cfg = nullptr;           // the user's settings (tool sources, roles); nullptr: counts that need them are unknown
  std::filesystem::path data_dir;        // overlay location; empty: shipped data only
  std::shared_ptr<const kb::Pack> pack;  // nullptr: the builtin pack is loaded on demand
  Json host_counts = Json::object();     // optional counts a host can enumerate (interfaces...)
};

// What a provider sees: the environment, the effective graph_entry profile
// values, and the binding's own parameters.
struct Context {
  const Env& env;
  const Json& values;
  // The effective runtime profiles read during one compute (domain -> values; null = unreadable), shared by its providers so a profile
  // is loaded once per view however many providers read it.
  std::shared_ptr<std::map<std::string, Json>> profiles = std::make_shared<std::map<std::string, Json>>();
};

// A provider returns an object name -> count. The empty name "" is the
// binding's primary count (named by the binding's "as", default binding id).
using ProviderFn = std::function<Json(const Context&, const Json& params)>;

Json known(std::int64_t value);
Json known_by(std::int64_t value, Json by);
Json unknown(std::string_view reason);

class Providers {
 public:
  static const Providers& builtin();
  void add(std::string id, ProviderFn fn) { fns_[std::move(id)] = std::move(fn); }
  const ProviderFn* find(const std::string& id) const;
  std::vector<std::string> ids() const;

 private:
  std::map<std::string, ProviderFn> fns_;
};

// options: {"lang"?: "en", "include_hidden"?: false, "overrides"?: {...profile overrides}}
// -> {"schema","lang","profile_hash","axes","areas":[...],"seed":[...],"issues":[...]}
Result<Json> compute(const Env& env, const Json& options = Json::object(), const Providers* providers = nullptr);

}  // namespace loom::graph_entry
