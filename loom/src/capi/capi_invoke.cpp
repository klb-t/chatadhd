// C API: loom_invoke — one entry point "perform action" (port, action, json). Ports: "storage" (storage
// plugins), "github" (GitHub sync), "actions" (action "list" = the action catalogue; "for", "explain" and
// "propose_chain" ask the action/element matching engine, loom/action_rules.h).
#include "loom/action_rules.h"
#include "loom/action_table.h"
#include "context.h"
#include "loom/storage_command.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

LOOM_API const char* loom_invoke(LoomContext* ctx, const char* port, const char* action, const char* request_json) {
  return guard_json("loom_invoke", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!port || !*port) return out_error(missing("port"));
    if (!action || !*action) return out_error(missing("action"));
    auto rj = parse_arg(request_json, Json::object());
    if (!rj) return out_error(rj.error());
    if (rj->is_null()) *rj = Json::object();
    if (!rj->is_object()) return out_error(Errc::InvalidArgument, "request must be a JSON object");
    const std::string p = port, a = action;
    if (p == "actions" && a == "list") return out(action_list(ctx->rt->paths().root));
    if (p == "actions") {
      auto r = actions::invoke(ctx->rt->paths().root, a, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "storage") {
      (*rj)["action"] = a.rfind("storage.", 0) == 0 ? a : "storage." + a;
      auto r = storage::run_command(*ctx->rt, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "github") {
      if (a.rfind("storage.", 0) == 0) return out_error(Errc::InvalidArgument, "storage actions belong to port \"storage\"");
      (*rj)["action"] = a;
      return loom_github_sync(ctx, json::dump(*rj).c_str());
    }
    return out_error(Errc::InvalidArgument, "unknown port '" + p + "'");
  });
}

}  // extern "C"
