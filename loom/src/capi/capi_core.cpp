// C API: lifecycle, logging, events, conversations/messages, search,
// config/secrets, provenance/events/tasks. (Foundation.)
#include <atomic>

#include "context.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/media_providers.h"
#include "loom/provenance.h"
#include "loom/tasks.h"
#include "loom/usage_policy.h"

using namespace loom;
using namespace loom::capi;

namespace {

LoomContext* open_ctx(const RuntimeOptions& opts, const char** error_json_out) {
  auto rt = Runtime::open(opts);
  if (!rt) {
    log::error("loom.capi", "loom_init failed: {}", rt.error().to_string());
    if (error_json_out) *error_json_out = out_error(rt.error());
    return nullptr;
  }
  auto ctx = std::make_unique<LoomContext>();
  ctx->rt = std::move(rt).value();
  return ctx.release();  // ownership passes to the caller until loom_shutdown
}

// Host log sink (process-wide, at most one).
std::mutex g_sink_mu;
int g_sink_token = 0;

Json messages_json(const std::vector<Message>& msgs) {
  Json arr = Json::array();
  for (const auto& m : msgs) arr.push_back(m.to_json());
  return arr;
}

}  // namespace

extern "C" {

// ── Lifecycle ──────────────────────────────────────────────────────
LOOM_API LoomContext* loom_init(const char* data_dir) {
  try {
    RuntimeOptions opts;
    opts.data_dir = opt_str(data_dir);
    return open_ctx(opts, nullptr);
  } catch (...) {
    return nullptr;
  }
}

LOOM_API LoomContext* loom_init_ex(const char* options_json, const char** error_json_out) {
  try {
    if (error_json_out) *error_json_out = nullptr;
    auto parsed = parse_arg(options_json, Json::object());
    if (!parsed) {
      if (error_json_out) *error_json_out = out_error(parsed.error());
      return nullptr;
    }
    auto opts = RuntimeOptions::from_json(*parsed);
    if (!opts) {
      if (error_json_out) *error_json_out = out_error(opts.error());
      return nullptr;
    }
    return open_ctx(*opts, error_json_out);
  } catch (const std::exception& e) {
    if (error_json_out) *error_json_out = out_error(Errc::Internal, e.what());
    return nullptr;
  } catch (...) {
    if (error_json_out) *error_json_out = out_error(Errc::Internal, "unknown exception");
    return nullptr;
  }
}

LOOM_API void loom_shutdown(LoomContext* ctx) {
  if (!ctx) return;
  guard_void("loom_shutdown", [&] {
    std::unique_ptr<LoomContext> owned(ctx);
    {
      std::lock_guard lk(owned->mu);
      for (auto& [id, tok] : owned->chat_requests) tok.cancel();
    }
    if (owned->rt) owned->rt->shutdown();
    owned->rt.reset();
  });
}

LOOM_API void loom_free_string(const char* str) { std::free(const_cast<char*>(str)); }

LOOM_API const char* loom_version(void) {
  return guard_json("loom_version", [] { return out(Runtime::build_info()); });
}

LOOM_API const char* loom_info(LoomContext* ctx) {
  return guard_json("loom_info", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(ctx->rt->info());
  });
}

// ── Logging ────────────────────────────────────────────────────────
LOOM_API int loom_set_log_sink(LoomLogCallback cb, int min_level, void* user_data) {
  return guard_int("loom_set_log_sink", [&] {
    std::lock_guard lk(g_sink_mu);
    if (g_sink_token) {
      log::remove_sink(g_sink_token);
      g_sink_token = 0;
    }
    if (cb) {
      g_sink_token = log::add_sink([cb, min_level, user_data](const log::Record& r) {
        if (static_cast<int>(r.level) < min_level) return;
        cb(static_cast<int>(r.level), r.logger.c_str(), r.message.c_str(), user_data);
      });
    }
    return LOOM_OK;
  });
}

LOOM_API void loom_set_log_level(int level) {
  guard_void("loom_set_log_level", [&] {
    log::Level l = level <= LOOM_LOG_DEBUG ? log::Level::Debug
                   : level <= LOOM_LOG_INFO ? log::Level::Info
                   : level <= LOOM_LOG_WARNING ? log::Level::Warning
                                               : log::Level::Error;
    log::set_level(l);
  });
}

LOOM_API void loom_set_log_stderr(int enabled) {
  guard_void("loom_set_log_stderr", [&] { log::set_stderr(enabled != 0); });
}

LOOM_API const char* loom_get_logs(int max_lines) {
  return guard_json("loom_get_logs", [&] {
    auto lines = log::recent(max_lines > 0 ? static_cast<std::size_t>(max_lines) : 500);
    return out(Json(lines));
  });
}

// ── Events ─────────────────────────────────────────────────────────
LOOM_API int64_t loom_subscribe(LoomContext* ctx, const char* event, LoomEventCallback cb, void* user_data) {
  try {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!event || !*event || !cb) return LOOM_E_INVALID_ARGUMENT;
    auto id = ctx->rt->bus().on(event, [cb, user_data](std::string_view ev, const Json& data) {
      std::string name(ev);
      std::string payload = json::dump(data);
      cb(name.c_str(), payload.c_str(), user_data);
    });
    return static_cast<int64_t>(id);
  } catch (...) {
    return LOOM_E_INTERNAL;
  }
}

LOOM_API int loom_unsubscribe(LoomContext* ctx, int64_t token) {
  return guard_int("loom_unsubscribe", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (token <= 0) return LOOM_E_INVALID_ARGUMENT;
    return ctx->rt->bus().off(static_cast<EventBus::SubscriptionId>(token)) ? LOOM_OK : LOOM_E_NOT_FOUND;
  });
}

LOOM_API int loom_emit(LoomContext* ctx, const char* event, const char* payload_json) {
  return guard_int("loom_emit", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!event || !*event) return LOOM_E_INVALID_ARGUMENT;
    auto payload = parse_arg(payload_json, Json(nullptr));
    if (!payload) return code(payload.error());
    ctx->rt->bus().emit(event, *payload);
    return LOOM_OK;
  });
}

// ── Conversations ──────────────────────────────────────────────────
LOOM_API const char* loom_list_conversations(LoomContext* ctx, int limit) {
  return guard_json("loom_list_conversations", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto r = ctx->rt->db().list_convs(limit > 0 ? limit : 50);
    if (!r) return out_error(r.error());
    Json arr = Json::array();
    for (const auto& c : *r) arr.push_back(c.to_json());
    return out(arr);
  });
}

LOOM_API const char* loom_create_conversation(LoomContext* ctx, const char* title) {
  return guard_json("loom_create_conversation", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto r = ctx->rt->db().create_conv(title && *title ? title : "New Chat");
    if (!r) return out_error(r.error());
    ctx->rt->bus().emit(events::kConvCreated, Json{{"id", r->id}, {"title", r->title}});
    return out(r->to_json());
  });
}

LOOM_API const char* loom_get_conversation(LoomContext* ctx, const char* conv_id) {
  return guard_json("loom_get_conversation", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!conv_id || !*conv_id) return out_error(missing("conv_id"));
    auto r = ctx->rt->db().get_conv(conv_id);
    if (!r) return out_error(r.error());
    if (!*r) return out_error(Errc::NotFound, std::string("conversation not found: ") + conv_id);
    return out((*r)->to_json());
  });
}

LOOM_API const char* loom_update_conversation(LoomContext* ctx, const char* conv_id, const char* patch_json) {
  return guard_json("loom_update_conversation", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!conv_id || !*conv_id) return out_error(missing("conv_id"));
    auto patch_j = parse_arg(patch_json);
    if (!patch_j) return out_error(patch_j.error());
    auto patch = ConvPatch::from_json(*patch_j);
    if (!patch) return out_error(patch.error());
    auto& db = ctx->rt->db();
    auto existing = db.get_conv(conv_id);
    if (!existing) return out_error(existing.error());
    if (!*existing) return out_error(Errc::NotFound, std::string("conversation not found: ") + conv_id);
    if (auto st = db.update_conv(conv_id, *patch); !st) return out_error(st.error());
    auto r = db.get_conv(conv_id);
    if (!r || !*r) return out_error(Errc::Internal, "conversation vanished during update");
    return out((*r)->to_json());
  });
}

LOOM_API int loom_delete_conversation(LoomContext* ctx, const char* conv_id) {
  return guard_int("loom_delete_conversation", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!conv_id || !*conv_id) return LOOM_E_INVALID_ARGUMENT;
    auto& db = ctx->rt->db();
    auto existing = db.get_conv(conv_id);
    if (!existing) return code(existing.error());
    if (!*existing) return LOOM_E_NOT_FOUND;
    return code(db.delete_conv(conv_id));
  });
}

// ── Messages ───────────────────────────────────────────────────────
LOOM_API const char* loom_get_messages(LoomContext* ctx, const char* conv_id) {
  return loom_get_messages_ex(ctx, conv_id, 0);
}

LOOM_API const char* loom_get_messages_ex(LoomContext* ctx, const char* conv_id, int include_all) {
  return guard_json("loom_get_messages", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!conv_id || !*conv_id) return out_error(missing("conv_id"));
    auto r = ctx->rt->db().get_msgs(conv_id, include_all != 0);
    if (!r) return out_error(r.error());
    return out(messages_json(*r));
  });
}

LOOM_API const char* loom_get_message(LoomContext* ctx, const char* msg_id) {
  return guard_json("loom_get_message", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!msg_id || !*msg_id) return out_error(missing("msg_id"));
    auto r = ctx->rt->db().get_msg(msg_id);
    if (!r) return out_error(r.error());
    if (!*r) return out_error(Errc::NotFound, std::string("message not found: ") + msg_id);
    return out((*r)->to_json());
  });
}

LOOM_API const char* loom_edit_message(LoomContext* ctx, const char* msg_id, const char* new_text) {
  return guard_json("loom_edit_message", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!msg_id || !*msg_id) return out_error(missing("msg_id"));
    if (!new_text) return out_error(missing("new_text"));
    auto& db = ctx->rt->db();
    auto r = db.edit_msg(msg_id, new_text);
    if (!r) return out_error(r.error());
    if (!*r) return out_error(Errc::NotFound, std::string("message not found: ") + msg_id);
    auto m = db.get_msg(**r);
    if (!m || !*m) return out_error(Errc::Internal, "edited message vanished");
    ctx->rt->bus().emit(events::kMsgUpdated,
                        Json{{"id", (*m)->id}, {"previous_id", msg_id}, {"conv_id", (*m)->conv_id}});
    return out((*m)->to_json());
  });
}

LOOM_API int loom_restore_version(LoomContext* ctx, const char* msg_id) {
  return guard_int("loom_restore_version", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!msg_id || !*msg_id) return LOOM_E_INVALID_ARGUMENT;
    auto r = ctx->rt->db().restore_version(msg_id);
    if (!r) return code(r.error());
    if (!*r) return LOOM_E_NOT_FOUND;
    ctx->rt->bus().emit(events::kMsgUpdated, Json{{"id", msg_id}, {"restored", true}});
    return LOOM_OK;
  });
}

LOOM_API const char* loom_get_versions(LoomContext* ctx, const char* msg_or_group_id) {
  return guard_json("loom_get_versions", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!msg_or_group_id || !*msg_or_group_id) return out_error(missing("msg_or_group_id"));
    auto& db = ctx->rt->db();
    std::string group = msg_or_group_id;
    if (group.rfind("vg_", 0) != 0) {
      auto m = db.get_msg(group);
      if (!m) return out_error(m.error());
      if (!*m) return out_error(Errc::NotFound, "message not found: " + group);
      if (!(*m)->version_group_id) return out(messages_json({**m}));
      group = *(*m)->version_group_id;
    }
    auto r = db.get_versions(group);
    if (!r) return out_error(r.error());
    return out(messages_json(*r));
  });
}

LOOM_API int loom_set_message_status(LoomContext* ctx, const char* msg_id, const char* status) {
  return guard_int("loom_set_message_status", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!msg_id || !*msg_id || !status) return LOOM_E_INVALID_ARGUMENT;
    auto& db = ctx->rt->db();
    auto m = db.get_msg(msg_id);
    if (!m) return code(m.error());
    if (!*m) return LOOM_E_NOT_FOUND;
    int rc = code(db.set_msg_status(msg_id, status));
    if (rc == LOOM_OK) ctx->rt->bus().emit(events::kMsgUpdated, Json{{"id", msg_id}, {"status", status}});
    return rc;
  });
}

LOOM_API int loom_update_message(LoomContext* ctx, const char* msg_id, const char* patch_json) {
  return guard_int("loom_update_message", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!msg_id || !*msg_id) return LOOM_E_INVALID_ARGUMENT;
    auto pj = parse_arg(patch_json);
    if (!pj) return code(pj.error());
    auto patch = MsgPatch::from_json(*pj);
    if (!patch) return code(patch.error());
    auto& db = ctx->rt->db();
    auto m = db.get_msg(msg_id);
    if (!m) return code(m.error());
    if (!*m) return LOOM_E_NOT_FOUND;
    int rc = code(db.update_msg(msg_id, *patch));
    if (rc == LOOM_OK) ctx->rt->bus().emit(events::kMsgUpdated, Json{{"id", msg_id}, {"fields", *pj}});
    return rc;
  });
}

LOOM_API const char* loom_search(LoomContext* ctx, const char* query, const char* options_json) {
  return guard_json("loom_search", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!query) return out_error(missing("query"));
    auto oj = parse_arg(options_json);
    if (!oj) return out_error(oj.error());
    if (!oj->is_object()) return out_error(Errc::InvalidArgument, "options must be an object");
    SearchOptions o;
    o.limit = static_cast<int>(json::get_int(*oj, "limit", 50));
    o.conv_id = json::get_opt_string(*oj, "conv_id");
    o.include_inactive = json::get_bool(*oj, "include_inactive", false);
    std::string mode = json::get_string(*oj, "mode", "auto");
    if (mode == "fts5") o.mode = FtsMode::Fts5;
    else if (mode == "like") o.mode = FtsMode::Like;
    else if (mode != "auto") return out_error(Errc::InvalidArgument, "mode must be auto, fts5 or like");
    auto r = ctx->rt->db().search_messages(query, o);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

// ── Config & secrets ───────────────────────────────────────────────
LOOM_API const char* loom_get_config(LoomContext* ctx) {
  return guard_json("loom_get_config", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    Json all = ctx->rt->config().all();
    const Json& ld = loom_config_defaults();
    for (auto it = ld.begin(); it != ld.end(); ++it) {
      if (!all.contains(it.key())) all[it.key()] = it.value();
    }
    return out(all);
  });
}

LOOM_API void loom_set_config(LoomContext* ctx, const char* key, const char* value) {
  guard_void("loom_set_config", [&] {
    if (!live(ctx) || !key || !*key) return;
    Json v = value ? json::parse_or(value, Json(std::string(value))) : Json(nullptr);
    if (std::string_view(key) == "loom_usage_policy") {
      if (auto st = validate_usage_policy_options(v); !st) {
        log::error("loom.capi", "loom_set_config: invalid usage policy");
        return;
      }
    }
    auto& cfg = ctx->rt->config();
    cfg.set(key, std::move(v));
    if (auto st = cfg.save(); !st) log::error("loom.capi", "loom_set_config: save failed: {}", st.error().message);
  });
}

LOOM_API int loom_set_config_json(LoomContext* ctx, const char* patch_json) {
  return guard_int("loom_set_config_json", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    auto pj = parse_arg(patch_json, Json(nullptr));
    if (!pj) return code(pj.error());
    if (!pj->is_object()) return LOOM_E_INVALID_ARGUMENT;
    if (auto it = pj->find("loom_usage_policy"); it != pj->end()) {
      if (auto st = validate_usage_policy_options(*it); !st) return code(st.error());
    }
    auto& cfg = ctx->rt->config();
    for (auto it = pj->begin(); it != pj->end(); ++it) cfg.set(it.key(), it.value());
    return code(cfg.save());
  });
}

LOOM_API int loom_set_secret(LoomContext* ctx, const char* key, const char* value) {
  return guard_int("loom_set_secret", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!key || !*key || !value) return LOOM_E_INVALID_ARGUMENT;
    auto& s = ctx->rt->secrets();
    s.set(key, std::string(value));
    int rc = code(s.save());
    ctx->rt->media().refresh();
    return rc;
  });
}

LOOM_API int loom_has_secret(LoomContext* ctx, const char* key) {
  return guard_int("loom_has_secret", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!key || !*key) return LOOM_E_INVALID_ARGUMENT;
    return ctx->rt->secrets().has(key) ? 1 : 0;
  });
}

LOOM_API int loom_delete_secret(LoomContext* ctx, const char* key) {
  return guard_int("loom_delete_secret", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!key || !*key) return LOOM_E_INVALID_ARGUMENT;
    auto& s = ctx->rt->secrets();
    if (!s.contains(key)) return LOOM_E_NOT_FOUND;
    s.erase(key);
    int rc = code(s.save());
    ctx->rt->media().refresh();
    return rc;
  });
}

LOOM_API const char* loom_list_secret_keys(LoomContext* ctx) {
  return guard_json("loom_list_secret_keys", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    return out(Json(ctx->rt->secrets().keys()));
  });
}

// ── Provenance, events, tasks ──────────────────────────────────────
LOOM_API const char* loom_list_sources(LoomContext* ctx, int limit) {
  return guard_json("loom_list_sources", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto r = ctx->rt->provenance().list_sources(limit > 0 ? limit : 100);
    if (!r) return out_error(r.error());
    Json arr = Json::array();
    for (const auto& s : *r) arr.push_back(s.to_json());
    return out(arr);
  });
}

LOOM_API const char* loom_get_provenance(LoomContext* ctx, const char* subject_id) {
  return guard_json("loom_get_provenance", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!subject_id || !*subject_id) return out_error(missing("subject_id"));
    auto& prov = ctx->rt->provenance();
    auto r = prov.for_subject(subject_id);
    if (!r) return out_error(r.error());
    Json records = Json::array();
    Json sources = Json::object();
    for (const auto& p : *r) {
      records.push_back(p.to_json());
      if (!p.source_id.empty() && !sources.contains(p.source_id)) {
        auto s = prov.get_source(p.source_id);
        if (s && *s) sources[p.source_id] = (*s)->to_json();
      }
    }
    return out(Json{{"subject_id", subject_id}, {"records", records}, {"sources", sources}});
  });
}

LOOM_API const char* loom_query_events(LoomContext* ctx, const char* query_json) {
  return guard_json("loom_query_events", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto qj = parse_arg(query_json);
    if (!qj) return out_error(qj.error());
    EventQuery q;
    q.after_seq = json::get_int(*qj, "after_seq", 0);
    q.type = json::get_opt_string(*qj, "type");
    q.subject_id = json::get_opt_string(*qj, "subject_id");
    q.limit = static_cast<int>(json::get_int(*qj, "limit", 500));
    auto r = ctx->rt->event_log().query(q);
    if (!r) return out_error(r.error());
    Json arr = Json::array();
    for (const auto& e : *r) arr.push_back(e.to_json());
    return out(arr);
  });
}

LOOM_API const char* loom_list_tasks(LoomContext* ctx, const char* filter_json) {
  return guard_json("loom_list_tasks", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto fj = parse_arg(filter_json);
    if (!fj) return out_error(fj.error());
    TaskFilter f;
    f.kind = json::get_opt_string(*fj, "kind");
    f.status = json::get_opt_string(*fj, "status");
    f.parent_id = json::get_opt_string(*fj, "parent_id");
    f.limit = static_cast<int>(json::get_int(*fj, "limit", 100));
    auto r = ctx->rt->tasks().list(f);
    if (!r) return out_error(r.error());
    Json arr = Json::array();
    for (const auto& t : *r) arr.push_back(t.to_json());
    return out(arr);
  });
}

LOOM_API const char* loom_get_task(LoomContext* ctx, const char* task_id) {
  return guard_json("loom_get_task", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!task_id || !*task_id) return out_error(missing("task_id"));
    auto r = ctx->rt->tasks().get(task_id);
    if (!r) return out_error(r.error());
    if (!*r) return out_error(Errc::NotFound, std::string("task not found: ") + task_id);
    return out((*r)->to_json());
  });
}

LOOM_API const char* loom_resume_tasks(LoomContext* ctx) {
  return guard_json("loom_resume_tasks", [&] {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto& tasks = ctx->rt->tasks();
    auto r = tasks.recover_interrupted();
    if (!r) return out_error(r.error());
    tasks.wake();
    return out(Json{{"recovered", *r}});
  });
}

LOOM_API int loom_cancel_task(LoomContext* ctx, const char* task_id) {
  return guard_int("loom_cancel_task", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (!task_id || !*task_id) return LOOM_E_INVALID_ARGUMENT;
    return code(ctx->rt->tasks().cancel(task_id));
  });
}

}  // extern "C"
