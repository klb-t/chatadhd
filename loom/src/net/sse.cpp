// OWNER: wave 2 net. Incremental Server-Sent Events parser (WHATWG subset).
#include "loom/net/sse.h"

namespace loom::net {

namespace {
constexpr std::string_view kBom = "\xEF\xBB\xBF";
}  // namespace

bool SseEvent::is_done() const noexcept {
  std::string_view d = data;
  while (!d.empty() && d.front() == ' ') d.remove_prefix(1);
  while (!d.empty() && d.back() == ' ') d.remove_suffix(1);
  return d == "[DONE]";
}

void SseParser::process_line(std::string_view line, const Callback& cb) {
  if (line.empty()) {
    if (has_data_) {
      SseEvent evt = cur_;
      cb(evt);
    }
    cur_ = SseEvent{};
    has_data_ = false;
    return;
  }
  if (line.front() == ':') return;  // comment

  std::string_view field, value;
  if (auto colon = line.find(':'); colon == std::string_view::npos) {
    field = line;
  } else {
    field = line.substr(0, colon);
    value = line.substr(colon + 1);
    if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
  }

  if (field == "data") {
    if (has_data_) {
      cur_.data.push_back('\n');
      cur_.data.append(value);
    } else {
      cur_.data.assign(value);
      has_data_ = true;
    }
  } else if (field == "event") {
    cur_.event.assign(value);
  } else if (field == "id") {
    cur_.id.assign(value);
  } else if (field == "retry") {
    bool all_digit = !value.empty();
    for (char c : value) {
      if (c < '0' || c > '9') {
        all_digit = false;
        break;
      }
    }
    if (all_digit) {
      int v = 0;
      for (char c : value) v = v * 10 + (c - '0');
      cur_.retry = v;
    }
  }
  // Unknown fields are ignored.
}

void SseParser::feed(std::string_view chunk, const Callback& cb) {
  buf_.append(chunk.data(), chunk.size());

  if (!bom_checked_ && buf_.size() >= kBom.size()) {
    bom_checked_ = true;
    if (buf_.compare(0, kBom.size(), kBom) == 0) buf_.erase(0, kBom.size());
  }

  std::size_t pos = 0;
  while (true) {
    std::size_t line_end = 0, next_pos = 0;
    bool found = false;
    for (std::size_t i = pos; i < buf_.size(); ++i) {
      char c = buf_[i];
      if (c == '\n') {
        line_end = i;
        next_pos = i + 1;
        found = true;
        break;
      }
      if (c == '\r') {
        if (i + 1 < buf_.size()) {
          line_end = i;
          next_pos = (buf_[i + 1] == '\n') ? i + 2 : i + 1;
          found = true;
        }
        // else: ambiguous at the current end of buffer - wait for more data.
        break;
      }
    }
    if (!found) break;
    process_line(std::string_view(buf_).substr(pos, line_end - pos), cb);
    pos = next_pos;
  }
  buf_.erase(0, pos);
}

void SseParser::finish(const Callback& cb) {
  if (!bom_checked_) {
    bom_checked_ = true;
    if (buf_.size() >= kBom.size() && buf_.compare(0, kBom.size(), kBom) == 0) buf_.erase(0, kBom.size());
  }
  if (!buf_.empty()) {
    std::string_view line = buf_;
    if (line.back() == '\r' || line.back() == '\n') line.remove_suffix(1);
    process_line(line, cb);
    buf_.clear();
  }
  if (has_data_) {
    SseEvent evt = cur_;
    cb(evt);
  }
  reset();
}

void SseParser::reset() {
  buf_.clear();
  bom_checked_ = false;
  last_was_cr_ = false;
  cur_ = SseEvent{};
  has_data_ = false;
}

}  // namespace loom::net
