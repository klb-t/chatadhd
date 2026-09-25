// OWNER: wave 2 net. Stub.
#include "loom/net/sse.h"

namespace loom::net {

bool SseEvent::is_done() const noexcept {
  std::string_view d = data;
  while (!d.empty() && d.front() == ' ') d.remove_prefix(1);
  while (!d.empty() && d.back() == ' ') d.remove_suffix(1);
  return d == "[DONE]";
}

void SseParser::feed(std::string_view, const Callback&) {}  // STUB: wave2
void SseParser::finish(const Callback&) {}                  // STUB: wave2
void SseParser::reset() {
  buf_.clear();
  bom_checked_ = false;
  last_was_cr_ = false;
  cur_ = SseEvent{};
  has_data_ = false;
}
void SseParser::process_line(std::string_view, const Callback&) {}  // STUB: wave2

}  // namespace loom::net
