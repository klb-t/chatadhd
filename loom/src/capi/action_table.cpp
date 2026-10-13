#include "loom/action_table.h"

namespace loom {
namespace {

Json row(const char* port, const char* action, const char* verb, const char* path, const char* summary,
         Json input, Json query = Json::array()) {
  return Json{{"port", port}, {"action", action}, {"verb", verb}, {"path", path},
              {"summary", summary}, {"input", std::move(input)}, {"query", std::move(query)}};
}

Json build() {
  Json t = Json::array();
  t.push_back(row("storage", "status", "GET", "/api/storage/status", "Backends, placement, effective state", Json::object()));
  t.push_back(row("storage", "probe", "GET", "/api/storage/probe", "Probe every configured backend", Json::object()));
  t.push_back(row("storage", "kinds", "GET", "/api/storage/kinds", "Available backend kinds", Json::object()));
  t.push_back(row("storage", "placement", "GET", "/api/storage/placement", "Placement of one or all categories",
                  Json{{"category", "optional string"}}, Json::array({Json{{"name", "category"}, {"always", false}}})));
  t.push_back(row("storage", "locate", "GET", "/api/storage/locate", "Where the graph says an object is held",
                  Json{{"hash", "string (required)"}}, Json::array({Json{{"name", "hash"}, {"always", true}}})));
  t.push_back(row("storage", "copy", "POST", "/api/storage/copy", "Copy a category to another backend",
                  Json{{"category", "string"}, {"to", "string"}, {"dry_run", "bool?"}}));
  t.push_back(row("storage", "move", "POST", "/api/storage/move", "Move a category to another backend",
                  Json{{"category", "string"}, {"to", "string"}, {"delete_source", "bool?"}, {"dry_run", "bool?"}}));
  t.push_back(row("storage", "backup", "POST", "/api/storage/backup", "Back up the database to a backend",
                  Json{{"to", "string"}, {"what", "\"database\"?"}}));
  return t;
}

}  // namespace

const Json& action_table() {
  static const Json t = build();
  return t;
}

}  // namespace loom
