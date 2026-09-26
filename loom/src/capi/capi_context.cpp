// C API: context+materialize — STUB: knowledge-wave. The context+materialize area replaces the bodies (the
// signatures are fixed by include/loom/loom.h).
#include "context.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

LOOM_API const char* loom_context_build(LoomContext* ctx, const char*) {
  return guard_json("loom_context_build", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_context_build is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}
LOOM_API const char* loom_materialize(LoomContext* ctx, const char*) {
  return guard_json("loom_materialize", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_materialize is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}

}  // extern "C"
