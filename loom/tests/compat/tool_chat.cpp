// Chat/semantic compat commands: run the real ChatEngine/SemanticLLM against
// a live HTTP server (usually a Python mock in test_chat_compat.py) so their
// outbound requests and outputs can be diffed against the Python engine
// hitting the SAME server. Uses the real default HTTP transport (plain HTTP
// to localhost needs no TLS), never ScriptedTransport.
#include "compat_registry.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/util/fs.h"

using namespace loom;
using namespace loom::compat;

namespace {

void apply_object(JsonStore& store, const Json* obj) {
  if (!obj || !obj->is_object()) return;
  for (auto it = obj->begin(); it != obj->end(); ++it) store.set(it.key(), it.value());
}

}  // namespace

LOOM_COMPAT_COMMAND(cmd_chat_send, "chat-send",
                    "<db> @args.json : {config, secrets, request} -> ChatEngine.send() result or error") {
  if (args.empty()) return fail("usage: chat-send <db> @args.json");
  auto parsed = args.size() > 1 ? arg_json(args[1]) : Result<Json>(Json::object());
  if (!parsed) return fail("bad args json: " + parsed.error().message);
  const Json& a = *parsed;

  auto dbr = Database::open(args[0]);
  if (!dbr) return fail("db open failed: " + dbr.error().to_string());
  auto db = std::move(*dbr);

  std::filesystem::path dir = std::filesystem::path(args[0]).parent_path();
  Config cfg(dir / "chat_compat_config.json");
  Secrets secrets(dir / "chat_compat_secrets.json");
  apply_object(cfg, json::find(a, "config"));
  apply_object(secrets, json::find(a, "secrets"));

  EventBus bus;
  auto analyzer = SemanticAnalyzer::create();
  if (!analyzer) return fail("analyzer create failed: " + analyzer.error().to_string());
  auto transport = net::make_default_transport();
  ChatEngine engine(cfg, secrets, *db, bus, *transport, **analyzer);

  const Json* req = json::find(a, "request");
  Json request_obj = req ? *req : Json::object();
  auto opts = ChatOptions::from_json(request_obj);
  if (!opts) return fail("bad request: " + opts.error().to_string());
  std::string message = json::get_string(request_obj, "message");

  auto result = engine.send(message, *opts);
  Json out = Json::object();
  if (!result) {
    out["error"] = Json{{"code", std::string(errc_name(result.error().code))}, {"message", result.error().message}};
  } else {
    out["result"] = result->to_json();
  }
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_semantic_analyse, "semantic-analyse",
                    "@args.json : {config, secrets, text} -> SemanticLLM.analyse() unified dict") {
  if (args.empty()) return fail("usage: semantic-analyse @args.json");
  auto parsed = arg_json(args[0]);
  if (!parsed) return fail("bad args json: " + parsed.error().message);
  const Json& a = *parsed;

  fsutil::TempDir td;
  Config cfg(td.path() / "config.json");
  Secrets secrets(td.path() / "secrets.json");
  apply_object(cfg, json::find(a, "config"));
  apply_object(secrets, json::find(a, "secrets"));

  auto analyzer = SemanticAnalyzer::create();
  if (!analyzer) return fail("analyzer create failed: " + analyzer.error().to_string());
  auto transport = net::make_default_transport();
  SemanticLLM llm(cfg, secrets, *transport, **analyzer);

  std::string text = json::get_string(a, "text");
  Json out = llm.analyse(text);
  print_json(Json{{"analysis", out}, {"enabled", llm.enabled()}});
  return 0;
}
