// Local harness transport only: all composition is the existing native resolver.
#include <iostream>
#include <string>
#include "loom/onboarding_layers.h"
#include "loom/util/json.h"

using loom::Json;
loom::Result<Json> execute(const Json& input) {
  if (!input.is_object() || !input.contains("pack"))
    return loom::Error(loom::Errc::InvalidArgument, "native layer request needs pack");
  LOOM_TRY_ASSIGN(auto resolver, loom::onboarding::DefaultLayers::create(
      input.at("pack"), input.value("state", Json::object())));
  const auto actions = input.value("actions", Json::array());
  if (!actions.is_array()) return loom::Error(loom::Errc::InvalidArgument, "actions must be an array");
  for (const auto& action : actions) {
    LOOM_TRY_ASSIGN(auto state, resolver.dispatch(action));
    LOOM_TRY_ASSIGN(resolver, loom::onboarding::DefaultLayers::create(resolver.pack(), state));
  }
  Json keys = input.value("keys", Json::array());
  if (!keys.is_array()) return loom::Error(loom::Errc::InvalidArgument, "keys must be an array");
  if (!input.contains("keys")) for (const auto& entry : resolver.pack().at("entries")) keys.push_back(entry.at("key"));
  Json rows = Json::array();
  for (const auto& key : keys) {
    if (!key.is_string()) return loom::Error(loom::Errc::InvalidArgument, "key must be a string");
    LOOM_TRY_ASSIGN(auto row, resolver.resolve(key.get<std::string>(), input.value("locale", std::string{})));
    rows.push_back(std::move(row));
  }
  return Json{{"schema", "loom.graph_perspective_native_resolution/1"},
    {"resolver", "loom::onboarding::DefaultLayers"},
    {"layers", resolver.snapshot()}, {"effectiveDefaults", rows}};
}
int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    Json output;
    try {
      auto request = loom::json::parse(line);
      auto result = request ? execute(*request) : loom::Result<Json>(request.error());
      if (result) output = *result;
      else output = Json{{"error", {{"code", loom::errc_name(result.error().code)}, {"message", result.error().message}}}};
    } catch (const std::exception& error) {
      output = Json{{"error", {{"code", "invalid_argument"}, {"message", error.what()}}}};
    }
    std::cout << loom::json::dump(output) << '\n';
  }
}
