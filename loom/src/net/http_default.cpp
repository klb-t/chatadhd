// OWNER: wave 2 net. Default transport (cpp-httplib + OpenSSL). Stub.
#include "loom/net/http.h"
#include "stub.h"

namespace loom::net {
namespace {

class UnimplementedTransport final : public HttpTransport {
 public:
  Result<HttpResponse> send(const HttpRequest& req, const StreamSink*, const CancelToken*) override {
    return LOOM_NOT_IMPLEMENTED("default HTTP transport (" + req.method + " " + req.url + ")");  // STUB: wave2
  }
  std::string name() const override { return "unimplemented"; }
};

}  // namespace

std::shared_ptr<HttpTransport> make_default_transport() {
  return std::make_shared<UnimplementedTransport>();  // STUB: wave2
}

}  // namespace loom::net
