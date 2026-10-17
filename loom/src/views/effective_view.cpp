// The effective view: ROM + memory overlays -> one list per kind of thing. See include/loom/effective_view.h.
#include "loom/effective_view.h"

#include <atomic>

#include <algorithm>

#include "loom/log.h"
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

namespace {
// A source {profile, pointer, key?} read out of a loaded profile as {id: entry}; an array is keyed by `key` (default "id").
Result<Json> source_entries(const Json& src, const RuntimeProfile& prof) {
  const std::string profile = json::get_string(src, "profile");
  const std::string pointer = json::get_string(src, "pointer");
  // Walked member by member (json::find_pointer): a pointer that names nothing, or is malformed, is a NotFound with the pointer.
  const Json* at = json::find_pointer(prof.values(), pointer);
  if (!at) return Error(Errc::NotFound, "view source " + profile + pointer + ": the profile has nothing at this pointer");
  if (at->is_object()) return *at;
  if (!at->is_array()) return Error(Errc::InvalidArgument, "view source " + profile + pointer + " is neither an object nor an array");
  const std::string key = json::get_string(src, "key", "id");
  Json out = Json::object();
  for (const auto& item : *at) {
    const std::string id = json::get_string(item, key);
    if (id.empty()) return Error(Errc::InvalidArgument, "view source " + profile + pointer + ": an item has no `" + key + "`");
    out[id] = item;
  }
  return out;
}

// The patch that turns `from` into `to` under a modify policy: replace = the whole entry; shallow = the changed top-level fields (a
// removed one null); deep (any other policy, as compose reads it) = the same, recursing into objects both sides have. A non-object
// side gives the whole `to` (compose reports a modify whose patch is not an object as an issue).
Json patch_between(const Json& from, const Json& to, const std::string& modify) {
  if (modify == "replace" || !from.is_object() || !to.is_object()) return to;
  const bool deep = modify != "shallow";
  Json patch = Json::object();
  for (auto it = from.begin(); it != from.end(); ++it)
    if (!to.contains(it.key())) patch[it.key()] = nullptr;
  for (auto it = to.begin(); it != to.end(); ++it) {
    const Json* before = json::find(from, it.key());
    if (before && *before == it.value()) continue;
    if (deep && before && before->is_object() && it.value().is_object()) patch[it.key()] = patch_between(*before, it.value(), modify);
    else patch[it.key()] = it.value();
  }
  return patch;
}
}  // namespace

Result<Json> rom_layer(const Json& view_kind_entry) {
  const Json* src = json::find(view_kind_entry, "source");
  if (!src || !src->is_object() || src->empty()) return Json::object();
  const std::string profile = json::get_string(*src, "profile");
  if (profile.empty()) return Json::object();
  LOOM_TRY_ASSIGN(auto prof, RuntimeProfile::builtin(profile));
  return source_entries(*src, prof);
}

Result<Overlay> profile_overlay(std::string_view kind, const Json& source, const Json& rom, const std::filesystem::path& data_dir,
                                const Policy& policy) {
  Overlay ov;
  const std::string profile = json::get_string(source, "profile");
  ov.name = "profile:" + profile;
  if (data_dir.empty() || profile.empty()) return ov;
  ov.source = (data_dir / "profiles" / (profile + ".pack")).string();
  LOOM_TRY_ASSIGN(auto prof, RuntimeProfile::load(profile, data_dir));
  LOOM_TRY_ASSIGN(auto mine, source_entries(source, prof));
  const std::string view(kind);
  if (rom.is_object())
    for (auto it = rom.begin(); it != rom.end(); ++it)
      if (!mine.contains(it.key())) ov.ops.push_back(Json{{"view", view}, {"op", "hide"}, {"id", it.key()}});
  for (auto it = mine.begin(); it != mine.end(); ++it) {
    const Json* before = rom.is_object() ? json::find(rom, it.key()) : nullptr;
    if (!before) ov.ops.push_back(Json{{"view", view}, {"op", "add"}, {"id", it.key()}, {"entry", it.value()}});
    else if (*before != it.value())
      ov.ops.push_back(Json{{"view", view}, {"op", "modify"}, {"id", it.key()}, {"patch", patch_between(*before, it.value(), policy.modify)}});
  }
  return ov;
}

namespace {
std::atomic<std::uint64_t> g_effective_loads{0};
}  // namespace

std::uint64_t effective_loads() { return g_effective_loads.load(); }

Result<Effective> effective(const std::filesystem::path& root, std::string_view kind, const std::vector<std::filesystem::path>& extra_dirs) {
  g_effective_loads.fetch_add(1);
  std::vector<std::filesystem::path> dirs{root};
  dirs.insert(dirs.end(), extra_dirs.begin(), extra_dirs.end());
  LOOM_TRY_ASSIGN(auto overlays, load_overlays(dirs));
  Json issues = Json::array();
  // The user's profile overlay of a source is its own layer, right after the ROM; one that does not load leaves the shipped entries
  // and is an issue (never an error of the whole view, never silent).
  auto layers_for = [&](std::string_view k, const Json& source, const Json& rom, const Policy& pol) {
    std::vector<Overlay> layers;
    auto po = profile_overlay(k, source, rom, root, pol);
    if (!po) {
      const std::string layer = "overlay:profile:" + json::get_string(source, "profile");
      log::debug("loom.views", "{}: {} not applied: {}", k, layer, po.error().message);
      bool known = false;
      for (const auto& i : issues) known = known || json::get_string(i, "layer") == layer;
      if (!known)
        issues.push_back(Json{{"code", "profile_overlay_unreadable"}, {"layer", layer}, {"op", ""}, {"id", ""},
                              {"note", "the user's profile overlay is not applied (the shipped entries are shown): " + po.error().message}});
    } else if (!po->ops.empty()) {
      layers.push_back(std::move(*po));
    }
    layers.insert(layers.end(), overlays.begin(), overlays.end());
    return layers;
  };
  auto with_issues = [&](Effective e) {
    Json all = issues;
    for (auto& i : e.issues) all.push_back(std::move(i));
    e.issues = std::move(all);
    return e;
  };
  // view_kinds is a kind itself: ROM from the builtin profile, then the user's graph_views overlay, default policy; its effective
  // entries say how every other kind is read.
  LOOM_TRY_ASSIGN(auto vk_profile, RuntimeProfile::builtin("graph_views"));
  const Json vk_rom = vk_profile.values().value("view_kinds", Json::object());
  const Json vk_source{{"profile", "graph_views"}, {"pointer", "/view_kinds"}};
  const std::vector<Overlay> vk_layers = layers_for("view_kinds", vk_source, vk_rom, Policy{});
  if (kind == "view_kinds") return with_issues(compose(kind, vk_rom, vk_layers, Policy{}));
  const Effective vk = compose("view_kinds", vk_rom, vk_layers, Policy{});
  Json rom = Json::object();
  Policy policy;
  std::vector<Overlay> layers = overlays;
  if (const EffectiveEntry* def = vk.find(kind); def && !def->disabled) {
    LOOM_TRY_ASSIGN(rom, rom_layer(def->value));
    policy = Policy::from_json(def->value.value("policy", Json::object()));
    if (const Json* src = json::find(def->value, "source"); src && src->is_object() && !json::get_string(*src, "profile").empty())
      layers = layers_for(kind, *src, rom, policy);
  }
  return with_issues(compose(kind, rom, layers, policy));
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
