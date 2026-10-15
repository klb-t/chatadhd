// C API: the effective view, edge kinds, perspectives and the scope resolver (P24). See loom/effective_view.h, loom/perspectives.h,
// loom/scope_resolver.h. Reads never write; loom_view_write only ever writes the user's overlay <data_dir>/views/<overlay>.json.
#include "context.h"
#include "loom/effective_view.h"
#include "loom/interaction.h"
#include "loom/perspectives.h"
#include "loom/scope_resolver.h"
#include "loom/view_engine.h"

using namespace loom;
using namespace loom::capi;

namespace {

std::vector<std::filesystem::path> extra_dirs_of(const Json& request) {
  std::vector<std::filesystem::path> out;
  const Json dirs = request.value("extra_dirs", Json::array());
  if (dirs.is_array())
    for (const auto& d : dirs)
      if (d.is_string()) out.emplace_back(d.get<std::string>());
  return out;
}

views::RenderContext render_context(LoomContext* ctx) {
  views::RenderContext rc;
  rc.db = &ctx->rt->db();
  rc.cfg = &ctx->rt->config();
  rc.root = ctx->rt->paths().root;
  return rc;
}

}  // namespace

extern "C" {

LOOM_API const char* loom_view_effective(LoomContext* ctx, const char* kind, const char* request_json) {
  return guard_json("loom_view_effective", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!kind || !*kind) return out_error(missing("kind"));
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    auto eff = views::effective(ctx->rt->paths().root, kind, extra_dirs_of(*rj));
    if (!eff) return out_error(eff.error());
    return out(eff->to_json(json::get_bool(*rj, "include_hidden", true)));
  });
}

LOOM_API const char* loom_view_write(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_view_write", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    const std::string overlay = json::get_string(*rj, "overlay", "user");
    const auto& root = ctx->rt->paths().root;
    if (json::get_bool(*rj, "remove", false)) {
      auto n = views::remove_ops(root, overlay, json::get_string(*rj, "view"), json::get_string(*rj, "id"));
      if (!n) return out_error(n.error());
      return out(Json{{"overlay", overlay}, {"removed", *n}});
    }
    const Json op = rj->value("op", Json::object());
    if (auto st = views::put_op(root, overlay, op); !st) return out_error(st.error());
    return out(Json{{"overlay", overlay}, {"written", op}});
  });
}

LOOM_API const char* loom_edge_kinds(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_edge_kinds", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    const auto extra = extra_dirs_of(*rj);
    auto kinds = views::EdgeKinds::open(ctx->rt->paths().root, extra);
    if (!kinds) return out_error(kinds.error());
    auto persp = views::effective(ctx->rt->paths().root, "perspectives", extra);
    if (!persp) return out_error(persp.error());
    const std::string one = json::get_string(*rj, "kind");
    if (!one.empty()) return out(kinds->describe(one, *persp));
    if (json::get_bool(*rj, "classify", false)) {
      auto used = views::link_types_in_use(ctx->rt->db());
      if (!used) return out_error(used.error());
      return out(kinds->classify(*used, *persp));
    }
    Json list = Json::array();
    for (const auto& e : kinds->view().entries) list.push_back(kinds->describe(e.id, *persp));
    return out(Json{{"schema", "loom.edge_kinds/1"}, {"kinds", list}, {"issues", kinds->view().issues}});
  });
}

LOOM_API const char* loom_perspectives(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_perspectives", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    auto p = views::Perspectives::open(ctx->rt->paths().root, extra_dirs_of(*rj));
    if (!p) return out_error(p.error());
    std::vector<std::string> ids;
    if (rj->contains("ids") && (*rj)["ids"].is_array())
      for (const auto& i : (*rj)["ids"])
        if (i.is_string()) ids.push_back(i.get<std::string>());
    const std::string one = json::get_string(*rj, "id");
    if (!one.empty()) ids.push_back(one);
    if (ids.empty()) return out(Json{{"schema", "loom.perspectives/1"}, {"perspectives", p->list()}, {"issues", p->view().issues}});
    if (ids.size() == 1 && !one.empty()) {
      auto r = p->render(render_context(ctx), one, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    auto r = p->render_many(render_context(ctx), ids, *rj);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_view_model(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_view_model", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    auto engine = views::ViewEngine::open(ctx->rt->paths().root, extra_dirs_of(*rj));
    if (!engine) return out_error(engine.error());
    const Json decl = rj->value("declaration", Json(nullptr));
    std::string id = json::get_string(*rj, "id");
    if (decl.is_string()) id = decl.get<std::string>();
    if (id.empty() && !decl.is_object()) return out(engine->list());
    auto r = decl.is_object() ? engine->evaluate_declaration(render_context(ctx), decl, *rj) : engine->evaluate(render_context(ctx), id, *rj);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_view_interaction(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_view_interaction", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    auto it = views::Interaction::open(ctx->rt->paths().root, extra_dirs_of(*rj));
    if (!it) return out_error(it.error());
    return out(it->resolve(*rj));
  });
}

LOOM_API const char* loom_scope_resolve(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_scope_resolve", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    scopes::Binding b;
    b.key = json::get_string(*rj, "key");
    if (b.key.empty()) return out_error(missing("key"));
    b.conv_id = json::get_string(*rj, "conversation_id");
    b.person_setting = json::get_string(*rj, "person_setting");
    if (rj->contains("installation")) b.installation = (*rj)["installation"];
    if (rj->contains("units")) b.units = (*rj)["units"];
    if (rj->contains("unit_values")) b.unit_values = (*rj)["unit_values"];
    auto resolver = scopes::Resolver::open(ctx->rt->paths().root, &ctx->rt->config(), &ctx->rt->db());
    if (!resolver) return out_error(resolver.error());
    if (json::get_bool(*rj, "inheritance", false)) {
      auto r = resolver->inheritance(b);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    auto r = resolver->resolve(b);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

}  // extern "C"
