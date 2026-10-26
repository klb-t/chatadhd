// sse_stream.h — a small thread-safe queue that bridges a Loom callback
// (invoked on a background thread) to an httplib chunked content provider
// (invoked on the connection's worker thread).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

#include <httplib.h>

namespace loom_server {

// One instance per in-flight SSE response. Shared (via shared_ptr) between
// the producer thread(s) that call push()/close() and the httplib content
// provider that drains it.
class SseQueue {
 public:
  // Formats `data_json` as a standard SSE "data:" event and enqueues it.
  void push_data(const std::string& data_json) {
    push_raw("data: " + data_json + "\n\n");
  }

  void push_raw(std::string chunk) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (closed_) return;
      items_.push_back(std::move(chunk));
    }
    cv_.notify_all();
  }

  // Marks the stream complete; no more pushes are accepted.
  void close() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  // Requests the producer stop (client went away). Producers should poll
  // cancelled() and call loom_chat_cancel / stop early when true.
  void cancel() { cancelled_.store(true); }
  bool cancelled() const { return cancelled_.load(); }

  // A feed that nobody writes to (the page's event feed between events) still holds a server thread. Without a write there is no way to
  // notice that the client went away, so an idle feed writes an SSE comment (": keep-alive", which every SSE reader ignores) every
  // `keepalive_ms` (0 = never, as before) and ends when the write fails; a stopping server ends every feed within one tick.
  static std::atomic<long>& keepalive_ms() {
    static std::atomic<long> value{15000};
    return value;
  }
  static std::atomic<bool>& stopping() {
    static std::atomic<bool> value{false};
    return value;
  }

  // httplib ContentProviderWithoutLength body. Returns false to end the
  // stream (either normal completion or a write failure / disconnect).
  bool provide(size_t /*offset*/, httplib::DataSink& sink) {
    std::unique_lock<std::mutex> lk(mu_);
    const long keepalive = keepalive_ms().load();
    constexpr auto kTick = std::chrono::milliseconds(500);
    const bool ready = cv_.wait_for(lk, kTick, [&] { return !items_.empty() || closed_ || stopping().load(); });
    if (!ready) {
      // Idle tick: nothing to send. The feed is kept, and probed when a keep-alive is due.
      idle_ms_ += static_cast<long>(kTick.count());
      if (keepalive <= 0 || idle_ms_ < keepalive) return true;
      idle_ms_ = 0;
      lk.unlock();
      static const char kComment[] = ": keep-alive\n\n";
      if (!sink.write(kComment, sizeof(kComment) - 1)) {
        cancel();
        return false;
      }
      return true;
    }
    idle_ms_ = 0;
    if (items_.empty() && !closed_ && stopping().load()) {
      lk.unlock();
      sink.done();
      return true;  // finished, not cancelled (see below)
    }
    while (!items_.empty()) {
      std::string item = std::move(items_.front());
      items_.pop_front();
      lk.unlock();
      if (!sink.write(item.data(), item.size())) {
        cancel();
        return false;
      }
      lk.lock();
    }
    if (closed_) {
      lk.unlock();
      // A finished stream returns true: httplib takes false as "cancelled" and closes the connection after the final
      // chunk although the answer advertised Keep-Alive, so a client may reuse it while the close is on its way
      // (CI asan, server.security 11b: the retried upload resent an already consumed body -> 400 "malformed").
      sink.done();
      return true;
    }
    return true;
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> items_;
  bool closed_ = false;
  std::atomic<bool> cancelled_{false};
  long idle_ms_ = 0;  // only the one thread that provides touches it
};

}  // namespace loom_server
