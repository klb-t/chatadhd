// C API: chat streaming, models/providers, semantic worker.
// OWNER (wave 2): net/chat/worker. The wrappers are complete; they return
// not_implemented until ChatEngine/ModelRegistry/ProviderRegistry/
// SemanticWorker are implemented.
#include "context.h"
#include "loom/chat_engine.h"
#include "loom/providers.h"
#include "loom/semantic_worker.h"
#include "loom/util/ids.h"

using namespace loom;
using namespace loom::capi;

namespace {

void emit_chunk(LoomStreamCallback cb, void* ud, const Json& chunk, int done) {
  if (!cb) return;
  std::string s = json::dump(chunk);
  cb(s.c_str(), done, ud);
}

// Shared by loom_chat and loom_chat_ex. Always delivers exactly one final
// chunk (done=1) and returns the final JSON (result or error).
Json run_chat(LoomContext* ctx, const std::string& message, ChatOptions opts, std::string request_id,
              LoomStreamCallback cb, void* ud) {
  if (request_id.empty()) request_id = gen_id("rq_");
  CancelToken token;
  {
    std::lock_guard lk(ctx->mu);
    if (ctx->chat_requests.count(request_id)) {
      Json err = error_json(Error(Errc::AlreadyExists, "request_id already in flight: " + request_id));
      emit_chunk(cb, ud, Json{{"type", "error"}, {"code", "already_exists"}, {"message", err["error"]["message"]}}, 1);
      return err;
    }
    ctx->chat_requests.emplace(request_id, token);
  }
  ChatCallbacks cbs;
  cbs.on_start = [&](std::string_view conv_id, std::string_view user_msg_id) {
    emit_chunk(cb, ud,
               Json{{"type", "start"},
                    {"request_id", request_id},
                    {"conv_id", std::string(conv_id)},
                    {"user_message_id", std::string(user_msg_id)}},
               0);
  };
  cbs.on_chunk = [&](std::string_view text) { emit_chunk(cb, ud, Json{{"type", "delta"}, {"text", std::string(text)}}, 0); };
  cbs.on_reasoning = [&](std::string_view text) {
    emit_chunk(cb, ud, Json{{"type", "reasoning"}, {"text", std::string(text)}}, 0);
  };
  if (!cb) opts.stream = false;
  auto r = ctx->rt->chat().send(message, opts, cbs, &token);
  {
    std::lock_guard lk(ctx->mu);
    ctx->chat_requests.erase(request_id);
  }
  if (!r) {
    emit_chunk(cb, ud,
               Json{{"type", "error"},
                    {"request_id", request_id},
                    {"code", std::string(errc_name(r.error().code))},
                    {"message", r.error().message}},
               1);
    return error_json(r.error());
  }
  Json result = r->to_json();
  result["request_id"] = request_id;
  Json done{{"type", "done"},
            {"request_id", request_id},
            {"message_id", r->assistant_message_id},
            {"conv_id", r->conv_id},
            {"text", r->text},
            {"usage", r->usage},
            {"model", r->model},
            {"cancelled", r->cancelled}};
  if (r->new_title) done["title"] = *r->new_title;
  if (!r->context_trace.is_null()) done["context_trace"] = r->context_trace;
  emit_chunk(cb, ud, done, 1);
  return result;
}

}  // namespace

extern "C" {

LOOM_API void loom_chat(LoomContext* ctx, const char* conv_id, const char* user_message, const char* model_id,
                        int context_depth, LoomStreamCallback callback, void* user_data) {
  guard_void("loom_chat", [&] {
    if (!live(ctx) || !user_message) {
      emit_chunk(callback, user_data,
                 Json{{"type", "error"},
                      {"code", "invalid_argument"},
                      {"message", !live(ctx) ? "ctx is NULL or shut down" : "user_message is required"}},
                 1);
      return;
    }
    ChatOptions o;
    o.conv_id = opt_str(conv_id);
    o.model = opt_str(model_id);
    if (context_depth >= 0) o.context_depth = context_depth;
    (void)run_chat(ctx, user_message, std::move(o), "", callback, user_data);
  });
}

LOOM_API const char* loom_chat_ex(LoomContext* ctx, const char* request_json, LoomStreamCallback callback,
                                  void* user_data) {
  return guard_json("loom_chat_ex", [&] {
    auto fail = [&](const Error& e) {
      emit_chunk(callback, user_data,
                 Json{{"type", "error"}, {"code", std::string(errc_name(e.code))}, {"message", e.message}}, 1);
      return out_error(e);
    };
    if (!live(ctx)) return fail(Error(Errc::InvalidArgument, "ctx is NULL or shut down"));
    auto rj = parse_arg(request_json, Json(nullptr));
    if (!rj) return fail(rj.error());
    if (!rj->is_object()) return fail(Error(Errc::InvalidArgument, "request must be a JSON object"));
    std::string message = json::get_string(*rj, "message");
    if (message.empty()) return fail(missing("message"));
    auto opts = ChatOptions::from_json(*rj);
    if (!opts) return fail(opts.error());
    return out(run_chat(ctx, message, std::move(opts).value(), json::get_string(*rj, "request_id"), callback,
                        user_data));
  });
}

LOOM_API int loom_chat_cancel(LoomContext* ctx, const char* request_id) {
  return guard_int("loom_chat_cancel", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!request_id || !*request_id) return LOOM_E_INVALID_ARGUMENT;
    std::lock_guard lk(ctx->mu);
    auto it = ctx->chat_requests.find(std::string_view(request_id));
    if (it == ctx->chat_requests.end()) return LOOM_E_NOT_FOUND;
    it->second.cancel();
    return LOOM_OK;
  });
}

// ── Models & providers ─────────────────────────────────────────────
LOOM_API const char* loom_get_models(LoomContext* ctx) {
  return guard_json("loom_get_models", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(ctx->rt->models().all());
  });
}

LOOM_API const char* loom_refresh_models(LoomContext* ctx) {
  return guard_json("loom_refresh_models", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto& m = ctx->rt->models();
    if (auto st = m.update_from_api(); !st) return out_error(st.error());
    return out(Json{{"count", m.size()}});
  });
}

LOOM_API const char* loom_get_providers(LoomContext* ctx) {
  return guard_json("loom_get_providers", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(ctx->rt->providers().status());
  });
}

LOOM_API int loom_can(LoomContext* ctx, const char* resource, const char* capability, const char* constraints_json) {
  return guard_int("loom_can", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!resource || !*resource || !capability || !*capability) return LOOM_E_INVALID_ARGUMENT;
    auto cj = parse_arg(constraints_json);
    if (!cj) return code(cj.error());
    return ctx->rt->providers().can(resource, capability, *cj) ? 1 : 0;
  });
}

// ── Semantic worker ────────────────────────────────────────────────
LOOM_API const char* loom_semantic_status(LoomContext* ctx) {
  return guard_json("loom_semantic_status", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(ctx->rt->worker().status().to_json());
  });
}

LOOM_API void loom_semantic_pause(LoomContext* ctx) {
  guard_void("loom_semantic_pause", [&] {
    if (live(ctx)) ctx->rt->worker().pause();
  });
}

LOOM_API void loom_semantic_resume(LoomContext* ctx) {
  guard_void("loom_semantic_resume", [&] {
    if (live(ctx)) ctx->rt->worker().resume();
  });
}

LOOM_API void loom_semantic_wake(LoomContext* ctx) {
  guard_void("loom_semantic_wake", [&] {
    if (live(ctx)) ctx->rt->worker().wake();
  });
}

}  // extern "C"
