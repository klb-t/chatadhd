// loom/effective_view.h — the effective view: ROM + N memory overlays composed into one list per kind of thing (P24, d).
//
// A "kind of thing" is any list the program keeps as data: edge kinds, node kinds, perspectives, scope levels, unit kinds, and kinds a user
// invents. Each has a ROM layer (shipped, read-only: a map {id: entry} or an array of entries with a key, found by a pointer into a builtin
// runtime profile; see view_kinds in data/runtime/graph_views.pack) and any number of memory overlays: <data_dir>/views/*.json, composed in
// (order, name) order, later layers last. An overlay is a list of operations on entries, nothing else; the ROM is never modified:
//
//   add      {view, op:"add", id, entry}        a new entry (an existing id is replaced, kept or reported: policy add_existing)
//   modify   {view, op:"modify", id, patch}     a field patch (policy modify: deep = objects merge and null removes a field | shallow | replace)
//   hide     {view, op:"hide", id}              the entry leaves the effective list (it stays in `hidden`, with who hid it)
//   disable  {view, op:"disable", id, restore?} the entry stays listed with disabled=true; readers skip it. restore:true undoes a disable
//   alias    {view, op:"alias", id, as}         `as` names the same entry; find("as") returns it
//
// Every effective entry carries its provenance: origin ("rom" | "overlay:<name>") and the list of layers that touched it, each {layer, op,
// fields?}. The merge policy per kind is data (view_kinds.<kind>.policy), and view_kinds is itself a kind: a user adds a new kind of thing with
// an `add` on "view_kinds" and fills it with operations. Readers (perspectives, scope resolver, edge-kind registry) read the effective view.
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::views {

inline constexpr const char* kOverlaySchema = "loom.view_overlay/1";
inline constexpr const char* kOverlayDir = "views";

struct Overlay {
  std::string name;    // file stem, or the layer name given by the caller
  std::string source;  // file path or "" for an in-memory overlay
  double order = 0;
  Json ops = Json::array();
};

struct Policy {
  std::string modify = "deep";          // deep | shallow | replace
  std::string add_existing = "replace";  // replace | keep | issue
  static Policy from_json(const Json& policy);
};

struct EffectiveEntry {
  std::string id;
  Json value = Json::object();
  std::string origin = "rom";
  Json provenance = Json::array();  // [{layer, op, fields?}]
  bool disabled = false;
  std::vector<std::string> aliases;
  std::size_t order = 0;
  Json to_json() const;
};

struct Effective {
  std::string kind;
  Policy policy;
  std::vector<EffectiveEntry> entries;  // hidden ones are not in it
  Json hidden = Json::array();          // [{id, by, provenance}]
  std::map<std::string, std::string> aliases;  // alias -> id
  Json issues = Json::array();          // [{code, layer, op, id, ...}]
  Json layers = Json::array();          // [{name, source, ops}] in composition order, "rom" first

  // An entry by id or alias; hidden and unknown ids give nullptr. Disabled entries are found (check .disabled).
  const EffectiveEntry* find(std::string_view id_or_alias) const;
  // Entries that readers use: not disabled.
  std::vector<const EffectiveEntry*> active() const;
  // {kind, policy, layers, entries:[{id, entry, origin, provenance, disabled?, aliases?}], hidden?, issues}
  Json to_json(bool include_hidden = true) const;
};

// Pure composition. `rom` is an object {id: entry} (the ROM layer); overlays are applied in the order given.
Effective compose(std::string_view kind, const Json& rom, const std::vector<Overlay>& overlays, const Policy& policy = {});

// <dir>/views/*.json of every directory, sorted by (order, name). A malformed file is an error naming the file (never skipped silently).
Result<std::vector<Overlay>> load_overlays(const std::vector<std::filesystem::path>& data_dirs);

// The ROM layer of a kind: the view_kinds entry's source {profile, pointer, key?} read from the builtin runtime profile (never the user's
// overlay of that profile). An array source is keyed by `key`. Empty source = an empty ROM layer.
Result<Json> rom_layer(const Json& view_kind_entry);

// The effective list of `kind` for a data directory (ROM + <root>/views/*.json). `extra_dirs`: further memories, composed after root.
// A kind that view_kinds does not know is still composed (emergent): empty ROM layer, default policy.
Result<Effective> effective(const std::filesystem::path& root, std::string_view kind, const std::vector<std::filesystem::path>& extra_dirs = {});

// Writing (the executors behind loom_view_write): put an operation into the overlay <root>/views/<overlay>.json, replacing an earlier operation
// of the same (view, op, id[, as]); validates the operation. remove_ops takes back every operation of (view, id) from that overlay (the undo).
Status put_op(const std::filesystem::path& root, std::string_view overlay, const Json& op);
Result<std::size_t> remove_ops(const std::filesystem::path& root, std::string_view overlay, std::string_view view, std::string_view id);

}  // namespace loom::views
