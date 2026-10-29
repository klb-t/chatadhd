// C ABI of the working-copy bundle (R103 B1b, include/loom/working_copy.h).
#include "context.h"
#include "loom/working_copy.h"

using namespace loom;
using namespace loom::capi;

LOOM_API const char* loom_working_copy_export(LoomContext* ctx, const char* options_json) {
  return guard_json("loom_working_copy_export", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto opts = parse_arg(options_json);
    if (!opts) return out_error(opts.error());
    return out_result(working_copy::export_bundle(*ctx->rt, *opts));
  });
}

LOOM_API const char* loom_working_copy_import(LoomContext* ctx, const char* bundle_json, const char* options_json) {
  return guard_json("loom_working_copy_import", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!bundle_json || !*bundle_json) return out_error(missing("bundle_json"));
    auto bundle = json::parse(bundle_json);
    if (!bundle) return out_error(bundle.error());
    auto opts = parse_arg(options_json);
    if (!opts) return out_error(opts.error());
    return out_result(working_copy::import_bundle(*ctx->rt, *bundle, *opts));
  });
}
