// Internal: the object behind the opaque LoomContext* and helpers shared by
// every src/capi/capi_*.cpp file. Not installed, not part of the ABI.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "loom/log.h"
#include "loom/loom.h"
#include "loom/result.h"
#include "loom/runtime.h"
#include "loom/util/cancel.h"
#include "loom/util/json.h"

struct LoomContext {
  std::unique_ptr<loom::Runtime> rt;
  std::mutex mu;
  std::map<std::string, loom::CancelToken, std::less<>> chat_requests;  // request_id -> token
  std::optional<loom::CancelToken> archive_run;  // set while loom_archive_run is in progress
  std::optional<loom::CancelToken> knowledge_run;  // set while loom_knowledge_run is in progress
};

namespace loom::capi {

// ── string ownership ────────────────────────────────────────────────
// malloc'ed copy; released by loom_free_string (free). Aborts only if the
// allocator fails for a tiny string.
inline const char* dup(std::string_view s) {
  auto* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (!p) return nullptr;
  if (!s.empty()) std::memcpy(p, s.data(), s.size());
  p[s.size()] = '\0';
  return p;
}

inline const char* out(const Json& j) { return dup(json::dump(j)); }

inline Json error_json(const Error& e) {
  return Json{{"error", Json{{"code", std::string(errc_name(e.code))}, {"message", e.message}}}};
}

inline const char* out_error(const Error& e) { return out(error_json(e)); }
inline const char* out_error(Errc c, std::string msg) { return out_error(Error(c, std::move(msg))); }

inline int code(const Error& e) { return -static_cast<int>(e.code); }
inline int code(const Status& s) { return s ? LOOM_OK : code(s.error()); }

inline Errc errc_from_code(int rc) {
  if (rc <= -1 && rc >= -static_cast<int>(Errc::Internal)) return static_cast<Errc>(-rc);
  return Errc::Internal;
}

template <class T>
const char* out_result(const Result<T>& r) {
  if (!r) return out_error(r.error());
  return out(Json(*r));
}

// ── exception firewall ──────────────────────────────────────────────
template <class F>
const char* guard_json(const char* fn, F&& f) noexcept {
  try {
    return f();
  } catch (const std::exception& e) {
    log::error("loom.capi", "{}: unexpected exception: {}", fn, e.what());
    return out_error(Errc::Internal, std::string(fn) + ": " + e.what());
  } catch (...) {
    log::error("loom.capi", "{}: unexpected non-standard exception", fn);
    return out_error(Errc::Internal, std::string(fn) + ": unknown exception");
  }
}

template <class F>
int guard_int(const char* fn, F&& f) noexcept {
  try {
    return f();
  } catch (const std::exception& e) {
    log::error("loom.capi", "{}: unexpected exception: {}", fn, e.what());
    return LOOM_E_INTERNAL;
  } catch (...) {
    log::error("loom.capi", "{}: unexpected non-standard exception", fn);
    return LOOM_E_INTERNAL;
  }
}

template <class F>
void guard_void(const char* fn, F&& f) noexcept {
  try {
    f();
  } catch (const std::exception& e) {
    log::error("loom.capi", "{}: unexpected exception: {}", fn, e.what());
  } catch (...) {
    log::error("loom.capi", "{}: unexpected non-standard exception", fn);
  }
}

// ── argument helpers ────────────────────────────────────────────────
inline bool live(LoomContext* ctx) { return ctx && ctx->rt; }

inline std::optional<std::string> opt_str(const char* s) {
  if (!s || !*s) return std::nullopt;
  return std::string(s);
}

// Parses an optional JSON argument: NULL/"" -> `fallback`.
inline Result<Json> parse_arg(const char* s, Json fallback = Json::object()) {
  if (!s || !*s) return fallback;
  return json::parse(s);
}

inline Error missing(const char* what) { return Error(Errc::InvalidArgument, std::string(what) + " is required"); }

}  // namespace loom::capi

// Common prologues.
#define LOOM_CAPI_REQUIRE_CTX_JSON(ctx) \
  if (!::loom::capi::live(ctx)) return ::loom::capi::out_error(::loom::Errc::InvalidArgument, "ctx is NULL or shut down")
#define LOOM_CAPI_REQUIRE_CTX_INT(ctx) \
  if (!::loom::capi::live(ctx)) return LOOM_E_INVALID_ARGUMENT
