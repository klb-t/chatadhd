// C API: knowledge layer foundation — data pack, KnowledgeStore queries,
// owner judgements, knowledge pipeline runs.
#include <limits>

#include "context.h"
#include "loom/knowledge.h"
#include "loom/knowledge_store.h"

using namespace loom;
using namespace loom::capi;

namespace {

int clamp_int(std::int64_t v) { return static_cast<int>(std::min<std::int64_t>(v, std::numeric_limits<int>::max())); }

template <class T>
Json arr(const std::vector<T>& v) {
  Json a = Json::array();
  for (const auto& x : v) a.push_back(x.to_json());
  return a;
}

std::optional<std::string> opt(const Json& q, const char* key) { return json::get_opt_string(q, key); }

Result<std::string> latest_run(kb::KnowledgeStore& ks) {
  LOOM_TRY_ASSIGN(auto runs, ks.list_runs(50));
  for (const auto& r : runs) {
    if (r.status == "done") return r.id;
  }
  if (!runs.empty()) return runs.front().id;
  return Error(Errc::NotFound, "no knowledge run yet");
}

template <class E>
Result<std::optional<E>> opt_enum(const Json& q, const char* key) {
  auto s = json::get_opt_string(q, key);
  if (!s) return std::optional<E>{};
  LOOM_TRY_ASSIGN(E e, model::parse<E>(*s, key));
  return std::optional<E>(e);
}

Result<Json> run_query(kb::KnowledgeStore& ks, const Json& q) {
  std::string run = json::get_string(q, "run");
  if (run.empty()) {
    LOOM_TRY_ASSIGN(run, latest_run(ks));
  }
  std::string what = json::get_string(q, "what");
  int limit = static_cast<int>(json::get_int(q, "limit", 10000));
  Json items;
  if (what == "claims") {
    kb::ClaimQuery cq;
    cq.subject = opt(q, "subject");
    cq.predicate = opt(q, "predicate");
    cq.object = opt(q, "object");
    cq.observation = opt(q, "observation");
    cq.branch = opt(q, "branch");
    LOOM_TRY_ASSIGN(cq.evidence, opt_enum<model::EvidenceClass>(q, "evidence"));
    LOOM_TRY_ASSIGN(cq.origin, opt_enum<model::Origin>(q, "origin"));
    LOOM_TRY_ASSIGN(cq.status, opt_enum<model::ClaimStatus>(q, "status"));
    cq.limit = limit;
    LOOM_TRY_ASSIGN(auto v, ks.query_claims(run, cq));
    items = arr(v);
  } else if (what == "entities") {
    kb::EntityQuery eq;
    eq.kind = opt(q, "kind");
    eq.canonical_key = opt(q, "canonical_key");
    eq.alias_key = opt(q, "alias_key");
    eq.parent = opt(q, "parent");
    eq.limit = limit;
    LOOM_TRY_ASSIGN(auto v, ks.query_entities(run, eq));
    items = arr(v);
  } else if (what == "instances") {
    LOOM_TRY_ASSIGN(auto v, ks.query_instances(run, json::get_string(q, "paradigm"), json::get_string(q, "subject")));
    items = arr(v);
  } else if (what == "slots") {
    kb::SlotQuery sq;
    sq.instance = opt(q, "instance");
    sq.slot = opt(q, "slot");
    sq.claim = opt(q, "claim");
    LOOM_TRY_ASSIGN(sq.role, opt_enum<model::Role>(q, "role"));
    sq.limit = limit;
    LOOM_TRY_ASSIGN(auto v, ks.query_slots(run, sq));
    items = arr(v);
  } else if (what == "principles") {
    LOOM_TRY_ASSIGN(auto v, ks.list_principles(run));
    items = arr(v);
  } else if (what == "operators") {
    LOOM_TRY_ASSIGN(auto v, ks.list_operators(run));
    items = arr(v);
  } else if (what == "morphisms") {
    LOOM_TRY_ASSIGN(auto v, ks.list_morphisms(run));
    items = arr(v);
  } else if (what == "decisions") {
    LOOM_TRY_ASSIGN(auto v, ks.list_decisions(run, json::get_string(q, "subject")));
    items = arr(v);
  } else if (what == "forks") {
    LOOM_TRY_ASSIGN(auto v, ks.list_forks(run, json::get_string(q, "subject")));
    items = arr(v);
  } else if (what == "areas") {
    LOOM_TRY_ASSIGN(auto v, ks.list_areas(run, json::get_string(q, "subject")));
    items = arr(v);
  } else if (what == "predictions") {
    LOOM_TRY_ASSIGN(auto v, ks.list_predictions(run));
    items = arr(v);
  } else if (what == "models") {
    LOOM_TRY_ASSIGN(auto v, ks.list_models(run));
    items = arr(v);
  } else if (what == "products") {
    LOOM_TRY_ASSIGN(auto v, ks.list_products(run));
    items = arr(v);
  } else if (what == "status_history") {
    LOOM_TRY_ASSIGN(auto v, ks.status_history(run, json::get_string(q, "entity")));
    items = arr(v);
  } else if (what == "stats") {
    LOOM_TRY_ASSIGN(items, ks.stats(run));
  } else {
    return Error(Errc::InvalidArgument, "unknown query 'what': " + what);
  }
  return Json{{"run", run}, {"items", items}};
}

}  // namespace

extern "C" {

LOOM_API const char* loom_kb_pack(LoomContext* ctx) {
  return guard_json("loom_kb_pack", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto p = ctx->rt->knowledge().pack();
    if (!p) return out_error(p.error());
    return out((*p)->manifest());
  });
}

LOOM_API const char* loom_kb_policy(LoomContext* ctx, const char* name) {
  return guard_json("loom_kb_policy", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!name || !*name) return out_error(missing("name"));
    auto p = ctx->rt->knowledge().pack();
    if (!p) return out_error(p.error());
    std::string n(name);
    const Json* d = &(*p)->policy(n);
    if (!d->is_object() && n == "goal_types") d = &(*p)->file("goals/goal_types.json");
    if (!d->is_object() && n == "anchoring") d = &(*p)->file("morphisms/anchoring.json");
    if (!d->is_object()) d = &(*p)->file(n);
    if (!d->is_object()) return out_error(Errc::NotFound, "no pack document '" + n + "'");
    return out(*d);
  });
}

LOOM_API const char* loom_kb_runs(LoomContext* ctx, int limit) {
  return guard_json("loom_kb_runs", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto v = ctx->rt->knowledge().store().list_runs(limit <= 0 ? 50 : limit);
    if (!v) return out_error(v.error());
    return out(arr(*v));
  });
}

LOOM_API const char* loom_kb_query(LoomContext* ctx, const char* query_json) {
  return guard_json("loom_kb_query", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto q = parse_arg(query_json);
    if (!q) return out_error(q.error());
    if (!q->is_object()) return out_error(Errc::InvalidArgument, "query must be a JSON object");
    return out_result(run_query(ctx->rt->knowledge().store(), *q));
  });
}

LOOM_API const char* loom_kb_judge(LoomContext* ctx, const char* judgement_json) {
  return guard_json("loom_kb_judge", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!judgement_json || !*judgement_json) return out_error(missing("judgement_json"));
    auto j = json::parse(judgement_json);
    if (!j) return out_error(j.error());
    std::string replay = json::get_string(*j, "replay_run");
    Json body = *j;
    if (body.is_object()) body.erase("replay_run");
    auto parsed = model::Judgement::from_json(body);
    if (!parsed) return out_error(parsed.error());
    auto& ks = ctx->rt->knowledge().store();
    auto stored = ks.add_judgement(*parsed);
    if (!stored) return out_error(stored.error());
    Json res = stored->to_json();
    if (!replay.empty()) {
      auto rep = ks.replay_judgements(replay);
      if (!rep) return out_error(rep.error());
      res["replay"] = rep->to_json();
    }
    return out(res);
  });
}

LOOM_API const char* loom_knowledge_run(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud) {
  return guard_json("loom_knowledge_run", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto cj = parse_arg(config_json);
    if (!cj) return out_error(cj.error());
    auto cfg = knowledge::KnowledgeConfig::from_json(*cj);
    if (!cfg) return out_error(cfg.error());
    CancelToken token;
    {
      std::lock_guard lk(ctx->mu);
      if (ctx->knowledge_run) return out_error(Errc::Busy, "a knowledge run is already in progress");
      ctx->knowledge_run = token;
    }
    struct Clear {
      LoomContext* c;
      ~Clear() {
        std::lock_guard lk(c->mu);
        c->knowledge_run.reset();
      }
    } clear{ctx};
    knowledge::ProgressFn progress;
    if (cb) {
      progress = [cb, ud](std::string_view stage, std::int64_t cur, std::int64_t total, std::string_view msg) {
        std::string s = std::string(stage) + ": " + std::string(msg);
        cb(clamp_int(cur), total < 0 ? -1 : clamp_int(total), s.c_str(), ud);
      };
    }
    auto r = ctx->rt->knowledge().run(*cfg, progress, &token);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API int loom_knowledge_cancel(LoomContext* ctx) {
  return guard_int("loom_knowledge_cancel", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    std::lock_guard lk(ctx->mu);
    if (!ctx->knowledge_run) return LOOM_E_NOT_FOUND;
    ctx->knowledge_run->cancel();
    return LOOM_OK;
  });
}

LOOM_API const char* loom_knowledge_status(LoomContext* ctx, const char* task_id) {
  return guard_json("loom_knowledge_status", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_result(ctx->rt->knowledge().status(task_id ? task_id : ""));
  });
}

}  // extern "C"
