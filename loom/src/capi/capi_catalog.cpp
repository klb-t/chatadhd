// C API: catalog — STUB: knowledge-wave. The catalog area replaces the bodies (the
// signatures are fixed by include/loom/loom.h).
#include "context.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

LOOM_API const char* loom_catalog_scan(LoomContext* ctx, const char*, LoomProgressCallback, void*) {
  return guard_json("loom_catalog_scan", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_catalog_scan is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_catalog_score(LoomContext* ctx, const char*, LoomProgressCallback, void*) {
  return guard_json("loom_catalog_score", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_catalog_score is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_catalog_query(LoomContext* ctx, const char*) {
  return guard_json("loom_catalog_query", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_catalog_query is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_catalog_preview(LoomContext* ctx, const char*) {
  return guard_json("loom_catalog_preview", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_catalog_preview is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_catalog_override(LoomContext* ctx, const char*) {
  return guard_json("loom_catalog_override", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_catalog_override is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_catalog_import(LoomContext* ctx, const char*, LoomProgressCallback, void*) {
  return guard_json("loom_catalog_import", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_catalog_import is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}

}  // extern "C"
