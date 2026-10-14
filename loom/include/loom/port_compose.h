// Composition operators for ports (B-MOD-04): chain / fallback / ensemble / mirror.
//
// Derived from three loops that already existed: the ASR/OCR provider order (MediaProviders: first provider
// that works), the storage `placement` (write to every target until the write quorum is met, read from the
// first that has the object) and the fusion of search methods (method_channels: several score sets -> one).
// The operators are plain templates over "members" (anything callable), so the same code serves an
// AdapterRegistry<Port, Env> (see build_members), a vector of shared_ptr<Port>, or storage targets.
//
// Not here on purpose: "kolejno modeli" / first_success over model variants stays in llm::use_model
// (request.mode sequential | first_success). Ensemble of models is built on top of it by the caller,
// never by a second copy of the HTTP/usage-guard path.
//
// Data description (a role entry of the `ports` profile):
//   {"compose": "fallback"|"chain"|"ensemble"|"mirror", "members": [<entry>|<adapter name>, ...],
//    "quorum": 1, "fuse": {"operation": "rrf", "signal": "reciprocal_rank"}}
// where <entry> is {"adapter": "...", "params": {...}} or itself a composition.
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "loom/adapter_registry.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::compose {

inline constexpr const char* kOperators[] = {"chain", "fallback", "ensemble", "mirror"};

struct Spec {
  std::string op;                    // chain | fallback | ensemble | mirror ("" = not a composition)
  std::vector<Json> members;         // entries, in order
  int quorum = 1;                    // mirror: successful writes needed; ensemble: members that must answer
  Json fuse = Json::object();        // ensemble: how the answers are combined (caller interprets)
  static Result<Spec> parse(const Json& entry) {
    Spec s;
    if (!entry.is_object() || !entry.contains("compose")) return s;
    s.op = json::get_string(entry, "compose");
    bool known = false;
    for (const char* o : kOperators) known = known || s.op == o;
    if (!known) return Error(Errc::InvalidArgument, "unknown composition operator '" + s.op + "'");
    const Json* m = json::find(entry, "members");
    if (!m || !m->is_array() || m->empty()) return Error(Errc::InvalidArgument, "composition '" + s.op + "' needs a non-empty members array");
    for (const auto& x : *m) s.members.push_back(x.is_string() ? Json{{"adapter", x}} : x);
    s.quorum = static_cast<int>(json::get_int(entry, "quorum", 1));
    if (const Json* f = json::find(entry, "fuse"); f && f->is_object()) s.fuse = *f;
    return s;
  }
};

template <class Port>
struct Member {
  std::string name;
  std::shared_ptr<Port> port;
};

// Members of a spec from a registry: each entry's "adapter" through create_named, params as the config.
template <class Port, class Env>
Result<std::vector<Member<Port>>> build_members(const AdapterRegistry<Port, Env>& registry, const Spec& spec,
                                                const std::string& id, const Env& env) {
  std::vector<Member<Port>> out;
  for (const auto& e : spec.members) {
    const std::string name = json::get_string(e, "adapter");
    Json cfg = e.contains("params") && e["params"].is_object() ? e["params"] : Json::object();
    cfg["adapter"] = name;
    auto p = registry.create_named(name, id, cfg, env);
    if (!p) return p.error();
    out.push_back({name, std::shared_ptr<Port>(std::move(*p))});
  }
  return out;
}

// fallback: members in order, the first success wins. `call(member)` returns nullopt to skip a member,
// else its Result. `on_fail(member, error, last)` sees every failure (default: remember it as `last`);
// the error returned when nobody succeeded is `last`.
template <class R, class Members, class Call, class OnFail>
Result<R> fallback(const Members& members, Call&& call, Error last, OnFail&& on_fail) {
  for (const auto& m : members) {
    std::optional<Result<R>> r = call(m);
    if (!r) continue;
    if (*r) return std::move(*r);
    on_fail(m, r->error(), last);
  }
  return last;
}
template <class R, class Members, class Call>
Result<R> fallback(const Members& members, Call&& call, Error last) {
  return fallback<R>(members, std::forward<Call>(call), std::move(last),
                     [](const auto&, const Error& e, Error& l) { l = e; });
}

// mirror: every member is tried (a failing one degrades, never blocks); ok when at least max(1, quorum) took it.
// `call(member)` returns nullopt to skip a member (it counts neither way).
template <class Members, class Call, class OnDegraded>
Status mirror(const Members& members, Call&& call, int quorum, Error none_accepted, OnDegraded&& on_degraded) {
  int ok = 0;
  Error last = std::move(none_accepted);
  for (const auto& m : members) {
    std::optional<Status> st = call(m);
    if (!st) continue;
    if (*st) { ++ok; continue; }
    last = st->error();
    on_degraded(m, last);
  }
  if (ok >= std::max(1, quorum)) return {};
  return last;
}

// chain: a pipeline; each member's output is the next one's input. The first error stops it.
template <class V, class Members, class Step>
Result<V> chain(V input, const Members& members, Step&& step) {
  for (const auto& m : members) {
    auto r = step(m, std::move(input));
    if (!r) return r.error();
    input = std::move(*r);
  }
  return input;
}

template <class R>
struct EnsembleOutcome {
  std::vector<R> values;                                 // answers, in member order
  std::vector<std::string> produced_by;                  // names of the members that answered (provenance)
  std::vector<std::pair<std::string, Error>> degraded;   // members that failed: degradation, not an error
  Json to_json() const {
    Json d = Json::array();
    for (const auto& [n, e] : degraded) d.push_back(Json{{"member", n}, {"error", e.message}});
    return Json{{"produced_by", produced_by}, {"degraded", d}};
  }
};

// ensemble: all members answer; the missing ones degrade the result. Error only when fewer than
// max(1, quorum) answered. `name_of(member)` names a member for produced_by.
template <class R, class Members, class Call, class NameOf>
Result<EnsembleOutcome<R>> ensemble(const Members& members, Call&& call, NameOf&& name_of, int quorum = 1) {
  EnsembleOutcome<R> out;
  Error last(Errc::Unavailable, "no ensemble member answered");
  for (const auto& m : members) {
    std::optional<Result<R>> r = call(m);
    if (!r) continue;
    if (*r) { out.values.push_back(std::move(**r)); out.produced_by.push_back(name_of(m)); }
    else { last = r->error(); out.degraded.emplace_back(name_of(m), r->error()); }
  }
  if (static_cast<int>(out.values.size()) < std::max(1, quorum)) return last;
  return out;
}

}  // namespace loom::compose
