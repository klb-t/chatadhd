// What a plan would cost before it runs (see loom/action_estimate.h). Data: runtime profile action_run, `estimate`.
#include "loom/action_estimate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

#include "loom/config.h"
#include "loom/providers.h"
#include "loom/util/utf8.h"

namespace loom::actions {

namespace {

struct Band {
  double low = 0, high = 0;
};

Json band_json(const Band& b) { return Json{{"low", static_cast<std::int64_t>(std::floor(b.low))}, {"high", static_cast<std::int64_t>(std::ceil(b.high))}}; }

double rounded(double v) { return std::round(v * 1e8) / 1e8; }

// A price as the registry keeps it: a number, or a numeric string (OpenRouter sends strings).
std::optional<double> price_of(const Json& pricing, const std::string& key) {
  const Json* v = json::find(pricing, key);
  if (!v) return std::nullopt;
  double out = 0;
  if (v->is_number()) {
    out = v->get<double>();
  } else if (v->is_string()) {
    try {
      std::size_t used = 0;
      const std::string s = v->get<std::string>();
      out = std::stod(s, &used);
      if (used != s.size()) return std::nullopt;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  } else {
    return std::nullopt;
  }
  if (!std::isfinite(out) || out < 0) return std::nullopt;
  return out;
}

std::string stringified(const Json& v) { return v.is_string() ? v.get<std::string>() : json::dump(v); }

// An upper bound on the characters of a text that may hold placeholders ($arg, $localized, $format).
std::int64_t text_chars(const Json& node, const Json& args) {
  if (node.is_string()) return static_cast<std::int64_t>(utf8::length(node.get<std::string>()));
  if (!node.is_object()) return 0;
  if (const Json* a = json::find(node, "$arg"); a && a->is_string()) {
    if (const Json* given = json::find(args, a->get<std::string>()); given && !given->is_null())
      return static_cast<std::int64_t>(utf8::length(stringified(*given)));
    const Json* d = json::find(node, "default");
    return d ? text_chars(*d, args) : 0;
  }
  if (const Json* l = json::find(node, "$localized"); l && l->is_object()) {
    std::int64_t most = 0;
    for (auto it = l->begin(); it != l->end(); ++it) most = std::max(most, text_chars(it.value(), args));
    return most;
  }
  if (const Json* f = json::find(node, "$format")) {
    std::int64_t total = text_chars(*f, args);
    if (const Json* vars = json::find(node, "vars"); vars && vars->is_object())
      for (auto it = vars->begin(); it != vars->end(); ++it) total += text_chars(it.value(), args);
    return total;
  }
  return 0;
}

// The model a step names: a plain string, an argument ($arg), the first of `models`, else the default model.
std::string model_of(const Json& with, const Json& args, const std::string& fallback) {
  auto resolve = [&](const Json& v) -> std::string {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object())
      if (const Json* a = json::find(v, "$arg"); a && a->is_string()) {
        if (const Json* given = json::find(args, a->get<std::string>()); given && given->is_string()) return given->get<std::string>();
        if (const Json* d = json::find(v, "default"); d && d->is_string()) return d->get<std::string>();
      }
    return std::string();
  };
  if (const Json* list = json::find(with, "models"); list && list->is_array() && !list->empty()) {
    const Json& first = (*list)[0];
    if (first.is_object()) {
      if (const std::string m = resolve(first.value("model", Json(nullptr))); !m.empty()) return m;
    } else if (const std::string m = resolve(first); !m.empty()) {
      return m;
    }
  }
  if (const Json* m = json::find(with, "model"))
    if (const std::string name = resolve(*m); !name.empty()) return name;
  return fallback;
}

bool wants_text_only(const Json& with) {
  const Json* wants = json::find(with, "wants");
  if (!wants || !wants->is_array()) return true;
  for (const auto& w : *wants)
    if (w.is_string() && w.get<std::string>() != "text" && w.get<std::string>() != "json") return false;
  return true;
}

// A `when` that can be told before the run: {"arg": name}. Anything else may hold, so the step is counted.
bool skipped_before_run(const Json& when, const Json& args) {
  if (!when.is_object()) return false;
  if (const Json* a = json::find(when, "arg"); a && a->is_string() && !json::find(when, "equals")) {
    const Json* given = json::find(args, a->get<std::string>());
    return !given || given->is_null();
  }
  return false;
}

}  // namespace

Json estimate_plan(const EstimateServices& s, const Json& plan, const Json& args, std::int64_t input_chars) {
  const Json est = s.settings.value("estimate", Json::object());
  const Json cpt = est.value("chars_per_token", Json{{"low", 4.0}, {"high", 3.0}});
  const double cpt_low = std::max(0.01, json::get_number(cpt, "low", 4.0));    // fewer tokens
  const double cpt_high = std::max(0.01, json::get_number(cpt, "high", 3.0));  // more tokens
  const Json pricing_cfg = est.value("pricing", Json{{"input_key", "prompt"}, {"output_key", "completion"}, {"tokens_per_unit", 1}, {"currency", "USD"}});
  const std::string currency = json::get_string(pricing_cfg, "currency", "USD");
  const double per_unit = std::max(1.0, json::get_number(pricing_cfg, "tokens_per_unit", 1.0));
  const double floor_tokens = json::get_number(est, "output_floor_tokens", 32);
  const Json unknown_ratio = est.value("unknown_ratio", Json{{"low", 0.1}, {"high", 1.0}});
  const Json ratios = est.value("output_ratio", Json::object());
  const std::string default_model = [&] {
    const std::string key = json::get_string(s.settings.value("use_model", Json::object()), "model_setting", "default_model");
    if (!s.config) return std::string();
    const Json v = s.config->get(key);
    return v.is_string() ? v.get<std::string>() : std::string();
  }();
  const double config_cap = [&] {
    const std::string key = json::get_string(est, "max_tokens_setting", "max_tokens");
    if (!s.config || key.empty()) return -1.0;
    const Json v = s.config->get(key);
    return v.is_number() ? v.get<double>() : -1.0;
  }();

  Json calls = Json::array();
  Band tokens_in, tokens_out;
  Band usd;
  bool money_known = true;
  std::set<std::string> unpriced;
  std::string reason;
  std::int64_t model_calls = 0;
  Band previous_out{-1, -1};  // output of the previous model step, in tokens (-1: none yet)

  std::map<std::string, std::string> action_of_item;
  for (const auto& item : plan.value("items", Json::array())) action_of_item[item.value("id", "")] = item.value("action", "");

  for (const auto& step : plan.value("steps", Json::array())) {
    if (json::get_string(step, "action") != "use_model") continue;
    const Json with = step.value("with", Json::object());
    if (skipped_before_run(step.value("when", Json(nullptr)), args)) {
      calls.push_back(Json{{"step", step.value("id", "")}, {"action", "use_model"}, {"skipped", true}, {"why", "its condition is not met by the arguments"}});
      continue;
    }
    // The input of the step: the content of the element, or the output of an earlier model step.
    bool from_input = true;
    if (const Json* inputs = json::find(step, "inputs"); inputs && inputs->is_array() && !inputs->empty())
      from_input = json::get_string((*inputs)[0], "from") == "input";
    else if (inputs && inputs->is_array() && inputs->empty())
      from_input = false;  // takes no content
    Band content;
    if (from_input) {
      content = Band{static_cast<double>(input_chars) / cpt_low, static_cast<double>(input_chars) / cpt_high};
    } else if (previous_out.low >= 0) {
      content = previous_out;
    }
    const Json variants = step.value("variants", Json::array());
    const std::size_t count = variants.is_array() && !variants.empty() ? variants.size() : 1;
    const std::string action_id = action_of_item.count(step.value("item", "")) ? action_of_item[step.value("item", "")] : std::string();
    const Json* entry = action_id.empty() ? nullptr : json::find(s.catalogue, action_id);
    const std::string content_op = entry ? json::get_string(*entry, "content_op", "none") : std::string("none");
    Band step_out;
    for (std::size_t v = 0; v < count; ++v) {
      Json merged_with = with;
      if (variants.is_array() && !variants.empty() && variants[v].is_object())
        if (const Json* vw = json::find(variants[v], "with"); vw && vw->is_object())
          for (auto it = vw->begin(); it != vw->end(); ++it) merged_with[it.key()] = it.value();
      ++model_calls;
      const std::string model = model_of(merged_with, args, default_model);
      const bool text = wants_text_only(merged_with);
      Json call{{"step", step.value("id", "")}, {"action", "use_model"}, {"model", model.empty() ? Json(nullptr) : Json(model)}, {"kind", text ? "text" : "media"}};
      if (!text) {
        // The prompt is tokens; the picture is priced by the provider in its own way.
        const double prompt_chars = static_cast<double>(text_chars(merged_with.value("prompt", Json(nullptr)), args) + text_chars(merged_with.value("system", Json(nullptr)), args));
        Band in{content.low + prompt_chars / cpt_low, content.high + prompt_chars / cpt_high};
        tokens_in.low += in.low;
        tokens_in.high += in.high;
        call["tokens"] = Json{{"input", band_json(in)}, {"output", nullptr}};
        call["price"] = Json{{"known", false}, {"why", "media output is not priced per token"}};
        call["usd"] = nullptr;
        money_known = false;
        reason = "the output is not text and its price is not listed per token";
        if (!model.empty()) unpriced.insert(model);
        calls.push_back(std::move(call));
        continue;
      }
      const double prompt_chars = static_cast<double>(text_chars(merged_with.value("system", Json(nullptr)), args) + text_chars(merged_with.value("prompt", Json(nullptr)), args));
      Band in{content.low + prompt_chars / cpt_low, content.high + prompt_chars / cpt_high};
      Band out;
      if (const Json* hint = json::find(step, "estimate"); hint && hint->is_object() && json::find(*hint, "output_tokens")) {
        const Json h = hint->at("output_tokens");
        out = Band{json::get_number(h, "low", floor_tokens), json::get_number(h, "high", floor_tokens)};
      } else {
        const Json* r = json::find(ratios, content_op);
        const Json ratio = r ? *r : unknown_ratio;
        out = Band{std::max(floor_tokens, content.low * json::get_number(ratio, "low", 0.1)), std::max(floor_tokens, content.high * json::get_number(ratio, "high", 1.0))};
      }
      double cap = config_cap;
      if (const Json* p = json::find(merged_with, "parameters"); p && p->is_object())
        if (const Json* m = json::find(*p, "max_tokens"); m && m->is_number()) cap = m->get<double>();
      if (cap > 0) out.high = std::min(out.high, cap);
      out.low = std::min(out.low, out.high);
      tokens_in.low += in.low;
      tokens_in.high += in.high;
      tokens_out.low += out.low;
      tokens_out.high += out.high;
      step_out.low = std::max(step_out.low, out.low);
      step_out.high = std::max(step_out.high, out.high);
      call["tokens"] = Json{{"input", band_json(in)}, {"output", band_json(out)}};
      // Money, where the registry knows the model's price.
      std::optional<double> pin, pout;
      if (s.models && !model.empty()) {
        const Json pricing = s.models->get_pricing(model);
        pin = price_of(pricing, json::get_string(pricing_cfg, "input_key", "prompt"));
        pout = price_of(pricing, json::get_string(pricing_cfg, "output_key", "completion"));
      }
      if (pin && pout) {
        const Band cost{(in.low * *pin + out.low * *pout) / per_unit, (in.high * *pin + out.high * *pout) / per_unit};
        usd.low += cost.low;
        usd.high += cost.high;
        call["price"] = Json{{"known", true}, {"input", *pin}, {"output", *pout}, {"tokens_per_unit", per_unit}};
        call["usd"] = Json{{"low", rounded(cost.low)}, {"high", rounded(cost.high)}};
      } else {
        money_known = false;
        if (!model.empty()) unpriced.insert(model);
        reason = model.empty() ? "no model is named for this step" : "the registry lists no price for the model";
        call["price"] = Json{{"known", false}, {"why", reason}};
        call["usd"] = nullptr;
      }
      calls.push_back(std::move(call));
    }
    previous_out = step_out;
  }

  Json money;
  if (model_calls == 0) {
    money = Json{{"known", true}, {"currency", currency}, {"low", 0.0}, {"high", 0.0}, {"note", "no model is called"}};
  } else if (money_known) {
    money = Json{{"known", true}, {"currency", currency}, {"low", rounded(usd.low)}, {"high", rounded(usd.high)}};
  } else {
    // Unknown stays unknown: nothing here is a zero.
    money = Json{{"known", false}, {"currency", currency}, {"reason", reason}, {"models", Json(std::vector<std::string>(unpriced.begin(), unpriced.end()))}};
  }
  Json out;
  out["schema"] = std::string(kEstimateSchema);
  out["input_chars"] = input_chars;
  out["model_calls"] = model_calls;
  out["calls"] = calls;
  out["tokens"] = Json{{"input", band_json(tokens_in)}, {"output", band_json(tokens_out)}};
  out["money"] = money;
  out["assumptions"] = Json{{"chars_per_token", cpt},
                            {"output_floor_tokens", floor_tokens},
                            {"completion_limit", config_cap > 0 ? Json(config_cap) : Json(nullptr)},
                            {"price_source", "the model registry (models.json); not verified current"},
                            {"note", "an estimate from the size of the input: not a limit and not a promise; the usage guard and the rules are separate"}};
  return out;
}

}  // namespace loom::actions
