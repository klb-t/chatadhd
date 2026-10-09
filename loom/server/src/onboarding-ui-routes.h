#pragma once

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

#include "loom/onboarding_store.h"
#include "native-ui-common.h"

namespace loom_server {
namespace onboarding_ui_detail {
using native_ui::Json;

inline loom::Error invalid(std::string message) {
  return {loom::Errc::InvalidArgument, std::move(message)};
}

inline loom::Status fields(const Json& body,
                           std::initializer_list<std::string_view> operation_fields) {
  for (const auto& field : body.items()) {
    if (field.key() == "operation" || field.key() == "user_id") continue;
    bool accepted = false;
    for (const auto key : operation_fields) if (field.key() == key) accepted = true;
    if (!accepted) return invalid("unexpected onboarding field: " + field.key());
  }
  return loom::ok_status();
}

inline loom::Result<std::string> required_string(const Json& body, const char* key) {
  const auto item = body.find(key);
  if (item == body.end() || !item->is_string() || item->get_ref<const std::string&>().empty())
    return invalid(std::string(key) + " must be a nonempty string");
  return item->get<std::string>();
}

inline loom::Status required_object(const Json& body, const char* key) {
  const auto item = body.find(key);
  if (item == body.end() || !item->is_object())
    return invalid(std::string(key) + " must be an object");
  return loom::ok_status();
}

// The outer store revision is an int64 CAS token. Never round it through a
// JavaScript number or substitute the independently versioned inner profile.
inline loom::Result<std::int64_t> revision(const Json& body) {
  LOOM_TRY_ASSIGN(auto token, required_string(body, "expected_revision"));
  if (token.size() > 1 && token.front() == '0')
    return invalid("expected_revision must be a canonical nonnegative int64 decimal string");
  for (const auto byte : token) if (byte < '0' || byte > '9')
    return invalid("expected_revision must be a canonical nonnegative int64 decimal string");
  std::int64_t result = 0;
  const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
    return invalid("expected_revision must be a canonical nonnegative int64 decimal string");
  return result;
}

inline loom::Result<Json> envelope(loom::Result<Json> result) {
  if (!result) return result.error();
  const auto item = result->find("revision");
  if (item == result->end() || !item->is_number_integer())
    return loom::Error(loom::Errc::Internal, "native onboarding snapshot has no int64 revision");
  if (item->is_number_unsigned() &&
      item->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return loom::Error(loom::Errc::Internal, "native onboarding snapshot revision exceeds int64");
  const auto token = item->get<std::int64_t>();
  if (token < 0) return loom::Error(loom::Errc::Internal, "native onboarding snapshot revision is negative");
  return Json{{"snapshot", std::move(result).value()}, {"revision", std::to_string(token)}};
}

inline loom::Result<Json> dispatch(LoomContext* ctx, const Json& body) {
  if (!ctx || !ctx->rt) return invalid("ctx is NULL or shut down");
  if (!body.is_object()) return invalid("onboarding request must be an object");
  LOOM_TRY_ASSIGN(auto operation, required_string(body, "operation"));
  LOOM_TRY_ASSIGN(auto user, required_string(body, "user_id"));
  loom::onboarding::OnboardingStore store(ctx->rt->db());

  if (operation == "open") {
    LOOM_TRY(fields(body, {"legacy"}));
    if (body.contains("legacy")) {
      LOOM_TRY(required_object(body, "legacy"));
      return envelope(store.open(user, body.at("legacy")));
    }
    return envelope(store.open(user));
  }
  if (operation == "read") {
    LOOM_TRY(fields(body, {}));
    return envelope(store.read(user));
  }
  if (operation == "apply") {
    LOOM_TRY(fields(body, {"expected_revision", "action"}));
    LOOM_TRY_ASSIGN(auto expected, revision(body));
    LOOM_TRY(required_object(body, "action"));
    return envelope(store.apply(user, expected, body.at("action")));
  }
  if (operation == "update_pack") {
    LOOM_TRY(fields(body, {"expected_revision", "pack", "scenario"}));
    LOOM_TRY_ASSIGN(auto expected, revision(body));
    LOOM_TRY(required_object(body, "pack"));
    LOOM_TRY(required_object(body, "scenario"));
    return envelope(store.update_pack(user, expected, body.at("pack"), body.at("scenario")));
  }
  if (operation == "install_entries") {
    LOOM_TRY(fields(body, {"expected_revision", "extension"}));
    LOOM_TRY_ASSIGN(auto expected, revision(body));
    LOOM_TRY(required_object(body, "extension"));
    return envelope(store.install_entries(user, expected, body.at("extension")));
  }
  if (operation == "model_request") {
    LOOM_TRY(fields(body, {"provider"}));
    LOOM_TRY_ASSIGN(auto provider, required_string(body, "provider"));
    return store.model_request(user, provider);
  }
  if (operation == "policy_decision") {
    LOOM_TRY(fields(body, {"request"}));
    LOOM_TRY(required_object(body, "request"));
    return store.policy_decision(user, body.at("request"));
  }
  return invalid("unknown onboarding operation: " + operation);
}
}  // namespace onboarding_ui_detail

// Explicit private-runtime bridge for the owner UI. Store validation, privacy,
// pack resolution, graph transactions and concurrency remain in the kernel.
// model_request prepares data only; this route never completes provider calls.
inline void register_onboarding_ui_routes(httplib::Server& server, LoomContext* ctx) {
  server.Post("/api/onboarding", [ctx](const httplib::Request& req, httplib::Response& res) {
    native_ui::protect(res, [&] {
      auto body = native_ui::parse(req);
      if (!body) { native_ui::fail(res, body.error()); return; }
      auto result = onboarding_ui_detail::dispatch(ctx, *body);
      if (!result) { native_ui::fail(res, result.error()); return; }
      native_ui::reply(res, *result);
    });
  });
}
}  // namespace loom_server
