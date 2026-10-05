// loom/net/sse.h — incremental Server-Sent Events parser.   [OWNER: wave 2 net]
//
// Chunks arrive at arbitrary boundaries (mid-line, mid-UTF-8 sequence, CR/LF
// split across chunks). The parser follows the WHATWG EventSource rules:
//   * lines end with "\r\n", "\n" or "\r"; a leading UTF-8 BOM is skipped;
//   * "field: value" — one optional space after the colon is removed;
//   * lines starting with ':' are comments (OpenRouter sends
//     ": OPENROUTER PROCESSING" keep-alives) and are ignored;
//   * multiple "data" lines are joined with "\n";
//   * a blank line dispatches the event (only if data was seen);
//   * "id" and "retry" (integer) are recorded; unknown fields are ignored.
// Python's ChatEngine._read_stream only looked at "data: " lines and stopped
// at "[DONE]"; is_done() reproduces that check.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace loom::net {

struct SseEvent {
  std::string event;  // "" = default "message"
  std::string data;
  std::string id;
  std::optional<int> retry;
  bool is_done() const noexcept;  // data == "[DONE]" (after trimming spaces)
};

class SseParser {
 public:
  using Callback = std::function<void(const SseEvent&)>;

  // Feed the next chunk; `cb` is called for every completed event.
  void feed(std::string_view chunk, const Callback& cb);
  // End of stream: dispatch a pending event that lacked the final blank line.
  void finish(const Callback& cb);
  void reset();

 private:
  void process_line(std::string_view line, const Callback& cb);
  std::string buf_;
  bool bom_checked_ = false;
  bool last_was_cr_ = false;
  SseEvent cur_;
  bool has_data_ = false;
};

}  // namespace loom::net
