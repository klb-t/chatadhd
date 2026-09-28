// C API: catalog (include/loom/catalog.h). JSON shapes are documented there.
#include <limits>

#include "context.h"
#include "loom/catalog.h"
#include "loom/knowledge.h"

using namespace loom;
using namespace loom::capi;

namespace {
int clamp_int(std::int64_t v) { return static_cast<int>(std::min<std::int64_t>(v, std::numeric_limits<int>::max())); }

Result<std::shared_ptr<const kb::Pack>> pack_of(LoomContext* ctx) { return ctx->rt->knowledge().pack(); }
}  // namespace

extern "C" {

LOOM_API const char* loom_catalog_scan(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud) {
  return guard_json("loom_catalog_scan", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto cj = parse_arg(config_json);
    if (!cj) return out_error(cj.error());
    auto cfg = catalog::ScanConfig::from_json(*cj);
    if (!cfg) return out_error(cfg.error());
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    catalog::ProgressFn progress;
    if (cb) {
      progress = [cb, ud](std::string_view step, std::int64_t cur, std::int64_t total, std::string_view msg) {
        std::string s = std::string(step) + ": " + std::string(msg);
        cb(clamp_int(cur), total < 0 ? -1 : clamp_int(total), s.c_str(), ud);
      };
    }
    auto r = cat.scan(*cfg, progress);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_catalog_score(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud) {
  return guard_json("loom_catalog_score", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto cj = parse_arg(config_json);
    if (!cj) return out_error(cj.error());
    auto cfg = catalog::ScoreConfig::from_json(*cj);
    if (!cfg) return out_error(cfg.error());
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    catalog::ProgressFn progress;
    if (cb) {
      progress = [cb, ud](std::string_view step, std::int64_t cur, std::int64_t total, std::string_view msg) {
        std::string s = std::string(step) + ": " + std::string(msg);
        cb(clamp_int(cur), total < 0 ? -1 : clamp_int(total), s.c_str(), ud);
      };
    }
    auto r = cat.score(*cfg, progress);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_catalog_select(LoomContext* ctx, const char* run_id) {
  return guard_json("loom_catalog_select", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    auto decisions = cat.select(run_id ? run_id : "");
    if (!decisions) return out_error(decisions.error());
    Json arr = Json::array();
    for (const auto& d : *decisions) arr.push_back(d.to_json());
    return out(Json{{"decisions", arr}});
  });
}

LOOM_API const char* loom_catalog_query(LoomContext* ctx, const char* query_json) {
  return guard_json("loom_catalog_query", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto qj = parse_arg(query_json);
    if (!qj) return out_error(qj.error());
    auto q = catalog::UnitQuery::from_json(*qj);
    if (!q) return out_error(q.error());
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    auto r = cat.query(*q);
    if (!r) return out_error(r.error());
    Json arr = Json::array();
    for (const auto& u : *r) arr.push_back(u.to_json());
    return out(arr);
  });
}

LOOM_API const char* loom_catalog_preview(LoomContext* ctx, const char* unit_id) {
  return guard_json("loom_catalog_preview", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!unit_id || !*unit_id) return out_error(missing("unit_id"));
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    auto r = cat.preview(unit_id);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

LOOM_API const char* loom_catalog_override(LoomContext* ctx, const char* override_json) {
  return guard_json("loom_catalog_override", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto oj = parse_arg(override_json);
    if (!oj) return out_error(oj.error());
    auto o = catalog::Override::from_json(*oj);
    if (!o) return out_error(o.error());
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    auto st = cat.set_override(*o);
    if (!st) return out_error(st.error());
    auto decisions = cat.select("");
    Json arr = Json::array();
    if (decisions) {
      for (const auto& d : *decisions) arr.push_back(d.to_json());
    }
    return out(Json{{"decisions", arr}});
  });
}

LOOM_API const char* loom_catalog_import(LoomContext* ctx, const char* options_json, LoomProgressCallback cb, void* ud) {
  return guard_json("loom_catalog_import", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto oj = parse_arg(options_json);
    if (!oj) return out_error(oj.error());
    auto opts = catalog::ImportOptions::from_json(*oj);
    if (!opts) return out_error(opts.error());
    auto pack = pack_of(ctx);
    if (!pack) return out_error(pack.error());
    catalog::Catalog cat(*ctx->rt, *pack);
    catalog::ProgressFn progress;
    if (cb) {
      progress = [cb, ud](std::string_view step, std::int64_t cur, std::int64_t total, std::string_view msg) {
        std::string s = std::string(step) + ": " + std::string(msg);
        cb(clamp_int(cur), total < 0 ? -1 : clamp_int(total), s.c_str(), ud);
      };
    }
    auto r = cat.import_selected(*opts, progress);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

}  // extern "C"
