// C API: context+materialize (context_engine.h, materialize.h).
#include "context.h"

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/materialize.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

/* ContextRequest JSON -> {"context_set":{...},"text":"..."} (loom.h). */
LOOM_API const char* loom_context_build(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_context_build", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto req_j = parse_arg(request_json);
    if (!req_j) return out_error(req_j.error());
    auto req = context::ContextRequest::from_json(*req_j);
    if (!req) return out_error(req.error());
    auto pack = ctx->rt->knowledge().pack();
    if (!pack) return out_error(pack.error());
    context::ContextEngine engine(*ctx->rt, ctx->rt->knowledge().store(), *pack);
    auto set = engine.select(*req);
    if (!set) return out_error(set.error());
    auto text = engine.render(*set);
    if (!text) return out_error(text.error());
    return out(Json{{"context_set", set->to_json()}, {"text", *text}});
  });
}

/* {"kind":"self_description|dossier|backlog|extrapolated_spec","run"?,"instance"?} -> Rendered JSON (loom.h). */
LOOM_API const char* loom_materialize(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_materialize", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto req_j = parse_arg(request_json);
    if (!req_j) return out_error(req_j.error());
    std::string kind = json::get_string(*req_j, "kind");
    std::string run = json::get_string(*req_j, "run");
    std::string instance = json::get_string(*req_j, "instance");
    auto pack = ctx->rt->knowledge().pack();
    if (!pack) return out_error(pack.error());
    materialize::Materializer m(*ctx->rt, ctx->rt->knowledge().store(), *pack);
    Result<materialize::Rendered> r = Error(Errc::InvalidArgument, "");
    if (kind == "self_description") {
      r = m.self_description(run);
    } else if (kind == "dossier") {
      if (instance.empty()) return out_error(missing("instance"));
      r = m.dossier(run, instance);
    } else if (kind == "backlog") {
      r = m.backlog(run);
    } else if (kind == "extrapolated_spec") {
      if (instance.empty()) return out_error(missing("instance"));
      r = m.extrapolated_spec(run, instance);
    } else {
      return out_error(Errc::InvalidArgument,
                       "kind must be one of: self_description, dossier, backlog, extrapolated_spec");
    }
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

}  // extern "C"
