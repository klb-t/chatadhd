#pragma once

#include <cerrno>
#include <filesystem>
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#define LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES 1
#else
#define LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES 0
#endif

#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "loom/util/sha256.h"

namespace loom::context {
namespace detail {
inline Status sync_context_claim_directory(const std::filesystem::path& directory) {
#if LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
  const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return Error(Errc::Io, "execution claim directory could not be opened for durability");
  int synced;
  do { synced = ::fsync(fd); } while (synced != 0 && errno == EINTR);
  const int closed = ::close(fd);
  if (synced != 0 || closed != 0) return Error(Errc::Io, "execution claim directory durability failed");
  return {};
#else
  (void)directory;
  return Error(Errc::Unavailable, "durable execution claim directory flush is unavailable on this platform");
#endif
}
}

// A process-independent, durable execution claim supplements UsagePolicy's
// idempotent admission. The latter does not itself atomically consume a spend
// authorization. Claim creation happens only after admission and before any
// transport attempt. A crash leaves the claim in place; its spend is UNKNOWN.
// A fresh explicit operation may retry, but the same admitted operation cannot
// silently send twice. This does not claim that providers execute exactly once.
inline Result<Json> claim_context_usage_operation(const std::filesystem::path& data_root,
                                                  const Json& decision) {
#if !LOOM_CONTEXT_CLAIM_HAS_DURABLE_DIRECTORIES
  (void)data_root; (void)decision;
  return Error(Errc::Unavailable, "durable execution claims require supported POSIX directory fsync");
#else
  const auto operation = json::get_string(decision, "operation_id");
  const auto receipt = json::get_string(decision, "receipt_id");
  if (!json::get_bool(decision, "authorized") || operation.empty() || receipt.empty())
    return Error(Errc::InvalidArgument, "execution claim needs an admitted operation and its receipt");
  const auto directory = data_root / "context_execution_claims";
  LOOM_TRY(fsutil::ensure_dir(directory));
  const auto claim_path = directory / Sha256::hex(operation);
  std::error_code error;
  const bool created = std::filesystem::create_directory(claim_path, error);
  if (error) return Error(Errc::Io, "execution claim could not be created");
  Json result{{"schema", "loom.context_execution_claim/1"}, {"operation_id", operation},
      {"receipt_id", receipt}, {"status", created ? "claimed" : "already_started"},
      {"claimed", created}, {"amount_spent", nullptr}};
  if (!created) {
    if (!std::filesystem::is_directory(claim_path, error) || error)
      return Error(Errc::Io, "execution claim path is not a directory");
    return result;
  }
  // Failed durability leaves a blocking claim and never authorizes transport.
  // Fixed temporary names are safe: only this operation's exclusive winner
  // writes this private directory; concurrent operations use different paths.
  LOOM_TRY(fsutil::atomic_write(claim_path / "claim.json", json::dump(result), {.owner_only = true}));
  LOOM_TRY(detail::sync_context_claim_directory(claim_path));
  LOOM_TRY(detail::sync_context_claim_directory(directory));
  LOOM_TRY(detail::sync_context_claim_directory(data_root));
  return result;
#endif
}
}  // namespace loom::context
