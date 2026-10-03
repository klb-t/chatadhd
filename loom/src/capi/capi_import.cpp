// C API: import / export.
// OWNER (wave 2): importer. Wrappers complete; not_implemented until
// ConversationImporter / ConversationExporter land.
#include <limits>

#include "context.h"
#include "loom/importer.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

LOOM_API const char* loom_detect_format(LoomContext* ctx, const char* path) {
  return guard_json("loom_detect_format", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!path || !*path) return out_error(missing("path"));
    return out(Json{{"format", ctx->rt->importer().detect_format(path)}});
  });
}

LOOM_API const char* loom_import_file(LoomContext* ctx, const char* path, const char* title, LoomProgressCallback cb,
                                      void* ud) {
  return guard_json("loom_import_file", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!path || !*path) return out_error(missing("path"));
    ImportOptions opts;
    opts.title = opt_str(title);
    if (cb) {
      opts.progress = [cb, ud](std::int64_t cur, std::int64_t total, std::string_view status) {
        auto clamp = [](std::int64_t v) {
          return static_cast<int>(std::min<std::int64_t>(v, std::numeric_limits<int>::max()));
        };
        std::string s(status);
        cb(clamp(cur), total < 0 ? -1 : clamp(total), s.c_str(), ud);
      };
    }
    auto r = ctx->rt->importer().import_file(path, opts);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

LOOM_API const char* loom_export_conversation(LoomContext* ctx, const char* conv_id, const char* fmt) {
  return guard_json("loom_export_conversation", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!conv_id || !*conv_id) return out_error(missing("conv_id"));
    std::string format = fmt && *fmt ? fmt : "json";
    auto r = ctx->rt->exporter().export_conversation(conv_id, format);
    if (!r) return out_error(r.error());
    return out(Json{{"format", format}, {"content", *r}});
  });
}

}  // extern "C"
