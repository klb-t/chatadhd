// sse_stream.h — a small thread-safe queue that bridges a Loom callback
// (invoked on a background thread) to an httplib chunked content provider
// (invoked on the connection's worker thread).
#pragma once

#include <atomic>
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

  // httplib ContentProviderWithoutLength body. Returns false to end the
  // stream (either normal completion or a write failure / disconnect).
  bool provide(size_t /*offset*/, httplib::DataSink& sink) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return !items_.empty() || closed_; });
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
      sink.done();
      return false;
    }
    return true;
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> items_;
  bool closed_ = false;
  std::atomic<bool> cancelled_{false};
};

}  // namespace loom_server
