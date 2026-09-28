// loom_jni.cpp — JNI glue between the Loom C ABI (include/loom/loom.h) and
// the Kotlin bridge (LoomNative.kt / LoomCallbacks.kt / LoomHttp.kt).
//
// Design
//   * One process-wide LoomContext* (a phone app has exactly one). No handles
//     are passed across the JNI boundary for it.
//   * Most loom.h functions are reached through one generic dispatcher,
//     nativeInvoke(method, argsJson), keyed by the same name loom.h uses
//     minus the "loom_" prefix (e.g. "get_conversation"). This keeps the JNI
//     surface small; Kotlin decides which calls need a background executor.
//   * Three calls need dedicated native entry points because Loom calls back
//     into them more than once: nativeChat (token/reasoning/done chunks),
//     nativeImportFile (progress + final result) and nativeSubscribe (event
//     stream). All three, plus the generic dispatcher's async use from
//     Kotlin, deliver through one Java entry point,
//     LoomCallbacks.onChunk(callbackId, chunkJson, done), matching the
//     documented bridge contract (window.__loomCallbacks[id](chunkJson, done)).
//   * HTTP is routed to Java: loom_set_http_transport(fn) is called once at
//     init with a C function that calls LoomHttp.sendRequest(requestJson,
//     handle) on the calling thread (blocking, like every Loom HTTP call).
//     LoomHttp streams the response back into native through
//     nativeHttpResponseBegin/Write/Fail, which are thin wrappers over
//     loom_http_response_begin/write/fail.
//   * Logging goes straight to logcat via loom_set_log_sink + __android_log_print
//     (no Java round trip needed). On a host build (LOOM_JNI_HOST, see
//     CMakeLists.txt) it falls back to stderr so the same file compiles for
//     the offline JNI smoke test.
//
// UTF-8 / modified-UTF-8
//   JNI's NewStringUTF/GetStringUTFChars use "modified UTF-8" (CESU-8: a
//   4-byte UTF-8 sequence for an astral code point, e.g. most emoji, is
//   *not* accepted on the way in and is re-encoded as a 6-byte surrogate
//   pair on the way out). Loom strings are plain UTF-8. To round-trip emoji
//   and any other astral-plane text correctly we never call those two
//   functions: jstring -> UTF-8 goes through GetStringChars (UTF-16) and a
//   local utf8_encode(), UTF-8 -> jstring goes through a local utf8_decode()
//   and NewString() (UTF-16). See the conversion helpers below.
//
// Why this file hand-rolls UTF-8 and JSON instead of reusing loom::utf8::*
// and loom::json::* (which do the same job inside loom_core): loom/loom.h
// is "the only interface platforms may use" (see loom/README.md and this
// task's brief) — everything else under loom/include is Loom's own internal
// C++ API, compiled with hidden symbol visibility in the shared build
// (libloom.so exports only the extern "C" loom_* functions). Calling into
// it here would link by accident when loom_jni.so statically absorbs
// loom_core (the on-device Android build), but fail — as it did the first
// time this file was written — the moment the JNI glue is linked against a
// prebuilt libloom.so instead (exactly what loom/android/verify/CMakeLists.txt
// does for the host smoke test, and a perfectly reasonable thing for a
// future build to do on-device too). nlohmann::json is the one exception:
// it is a header-only third-party library (loom/third_party/nlohmann, no
// linkage of its own), so using it here creates no such dependency.
#include <jni.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "loom/loom.h"

#if defined(__ANDROID__)
#include <android/log.h>
#else
#include <cstdio>
#endif

using Json = nlohmann::json;

namespace {

// Non-throwing parse: empty/invalid text -> fallback (loom::json::parse_or's
// contract, reimplemented locally — see the file-header note above).
Json parse_or(const std::string& text, Json fallback) {
  if (text.empty()) return fallback;
  Json j = Json::parse(text, nullptr, false);
  return j.is_discarded() ? fallback : j;
}

std::string dump(const Json& j) { return j.dump(); }

// Minimal well-formed-UTF-8 decoder (Unicode replacement character U+FFFD
// for any ill-formed byte, matching the usual "errors=replace" behaviour).
std::u32string utf8_decode(std::string_view s) {
  std::u32string out;
  out.reserve(s.size());
  size_t i = 0, n = s.size();
  while (i < n) {
    auto c = static_cast<unsigned char>(s[i]);
    int len;
    char32_t cp;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      len = 4;
    } else {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }
    if (i + static_cast<size_t>(len) > n) {
      out.push_back(0xFFFD);
      break;
    }
    bool ok = true;
    char32_t acc = cp;
    for (int k = 1; k < len; ++k) {
      auto cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
      if ((cc & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      acc = (acc << 6) | (cc & 0x3F);
    }
    if (!ok) {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }
    out.push_back(acc);
    i += static_cast<size_t>(len);
  }
  return out;
}

std::string utf8_encode(const std::u32string& s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t cp : s) {
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

}  // namespace

// ── UTF-8 <-> UTF-16 (see file header) ───────────────────────────────────
namespace {

std::u16string utf32_to_utf16(const std::u32string& s) {
  std::u16string out;
  out.reserve(s.size());
  for (char32_t cp : s) {
    if (cp <= 0xFFFF) {
      out.push_back(static_cast<char16_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

std::u32string utf16_to_utf32(const char16_t* data, size_t len) {
  std::u32string out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    char16_t c = data[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len) {
      char16_t lo = data[i + 1];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        char32_t cp = 0x10000 + ((static_cast<char32_t>(c) - 0xD800) << 10) + (static_cast<char32_t>(lo) - 0xDC00);
        out.push_back(cp);
        ++i;
        continue;
      }
    }
    out.push_back(static_cast<char32_t>(c));
  }
  return out;
}

// jstring -> UTF-8. Never NULL: absent/NULL jstring -> "".
std::string j2s(JNIEnv* env, jstring s) {
  if (!s) return {};
  jsize len = env->GetStringLength(s);
  if (len == 0) return {};
  const jchar* chars = env->GetStringChars(s, nullptr);
  if (!chars) return {};
  std::u32string u32 = utf16_to_utf32(reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(len));
  env->ReleaseStringChars(s, chars);
  return utf8_encode(u32);
}

// UTF-8 -> jstring (surrogate-pair safe).
jstring s2j(JNIEnv* env, std::string_view s) {
  std::u32string u32 = utf8_decode(s);
  std::u16string u16 = utf32_to_utf16(u32);
  return env->NewString(reinterpret_cast<const jchar*>(u16.data()), static_cast<jsize>(u16.size()));
}

}  // namespace

// ── Process-wide state ───────────────────────────────────────────────────
namespace {

JavaVM* g_vm = nullptr;
LoomContext* g_ctx = nullptr;
std::shared_mutex g_ctx_mu;

jclass g_callbacks_class = nullptr;      // com/chatadhd/android/LoomCallbacks
jmethodID g_on_chunk = nullptr;          // static void onChunk(String id, String json, boolean done)
jclass g_http_class = nullptr;           // com/chatadhd/android/LoomHttp
jmethodID g_http_send = nullptr;         // static void sendRequest(String requestJson, long handle)

// Attaches the calling thread to the JVM if it is not already attached
// (true for every Loom-internal worker thread: SemanticWorker, TaskEngine,
// batch polling, ...), and detaches again only if this call attached it.
// Never detaches a thread the JVM itself created (the JS-interface thread
// Kotlin already called us from).
class AttachGuard {
 public:
  explicit AttachGuard(JavaVM* vm) : vm_(vm) {
    if (!vm_) return;
    jint rc = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED) {
#if defined(__ANDROID__)
      if (vm_->AttachCurrentThreadAsDaemon(&env_, nullptr) == JNI_OK) attached_ = true;
#else
      if (vm_->AttachCurrentThread(reinterpret_cast<void**>(&env_), nullptr) == JNI_OK) attached_ = true;
#endif
    } else if (rc != JNI_OK) {
      env_ = nullptr;
    }
  }
  ~AttachGuard() {
    if (attached_ && vm_) vm_->DetachCurrentThread();
  }
  AttachGuard(const AttachGuard&) = delete;
  AttachGuard& operator=(const AttachGuard&) = delete;
  JNIEnv* env() const { return env_; }

 private:
  JavaVM* vm_ = nullptr;
  JNIEnv* env_ = nullptr;
  bool attached_ = false;
};

void log_line(int level, const char* logger, const char* message) {
#if defined(__ANDROID__)
  int prio = ANDROID_LOG_INFO;
  if (level >= LOOM_LOG_ERROR) prio = ANDROID_LOG_ERROR;
  else if (level >= LOOM_LOG_WARNING) prio = ANDROID_LOG_WARN;
  else if (level >= LOOM_LOG_INFO) prio = ANDROID_LOG_INFO;
  else prio = ANDROID_LOG_DEBUG;
  __android_log_print(prio, logger && *logger ? logger : "loom", "%s", message ? message : "");
#else
  std::fprintf(stderr, "[loom] %s: %s\n", logger ? logger : "loom", message ? message : "");
#endif
}

void log_sink_trampoline(int level, const char* logger, const char* message, void* /*user_data*/) {
  log_line(level, logger, message);
}

// ── error-code wrapping for the int-returning half of loom.h ────────────
// Mirrors src/capi/context.h's error_json() shape ({"error":{"code","message"}})
// so JS-side error handling is uniform whether the JSON came straight from a
// loom_* call or was synthesized here for an int-returning one.
const char* errc_name_for(int rc) {
  switch (rc) {
    case LOOM_E_INVALID_ARGUMENT: return "invalid_argument";
    case LOOM_E_NOT_FOUND: return "not_found";
    case LOOM_E_ALREADY_EXISTS: return "already_exists";
    case LOOM_E_IO: return "io";
    case LOOM_E_DATABASE: return "database";
    case LOOM_E_PARSE: return "parse";
    case LOOM_E_NETWORK: return "network";
    case LOOM_E_HTTP: return "http";
    case LOOM_E_AUTH: return "auth";
    case LOOM_E_CANCELLED: return "cancelled";
    case LOOM_E_TIMEOUT: return "timeout";
    case LOOM_E_UNAVAILABLE: return "unavailable";
    case LOOM_E_NOT_IMPLEMENTED: return "not_implemented";
    case LOOM_E_CRYPTO: return "crypto";
    case LOOM_E_CONFLICT: return "conflict";
    case LOOM_E_BUSY: return "busy";
    case LOOM_E_UNSUPPORTED: return "unsupported";
    case LOOM_E_RATE_LIMITED: return "rate_limited";
    case LOOM_E_PAUSED: return "paused";
    default: return "internal";
  }
}

std::string err_json(int rc) {
  Json j{{"error", Json{{"code", errc_name_for(rc)}, {"message", std::string("loom error ") + std::to_string(rc)}}}};
  return dump(j);
}

// int rc -> JSON. `ok` builds the success payload (default {"ok":true}).
std::string wrap_int(int rc, const std::function<Json()>& ok = nullptr) {
  if (rc < 0) return err_json(rc);
  return dump(ok ? ok() : Json{{"ok", true}});
}

// loom_has_secret / loom_can: 1/0 on success, negative on error.
std::string wrap_tri(int rc) {
  if (rc < 0) return err_json(rc);
  return dump(Json{{"value", rc != 0}});
}

std::string take(const char* r) {
  if (!r) return "{}";
  std::string s(r);
  loom_free_string(r);
  return s;
}

// ── argument helpers (args is a JSON array) ──────────────────────────────
std::string a_str(const Json& args, size_t i, const std::string& def = "") {
  if (i >= args.size() || args[i].is_null()) return def;
  if (args[i].is_string()) return args[i].get<std::string>();
  return args[i].dump();
}
const char* a_cstr(const Json& args, size_t i, std::string& storage) {
  if (i >= args.size() || args[i].is_null()) return nullptr;
  storage = args[i].is_string() ? args[i].get<std::string>() : args[i].dump();
  return storage.c_str();
}
int a_int(const Json& args, size_t i, int def = 0) {
  if (i >= args.size() || !args[i].is_number()) return def;
  return args[i].is_number_integer() ? static_cast<int>(args[i].get<int64_t>())
                                     : static_cast<int>(args[i].get<double>());
}

// ── generic dispatch table (see file header) ─────────────────────────────
// WebView uses named JSON objects; older Kotlin wrappers use positional
// arrays. Keep both encodings on one dispatcher, with argument order as data.
Json positional_args(const std::string& method, const Json& args) {
  if (args.is_array()) return args;
  if (!args.is_object()) throw std::invalid_argument("bridge arguments must be an object or array");
  if (method == "create_memory") return Json::array({args});
  if (method == "update_memory") {
    Json patch = args;
    patch.erase("id");
    return Json::array({args.value("id", Json(nullptr)), patch});
  }
  static const std::unordered_map<std::string, std::vector<std::string>> names = {
      {"set_config", {"key", "value"}}, {"set_config_json", {"patch"}},
      {"set_secret", {"key", "value"}}, {"has_secret", {"key"}}, {"delete_secret", {"key"}},
      {"list_conversations", {"limit"}}, {"create_conversation", {"title"}},
      {"get_conversation", {"conv_id"}}, {"update_conversation", {"conv_id", "patch"}},
      {"delete_conversation", {"conv_id"}}, {"get_messages", {"conv_id"}},
      {"get_messages_ex", {"conv_id", "include_all"}}, {"get_message", {"msg_id"}},
      {"edit_message", {"msg_id", "new_text"}}, {"restore_version", {"msg_id"}},
      {"get_versions", {"msg_or_group_id"}}, {"set_message_status", {"msg_id", "status"}},
      {"update_message", {"msg_id", "patch"}}, {"search", {"query", "options"}},
      {"chat_cancel", {"request_id"}}, {"can", {"resource", "capability", "constraints"}},
      {"get_nodes", {"filter"}}, {"get_edges", {"filter"}}, {"expand_graph", {"seed_ids", "depth"}},
      {"get_graph_data", {"conv_id"}}, {"graph_reindex", {"conv_id"}},
      {"select_context", {"text", "depth", "max_tokens"}}, {"select_context_ex", {"request"}},
      {"delete_memory", {"id"}}, {"get_memory_context", {"max_chars"}},
      {"detect_format", {"path"}}, {"import_file", {"path", "title"}},
      {"export_conversation", {"conv_id", "format"}}, {"list_sources", {"limit"}},
      {"get_provenance", {"subject_id"}}, {"query_events", {"query"}},
      {"list_tasks", {"filter"}}, {"get_task", {"task_id"}}, {"cancel_task", {"task_id"}},
      {"crypto_setup", {"password"}}, {"crypto_unlock", {"password"}},
      {"crypto_encrypt", {"plaintext"}}, {"crypto_decrypt", {"blob"}},
      {"transcribe", {"path", "options"}}, {"ocr", {"path", "options"}},
      {"github_sync", {"request"}}, {"get_logs", {"max_lines"}},
      {"kb_policy", {"name"}}, {"kb_runs", {"limit"}}, {"kb_query", {"query"}},
      {"kb_judge", {"judgement"}}, {"knowledge_run", {"config"}},
      {"knowledge_status", {"task_id"}}, {"catalog_scan", {"config"}},
      {"catalog_score", {"config"}}, {"catalog_query", {"query"}},
      {"catalog_select", {"run_id"}},
      {"catalog_preview", {"unit_id"}}, {"catalog_override", {"override"}},
      {"catalog_import", {"options"}}, {"context_build", {"request"}},
      {"materialize", {"request"}}};
  Json result = Json::array();
  auto it = names.find(method);
  if (it != names.end()) {
    for (const auto& name : it->second) result.push_back(args.value(name, Json(nullptr)));
  } else if (!args.empty()) {
    throw std::invalid_argument("unexpected named arguments for bridge method: " + method);
  }
  return result;
}

using Fn = std::function<std::string(const Json&)>;

#define S1(NAME, EXPR) \
  table[NAME] = [](const Json& args) -> std::string { (void)args; return take(EXPR); }
#define I1(NAME, EXPR) \
  table[NAME] = [](const Json& args) -> std::string { (void)args; return wrap_int(EXPR); }

const std::unordered_map<std::string, Fn>& dispatch_table() {
  static const std::unordered_map<std::string, Fn> table = [] {
    std::unordered_map<std::string, Fn> table;
    // -- lifecycle / info --
    S1("info", loom_info(g_ctx));
    table["get_logs"] = [](const Json& a) { return take(loom_get_logs(a_int(a, 0, 200))); };
    // -- config / secrets --
    S1("get_config", loom_get_config(g_ctx));
    table["set_config"] = [](const Json& a) {
      std::string k, v;
      const char* kc = a_cstr(a, 0, k);
      const char* vc = a_cstr(a, 1, v);
      loom_set_config(g_ctx, kc, vc);
      return dump(Json{{"ok", true}});
    };
    table["set_config_json"] = [](const Json& a) {
      std::string p;
      return wrap_int(loom_set_config_json(g_ctx, a_cstr(a, 0, p)));
    };
    table["set_secret"] = [](const Json& a) {
      std::string k, v;
      const char* kc = a_cstr(a, 0, k);
      const char* vc = a_cstr(a, 1, v);
      return wrap_int(loom_set_secret(g_ctx, kc, vc));
    };
    table["has_secret"] = [](const Json& a) {
      std::string k;
      return wrap_tri(loom_has_secret(g_ctx, a_cstr(a, 0, k)));
    };
    table["delete_secret"] = [](const Json& a) {
      std::string k;
      return wrap_int(loom_delete_secret(g_ctx, a_cstr(a, 0, k)));
    };
    S1("list_secret_keys", loom_list_secret_keys(g_ctx));
    // -- conversations / messages --
    table["list_conversations"] = [](const Json& a) { return take(loom_list_conversations(g_ctx, a_int(a, 0, 50))); };
    table["create_conversation"] = [](const Json& a) {
      std::string t;
      return take(loom_create_conversation(g_ctx, a_cstr(a, 0, t)));
    };
    table["get_conversation"] = [](const Json& a) {
      std::string id;
      return take(loom_get_conversation(g_ctx, a_cstr(a, 0, id)));
    };
    table["update_conversation"] = [](const Json& a) {
      std::string id, p;
      return take(loom_update_conversation(g_ctx, a_cstr(a, 0, id), a_cstr(a, 1, p)));
    };
    table["delete_conversation"] = [](const Json& a) {
      std::string id;
      return wrap_int(loom_delete_conversation(g_ctx, a_cstr(a, 0, id)));
    };
    table["get_messages"] = [](const Json& a) {
      std::string id;
      return take(loom_get_messages(g_ctx, a_cstr(a, 0, id)));
    };
    table["get_messages_ex"] = [](const Json& a) {
      std::string id;
      return take(loom_get_messages_ex(g_ctx, a_cstr(a, 0, id), a_int(a, 1, 0)));
    };
    table["get_message"] = [](const Json& a) {
      std::string id;
      return take(loom_get_message(g_ctx, a_cstr(a, 0, id)));
    };
    table["edit_message"] = [](const Json& a) {
      std::string id, t;
      return take(loom_edit_message(g_ctx, a_cstr(a, 0, id), a_cstr(a, 1, t)));
    };
    table["restore_version"] = [](const Json& a) {
      std::string id;
      return wrap_int(loom_restore_version(g_ctx, a_cstr(a, 0, id)));
    };
    table["get_versions"] = [](const Json& a) {
      std::string id;
      return take(loom_get_versions(g_ctx, a_cstr(a, 0, id)));
    };
    table["set_message_status"] = [](const Json& a) {
      std::string id, s;
      const char* ic = a_cstr(a, 0, id);
      const char* sc = a_cstr(a, 1, s);
      return wrap_int(loom_set_message_status(g_ctx, ic, sc));
    };
    table["update_message"] = [](const Json& a) {
      std::string id, p;
      const char* ic = a_cstr(a, 0, id);
      const char* pc = a_cstr(a, 1, p);
      return wrap_int(loom_update_message(g_ctx, ic, pc));
    };
    table["search"] = [](const Json& a) {
      std::string q, o;
      return take(loom_search(g_ctx, a_cstr(a, 0, q), a_cstr(a, 1, o)));
    };
    // -- chat support --
    table["chat_cancel"] = [](const Json& a) {
      std::string id;
      return wrap_int(loom_chat_cancel(g_ctx, a_cstr(a, 0, id)));
    };
    // -- models / providers --
    S1("get_models", loom_get_models(g_ctx));
    S1("refresh_models", loom_refresh_models(g_ctx));
    S1("get_providers", loom_get_providers(g_ctx));
    table["can"] = [](const Json& a) {
      std::string r, c, cj;
      const char* rc = a_cstr(a, 0, r);
      const char* cc = a_cstr(a, 1, c);
      const char* cjc = a_cstr(a, 2, cj);
      return wrap_tri(loom_can(g_ctx, rc, cc, cjc));
    };
    // -- graph / context --
    table["get_nodes"] = [](const Json& a) {
      std::string f;
      return take(loom_get_nodes(g_ctx, a_cstr(a, 0, f)));
    };
    table["get_edges"] = [](const Json& a) {
      std::string f;
      return take(loom_get_edges(g_ctx, a_cstr(a, 0, f)));
    };
    table["expand_graph"] = [](const Json& a) {
      std::string ids;
      return take(loom_expand_graph(g_ctx, a_cstr(a, 0, ids), a_int(a, 1, 2)));
    };
    table["get_graph_data"] = [](const Json& a) {
      std::string c;
      return take(loom_get_graph_data(g_ctx, a_cstr(a, 0, c)));
    };
    table["graph_reindex"] = [](const Json& a) {
      std::string c;
      return take(loom_graph_reindex(g_ctx, a_cstr(a, 0, c)));
    };
    table["select_context"] = [](const Json& a) {
      std::string t;
      return take(loom_select_context(g_ctx, a_cstr(a, 0, t), a_int(a, 1, -1), a_int(a, 2, 0)));
    };
    table["select_context_ex"] = [](const Json& a) {
      std::string r;
      return take(loom_select_context_ex(g_ctx, a_cstr(a, 0, r)));
    };
    // -- knowledge workbench: the same C ABI used by HTTP and CLI --
    S1("kb_pack", loom_kb_pack(g_ctx));
    table["kb_runs"] = [](const Json& a) { return take(loom_kb_runs(g_ctx, a_int(a, 0, 50))); };
    I1("knowledge_cancel", loom_knowledge_cancel(g_ctx));
    table["knowledge_run"] = [](const Json& a) {
      std::string config;
      return take(loom_knowledge_run(g_ctx, a_cstr(a, 0, config), nullptr, nullptr));
    };
    table["catalog_scan"] = [](const Json& a) {
      std::string config;
      return take(loom_catalog_scan(g_ctx, a_cstr(a, 0, config), nullptr, nullptr));
    };
    table["catalog_score"] = [](const Json& a) {
      std::string config;
      return take(loom_catalog_score(g_ctx, a_cstr(a, 0, config), nullptr, nullptr));
    };
    table["catalog_import"] = [](const Json& a) {
      std::string options;
      return take(loom_catalog_import(g_ctx, a_cstr(a, 0, options), nullptr, nullptr));
    };
#define JSON_ARG(NAME) table[#NAME] = [](const Json& a) { std::string value; return take(loom_##NAME(g_ctx, a_cstr(a, 0, value))); }
    JSON_ARG(kb_policy);
    JSON_ARG(kb_query);
    JSON_ARG(kb_judge);
    JSON_ARG(knowledge_status);
    JSON_ARG(catalog_query);
    JSON_ARG(catalog_select);
    JSON_ARG(catalog_preview);
    JSON_ARG(catalog_override);
    JSON_ARG(context_build);
    JSON_ARG(materialize);
#undef JSON_ARG
    // -- semantic worker --
    S1("semantic_status", loom_semantic_status(g_ctx));
    table["semantic_pause"] = [](const Json&) {
      loom_semantic_pause(g_ctx);
      return dump(Json{{"ok", true}});
    };
    table["semantic_resume"] = [](const Json&) {
      loom_semantic_resume(g_ctx);
      return dump(Json{{"ok", true}});
    };
    table["semantic_wake"] = [](const Json&) {
      loom_semantic_wake(g_ctx);
      return dump(Json{{"ok", true}});
    };
    // -- memory tree --
    S1("list_memory", loom_list_memory(g_ctx));
    table["create_memory"] = [](const Json& a) {
      std::string j;
      return take(loom_create_memory(g_ctx, a_cstr(a, 0, j)));
    };
    table["update_memory"] = [](const Json& a) {
      std::string id, j;
      return take(loom_update_memory(g_ctx, a_cstr(a, 0, id), a_cstr(a, 1, j)));
    };
    table["delete_memory"] = [](const Json& a) {
      std::string id;
      return wrap_int(loom_delete_memory(g_ctx, a_cstr(a, 0, id)));
    };
    table["get_memory_context"] = [](const Json& a) { return take(loom_get_memory_context(g_ctx, a_int(a, 0, 16000))); };
    // -- import / export --
    table["detect_format"] = [](const Json& a) {
      std::string p;
      return take(loom_detect_format(g_ctx, a_cstr(a, 0, p)));
    };
    table["import_file"] = [](const Json& a) {
      std::string p, t;
      const char* pc = a_cstr(a, 0, p);
      const char* tc = a_cstr(a, 1, t);
      return take(loom_import_file(g_ctx, pc, tc, nullptr, nullptr));
    };
    table["export_conversation"] = [](const Json& a) {
      std::string c, f;
      const char* cc = a_cstr(a, 0, c);
      const char* fc = a_cstr(a, 1, f);
      return take(loom_export_conversation(g_ctx, cc, fc));
    };
    // -- provenance / events / tasks --
    table["list_sources"] = [](const Json& a) { return take(loom_list_sources(g_ctx, a_int(a, 0, 100))); };
    table["get_provenance"] = [](const Json& a) {
      std::string id;
      return take(loom_get_provenance(g_ctx, a_cstr(a, 0, id)));
    };
    table["query_events"] = [](const Json& a) {
      std::string q;
      return take(loom_query_events(g_ctx, a_cstr(a, 0, q)));
    };
    table["list_tasks"] = [](const Json& a) {
      std::string f;
      return take(loom_list_tasks(g_ctx, a_cstr(a, 0, f)));
    };
    table["get_task"] = [](const Json& a) {
      std::string id;
      return take(loom_get_task(g_ctx, a_cstr(a, 0, id)));
    };
    S1("resume_tasks", loom_resume_tasks(g_ctx));
    table["cancel_task"] = [](const Json& a) {
      std::string id;
      return wrap_int(loom_cancel_task(g_ctx, a_cstr(a, 0, id)));
    };
    // -- crypto --
    S1("crypto_status", loom_crypto_status(g_ctx));
    table["crypto_setup"] = [](const Json& a) {
      std::string p;
      return wrap_int(loom_crypto_setup(g_ctx, a_cstr(a, 0, p)));
    };
    table["crypto_unlock"] = [](const Json& a) {
      std::string p;
      return wrap_int(loom_crypto_unlock(g_ctx, a_cstr(a, 0, p)));
    };
    table["crypto_lock"] = [](const Json&) { return wrap_int(loom_crypto_lock(g_ctx)); };
    table["crypto_encrypt"] = [](const Json& a) {
      std::string p;
      return take(loom_crypto_encrypt(g_ctx, a_cstr(a, 0, p)));
    };
    table["crypto_decrypt"] = [](const Json& a) {
      std::string b;
      return take(loom_crypto_decrypt(g_ctx, a_cstr(a, 0, b)));
    };
    // -- media --
    table["transcribe"] = [](const Json& a) {
      std::string p, o;
      const char* pc = a_cstr(a, 0, p);
      const char* oc = a_cstr(a, 1, o);
      return take(loom_transcribe(g_ctx, pc, oc));
    };
    table["ocr"] = [](const Json& a) {
      std::string p, o;
      const char* pc = a_cstr(a, 0, p);
      const char* oc = a_cstr(a, 1, o);
      return take(loom_ocr(g_ctx, pc, oc));
    };
    S1("media_status", loom_media_status(g_ctx));
    // -- github --
    table["github_sync"] = [](const Json& a) {
      std::string r;
      return take(loom_github_sync(g_ctx, a_cstr(a, 0, r)));
    };
    return table;
  }();
  return table;
}
#undef S1
#undef I1

// ── HTTP transport: bridge to LoomHttp.sendRequest(requestJson, handle) ──
int http_send_trampoline(const char* request_json, LoomHttpResponse* response, void* /*user_data*/) {
  AttachGuard g(g_vm);
  JNIEnv* env = g.env();
  if (!env || !g_http_class || !g_http_send) {
    loom_http_response_fail(response, LOOM_E_UNAVAILABLE, "Java HTTP bridge not installed");
    return LOOM_OK;  // failure was reported through the response sink already
  }
  jstring jreq = s2j(env, request_json ? request_json : "{}");
  // handle is only ever dereferenced by loom_http_response_* on this same
  // call stack (LoomHttp.sendRequest is a blocking call), so passing the
  // raw pointer as a jlong is safe: it never outlives this frame.
  auto handle = reinterpret_cast<jlong>(response);
  env->CallStaticVoidMethod(g_http_class, g_http_send, jreq, handle);
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    if (!loom_http_response_cancelled(response)) {
      loom_http_response_fail(response, LOOM_E_INTERNAL, "LoomHttp.sendRequest threw");
    }
  }
  env->DeleteLocalRef(jreq);
  return LOOM_OK;
}

// ── streaming delivery: LoomCallbacks.onChunk(id, json, done) ───────────
void deliver_chunk(const std::string& callback_id, const std::string& chunk_json, bool done) {
  AttachGuard g(g_vm);
  JNIEnv* env = g.env();
  if (!env || !g_callbacks_class || !g_on_chunk) return;
  jstring jid = s2j(env, callback_id);
  jstring jchunk = s2j(env, chunk_json);
  env->CallStaticVoidMethod(g_callbacks_class, g_on_chunk, jid, jchunk, static_cast<jboolean>(done));
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
  env->DeleteLocalRef(jid);
  env->DeleteLocalRef(jchunk);
}

void chat_stream_trampoline(const char* chunk, int done, void* user_data) {
  auto* id = static_cast<std::string*>(user_data);
  deliver_chunk(*id, chunk ? chunk : "{}", done != 0);
}

struct ImportProgressCtx {
  std::string callback_id;
};
void import_progress_trampoline(int current, int total, const char* status, void* user_data) {
  auto* ctx = static_cast<ImportProgressCtx*>(user_data);
  Json j{{"type", "progress"}, {"current", current}, {"total", total}, {"status", status ? status : ""}};
  deliver_chunk(ctx->callback_id, dump(j), false);
}

std::mutex g_sub_mu;
std::unordered_map<int64_t, std::unique_ptr<std::string>> g_subscriptions;  // token -> callback id

void event_trampoline(const char* event, const char* payload_json, void* user_data) {
  auto* id = static_cast<std::string*>(user_data);
  Json payload = parse_or(payload_json ? payload_json : "", Json(nullptr));
  Json j{{"event", event ? event : ""}, {"payload", payload}};
  deliver_chunk(*id, dump(j), false);
}

}  // namespace

// ── JNI_OnLoad / OnUnload ────────────────────────────────────────────────
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  g_vm = vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

  jclass cb = env->FindClass("com/chatadhd/android/LoomCallbacks");
  if (cb) {
    g_callbacks_class = static_cast<jclass>(env->NewGlobalRef(cb));
    g_on_chunk = env->GetStaticMethodID(g_callbacks_class, "onChunk", "(Ljava/lang/String;Ljava/lang/String;Z)V");
    env->DeleteLocalRef(cb);
  }
  jclass http = env->FindClass("com/chatadhd/android/LoomHttp");
  if (http) {
    g_http_class = static_cast<jclass>(env->NewGlobalRef(http));
    g_http_send = env->GetStaticMethodID(g_http_class, "sendRequest", "(Ljava/lang/String;J)V");
    env->DeleteLocalRef(http);
  }
  // A class-not-found here (host smoke test builds without LoomCallbacks/
  // LoomHttp on the classpath) is not fatal: the affected trampolines just
  // no-op until nativeInit is exercised against the real classes.
  env->ExceptionClear();
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* /*reserved*/) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return;
  if (g_callbacks_class) env->DeleteGlobalRef(g_callbacks_class);
  if (g_http_class) env->DeleteGlobalRef(g_http_class);
  g_callbacks_class = nullptr;
  g_http_class = nullptr;
}

// ── LoomNative external funs ─────────────────────────────────────────────
extern "C" {

JNIEXPORT jstring JNICALL Java_com_chatadhd_android_LoomNative_nativeInit(JNIEnv* env, jclass, jstring dataDir,
                                                                          jstring optionsJson) {
  std::unique_lock<std::shared_mutex> lock(g_ctx_mu);
  if (g_ctx) {
    return s2j(env, dump(Json{{"ok", true}, {"info", parse_or(take(loom_info(g_ctx)), Json::object())}}));
  }
  loom_set_log_sink(log_sink_trampoline, LOOM_LOG_DEBUG, nullptr);
  loom_set_log_stderr(false);

  Json opts = optionsJson ? parse_or(j2s(env, optionsJson), Json::object()) : Json::object();
  std::string dd = j2s(env, dataDir);
  if (!dd.empty()) opts["data_dir"] = dd;
  std::string opts_str = dump(opts);

  const char* err_json_out = nullptr;
  LoomContext* ctx = loom_init_ex(opts_str.c_str(), &err_json_out);
  if (!ctx) {
    std::string err = err_json_out ? take(err_json_out) : err_json(LOOM_E_INTERNAL);
    return s2j(env, dump(Json{{"ok", false}, {"error", parse_or(err, Json::object())["error"]}}));
  }
  g_ctx = ctx;
  loom_set_http_transport(g_ctx, http_send_trampoline, nullptr);

  Json info = parse_or(take(loom_info(g_ctx)), Json::object());
  return s2j(env, dump(Json{{"ok", true}, {"info", info}}));
}

JNIEXPORT void JNICALL Java_com_chatadhd_android_LoomNative_nativeShutdown(JNIEnv*, jclass) {
  std::unique_lock<std::shared_mutex> lock(g_ctx_mu);
  if (!g_ctx) return;
  loom_shutdown(g_ctx);
  g_ctx = nullptr;
}

JNIEXPORT jstring JNICALL Java_com_chatadhd_android_LoomNative_nativeVersion(JNIEnv* env, jclass) {
  return s2j(env, take(loom_version()));
}

JNIEXPORT jstring JNICALL Java_com_chatadhd_android_LoomNative_nativeInvoke(JNIEnv* env, jclass, jstring method,
                                                                            jstring argsJson) {
  std::string m = j2s(env, method);
  std::shared_lock<std::shared_mutex> lock(g_ctx_mu);
  if (!g_ctx) return s2j(env, err_json(LOOM_E_INVALID_ARGUMENT));
  const auto& table = dispatch_table();
  auto it = table.find(m);
  if (it == table.end()) {
    Json e{{"error", Json{{"code", "not_implemented"}, {"message", "no such bridge method: " + m}}}};
    return s2j(env, dump(e));
  }
  std::string result;
  try {
    Json args = Json::parse(j2s(env, argsJson));
    args = positional_args(m, args);
    result = it->second(args);
  } catch (const Json::exception& e) {
    result = dump(Json{{"error", Json{{"code", "invalid_argument"}, {"message", e.what()}}}});
  } catch (const std::invalid_argument& e) {
    result = dump(Json{{"error", Json{{"code", "invalid_argument"}, {"message", e.what()}}}});
  } catch (const std::exception& e) {
    result = dump(Json{{"error", Json{{"code", "internal"}, {"message", e.what()}}}});
  } catch (...) {
    result = dump(Json{{"error", Json{{"code", "internal"}, {"message", "unknown exception"}}}});
  }
  return s2j(env, result);
}

JNIEXPORT jstring JNICALL Java_com_chatadhd_android_LoomNative_nativeChat(JNIEnv* env, jclass, jstring requestJson,
                                                                          jstring callbackId) {
  std::shared_lock<std::shared_mutex> lock(g_ctx_mu);
  if (!g_ctx) return s2j(env, err_json(LOOM_E_INVALID_ARGUMENT));
  std::string req = j2s(env, requestJson);
  std::string id = j2s(env, callbackId);
  const char* r = loom_chat_ex(g_ctx, req.c_str(), chat_stream_trampoline, &id);
  return s2j(env, take(r));
}

JNIEXPORT jstring JNICALL Java_com_chatadhd_android_LoomNative_nativeImportFile(JNIEnv* env, jclass, jstring path,
                                                                                jstring title, jstring callbackId) {
  std::shared_lock<std::shared_mutex> lock(g_ctx_mu);
  if (!g_ctx) return s2j(env, err_json(LOOM_E_INVALID_ARGUMENT));
  std::string p = j2s(env, path);
  std::string t = j2s(env, title);
  ImportProgressCtx ctx{j2s(env, callbackId)};
  const char* r = loom_import_file(g_ctx, p.c_str(), title ? t.c_str() : nullptr, import_progress_trampoline, &ctx);
  std::string result = take(r);
  deliver_chunk(ctx.callback_id, result, true);
  return s2j(env, result);
}

JNIEXPORT jlong JNICALL Java_com_chatadhd_android_LoomNative_nativeSubscribe(JNIEnv* env, jclass, jstring event,
                                                                             jstring callbackId) {
  std::shared_lock<std::shared_mutex> ctx_lock(g_ctx_mu);
  if (!g_ctx) return static_cast<jlong>(LOOM_E_INVALID_ARGUMENT);
  std::string ev = j2s(env, event);
  auto id = std::make_unique<std::string>(j2s(env, callbackId));
  int64_t token = loom_subscribe(g_ctx, ev.c_str(), event_trampoline, id.get());
  if (token > 0) {
    std::lock_guard<std::mutex> lock(g_sub_mu);
    g_subscriptions[token] = std::move(id);
  }
  return static_cast<jlong>(token);
}

JNIEXPORT jint JNICALL Java_com_chatadhd_android_LoomNative_nativeUnsubscribe(JNIEnv*, jclass, jlong token) {
  std::shared_lock<std::shared_mutex> ctx_lock(g_ctx_mu);
  if (!g_ctx) return LOOM_E_INVALID_ARGUMENT;
  int rc = loom_unsubscribe(g_ctx, token);
  std::lock_guard<std::mutex> lock(g_sub_mu);
  g_subscriptions.erase(token);
  return rc;
}

JNIEXPORT void JNICALL Java_com_chatadhd_android_LoomNative_nativeSetLogLevel(JNIEnv*, jclass, jint level) {
  loom_set_log_level(level);
}

// -- called BY LoomHttp.kt to feed a response back into the transport --
JNIEXPORT jint JNICALL Java_com_chatadhd_android_LoomNative_nativeHttpResponseBegin(JNIEnv* env, jclass, jlong handle,
                                                                                    jint status, jstring headersJson) {
  std::string h = j2s(env, headersJson);
  return loom_http_response_begin(reinterpret_cast<LoomHttpResponse*>(handle), status, h.empty() ? nullptr : h.c_str());
}

JNIEXPORT jint JNICALL Java_com_chatadhd_android_LoomNative_nativeHttpResponseWrite(JNIEnv* env, jclass, jlong handle,
                                                                                    jbyteArray data) {
  jsize len = data ? env->GetArrayLength(data) : 0;
  if (len == 0) return loom_http_response_write(reinterpret_cast<LoomHttpResponse*>(handle), "", 0);
  jbyte* bytes = env->GetByteArrayElements(data, nullptr);
  int rc = loom_http_response_write(reinterpret_cast<LoomHttpResponse*>(handle), reinterpret_cast<const char*>(bytes),
                                    static_cast<size_t>(len));
  env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
  return rc;
}

JNIEXPORT jint JNICALL Java_com_chatadhd_android_LoomNative_nativeHttpResponseFail(JNIEnv* env, jclass, jlong handle,
                                                                                   jint code, jstring message) {
  std::string m = j2s(env, message);
  return loom_http_response_fail(reinterpret_cast<LoomHttpResponse*>(handle), code, m.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_chatadhd_android_LoomNative_nativeHttpResponseCancelled(JNIEnv*, jclass,
                                                                                             jlong handle) {
  return loom_http_response_cancelled(reinterpret_cast<const LoomHttpResponse*>(handle)) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
