// Immutable archive policy scoped to a runtime or an explicit pure-function call.
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "loom/kb.h"
#include "loom/runtime_profile.h"

namespace loom::archive {
class ArchiveProfile {
 public:
  static Result<ArchiveProfile> builtin();
  static Result<ArchiveProfile> load(const std::filesystem::path& root = {}, const Json& overrides = Json::object());
  Result<ArchiveProfile> with_overrides(const Json& overrides) const;
  Result<ArchiveProfile> with_patch(const Json& patch) const;
  Result<Json> provenance() const;
  static Result<ArchiveProfile> from_provenance(const Json& snapshot);
  const Json& value(std::string_view pointer) const;
  int integer(std::string_view pointer) const { return value(pointer).get<int>(); }
  double number(std::string_view pointer) const { return value(pointer).get<double>(); }
  std::string text(std::string_view pointer) const { return value(pointer).get<std::string>(); }
  bool contains(std::string_view pointer, std::string_view word) const;
  const Json& items() const { return pack_->lexicon("item_cues"); }
  bool stopword(std::string_view word) const { return stopwords_.count(std::string(word)) != 0; }
  const std::string& hash() const noexcept { return hash_; }
  bool is_builtin() const noexcept { return builtin_; }
  Json inspection() const;
 private:
  ArchiveProfile(std::shared_ptr<const kb::Pack> pack, RuntimeProfile profile, bool builtin_pack);
  std::shared_ptr<const kb::Pack> pack_;
  RuntimeProfile profile_;
  std::unordered_set<std::string> stopwords_;
  std::string hash_;
  bool builtin_ = false;
  bool builtin_pack_ = false;
};

// Legacy pure-function callers get local immutable defaults. Production passes
// an already loaded instance through every helper; no global mutable policy.
class ProfileScope {
 public:
  explicit ProfileScope(const ArchiveProfile* profile);
  const ArchiveProfile& get() const { return *profile_; }
  const ArchiveProfile* ptr() const { return profile_; }
 private:
  std::optional<ArchiveProfile> owned_;
  const ArchiveProfile* profile_;
};
}  // namespace loom::archive
