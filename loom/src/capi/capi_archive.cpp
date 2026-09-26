// C API: Archive Intelligence runs, artifacts, and loom_import_file_ex.
#include <limits>

#include "context.h"
#include "loom/archive.h"
#include "loom/importer.h"
#include "loom/provenance.h"

using namespace loom;
using namespace loom::capi;

namespace {
int clamp_int(std::int64_t v) { return static_cast<int>(std::min<std::int64_t>(v, std::numeric_limits<int>::max())); }
}  // namespace

extern "C" {

LOOM_API const char* loom_import_file_ex(LoomContext* ctx, const char* path, const char* options_json,
                                         LoomProgressCallback cb, void* ud) {
  return guard_json("loom_import_file_ex", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!path || !*path) return out_error(missing("path"));
    auto opts_j = parse_arg(options_json);
    if (!opts_j) return out_error(opts_j.error());
    if (!opts_j->is_object()) return out_error(Errc::InvalidArgument, "options must be a JSON object");
    ImportOptions opts;
    for (auto it = opts_j->begin(); it != opts_j->end(); ++it) {
      const std::string& k = it.key();
      const Json& v = it.value();
      if (k == "title" && (v.is_string() || v.is_null())) {
        if (v.is_string() && !v.get<std::string>().empty()) opts.title = v.get<std::string>();
      } else if (k == "force" && v.is_boolean()) {
        opts.force = v.get<bool>();
      } else if (k == "record_provenance" && v.is_boolean()) {
        opts.record_provenance = v.get<bool>();
      } else {
        return out_error(Errc::InvalidArgument, "unknown or invalid import option: " + k);
      }
    }
    if (cb) {
      opts.progress = [cb, ud](std::int64_t cur, std::int64_t total, std::string_view status) {
        std::string s(status);
        cb(clamp_int(cur), total < 0 ? -1 : clamp_int(total), s.c_str(), ud);
      };
    }
    auto r = ctx->rt->importer().import_file(path, opts);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API const char* loom_archive_run(LoomContext* ctx, const char* config_json, LoomProgressCallback cb, void* ud) {
  return guard_json("loom_archive_run", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto cj = parse_arg(config_json);
    if (!cj) return out_error(cj.error());
    auto cfg = archive::ArchiveConfig::from_json(*cj);
    if (!cfg) return out_error(cfg.error());
    CancelToken token;
    {
      std::lock_guard lk(ctx->mu);
      if (ctx->archive_run) return out_error(Errc::Busy, "an archive run is already in progress");
      ctx->archive_run = token;
    }
    struct Clear {
      LoomContext* c;
      ~Clear() {
        std::lock_guard lk(c->mu);
        c->archive_run.reset();
      }
    } clear{ctx};
    archive::ArchiveProgressFn progress;
    if (cb) {
      progress = [cb, ud](std::string_view stage, std::int64_t cur, std::int64_t total, std::string_view msg) {
        std::string s = std::string(stage) + ": " + std::string(msg);
        cb(clamp_int(cur), total < 0 ? -1 : clamp_int(total), s.c_str(), ud);
      };
    }
    auto r = ctx->rt->archive().run(*cfg, progress, &token);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API int loom_archive_cancel(LoomContext* ctx) {
  return guard_int("loom_archive_cancel", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    std::lock_guard lk(ctx->mu);
    if (!ctx->archive_run) return LOOM_E_NOT_FOUND;
    ctx->archive_run->cancel();
    return LOOM_OK;
  });
}

LOOM_API const char* loom_archive_status(LoomContext* ctx, const char* run_id) {
  return guard_json("loom_archive_status", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_result(ctx->rt->archive().status(run_id ? run_id : ""));
  });
}

LOOM_API const char* loom_list_artifacts(LoomContext* ctx, const char* filter_json) {
  return guard_json("loom_list_artifacts", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto f = parse_arg(filter_json);
    if (!f) return out_error(f.error());
    int limit = static_cast<int>(json::get_int(*f, "limit", 100));
    if (limit <= 0) limit = 100;
    std::optional<std::string> kind = json::get_opt_string(*f, "kind");
    std::optional<std::string> run = json::get_opt_string(*f, "run_id");
    auto v = ctx->rt->provenance().list_artifacts(run ? 100000 : limit,
                                                  kind ? std::optional<std::string_view>(*kind) : std::nullopt);
    if (!v) return out_error(v.error());
    Json a = Json::array();
    for (const auto& x : *v) {
      if (run && x.task_id != *run) continue;
      a.push_back(x.to_json());
      if (static_cast<int>(a.size()) >= limit) break;
    }
    return out(a);
  });
}

LOOM_API const char* loom_get_artifact(LoomContext* ctx, const char* artifact_id, int include_content) {
  return guard_json("loom_get_artifact", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!artifact_id || !*artifact_id) return out_error(missing("artifact_id"));
    auto a = ctx->rt->provenance().get_artifact(artifact_id);
    if (!a) return out_error(a.error());
    if (!*a) return out_error(Errc::NotFound, std::string("artifact not found: ") + artifact_id);
    Json j{{"artifact", (*a)->to_json()}};
    if (include_content) {
      auto content = ctx->rt->blobs().read((*a)->blob_hash);
      if (!content) return out_error(content.error());
      j["content"] = std::move(*content);
    }
    return out(j);
  });
}

}  // extern "C"
