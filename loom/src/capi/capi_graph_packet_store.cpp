#include "context.h"
#include "loom/graph_packet_store.h"

using namespace loom;
using namespace loom::capi;

extern "C" LOOM_API const char* loom_graph_packet_store(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_graph_packet_store", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto request = parse_arg(request_json);
    if (!request) return out_error(request.error());
    kb::GraphPacketStore adapter(ctx->rt->db());
    return out_result(adapter.execute(*request));
  });
}
