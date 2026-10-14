// ROM: read-only memory over the builtin catalogue. See include/loom/rom.h.
#include "loom/rom.h"

#include <algorithm>

#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

namespace loom::rom {

namespace {

std::string bare_id(std::string_view address) {
  if (address.substr(0, kAddressPrefix.size()) == kAddressPrefix) address.remove_prefix(kAddressPrefix.size());
  return std::string(address);
}

Error refusal(std::string_view what, std::string_view address) {
  return Error(Errc::Unsupported, "rom_read_only: cannot " + std::string(what) + " " + std::string(address) +
                                      "; the ROM ships with the program and is never written. Override it with a copy in a "
                                      "writable memory (graph_entry overlay, overrides.rom.items.<id>)");
}

void deep_merge(Json& base, const Json& changes) {
  if (!base.is_object() || !changes.is_object()) { base = changes; return; }
  for (auto it = changes.begin(); it != changes.end(); ++it) {
    if (base.contains(it.key())) deep_merge(base[it.key()], it.value());
    else base[it.key()] = it.value();
  }
}

std::filesystem::path overlay_path(const std::filesystem::path& dir) { return dir / "profiles" / "graph_entry.pack"; }

Result<Json> read_overlay(const std::filesystem::path& dir) {
  Json fresh = Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "graph_entry"}, {"overrides", Json::object()}};
  std::error_code ec;
  if (dir.empty() || !std::filesystem::exists(overlay_path(dir), ec)) return fresh;
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(overlay_path(dir)));
  return json::parse(bytes);
}

}  // namespace

Result<Rom> Rom::open() {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("graph_entry"));
  const Json* items = json::find(profile.values(), "rom");
  items = items ? json::find(*items, "items") : nullptr;
  if (!items || !items->is_object() || items->empty())
    return Error(Errc::Unavailable, "rom catalogue absent from the builtin graph_entry profile");
  Rom out;
  out.items_ = *items;
  return out;
}

std::vector<std::string> Rom::ids() const {
  std::vector<std::string> out;
  for (auto it = items_.begin(); it != items_.end(); ++it) out.push_back(it.key());
  std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
    return json::get_int(items_[a], "number") < json::get_int(items_[b], "number");
  });
  return out;
}

Result<Json> Rom::read(std::string_view address) const {
  const std::string id = bare_id(address);
  const Json* item = json::find(items_, id);
  if (!item) return Error(Errc::NotFound, "no ROM item " + std::string(address));
  return *item;
}

Json Rom::status_counts() const {
  Json out = Json::object();
  for (auto it = items_.begin(); it != items_.end(); ++it) {
    const std::string s = json::get_string(it.value(), "status", "unknown");
    out[s] = out.contains(s) ? out[s].get<std::int64_t>() + 1 : 1;
  }
  return out;
}

Json Rom::with_hashes(const std::filesystem::path& repo_root) const {
  Json out = items_;
  for (auto& item : out.items()) {
    auto files = item.value().find("files");
    if (files == item.value().end() || !files->is_array()) continue;
    for (auto& f : *files) {
      auto h = sha256_file_hex(repo_root / json::get_string(f, "path"));
      if (h) f["sha256"] = *h;
      else { f["sha256"] = nullptr; f["unreadable"] = true; }
    }
  }
  return out;
}

Status Rom::write(std::string_view address, const Json&) const { return refusal("write", address); }
Status Rom::remove(std::string_view address) const { return refusal("remove", address); }

MountRegistry MountRegistry::standard(const std::filesystem::path& data_dir) {
  MountRegistry r;
  r.mounts_.push_back(Mount{std::string(kMountName), false, {}});
  r.mounts_.push_back(Mount{"user", true, data_dir});
  return r;
}

Status MountRegistry::add(Mount mount) {
  if (mount.name.empty()) return Error(Errc::InvalidArgument, "mount name required");
  for (const Mount& m : mounts_)
    if (m.name == mount.name)
      return Error(Errc::AlreadyExists, mount.name == kMountName ? "mount name \"rom\" is reserved for the read-only ROM"
                                                                  : "mount already present: " + mount.name);
  mounts_.push_back(std::move(mount));
  return {};
}

Result<Json> MountRegistry::read(std::string_view address) const {
  LOOM_TRY_ASSIGN(auto rom, Rom::open());
  LOOM_TRY_ASSIGN(auto base, rom.read(address));
  Json item = base;
  bool overlaid = false;
  const std::string id = bare_id(address);
  for (const Mount& m : mounts_) {
    if (!m.writable) continue;
    LOOM_TRY_ASSIGN(auto doc, read_overlay(m.data_dir));
    const Json* over = json::find(doc, "overrides");
    over = over ? json::find(*over, "rom") : nullptr;
    over = over ? json::find(*over, "items") : nullptr;
    over = over ? json::find(*over, id) : nullptr;
    if (over) { deep_merge(item, *over); overlaid = true; }
  }
  return Json{{"address", std::string(kAddressPrefix) + id}, {"source", overlaid ? "overlay" : "rom"},
              {"item", std::move(item)}, {"rom_item", std::move(base)}};
}

Status MountRegistry::write(std::string_view mount, std::string_view address, const Json& patch) const {
  auto it = std::find_if(mounts_.begin(), mounts_.end(), [&](const Mount& m) { return m.name == mount; });
  if (it == mounts_.end()) return Error(Errc::NotFound, "no such mount: " + std::string(mount));
  if (!it->writable) return refusal("write", address);
  if (!patch.is_object()) return Error(Errc::InvalidArgument, "overlay patch must be an object");
  LOOM_TRY_ASSIGN(auto rom, Rom::open());
  LOOM_TRY(rom.read(address));
  LOOM_TRY_ASSIGN(auto doc, read_overlay(it->data_dir));
  Json& overrides = doc["overrides"];
  if (!overrides.is_object()) overrides = Json::object();
  deep_merge(overrides["rom"]["items"][bare_id(address)], patch);
  // The same validation the loader applies, before anything touches the disk.
  LOOM_TRY_ASSIGN(auto base, RuntimeProfile::builtin("graph_entry"));
  LOOM_TRY(base.with_overlay(doc));
  return fsutil::atomic_write(overlay_path(it->data_dir), json::dump(doc, 2) + "\n");
}

Status MountRegistry::remove(std::string_view mount, std::string_view address) const {
  auto it = std::find_if(mounts_.begin(), mounts_.end(), [&](const Mount& m) { return m.name == mount; });
  if (it == mounts_.end()) return Error(Errc::NotFound, "no such mount: " + std::string(mount));
  if (!it->writable) return refusal("remove", address);
  LOOM_TRY_ASSIGN(auto doc, read_overlay(it->data_dir));
  Json* items = nullptr;
  if (doc.contains("overrides") && doc["overrides"].contains("rom") && doc["overrides"]["rom"].contains("items"))
    items = &doc["overrides"]["rom"]["items"];
  if (!items || !items->contains(bare_id(address))) return {};
  items->erase(bare_id(address));
  return fsutil::atomic_write(overlay_path(it->data_dir), json::dump(doc, 2) + "\n");
}

}  // namespace loom::rom
