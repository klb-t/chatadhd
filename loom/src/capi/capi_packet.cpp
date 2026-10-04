#include <chrono>

#include "context.h"
#include "packet/packet.h"
#if __has_include("loom/usage_policy.h")
#include "loom/usage_policy.h"
#endif

using namespace loom;
using namespace loom::capi;

extern "C" LOOM_API const char* loom_packet(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_packet", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!request_json) return out_error(Errc::InvalidArgument, "packet request required");
    Json request;
    try {
      request = packet::parse_strict(request_json);
    } catch (const std::invalid_argument& e) {
      if (std::string_view(e.what()) == "graph_packet_native_integer_representation_unavailable")
        return out_error(Errc::Unavailable, e.what());
      return out_error(Errc::Parse, "packet request: invalid strict JSON");
    } catch (const std::exception&) {
      return out_error(Errc::Parse, "packet request: invalid strict JSON");
    }
    // Thread 2 supplies the durable policy implementation. Preserve the exact
    // request binding in its estimate: reusing an operation ID for changed
    // packet data must conflict even when the declared quantities are equal.
    if (request.contains("usage_estimate")) {
#if __has_include("loom/usage_policy.h")
      auto options = effective_usage_policy_options(ctx->rt->config());
      if (!options) return out_error(options.error());
      auto policy = UsagePolicy::open(ctx->rt->paths().root / "usage-policy.sqlite", *options);
      if (!policy) return out_error(policy.error());
      Json estimate = request["usage_estimate"];
      if (!estimate.is_object()) return out_error(Errc::InvalidArgument, "usage_estimate must be an object");
      Json bound = request;
      bound.erase("usage_estimate");
      estimate["packet_request_sha256"] = packet::digest(bound);
      auto decision = (*policy)->request(estimate);
      if (!decision) return out_error(decision.error());
      if (!json::get_bool(*decision, "authorized"))
        return out(Json{{"usage_decision", *decision}, {"executed", false}});
      const auto started = std::chrono::steady_clock::now();
      auto result = packet::execute(bound);
      if (!result || result->contains("error")) {
        auto cancelled =
            (*policy)->cancel(json::get_string(estimate, "operation_id"), "packet operation failed");
        Json value = result ? *result : error_json(result.error());
        value["usage_decision"] = *decision;
        value["usage_settlement"] = cancelled ? *cancelled : error_json(cancelled.error());
        return out(value);
      }
      // Only actual quantities we measured are reported. Other resource names
      // remain unknown/reserved, as required by the shared policy contract.
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      Json measured = Json::object();
      const Json available{{"input_bytes", std::strlen(request_json)},
                           {"output_bytes", json::dump(*result).size()},
                           {"wall_seconds", elapsed},
                           {"calls", 1},
                           {"money_usd", 0}};
      if (estimate.contains("resources") && estimate["resources"].is_object())
        for (auto it = estimate["resources"].begin(); it != estimate["resources"].end(); ++it)
          measured[it.key()] = available.contains(it.key()) ? available[it.key()] : Json(nullptr);
      auto settled =
          (*policy)->complete(json::get_string(estimate, "operation_id"),
                              Json{{"resources", measured}, {"provenance", "instrument_measured"}});
      return out(Json{{"result", *result},
                      {"usage_decision", *decision},
                      {"usage_settlement", settled ? *settled : error_json(settled.error())},
                      {"executed", true}});
#else
      return out_error(Errc::Unavailable, "usage policy dependency (thread 2) is not integrated");
#endif
    }
    return out_result(packet::execute(request));
  });
}
