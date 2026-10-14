// Safety settings for untrusted input (R20). Every value is a setting in the user's config.json with a preset
// served from loom_config_defaults(); 0 always means "no limit" (or, for booleans, the permissive choice is
// the explicit opt-in documented next to the key).
//   loom_zip_max_total_bytes   most uncompressed bytes one archive may expand to (preset 64 GiB)
//   loom_zip_max_ratio         largest uncompressed/compressed ratio of one entry (preset 1000)
//   loom_git_allow_repo_config true = run git in a user's repository with its own hooks and configuration
//                              (preset false: hooks, fsmonitor and other config-driven programs are disabled)
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "loom/result.h"
#include "../../third_party/miniz/miniz.h"

namespace loom::safety {

struct ZipLimits {
  std::uint64_t max_total_bytes = 0;  // 0 = no limit
  double max_ratio = 0;               // 0 = no limit
};

// Operator ceiling of an account runtime (server_accounts zip/git): registered per data dir when the runtime
// opens; the effective setting is the more restrictive of ceiling and config.json. 0 = no ceiling.
struct OperatorCeiling {
  std::int64_t zip_max_total_bytes = 0;
  double zip_max_ratio = 0;
  bool git_allow_repo_config = true;
};
void set_operator_ceiling(const std::filesystem::path& data_dir, OperatorCeiling c);

// <data_dir>/config.json keys over the presets; a missing or unreadable file means the presets.
ZipLimits zip_limits(const std::filesystem::path& data_dir);
bool git_allow_repo_config(const std::filesystem::path& data_dir);

// Arguments that stop git from running anything a repository can name: hooks, fsmonitor, pagers, external
// diff/textconv, credential and ssh helpers, and non-local transports. Placed before the git subcommand.
std::vector<std::string> git_hardening_args();

// Per-archive accounting. Entries are checked from the central directory before anything is written, and
// again while streaming (the declared sizes are the archive's claim, not a fact), so a bomb is cut off
// before it writes past the limit. A refusal is Errc::Parse with a message that names the setting.
class ZipBudget {
 public:
  explicit ZipBudget(ZipLimits limits) : limits_(limits) {}
  Status admit(const mz_zip_archive_file_stat& st);
  // Streams entry `index` to `dest`; the partial file is removed on any failure.
  Status extract_to_file(mz_zip_archive& zip, mz_uint index, const std::filesystem::path& dest);
  Result<std::string> extract_to_string(mz_zip_archive& zip, mz_uint index);
  // True when the last failure was a limit (not a corrupt entry): callers abort the whole archive.
  bool refused() const noexcept { return refused_; }
  const std::string& reason() const noexcept { return reason_; }

 private:
  Status refuse(std::string why);
  ZipLimits limits_;
  std::uint64_t declared_ = 0, written_ = 0;
  bool refused_ = false;
  std::string reason_;
};

}  // namespace loom::safety
