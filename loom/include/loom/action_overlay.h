// loom/action_overlay.h — the user's own rules and actions, written into the user overlays (R54, R55; GRAF_HUB_KOD_SILNIK §7, §9).
//
// The catalogue of actions and the rules grow from what people say in conversations. Nothing here edits a shipped data pack:
// every write goes to <data_dir>/profiles/<domain>.pack, the overlay the profile loader already merges over the shipped
// data (objects merge by key, arrays replace). An entry is addressed by its path of keys under `overrides`; it can be taken
// out again, which is the undo of everything below.
//
//   rules.set      write a scoped rule or value (set_value | confirm | require_confirmation | deny | allow ...) at
//                  conversation, person, installation or any other level of the scope chain, into profiles/action_rules.pack;
//   rules.disable  switch a rule off (a shipped rule by an override, an own rule in place); a locked rule refuses with its reason;
//   rules.list     the rules in force, each with where it comes from;
//   actions.define write a new action (a composite with use_model steps), its phrases and the proposal it emerged from;
//   actions.remove take an own action back out (its phrases, its entry), the provenance node stays as history;
//   groups.define  write a named bundle of traits (a group) into profiles/graph_entry.pack;
//   groups.remove  take it out.
//
// Writing is itself an action of the catalogue: it goes through the rules of the scope (a shipped preset rule asks the user to
// confirm every write) and the router proposes it like any other command. These functions are the executors behind the
// catalogue entries, the CLI (`loom rules ...`), loom_invoke("actions", "rules.set"|...) and the HTTP routes.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Database;
}

namespace loom::actions {

class OverlayStore {
 public:
  explicit OverlayStore(std::filesystem::path data_dir) : root_(std::move(data_dir)) {}

  // overrides.<path...> = value. The result is validated against the profile's schema before it is written; an overlay that would
  // not load is never left on disk.
  Status put(std::string_view domain, const std::vector<std::string>& path, const Json& value) const;
  // Takes overrides.<path...> out (and the parents it leaves empty). false = there was nothing to remove.
  Result<bool> remove(std::string_view domain, const std::vector<std::string>& path) const;
  // The value under overrides.<path...> (null when absent).
  Result<Json> entry(std::string_view domain, const std::vector<std::string>& path) const;
  std::filesystem::path file(std::string_view domain) const;

 private:
  Result<Json> document(std::string_view domain) const;
  Status write(std::string_view domain, const Json& document) const;
  std::filesystem::path root_;
};

// Requests (JSON objects) and results are described at the catalogue entries `rules.set` ... in runtime/actions.pack.
// `dry_run: true` validates and says whether the write would be refused, and writes nothing.
Result<Json> rules_set(const std::filesystem::path& root, const Json& request);
Result<Json> rules_disable(const std::filesystem::path& root, const Json& request);
Result<Json> rules_list(const std::filesystem::path& root, const Json& request);
// What rules.set / rules.disable would do, without writing: {"ok", "rule"?, "pin"?: {rule, scope, reason?, initiator?}, "note"?}.
// The router asks before it proposes, so a refusal (a pinned rule) is said with its reason instead of being offered.
Result<Json> rules_preview(const std::filesystem::path& root, std::string_view action, const Json& request);

// A new action out of a conversation (R55). {id?, name?, instruction, requires?, phrases: {lang: [..]}, lang?, provenance?}.
Result<Json> actions_define(const std::filesystem::path& root, const Json& request);
Result<Json> actions_remove(const std::filesystem::path& root, const Json& request);
Result<Json> groups_define(const std::filesystem::path& root, const Json& request);
Result<Json> groups_remove(const std::filesystem::path& root, const Json& request);

}  // namespace loom::actions
