// C API: loom_invoke — one entry point "perform action" (port, action, json). Ports: "storage" (storage
// plugins), "github" (GitHub sync), "actions" (action "list" = the action catalogue; "for", "explain" and
// "propose_chain" ask the action/element matching engine, loom/action_rules.h; "run", "confirm", "cancel",
// "status" and "preview" run actions through the interpreter, loom/action_runner.h), "chat" ("route" decides
// message or command for a chat input, loom/chat_route.h), "ports" (read-only: "resolve", "explain" and
// "roles" of the ports profile, loom/port_profile.h; request {role, conversation_id?}).
#include "loom/action_runner.h"
#include "loom/action_table.h"
#include "loom/chat_route.h"
#include "loom/port_profile.h"
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
      auto r = ctx->rt->actions().invoke(a, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "ports") {
      if (a != "resolve" && a != "explain" && a != "roles") return out_error(Errc::InvalidArgument, "unknown ports action '" + a + "'");
      auto prof = PortProfile::load(ctx->rt->paths().root);
      if (!prof) return out_error(prof.error());
      if (a == "roles") return out(prof->roles());
      const std::string role = json::get_string(*rj, "role");
      const std::string cid = json::get_string(*rj, "conversation_id");
      Json meta = Json::object();
      if (!cid.empty()) {
        auto conv = ctx->rt->db().get_conv(cid);
        if (!conv) return out_error(conv.error());
        if (!*conv) return out_error(Errc::NotFound, "conversation not found: " + cid);
        meta = (*conv)->metadata.value("ports", Json::object());
      }
      auto r = a == "resolve" ? prof->resolve(role, meta, cid, ctx->rt->config().all())
                              : prof->explain(role, meta, cid, ctx->rt->config().all());
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "chat") {
      if (a != "route") return out_error(Errc::InvalidArgument, "unknown chat action '" + a + "'");
      auto r = chat_route::route(*ctx->rt, *rj);
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
