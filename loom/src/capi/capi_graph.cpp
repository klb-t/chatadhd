// C API: knowledge graph, context selection, memory tree.
// OWNER (wave 2): semantic/graph. Node/edge/graph-data queries run on the
// (complete) Database; expand/reindex/context/memory return not_implemented
// until GraphEngine/GraphMemorySelector/ContextSelector/MemoryEngine land.
#include "context.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"

using namespace loom;
using namespace loom::capi;

namespace {

Json memory_nodes_json(const std::vector<MemoryNode>& nodes) {
  Json arr = Json::array();
  for (const auto& n : nodes) arr.push_back(n.to_json());
  return arr;
}

const char* memory_node_out(MemoryEngine& mem, const std::string& id) {
  auto n = mem.get_node(id);
  if (!n) return out_error(Errc::NotFound, "memory node not found: " + id);
  return out(n->to_json());
}

}  // namespace

extern "C" {

LOOM_API const char* loom_get_nodes(LoomContext* ctx, const char* filter_json) {
  return guard_json("loom_get_nodes", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto fj = parse_arg(filter_json);
    if (!fj) return out_error(fj.error());
    auto kind = json::get_opt_string(*fj, "kind");
    auto label = json::get_opt_string(*fj, "label");
    int limit = static_cast<int>(json::get_int(*fj, "limit", 200));
    auto& db = ctx->rt->db();
    Json arr = Json::array();
    if (label) {
      auto n = db.find_node(*label, kind ? std::optional<std::string_view>(*kind) : std::nullopt);
      if (!n) return out_error(n.error());
      if (*n) arr.push_back((*n)->to_json());
      return out(arr);
    }
    auto r = db.list_nodes(kind ? std::optional<std::string_view>(*kind) : std::nullopt, limit > 0 ? limit : 200);
    if (!r) return out_error(r.error());
    for (const auto& n : *r) arr.push_back(n.to_json());
    return out(arr);
  });
}

LOOM_API const char* loom_get_edges(LoomContext* ctx, const char* filter_json) {
  return guard_json("loom_get_edges", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto fj = parse_arg(filter_json);
    if (!fj) return out_error(fj.error());
    auto node_id = json::get_opt_string(*fj, "node_id");
    auto link_type = json::get_opt_string(*fj, "link_type");
    std::int64_t limit = json::get_int(*fj, "limit", 0);
    auto r = ctx->rt->db().get_links(node_id ? std::optional<std::string_view>(*node_id) : std::nullopt,
                                     link_type ? std::optional<std::string_view>(*link_type) : std::nullopt);
    if (!r) return out_error(r.error());
    Json arr = Json::array();
    for (const auto& l : *r) {
      if (limit > 0 && static_cast<std::int64_t>(arr.size()) >= limit) break;
      arr.push_back(l.to_json());
    }
    return out(arr);
  });
}

LOOM_API const char* loom_expand_graph(LoomContext* ctx, const char* seed_ids_json, int depth) {
  return guard_json("loom_expand_graph", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto sj = parse_arg(seed_ids_json, Json(nullptr));
    if (!sj) return out_error(sj.error());
    if (!sj->is_array()) return out_error(Errc::InvalidArgument, "seed_ids_json must be a JSON array of ids");
    std::vector<std::string> seeds;
    for (const auto& s : *sj) {
      if (!s.is_string()) return out_error(Errc::InvalidArgument, "seed ids must be strings");
      seeds.push_back(s.get<std::string>());
    }
    auto r = ctx->rt->graph_memory().expand(seeds, depth < 0 ? 1 : depth);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_get_graph_data(LoomContext* ctx, const char* conv_id) {
  return guard_json("loom_get_graph_data", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto r = ctx->rt->db().get_graph_data(conv_id && *conv_id ? std::optional<std::string_view>(conv_id)
                                                                : std::nullopt);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_graph_reindex(LoomContext* ctx, const char* conv_id) {
  return guard_json("loom_graph_reindex", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto& g = ctx->rt->graph();
    int n = (conv_id && *conv_id) ? g.reindex_conversation(conv_id) : g.reindex_all();
    return out(Json{{"reindexed", n}});
  });
}

LOOM_API const char* loom_select_context(LoomContext* ctx, const char* text, int depth, int max_tokens) {
  return guard_json("loom_select_context", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!text) return out_error(missing("text"));
    ContextRequest req;
    req.text = text;
    req.depth = depth > 0 ? depth : 0;
    req.max_tokens = max_tokens > 0 ? max_tokens : 4000;
    auto r = ctx->rt->context().select(req);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API const char* loom_select_context_ex(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_select_context_ex", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto rj = parse_arg(request_json, Json(nullptr));
    if (!rj) return out_error(rj.error());
    auto req = ContextRequest::from_json(*rj);
    if (!req) return out_error(req.error());
    auto r = ctx->rt->context().select(*req);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

// ── Memory tree ────────────────────────────────────────────────────
LOOM_API const char* loom_list_memory(LoomContext* ctx) {
  return guard_json("loom_list_memory", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(Json{{"nodes", memory_nodes_json(ctx->rt->memory().get_all())}});
  });
}

LOOM_API const char* loom_create_memory(LoomContext* ctx, const char* json_arg) {
  return guard_json("loom_create_memory", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto j = parse_arg(json_arg, Json(nullptr));
    if (!j) return out_error(j.error());
    if (!j->is_object()) return out_error(Errc::InvalidArgument, "memory node must be a JSON object");
    const Json* content = json::find(*j, "content");
    if (!content || !content->is_string()) return out_error(missing("content"));
    std::vector<std::string> tags;
    if (const Json* t = json::find(*j, "tags"); t && t->is_array()) {
      for (const auto& x : *t) {
        if (x.is_string()) tags.push_back(x.get<std::string>());
      }
    }
    Json meta = Json::object();
    if (const Json* m = json::find(*j, "metadata"); m && m->is_object()) meta = *m;
    auto& mem = ctx->rt->memory();
    auto id = mem.add_node(content->get<std::string>(), json::get_opt_string(*j, "parent_id"),
                           json::get_string(*j, "node_type", "text"), std::move(meta), std::move(tags));
    if (!id) return out_error(id.error());
    return memory_node_out(mem, *id);
  });
}

LOOM_API const char* loom_update_memory(LoomContext* ctx, const char* id, const char* json_arg) {
  return guard_json("loom_update_memory", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!id || !*id) return out_error(missing("id"));
    auto j = parse_arg(json_arg, Json(nullptr));
    if (!j) return out_error(j.error());
    if (!j->is_object()) return out_error(Errc::InvalidArgument, "update must be a JSON object");
    auto& mem = ctx->rt->memory();
    if (auto st = mem.update_node(id, *j); !st) return out_error(st.error());
    return memory_node_out(mem, id);
  });
}

LOOM_API int loom_delete_memory(LoomContext* ctx, const char* id) {
  return guard_int("loom_delete_memory", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!id || !*id) return LOOM_E_INVALID_ARGUMENT;
    return code(ctx->rt->memory().delete_node(id, /*recursive=*/true));
  });
}

LOOM_API const char* loom_get_memory_context(LoomContext* ctx, int max_chars) {
  return guard_json("loom_get_memory_context", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    std::size_t n = max_chars > 0 ? static_cast<std::size_t>(max_chars) : 16000;
    return out(Json{{"context", ctx->rt->memory().get_active_context(n)}});
  });
}

}  // extern "C"
