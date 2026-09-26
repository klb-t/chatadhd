// C API: generalize — STUB: knowledge-wave. The generalize area replaces the bodies (the
// signatures are fixed by include/loom/loom.h).
#include "context.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

LOOM_API const char* loom_generalize_predict(LoomContext* ctx, const char*) {
  return guard_json("loom_generalize_predict", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out_error(Errc::NotImplemented, "loom_generalize_predict is not implemented yet (knowledge wave)");  // STUB: knowledge-wave
  });
}

}  // extern "C"
