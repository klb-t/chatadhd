// loom/util/cancel.h — cooperative cancellation shared between threads.
#pragma once

#include <atomic>
#include <memory>

namespace loom {

// Copyable handle to one shared flag. Default-constructed tokens are live
// (never cancelled until cancel() is called on any copy).
class CancelToken {
 public:
  CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
  void cancel() const noexcept { flag_->store(true, std::memory_order_release); }
  bool cancelled() const noexcept { return flag_->load(std::memory_order_acquire); }
  void reset() const noexcept { flag_->store(false, std::memory_order_release); }

 private:
  std::shared_ptr<std::atomic<bool>> flag_;
};

}  // namespace loom
