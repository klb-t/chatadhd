// One media call (ASR / OCR) as the one model action: the providers describe the
// call, use_model admits it (usage guard), puts it on the wire through the
// adapter of the API family and records its provenance.  Internal to src/media.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/use_model.h"

namespace loom::media_detail {

struct WireCall {
  std::string action;      // operation kind: "media.asr" | "media.ocr"
  std::string manifest;    // provider manifest id
  std::string adapter;     // wire family (llm adapter)
  std::string endpoint;    // absolute URL from the descriptor
  std::string credential;  // the key the provider holds
  net::Headers headers;    // descriptor headers
  std::string model;
  llm::Part input;         // audio / image bytes
  Json parameters = Json::object();  // ordered, appended after the model on the wire
  int timeout_ms = 60000;
};

// The usage guard's hold of a media call, carried from send() to the MediaProviders entry that
// started the call (same thread, synchronous).  Scope = one entry call:
//  - `confirmation` (optional, {approved, receipt_id, ref, operation_id} as chat takes it) answers
//    the FIRST send() of the scope - the identical held request - and is then consumed;
//  - a hold seen by send() is stored as `pending` (shape "loom.action_usage_pending/1") and send()
//    returns Errc::Paused: it is not a provider failure and the providers must not fall back.
class HoldScope {
 public:
  explicit HoldScope(Json confirmation = Json::object());
  ~HoldScope();
  HoldScope(const HoldScope&) = delete;
  HoldScope& operator=(const HoldScope&) = delete;
  bool confirming() const noexcept { return confirming_; }
  Json pending() const;  // null when no hold was seen
 private:
  bool confirming_;
  void* prev_;
};

// The 2xx response (status + body), or the error already worded for the caller:
// "<label>: <detail>" with Errc::Network / Errc::Http / Errc::Paused (held by the usage guard; see HoldScope).
Result<net::HttpResponse> send(net::HttpTransport& http, const std::shared_ptr<MediaCallContext>& ctx,
                               const WireCall& call, std::string_view label, std::size_t error_body_chars);

}  // namespace loom::media_detail
