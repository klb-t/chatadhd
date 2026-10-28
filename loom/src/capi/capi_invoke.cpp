// C API: loom_invoke — one entry point "perform action" (port, action, json). Ports: "storage" (storage
// plugins), "github" (GitHub sync), "actions" (action "list" = the action catalogue; "for", "explain" and
// "propose_chain" ask the action/element matching engine, loom/action_rules.h; "run", "confirm", "cancel",
// "status" and "preview" run actions through the interpreter, loom/action_runner.h), "chat" ("route" decides
// message or command for a chat input, loom/chat_route.h), "environments" ("sync", "list", "can_do": environments, capabilities and accesses in the graph, loom/environments.h), "ports" (read-only: "resolve", "explain" and
// "roles" of the ports profile, loom/port_profile.h; request {role, conversation_id?}; "graph" writes the adapters
// and the role choices as graph nodes, loom/port_graph.h, and returns their ids), "agent" ("run", "tools", "formats": the
// tool loop of use_model over the catalogue of actions, loom/agent.h).
#include "loom/action_runner.h"
#include "loom/agent.h"
#include "loom/action_table.h"
#include "loom/chat_engine.h"
#include "loom/chat_route.h"
#include "loom/runtime.h"
#include "loom/environments.h"
#include "loom/memory_folder.h"
#include "loom/tool_sources.h"
#include "loom/media_providers.h"
#include "loom/port_graph.h"
#include "loom/port_profile.h"
#include "loom/storage_backend.h"
#include "loom/batch_api.h"
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
      // The clients' answer to a run settles the proposal of the conversation that waits for it (chat_route.h).
      auto r = a == "confirm"          ? chat_route::confirm(*ctx->rt, *rj)
               : a == "client_result" ? chat_route::client_result(*ctx->rt, *rj)
               : a == "cancel"        ? chat_route::cancel(*ctx->rt, *rj)
                                      : ctx->rt->actions().invoke(a, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "ports") {
      if (a != "resolve" && a != "explain" && a != "roles" && a != "graph") return out_error(Errc::InvalidArgument, "unknown ports action '" + a + "'");
      auto prof = PortProfile::load(ctx->rt->paths().root);
      if (!prof) return out_error(prof.error());
      if (a == "roles") return out(prof->roles());
      if (a == "graph") {
        Database& db = ctx->rt->db();
        Json adapters = Json::object();
        auto add = [&](const char* port_name, std::vector<port_graph::AdapterInfo> infos) -> Status {
          LOOM_TRY_ASSIGN(auto ids, port_graph::record_adapters(db, port_name, infos));
          adapters[port_name] = ids;
          return Status{};
        };
        if (auto st = add("asr", port_graph::adapters_of(asr_adapters())); !st) return out_error(st.error());
        if (auto st = add("ocr", port_graph::adapters_of(ocr_adapters())); !st) return out_error(st.error());
        if (auto st = add("storage", port_graph::adapters_of(storage::BackendRegistry::builtin())); !st) return out_error(st.error());
        if (auto st = add("batch", port_graph::adapters_of(SemanticBatchAPI::backends())); !st) return out_error(st.error());
        const std::string cid = json::get_string(*rj, "conversation_id");
        Json meta = Json::object();
        if (!cid.empty()) {
          auto conv = db.get_conv(cid);
          if (!conv) return out_error(conv.error());
          if (!*conv) return out_error(Errc::NotFound, "conversation not found: " + cid);
          meta = (*conv)->metadata.value("ports", Json::object());
        }
        auto choices = port_graph::record_role_choices(db, *prof, meta, cid, ctx->rt->config().all());
        if (!choices) return out_error(choices.error());
        return out(Json{{"adapters", adapters}, {"role_choices", *choices}});
      }
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
    if (p == "environments") {
      // Environments, capabilities and accesses of the graph (loom/environments.h): the question "what can I do on my phone / in this
      // directory" is answered from the graph.
      if (a != "sync" && a != "list" && a != "can_do") return out_error(Errc::InvalidArgument, "unknown environments action '" + a + "'");
      auto r = a == "sync"   ? environments::sync(*ctx->rt, *rj)
               : a == "list" ? environments::list(*ctx->rt, *rj)
                             : environments::can_do(*ctx->rt, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "memory_folder") {
      // A folder into the memory tree with the full content of its files (loom/memory_folder.h, R39): action "attach_folder".
      auto r = memory_folder::invoke(*ctx->rt, a, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "tool_source") {
      // Tool sources (loom/tool_sources.h): list | sync | add | remove | trust | call:<source>:<tool>. "call" is the port the interpreter
      // executes behind the rules; reaching it here is the embedding client's own call (like port "storage").
      if (a != "list" && a != "sync" && a != "add" && a != "remove" && a != "trust" && a.rfind("call", 0) != 0)
        return out_error(Errc::InvalidArgument, "unknown tool_source action '" + a + "'");
      auto r = tool_sources::invoke(*ctx->rt, a, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "chat") {
      if (a == "request") {
        // RI-2: the request the kernel sends (or would send) to the provider, masked by the kernel (chat_engine.h provider_request_preview /
        // last_provider_request). {preview:true, message, ...chat options} builds the next one without sending; otherwise the record of the
        // last one sent in conversation `conv_id` (or `conversation_id`).
        if (json::get_bool(*rj, "preview", false)) {
          Json chat_request = *rj;
          chat_request.erase("preview");
          auto opts = ChatOptions::from_json(chat_request);
          if (!opts) return out_error(opts.error());
          auto r = ctx->rt->chat().provider_request_preview(json::get_string(*rj, "message"), *opts);
          if (!r) return out_error(r.error());
          return out(*r);
        }
        std::string cid = json::get_string(*rj, "conv_id");
        if (cid.empty()) cid = json::get_string(*rj, "conversation_id");
        if (cid.empty()) return out_error(missing("conv_id"));
        return out(ctx->rt->chat().last_provider_request(cid));
      }
      if (a != "route") return out_error(Errc::InvalidArgument, "unknown chat action '" + a + "'");
      auto r = chat_route::route(*ctx->rt, *rj);
      if (!r) return out_error(r.error());
      return out(*r);
    }
    if (p == "agent") {  // the tool loop of use_model (agent.h): run | tools | formats
      auto r = agent::invoke(*ctx->rt, a, *rj);
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
