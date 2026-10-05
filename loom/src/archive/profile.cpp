#include "archive/profile.h"
#include <algorithm>
#include <stdexcept>
#include <map>
#include "loom/util/sha256.h"

namespace loom::archive {
namespace {
Status validate_archive(const RuntimeProfile& profile) {
  const auto& values = profile.values();
  if (values.at("items").at("confidence_min").get<double>() > values.at("items").at("confidence_max").get<double>())
    return Error(Errc::InvalidArgument, "archive confidence_min must not exceed confidence_max");
  if (values.at("pipeline").at("auto_hits_min").get<int>() > values.at("pipeline").at("auto_hits_max").get<int>())
    return Error(Errc::InvalidArgument, "archive auto_hits_min must not exceed auto_hits_max");
  for (const auto& bound : values.at("config_bounds")) {
    if (!bound.at("maximum").is_null() && bound.at("minimum").get<int>() > bound.at("maximum").get<int>())
      return Error(Errc::InvalidArgument, "archive config minimum must not exceed maximum");
  }
  return {};
}
}  // namespace

ArchiveProfile::ArchiveProfile(std::shared_ptr<const kb::Pack> pack, RuntimeProfile profile, bool builtin_pack)
    : pack_(std::move(pack)), profile_(std::move(profile)), builtin_(builtin_pack && profile_.is_builtin()),
      builtin_pack_(builtin_pack) {
  const Json& words = pack_->lexicon("stopwords_base");
  for (auto it = words.begin(); it != words.end(); ++it) {
    if (it.value().is_array()) {
      for (const auto& word : it.value()) if (word.is_string()) stopwords_.insert(word.get<std::string>());
    }
  }
  hash_ = Sha256::hex(json::canonical(Json{{"kb_pack", pack_->hash()}, {"runtime_profile", profile_.hash()}}));
}
Result<ArchiveProfile> ArchiveProfile::builtin() {
  static const Result<ArchiveProfile> cached = []() -> Result<ArchiveProfile> {
    LOOM_TRY_ASSIGN(auto pack, kb::Pack::load_builtin());
    LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("archive"));
    LOOM_TRY(validate_archive(profile));
    return ArchiveProfile(std::move(pack), std::move(profile), true);
  }();
  return cached;
}
Result<ArchiveProfile> ArchiveProfile::load(const std::filesystem::path& root, const Json& overrides) {
  LOOM_TRY_ASSIGN(auto pack, kb::Pack::load_with_overlay(root.empty() ? std::filesystem::path{} : root / "kb"));
  LOOM_TRY_ASSIGN(auto base, kb::Pack::load_builtin());
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("archive", root, overrides));
  LOOM_TRY(validate_archive(profile));
  const bool builtin_pack = pack->hash() == base->hash();
  return ArchiveProfile(std::move(pack), std::move(profile), builtin_pack);
}
Result<ArchiveProfile> ArchiveProfile::with_overrides(const Json& overrides) const {
  LOOM_TRY_ASSIGN(auto profile, profile_.with_overrides(overrides));
  LOOM_TRY(validate_archive(profile));
  return ArchiveProfile(pack_, std::move(profile), builtin_pack_);
}
Result<ArchiveProfile> ArchiveProfile::with_patch(const Json& patch) const {
  LOOM_TRY_ASSIGN(auto profile, profile_.with_patch(patch));
  LOOM_TRY(validate_archive(profile));
  return ArchiveProfile(pack_, std::move(profile), builtin_pack_);
}
Result<Json> ArchiveProfile::provenance() const {
  LOOM_TRY_ASSIGN(auto base_pack, kb::Pack::load_builtin());
  LOOM_TRY_ASSIGN(auto base_profile, RuntimeProfile::builtin("archive"));
  Json kb_overrides = Json::object(), kb_removed = Json::array();
  for (const auto& path : pack_->files()) {
    if (json::canonical(pack_->file(path)) != json::canonical(base_pack->file(path)))
      kb_overrides[path] = pack_->file(path);
  }
  const auto files = pack_->files();
  for (const auto& path : base_pack->files()) {
    if (std::find(files.begin(), files.end(), path) == files.end()) kb_removed.push_back(path);
  }
  return Json{{"schema", "loom.archive_profile_provenance/1"}, {"hash", hash_},
              {"runtime_profile_hash", profile_.hash()}, {"kb_pack_hash", pack_->hash()},
              {"base_runtime_profile_hash", base_profile.hash()}, {"base_kb_pack_hash", base_pack->hash()},
              {"runtime_overlay", Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "archive"},
                                        {"overrides", Json::object()},
                                        {"patch", Json::diff(base_profile.values(), profile_.values())}}},
              {"kb_overrides", kb_overrides}, {"kb_removed", kb_removed}};
}
Result<ArchiveProfile> ArchiveProfile::from_provenance(const Json& snapshot) {
  if (!snapshot.is_object() || json::get_string(snapshot, "schema") != "loom.archive_profile_provenance/1" ||
      !snapshot.contains("runtime_overlay") || !snapshot.contains("kb_overrides") ||
      !snapshot["kb_overrides"].is_object() || !snapshot.contains("kb_removed") || !snapshot["kb_removed"].is_array())
    return Error(Errc::InvalidArgument, "invalid archive profile provenance");
  LOOM_TRY_ASSIGN(auto base_pack, kb::Pack::load_builtin());
  LOOM_TRY_ASSIGN(auto base_profile, RuntimeProfile::builtin("archive"));
  if (base_pack->hash() != json::get_string(snapshot, "base_kb_pack_hash") ||
      base_profile.hash() != json::get_string(snapshot, "base_runtime_profile_hash"))
    return Error(Errc::Conflict, "archive profile provenance requires a different builtin data revision");
  LOOM_TRY_ASSIGN(auto profile, base_profile.with_overlay(snapshot["runtime_overlay"]));
  LOOM_TRY(validate_archive(profile));
  std::map<std::string, Json> docs;
  for (const auto& path : base_pack->files()) docs[path] = base_pack->file(path);
  for (const auto& path : snapshot["kb_removed"]) {
    if (!path.is_string()) return Error(Errc::InvalidArgument, "invalid archive profile removed KB path");
    docs.erase(path.get<std::string>());
  }
  for (auto it = snapshot["kb_overrides"].begin(); it != snapshot["kb_overrides"].end(); ++it) docs[it.key()] = it.value();
  LOOM_TRY_ASSIGN(auto pack, kb::Pack::from_documents(std::move(docs)));
  const bool builtin_pack = pack->hash() == base_pack->hash();
  ArchiveProfile out(std::move(pack), std::move(profile), builtin_pack);
  if (out.hash() != json::get_string(snapshot, "hash"))
    return Error(Errc::Conflict, "archive profile provenance hash mismatch");
  return out;
}
const Json& ArchiveProfile::value(std::string_view pointer) const {
  return profile_.values().at(Json::json_pointer(std::string(pointer)));
}
bool ArchiveProfile::contains(std::string_view pointer, std::string_view word) const {
  for (const auto& x : value(pointer)) if (x.is_string() && x.get_ref<const std::string&>() == word) return true;
  return false;
}
Json ArchiveProfile::inspection() const {
  Json out = profile_.inspection();
  out["hash"] = hash_;
  out["runtime_profile_hash"] = profile_.hash();
  out["kb_pack_hash"] = pack_->hash();
  out["kb_sources"] = Json{{"stopwords", pack_->lexicon("stopwords_base")}, {"items", items()}};
  out["is_builtin"] = is_builtin();
  return out;
}
ProfileScope::ProfileScope(const ArchiveProfile* profile) : profile_(profile) {
  if (!profile_) {
    static const ArchiveProfile cached = [] {
      auto p = ArchiveProfile::builtin();
      if (!p) throw std::runtime_error(p.error().to_string());
      return std::move(*p);
    }();
    profile_ = &cached;
  }
}
}  // namespace loom::archive
