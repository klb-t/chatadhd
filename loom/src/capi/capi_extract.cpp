// C API: extract+resolve — STUB: knowledge-wave. The extract+resolve area replaces the bodies (the
// signatures are fixed by include/loom/loom.h).
#include "context.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

LOOM_API const char* loom_extract_preview(LoomContext* ctx, const char*, const char*) {
  return guard_json("loom_extract_preview", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_extract_preview is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_resolve_lineage(LoomContext* ctx, const char*) {
  return guard_json("loom_resolve_lineage", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_resolve_lineage is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}

}  // extern "C"
