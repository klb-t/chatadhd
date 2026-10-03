// loom/result.h — error model for the whole kernel.
//
// Loom does not use exceptions for control flow. Fallible functions return
// loom::Result<T> (value or loom::Error) or loom::Status (= Result<void>).
// Exceptions thrown by third-party code are caught at module boundaries and
// always at the C ABI (see loom.h).
#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace loom {

// Error categories. The snake_case spelling returned by errc_name() is part of
// the C ABI contract ({"error":{"code":"not_found",...}}), so never rename.
enum class Errc : int {
  InvalidArgument = 1,
  NotFound,
  AlreadyExists,
  Io,
  Database,
  Parse,
  Network,
  Http,
  Auth,
  Cancelled,
  Timeout,
  Unavailable,     // optional capability missing (no OpenSSL, no API key, no FTS5, ...)
  NotImplemented,  // wave-2 stub or unsupported code path
  Crypto,
  Conflict,
  Busy,
  Unsupported,
  RateLimited,
  Paused,          // task yielded because a pause was requested
  Internal,
};

std::string_view errc_name(Errc code) noexcept;
std::optional<Errc> errc_from_name(std::string_view name) noexcept;

struct Error {
  Errc code = Errc::Internal;
  std::string message;

  Error() = default;
  Error(Errc c, std::string msg) : code(c), message(std::move(msg)) {}

  // "not_found: conversation c_123 does not exist"
  std::string to_string() const;
};

inline Error make_error(Errc code, std::string message) { return Error(code, std::move(message)); }

template <class T>
class [[nodiscard]] Result {
  static_assert(!std::is_same_v<std::remove_cvref_t<T>, Error>, "Result<Error> is not allowed");

 public:
  using value_type = T;

  Result(const T& v) : v_(std::in_place_index<0>, v) {}                 // NOLINT(google-explicit-constructor)
  Result(T&& v) : v_(std::in_place_index<0>, std::move(v)) {}           // NOLINT(google-explicit-constructor)
  Result(const Error& e) : v_(std::in_place_index<1>, e) {}             // NOLINT(google-explicit-constructor)
  Result(Error&& e) : v_(std::in_place_index<1>, std::move(e)) {}       // NOLINT(google-explicit-constructor)
  template <class U,
            class = std::enable_if_t<std::is_constructible_v<T, U&&> && !std::is_same_v<std::remove_cvref_t<U>, T> &&
                                     !std::is_same_v<std::remove_cvref_t<U>, Error> &&
                                     !std::is_same_v<std::remove_cvref_t<U>, Result>>>
  Result(U&& u) : v_(std::in_place_index<0>, std::forward<U>(u)) {}     // NOLINT(google-explicit-constructor)

  bool has_value() const noexcept { return v_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  T& value() & { assert(has_value()); return std::get<0>(v_); }
  const T& value() const& { assert(has_value()); return std::get<0>(v_); }
  T&& value() && { assert(has_value()); return std::get<0>(std::move(v_)); }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T&& operator*() && { return std::move(*this).value(); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  const Error& error() const& { assert(!has_value()); return std::get<1>(v_); }
  Error&& error() && { assert(!has_value()); return std::get<1>(std::move(v_)); }

  template <class U>
  T value_or(U&& fallback) const& {
    return has_value() ? std::get<0>(v_) : static_cast<T>(std::forward<U>(fallback));
  }
  template <class U>
  T value_or(U&& fallback) && {
    return has_value() ? std::get<0>(std::move(v_)) : static_cast<T>(std::forward<U>(fallback));
  }

 private:
  std::variant<T, Error> v_;
};

template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  Result(const Error& e) : err_(e) {}       // NOLINT(google-explicit-constructor)
  Result(Error&& e) : err_(std::move(e)) {} // NOLINT(google-explicit-constructor)

  bool has_value() const noexcept { return !err_.has_value(); }
  explicit operator bool() const noexcept { return has_value(); }
  void value() const { assert(has_value()); }
  const Error& error() const& { assert(!has_value()); return *err_; }
  Error&& error() && { assert(!has_value()); return std::move(*err_); }

 private:
  std::optional<Error> err_;
};

using Status = Result<void>;

inline Status ok_status() { return Status(); }

}  // namespace loom

#define LOOM_DETAIL_CONCAT_INNER(a, b) a##b
#define LOOM_DETAIL_CONCAT(a, b) LOOM_DETAIL_CONCAT_INNER(a, b)

// Propagate an error from an expression returning Result<T>/Status.
#define LOOM_TRY(expr)                                                      \
  do {                                                                      \
    auto&& loom_try_result_ = (expr);                                       \
    if (!loom_try_result_) return ::loom::Error(loom_try_result_.error());  \
  } while (0)

// `LOOM_TRY_ASSIGN(auto x, compute());` or `LOOM_TRY_ASSIGN(x, compute());`
#define LOOM_TRY_ASSIGN(lhs, expr) LOOM_DETAIL_TRY_ASSIGN(LOOM_DETAIL_CONCAT(loom_try_tmp_, __LINE__), lhs, expr)
#define LOOM_DETAIL_TRY_ASSIGN(tmp, lhs, expr)                 \
  auto tmp = (expr);                                           \
  if (!tmp) return ::loom::Error(std::move(tmp).error());      \
  lhs = std::move(tmp).value()
