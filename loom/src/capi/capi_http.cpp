// C API: platform HTTP injection (loom_set_http_transport + LoomHttpResponse).
// OWNER (wave 2): net. Complete in the foundation because the protocol is
// part of the ABI contract; the net agent may extend it (e.g. upload streams).
#include "context.h"
#include "loom/net/http.h"

using namespace loom;
using namespace loom::capi;

// Opaque to C: one per request, lives on the Loom thread's stack.
struct LoomHttpResponse {
  const net::StreamSink* sink = nullptr;
  const CancelToken* cancel = nullptr;
  bool began = false;
  bool aborted = false;
  std::optional<Error> failure;
  net::HttpResponse resp;
};

namespace {

class CallbackTransport final : public net::HttpTransport {
 public:
  CallbackTransport(LoomHttpSendFn fn, void* ud) : fn_(fn), ud_(ud) {}

  Result<net::HttpResponse> send(const net::HttpRequest& req, const net::StreamSink* sink,
                                 const CancelToken* cancel) override {
    if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "request cancelled");
    LoomHttpResponse r;
    r.sink = sink;
    r.cancel = cancel;
    std::string rj = json::dump(req.to_json());
    int rc = LOOM_OK;
    try {
      rc = fn_(rj.c_str(), &r, ud_);
    } catch (...) {
      return Error(Errc::Internal, "platform HTTP callback threw");
    }
    if (r.failure) return *r.failure;
    if (r.aborted || (cancel && cancel->cancelled())) return Error(Errc::Cancelled, "request cancelled");
    if (rc != LOOM_OK) return Error(errc_from_code(rc), "platform HTTP transport failed (" + std::to_string(rc) + ")");
    if (!r.began) return Error(Errc::Network, "platform HTTP transport returned no response");
    return std::move(r.resp);
  }
  std::string name() const override { return "platform-callback"; }

 private:
  LoomHttpSendFn fn_;
  void* ud_;
};

}  // namespace

extern "C" {

LOOM_API int loom_http_response_begin(LoomHttpResponse* r, int status, const char* headers_json) {
  return guard_int("loom_http_response_begin", [&] {
    if (!r || r->began) return LOOM_E_INVALID_ARGUMENT;
    r->began = true;
    r->resp.status = status;
    if (headers_json && *headers_json) {
      auto h = json::parse(headers_json);
      if (!h) return code(h.error());
      if (h->is_object()) {
        for (auto it = h->begin(); it != h->end(); ++it) {
          if (it.value().is_string()) r->resp.headers.emplace_back(it.key(), it.value().get<std::string>());
        }
      }
    }
    if (r->cancel && r->cancel->cancelled()) {
      r->aborted = true;
      return LOOM_E_CANCELLED;
    }
    if (r->sink && r->sink->on_headers && !r->sink->on_headers(status, r->resp.headers)) {
      r->aborted = true;
      return LOOM_E_CANCELLED;
    }
    return LOOM_OK;
  });
}

LOOM_API int loom_http_response_write(LoomHttpResponse* r, const char* data, size_t len) {
  return guard_int("loom_http_response_write", [&] {
    if (!r || !r->began || (!data && len > 0)) return LOOM_E_INVALID_ARGUMENT;
    if (r->aborted || (r->cancel && r->cancel->cancelled())) {
      r->aborted = true;
      return LOOM_E_CANCELLED;
    }
    std::string_view chunk(data ? data : "", len);
    if (r->sink && r->sink->on_data) {
      if (!r->sink->on_data(chunk)) {
        r->aborted = true;
        return LOOM_E_CANCELLED;
      }
    } else {
      r->resp.body.append(chunk);
    }
    return LOOM_OK;
  });
}

LOOM_API int loom_http_response_fail(LoomHttpResponse* r, int error_code, const char* message) {
  return guard_int("loom_http_response_fail", [&] {
    if (!r) return LOOM_E_INVALID_ARGUMENT;
    r->failure = Error(errc_from_code(error_code < 0 ? error_code : LOOM_E_NETWORK),
                       message ? message : "platform HTTP error");
    return LOOM_OK;
  });
}

LOOM_API int loom_http_response_cancelled(const LoomHttpResponse* r) {
  if (!r) return 0;
  return (r->aborted || (r->cancel && r->cancel->cancelled())) ? 1 : 0;
}

LOOM_API int loom_set_http_transport(LoomContext* ctx, LoomHttpSendFn fn, void* user_data) {
  return guard_int("loom_set_http_transport", [&] {
    LOOM_CAPI_REQUIRE_CTX_INT(ctx);
    if (fn) {
      ctx->rt->set_http_transport(std::make_shared<CallbackTransport>(fn, user_data));
    } else {
      ctx->rt->set_http_transport(nullptr);
    }
    return LOOM_OK;
  });
}

}  // extern "C"
