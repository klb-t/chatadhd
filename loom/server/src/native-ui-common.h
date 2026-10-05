#pragma once

// Server-owned adapters borrow the existing static kernel's Runtime. This is
// a source dependency on its authoritative context declaration, not a new C ABI
// export or a second runtime/database. Embedded hosts remain capability-driven.
#include "../../src/capi/context.h"

#include <httplib.h>
#include <exception>
#include <utility>

namespace loom_server::native_ui {
using Json = loom::Json;

// Shared by the HTTP config writes and native UI context/profile CAS boundary.
// It does not claim to serialize a different process or an embedded transport.
inline std::mutex& config_mutex() { static std::mutex mutex; return mutex; }

inline int error_status(loom::Errc code) {
  switch (code) {
    case loom::Errc::InvalidArgument:
    case loom::Errc::Parse:
    case loom::Errc::Unsupported: return 400;
    case loom::Errc::NotFound: return 404;
    case loom::Errc::AlreadyExists:
    case loom::Errc::Conflict:
    case loom::Errc::Busy:
    case loom::Errc::Cancelled:
    case loom::Errc::Paused: return 409;
    case loom::Errc::Auth: return 401;
    case loom::Errc::Unavailable: return 503;
    case loom::Errc::NotImplemented: return 501;
    case loom::Errc::Timeout: return 504;
    case loom::Errc::Network:
    case loom::Errc::Http: return 502;
    case loom::Errc::RateLimited: return 429;
    default: return 500;
  }
}

inline void reply(httplib::Response& response, const Json& value) {
  response.status = 200;
  response.set_content(value.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json");
}

inline void fail(httplib::Response& response, const loom::Error& error) {
  response.status = error_status(error.code);
  response.set_content(loom::capi::error_json(error).dump(), "application/json");
}

inline loom::Result<Json> parse(const httplib::Request& request) {
  Json body = Json::parse(request.body, nullptr, false);
  if (body.is_discarded()) return loom::Error(loom::Errc::Parse, "body must contain valid JSON");
  if (!body.is_object()) return loom::Error(loom::Errc::InvalidArgument, "body must be a JSON object");
  return body;
}

template<class F>
inline void protect(httplib::Response& response, F&& call) {
  try { std::forward<F>(call)(); }
  catch (const Json::exception&) {
    fail(response, loom::Error(loom::Errc::InvalidArgument, "invalid native UI command field"));
  } catch (const std::exception&) {
    fail(response, loom::Error(loom::Errc::Internal, "native UI operation failed"));
  } catch (...) {
    fail(response, loom::Error(loom::Errc::Internal, "native UI operation failed"));
  }
}
}  // namespace loom_server::native_ui
