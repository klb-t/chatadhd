#include "loom/result.h"

#include <array>
#include <utility>

namespace loom {
namespace {
constexpr std::array<std::pair<Errc, std::string_view>, 20> kNames{{
    {Errc::InvalidArgument, "invalid_argument"},
    {Errc::NotFound, "not_found"},
    {Errc::AlreadyExists, "already_exists"},
    {Errc::Io, "io"},
    {Errc::Database, "database"},
    {Errc::Parse, "parse"},
    {Errc::Network, "network"},
    {Errc::Http, "http"},
    {Errc::Auth, "auth"},
    {Errc::Cancelled, "cancelled"},
    {Errc::Timeout, "timeout"},
    {Errc::Unavailable, "unavailable"},
    {Errc::NotImplemented, "not_implemented"},
    {Errc::Crypto, "crypto"},
    {Errc::Conflict, "conflict"},
    {Errc::Busy, "busy"},
    {Errc::Unsupported, "unsupported"},
    {Errc::RateLimited, "rate_limited"},
    {Errc::Paused, "paused"},
    {Errc::Internal, "internal"},
}};
}  // namespace

std::string_view errc_name(Errc code) noexcept {
  for (const auto& [c, n] : kNames) {
    if (c == code) return n;
  }
  return "internal";
}

std::optional<Errc> errc_from_name(std::string_view name) noexcept {
  for (const auto& [c, n] : kNames) {
    if (n == name) return c;
  }
  return std::nullopt;
}

std::string Error::to_string() const {
  std::string out(errc_name(code));
  if (!message.empty()) {
    out += ": ";
    out += message;
  }
  return out;
}

}  // namespace loom
