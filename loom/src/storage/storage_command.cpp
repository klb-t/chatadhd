#include "loom/storage_command.h"

#include "loom/runtime.h"
#include "loom/storage_backend.h"

namespace loom::storage {

bool is_storage_action(std::string_view action) noexcept { return action.rfind("storage.", 0) == 0; }

Result<Json> run_command(Runtime& rt, const Json& req) {
  const std::string action = json::get_string(req, "action");
  StorageService& svc = rt.storage();
  if (action == "storage.status") return svc.describe();
  if (action == "storage.probe") return svc.probe_all();
  if (action == "storage.kinds") return svc.describe()["kinds"];
  if (action == "storage.placement") {
    const std::string cat = json::get_string(req, "category");
    Json d = svc.describe();
    if (cat.empty()) return Json{{"categories", d["categories"]}};
    for (const auto& c : d["categories"]) if (c["id"] == cat) return c;
    return Error(Errc::NotFound, "unknown category '" + cat + "'");
  }
  if (action == "storage.locate") {
    const std::string h = json::get_string(req, "hash");
    if (h.empty()) return Error(Errc::InvalidArgument, "missing hash");
    return Json{{"hash", h}, {"locations", svc.locate(h)}};
  }
  if (action == "storage.copy" || action == "storage.move") {
    StorageService::MoveOptions o;
    o.category = json::get_string(req, "category");
    o.to = json::get_string(req, "to");
    if (o.category.empty() || o.to.empty()) return Error(Errc::InvalidArgument, "category and to are required");
    o.move = action == "storage.move";
    o.delete_source = json::get_bool(req, "delete_source", false);
    o.dry_run = json::get_bool(req, "dry_run", false);
    return svc.transfer(o);
  }
  if (action == "storage.backup") {
    const std::string to = json::get_string(req, "to");
    if (to.empty()) return Error(Errc::InvalidArgument, "to is required");
    const std::string what = json::get_string(req, "what", "database");
    if (what != "database") return Error(Errc::Unsupported, "backup supports what=database");
    return svc.backup_database(to);
  }
  return Error(Errc::InvalidArgument, "unknown storage action '" + action + "'");
}

}  // namespace loom::storage
