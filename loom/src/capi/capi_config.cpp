// Usage/configuration API. Execution callers are owned by lanes 3, 4 and 5.
#include "context.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/economy.h"
#include "loom/runtime.h"
#include "loom/semantic_worker.h"
#include "loom/usage_policy.h"

using namespace loom;
using namespace loom::capi;

extern "C" const char* loom_usage_policy_json(LoomContext* ctx, const char* command_json) {
  return guard_json("loom_usage_policy_json", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto parsed = parse_arg(command_json, Json(nullptr));
    if (!parsed) return out_error(parsed.error());
    if (!parsed->is_object()) return out_error(Errc::InvalidArgument, "policy command must be an object");
    const Json& command = *parsed;
    const auto action = json::get_string(command, "action");
    auto ledger_path = ctx->rt->paths().root / "usage-policy.sqlite";
    if (action == "economy_report") {
      // Read-only resource-economy report (R50); optional {"overrides": {...}} replaces presets for this call.
      auto report = economy::report_for_data_dir(ctx->rt->paths().root, ctx->rt->paths().db, command);
      return report ? out(*report) : out_error(report.error());
    }
    if (action == "settings" || action == "preview_settings") {
      const Json* proposed_override = nullptr;
      if (action == "preview_settings") {
        if (!command.contains("override")) return out_error(missing("override"));
        proposed_override = &command["override"];
      }
      auto settings = usage_policy_settings(ctx->rt->config(), proposed_override);
      if (!settings) return out_error(settings.error());
      (*settings)["ledger_path"] = ledger_path.string();
      (*settings)["capabilities"] = Json{{"schema", "loom.usage_policy/1"}, {"resource_names", "open"},
          {"number_representation", "finite_binary64"}, {"growth_requires_confirmation", true},
          {"scope", "explicit_callers"}, {"preset_application", "next_policy_open"}};
      return out(*settings);
    }
    if (action == "semantic_confirm") {
      if (!command.contains("approved") || !command["approved"].is_boolean())
        return out_error(Errc::InvalidArgument, "approved must be boolean");
      auto answered = ctx->rt->worker().confirm_usage(command["approved"].get<bool>(),
          json::get_string(command, "receipt_id"), json::get_string(command, "confirmation_ref"));
      return answered ? out(*answered) : out_error(answered.error());
    }
    if (action == "semantic_requeue") {
      Json filter = command.contains("filter") ? command["filter"] : Json::object();
      auto result = ctx->rt->worker().requeue(filter);
      return result ? out(*result) : out_error(result.error());
    }
    if (action == "semantic_retry_mode") {
      auto set = SemanticWorker::persist_retry_mode(ctx->rt->config(), json::get_string(command, "mode"));
      if (set) ctx->rt->worker().wake();
      return set ? out(ctx->rt->worker().status().to_json()) : out_error(set.error());
    }
    if (action == "chat_preview") {
      const std::string message = json::get_string(command, "message");
      if (message.empty()) return out_error(missing("message"));
      Json chat_request = command;
      chat_request.erase("action");
      auto opts = ChatOptions::from_json(chat_request);
      if (!opts) return out_error(opts.error());
      auto preview = ctx->rt->chat().preview_usage(message, *opts);
      return preview ? out(*preview) : out_error(preview.error());
    }
    if (action != "preview" && action != "request" && action != "confirm" && action != "complete" &&
        action != "cancel" && action != "inspect")
      return out_error(Errc::InvalidArgument, "unknown usage policy action");
    auto options = effective_usage_policy_options(ctx->rt->config());
    if (!options) return out_error(options.error());
    auto policy = UsagePolicy::open(ledger_path, *options);
    if (!policy) return out_error(policy.error());
    Result<Json> result = Error(Errc::InvalidArgument, "invalid usage command");
    if (action == "preview" || action == "request") {
      if (!command.contains("estimate")) return out_error(missing("estimate"));
      result = action == "preview" ? (*policy)->preview(command["estimate"]) : (*policy)->request(command["estimate"]);
    } else if (action == "confirm") {
      if (!command.contains("approved") || !command["approved"].is_boolean())
        return out_error(Errc::InvalidArgument, "approved must be boolean");
      result = (*policy)->confirm(json::get_string(command, "operation_id"), json::get_string(command, "receipt_id"),
                                  command["approved"].get<bool>(), json::get_string(command, "confirmation_ref"));
    } else if (action == "complete") {
      if (!command.contains("actual")) return out_error(missing("actual"));
      result = (*policy)->complete(json::get_string(command, "operation_id"), command["actual"]);
    } else if (action == "cancel") {
      result = (*policy)->cancel(json::get_string(command, "operation_id"), json::get_string(command, "reason"));
    } else {
      result = (*policy)->inspect(json::get_string(command, "baseline_key"));
    }
    return result ? out(*result) : out_error(result.error());
  });
}
