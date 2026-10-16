// The effective view: ROM + memory overlays -> one list per kind of thing. See include/loom/effective_view.h.
#include "loom/effective_view.h"

#include <algorithm>

#include "loom/runtime_profile.h"
#include "loom/util/fs.h"

namespace loom::views {
namespace {


void deep_patch(Json& base, const Json& patch, Json& fields, const std::string& prefix) {
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    const std::string path = prefix.empty() ? it.key() : prefix + "." + it.key();
    if (it.value().is_null()) {
      base.erase(it.key());
      fields.push_back(path);
    } else if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object()) {
      deep_patch(base[it.key()], it.value(), fields, path);
    } else {
      base[it.key()] = it.value();
      fields.push_back(path);
    }
  }
}

Json provenance_step(const std::string& layer, const std::string& op, const Json& fields = Json()) {
  Json s{{"layer", layer}, {"op", op}};
  if (fields.is_array() && !fields.empty()) s["fields"] = fields;
  return s;
}

}  // namespace

Policy Policy::from_json(const Json& policy) {
  Policy p;
  p.modify = json::get_string(policy, "modify", p.modify);
  p.add_existing = json::get_string(policy, "add_existing", p.add_existing);
  return p;
}

Json EffectiveEntry::to_json() const {
  Json o{{"id", id}, {"entry", value}, {"origin", origin}, {"provenance", provenance}};
  if (disabled) o["disabled"] = true;
  if (!aliases.empty()) o["aliases"] = aliases;
  return o;
}

const EffectiveEntry* Effective::find(std::string_view id_or_alias) const {
  std::string id(id_or_alias);
  if (auto a = aliases.find(id); a != aliases.end()) id = a->second;
  for (const auto& e : entries)
    if (e.id == id) return &e;
  return nullptr;
}

std::vector<const EffectiveEntry*> Effective::active() const {
  std::vector<const EffectiveEntry*> out;
  for (const auto& e : entries)
    if (!e.disabled) out.push_back(&e);
  return out;
}

Json Effective::to_json(bool include_hidden) const {
  Json list = Json::array();
  for (const auto& e : entries) list.push_back(e.to_json());
  Json o{{"schema", "loom.effective_view/1"}, {"kind", kind},
         {"policy", Json{{"modify", policy.modify}, {"add_existing", policy.add_existing}}},
         {"layers", layers}, {"entries", list}, {"issues", issues}};
  if (include_hidden) o["hidden"] = hidden;
  return o;
}

Effective compose(std::string_view kind, const Json& rom, const std::vector<Overlay>& overlays, const Policy& policy) {
  Effective out;
  out.kind = std::string(kind);
  out.policy = policy;
  out.layers.push_back(Json{{"name", "rom"}, {"source", "rom"}, {"ops", rom.is_object() ? rom.size() : 0}});
  std::size_t order = 0;
  if (rom.is_object()) {
    for (auto it = rom.begin(); it != rom.end(); ++it) {
      EffectiveEntry e;
      e.id = it.key();
      e.value = it.value();
      e.provenance.push_back(provenance_step("rom", "rom"));
      e.order = order++;
      out.entries.push_back(std::move(e));
    }
  }
  auto issue = [&](const std::string& code, const Overlay& ov, const Json& op, const std::string& note) {
    out.issues.push_back(Json{{"code", code}, {"layer", ov.name}, {"op", json::get_string(op, "op")}, {"id", json::get_string(op, "id")}, {"note", note}});
  };
  auto locate = [&](const std::string& id_or_alias) -> EffectiveEntry* {
    std::string id = id_or_alias;
    if (auto a = out.aliases.find(id); a != out.aliases.end()) id = a->second;
    for (auto& e : out.entries)
      if (e.id == id) return &e;
    return nullptr;
  };
  for (const auto& ov : overlays) {
    std::size_t used = 0;
    for (const auto& op : ov.ops) {
      if (!op.is_object() || json::get_string(op, "view") != out.kind) continue;
      ++used;
      const std::string verb = json::get_string(op, "op");
      const std::string id = json::get_string(op, "id");
      const std::string layer = "overlay:" + ov.name;
      if (id.empty()) {
        issue("op_without_id", ov, op, "an operation needs an id");
        continue;
      }
      if (verb == "add") {
        const Json* entry = json::find(op, "entry");
        if (!entry || !entry->is_object()) {
          issue("add_without_entry", ov, op, "add needs an entry object");
          continue;
        }
        EffectiveEntry* have = locate(id);
        if (have) {
          if (out.policy.add_existing == "keep") {
            issue("add_existing_kept", ov, op, "the id exists; policy add_existing=keep left it as it was");
            continue;
          }
          if (out.policy.add_existing == "issue") {
            issue("add_existing", ov, op, "the id exists; policy add_existing=issue refuses to replace it");
            continue;
          }
          have->value = *entry;
          have->origin = layer;
          have->disabled = false;
          have->provenance.push_back(provenance_step(layer, "add", Json::array({"*"})));
          continue;
        }
        // a hidden id comes back when an overlay adds it again
        for (std::size_t i = 0; i < out.hidden.size(); ++i)
          if (out.hidden[i].value("id", "") == id) { out.hidden.erase(i); break; }
        EffectiveEntry e;
        e.id = id;
        e.value = *entry;
        e.origin = layer;
        e.provenance.push_back(provenance_step(layer, "add"));
        e.order = order++;
        out.entries.push_back(std::move(e));
      } else if (verb == "modify") {
        EffectiveEntry* e = locate(id);
        const Json* patch = json::find(op, "patch");
        if (!e) {
          issue("modify_unknown", ov, op, "no such entry (hidden, never added or misspelled)");
          continue;
        }
        if (!patch || !patch->is_object()) {
          issue("modify_without_patch", ov, op, "modify needs a patch object");
          continue;
        }
        Json fields = Json::array();
        if (out.policy.modify == "replace") {
          e->value = *patch;
          fields.push_back("*");
        } else if (out.policy.modify == "shallow") {
          for (auto it = patch->begin(); it != patch->end(); ++it) {
            if (it.value().is_null()) e->value.erase(it.key());
            else e->value[it.key()] = it.value();
            fields.push_back(it.key());
          }
        } else {
          deep_patch(e->value, *patch, fields, "");
        }
        e->provenance.push_back(provenance_step(layer, "modify", fields));
      } else if (verb == "hide") {
        std::string real = id;
        if (auto a = out.aliases.find(id); a != out.aliases.end()) real = a->second;
        auto it = std::find_if(out.entries.begin(), out.entries.end(), [&](const EffectiveEntry& e) { return e.id == real; });
        if (it == out.entries.end()) {
          issue("hide_unknown", ov, op, "no such entry");
          continue;
        }
        Json prov = it->provenance;
        prov.push_back(provenance_step(layer, "hide"));
        out.hidden.push_back(Json{{"id", it->id}, {"by", layer}, {"provenance", prov}});
        for (auto a = out.aliases.begin(); a != out.aliases.end();) a = a->second == real ? out.aliases.erase(a) : std::next(a);
        out.entries.erase(it);
      } else if (verb == "disable") {
        EffectiveEntry* e = locate(id);
        if (!e) {
          issue("disable_unknown", ov, op, "no such entry");
          continue;
        }
        const bool restore = json::get_bool(op, "restore", false);
        e->disabled = !restore;
        e->provenance.push_back(provenance_step(layer, restore ? "enable" : "disable"));
      } else if (verb == "alias") {
        const std::string as = json::get_string(op, "as");
        EffectiveEntry* e = locate(id);
        if (!e || as.empty()) {
          issue(!e ? "alias_unknown" : "alias_without_name", ov, op, !e ? "no such entry" : "alias needs `as`");
          continue;
        }
        if (as == e->id) continue;
        if (locate(as) && locate(as)->id != e->id) {
          issue("alias_taken", ov, op, "`as` is the id of another entry");
          continue;
        }
        out.aliases[as] = e->id;
        if (std::find(e->aliases.begin(), e->aliases.end(), as) == e->aliases.end()) e->aliases.push_back(as);
        e->provenance.push_back(provenance_step(layer, "alias", Json::array({as})));
      } else {
        issue("unknown_op", ov, op, "operations are add, modify, hide, disable, alias");
      }
    }
    out.layers.push_back(Json{{"name", ov.name}, {"source", ov.source}, {"ops", used}});
  }
  return out;
}

Result<std::vector<Overlay>> load_overlays(const std::vector<std::filesystem::path>& data_dirs) {
  std::vector<Overlay> all;
  for (const auto& dir : data_dirs) {
    const auto views_dir = dir / kOverlayDir;
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(views_dir, ec)) continue;
    std::vector<std::filesystem::path> files;
    for (const auto& de : std::filesystem::directory_iterator(views_dir, ec))
      if (de.is_regular_file() && de.path().extension() == ".json") files.push_back(de.path());
    if (ec) return Error(Errc::Io, "cannot list " + views_dir.string() + ": " + ec.message());
    std::sort(files.begin(), files.end());
    std::vector<Overlay> mine;
    for (const auto& f : files) {
      LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(f));
      auto doc = json::parse(bytes);
      if (!doc) return Error(Errc::Parse, "view overlay " + f.string() + ": " + doc.error().message);
      if (!doc->is_object() || json::get_string(*doc, "schema") != kOverlaySchema)
        return Error(Errc::Parse, "view overlay " + f.string() + ": schema must be " + kOverlaySchema);
      Overlay o;
      o.name = json::get_string(*doc, "name", f.stem().string());
      o.source = f.string();
      o.order = json::get_number(*doc, "order", 0);
      const Json* ops = json::find(*doc, "ops");
      if (ops && ops->is_array()) o.ops = *ops;
      mine.push_back(std::move(o));
    }
    std::stable_sort(mine.begin(), mine.end(), [](const Overlay& a, const Overlay& b) { return a.order < b.order; });
    for (auto& o : mine) all.push_back(std::move(o));
  }
  return all;
}

Result<Json> rom_layer(const Json& view_kind_entry) { return rom_layer(view_kind_entry, {}); }

Result<Json> rom_layer(const Json& view_kind_entry, const std::filesystem::path& data_dir) {
  const Json* src = json::find(view_kind_entry, "source");
  if (!src || !src->is_object() || src->empty()) return Json::object();
  const std::string profile = json::get_string(*src, "profile");
  if (profile.empty()) return Json::object();
  LOOM_TRY_ASSIGN(auto prof, data_dir.empty() ? RuntimeProfile::builtin(profile) : RuntimeProfile::load(profile, data_dir));
  const std::string pointer = json::get_string(*src, "pointer");
  const Json* at = pointer.empty() ? &prof.values() : nullptr;
  Json holder;
  if (!at) {
    try {
      holder = prof.values().at(Json::json_pointer(pointer));
      at = &holder;
    } catch (const std::exception& e) {
      return Error(Errc::NotFound, "view source " + profile + pointer + ": " + e.what());
    }
  }
  if (at->is_object()) return *at;
  if (!at->is_array()) return Error(Errc::InvalidArgument, "view source " + profile + pointer + " is neither an object nor an array");
  const std::string key = json::get_string(*src, "key", "id");
  Json out = Json::object();
  for (const auto& item : *at) {
    const std::string id = json::get_string(item, key);
    if (id.empty()) return Error(Errc::InvalidArgument, "view source " + profile + pointer + ": an item has no `" + key + "`");
    out[id] = item;
  }
  return out;
}

Result<Effective> effective(const std::filesystem::path& root, std::string_view kind, const std::vector<std::filesystem::path>& extra_dirs) {
  std::vector<std::filesystem::path> dirs{root};
  dirs.insert(dirs.end(), extra_dirs.begin(), extra_dirs.end());
  LOOM_TRY_ASSIGN(auto overlays, load_overlays(dirs));
  // view_kinds is a kind itself: ROM from the profile (with the user's profile overlay, like every source), default policy; its
  // effective entries say how every other kind is read.
  LOOM_TRY_ASSIGN(auto vk_profile, root.empty() ? RuntimeProfile::builtin("graph_views") : RuntimeProfile::load("graph_views", root));
  const Json vk_rom = vk_profile.values().value("view_kinds", Json::object());
  if (kind == "view_kinds") return compose(kind, vk_rom, overlays, Policy{});
  const Effective vk = compose("view_kinds", vk_rom, overlays, Policy{});
  Json rom = Json::object();
  Policy policy;
  if (const EffectiveEntry* def = vk.find(kind); def && !def->disabled) {
    LOOM_TRY_ASSIGN(rom, rom_layer(def->value, root));
    policy = Policy::from_json(def->value.value("policy", Json::object()));
  }
  return compose(kind, rom, overlays, policy);
}

Status put_op(const std::filesystem::path& root, std::string_view overlay, const Json& op) {
  if (overlay.empty() || overlay.find_first_of("/\\") != std::string_view::npos || overlay.front() == '.')
    return Error(Errc::InvalidArgument, "overlay name must be a plain file stem");
  const std::string verb = json::get_string(op, "op");
  if (json::get_string(op, "view").empty() || json::get_string(op, "id").empty())
    return Error(Errc::InvalidArgument, "an operation needs view and id");
  if (verb != "add" && verb != "modify" && verb != "hide" && verb != "disable" && verb != "alias")
    return Error(Errc::InvalidArgument, "op must be add, modify, hide, disable or alias");
  if (verb == "add" && !(op.contains("entry") && op["entry"].is_object())) return Error(Errc::InvalidArgument, "add needs an entry object");
  if (verb == "modify" && !(op.contains("patch") && op["patch"].is_object())) return Error(Errc::InvalidArgument, "modify needs a patch object");
  if (verb == "alias" && json::get_string(op, "as").empty()) return Error(Errc::InvalidArgument, "alias needs `as`");
  const auto file = root / kOverlayDir / (std::string(overlay) + ".json");
  Json doc = Json{{"schema", kOverlaySchema}, {"name", std::string(overlay)}, {"ops", Json::array()}};
  std::error_code ec;
  if (std::filesystem::exists(file, ec)) {
    LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(file));
    auto parsed = json::parse(bytes);
    if (!parsed) return Error(Errc::Parse, "view overlay " + file.string() + ": " + parsed.error().message);
    doc = *parsed;
    if (!doc.contains("ops") || !doc["ops"].is_array()) doc["ops"] = Json::array();
  }
  Json ops = Json::array();
  for (const auto& o : doc["ops"]) {
    const bool same = json::get_string(o, "view") == json::get_string(op, "view") && json::get_string(o, "id") == json::get_string(op, "id") &&
                      json::get_string(o, "op") == verb && json::get_string(o, "as") == json::get_string(op, "as");
    if (!same) ops.push_back(o);
  }
  ops.push_back(op);
  doc["ops"] = ops;
  LOOM_TRY(fsutil::ensure_dir(root / kOverlayDir));
  return fsutil::atomic_write(file, json::dump(doc, 2) + "\n");
}

Result<std::size_t> remove_ops(const std::filesystem::path& root, std::string_view overlay, std::string_view view, std::string_view id) {
  const auto file = root / kOverlayDir / (std::string(overlay) + ".json");
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return std::size_t{0};
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(file));
  auto parsed = json::parse(bytes);
  if (!parsed) return Error(Errc::Parse, "view overlay " + file.string() + ": " + parsed.error().message);
  Json doc = *parsed;
  Json kept = Json::array();
  std::size_t removed = 0;
  for (const auto& o : doc.value("ops", Json::array())) {
    if (json::get_string(o, "view") == view && json::get_string(o, "id") == id) ++removed;
    else kept.push_back(o);
  }
  if (removed == 0) return removed;
  doc["ops"] = kept;
  LOOM_TRY(fsutil::atomic_write(file, json::dump(doc, 2) + "\n"));
  return removed;
}

}  // namespace loom::views
