// The semantic analyzer as a producer of the annotation layer: one prompt contract (entities, topics, relations and segments), one writer.
#include <algorithm>
#include <map>
#include <mutex>

#include "loom/annotation_layer.h"
#include "loom/config.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/util/utf8.h"

namespace loom::annotations {
namespace {

constexpr const char* kLog = "loom.annotations";

std::int64_t mtime_of(const std::filesystem::path& p) {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(p, ec);
  return ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) s.replace(pos, from.size(), to);
  return s;
}

bool in_list(const Json& list, const std::string& v) {
  if (!list.is_array()) return false;
  for (const auto& x : list)
    if (x.is_string() && x.get<std::string>() == v) return true;
  return false;
}

}  // namespace

Result<Policy> Policy::load_cached(const std::filesystem::path& root) {
  static std::mutex mu;
  static std::map<std::string, std::pair<std::int64_t, Policy>> cache;
  const std::int64_t stamp = mtime_of(root / "profiles" / "guardian.pack");
  {
    std::lock_guard lk(mu);
    auto it = cache.find(root.string());
    if (it != cache.end() && it->second.first == stamp) return it->second.second;
  }
  LOOM_TRY_ASSIGN(auto fresh, Policy::load(root));
  std::lock_guard lk(mu);
  cache[root.string()] = {stamp, fresh};
  return fresh;
}

SegmentsPlan segments_plan(const Config& cfg) {
  SegmentsPlan plan;
  auto pol = Policy::load_cached(cfg.path().parent_path());
  if (!pol) return plan;
  const Json an = pol->p.value("analyzer", Json::object());
  if (!an.is_object() || an.empty()) return plan;
  const Json presets = an.value("presets", Json::object());
  std::string name = json::get_string(an, "active", "auto");
  const Json chosen = cfg.get(json::get_string(an, "setting", "semantic_segments"));
  if (chosen.is_string() && !chosen.get<std::string>().empty()) name = presets.contains(chosen.get<std::string>()) ? chosen.get<std::string>() : json::get_string(an, "unknown", "auto");
  plan.preset = name;
  const Json preset = presets.value(name, Json::object());
  if (json::get_bool(preset, "always", false)) plan.wanted = true;
  else if (json::get_bool(preset, "never", false)) plan.wanted = false;
  else if (preset.contains("when") && preset["when"].is_object()) {
    for (auto it = preset["when"].begin(); it != preset["when"].end(); ++it) {
      const Json v = cfg.get(it.key());
      if (v.is_string() && in_list(it.value(), v.get<std::string>())) plan.wanted = true;
    }
  }
  plan.categories = an.value("categories", Json::object());
  plan.max_tokens = static_cast<int>(json::get_int(an, "max_tokens", 1200));
  plan.version = method_version(an.value("prompt", Json::object()).dump() + plan.categories.dump());
  return plan;
}

std::string extend_prompt(std::string_view base, const SegmentsPlan& plan, const Config& cfg) {
  if (!plan.wanted) return std::string(base);
  auto pol = Policy::load_cached(cfg.path().parent_path());
  if (!pol) return std::string(base);
  const std::string addendum = json::get_string(pol->p.value("analyzer", Json::object()).value("prompt", Json::object()), "addendum");
  if (addendum.empty()) return std::string(base);
  std::string names, help;
  for (auto it = plan.categories.begin(); it != plan.categories.end(); ++it) {
    names += (names.empty() ? "" : "|") + it.key();
    help += "- " + it.key() + ": " + json::get_string(it.value(), "en", it.key()) + "\n";
  }
  std::string text = replace_all(replace_all(addendum, "{{categories}}", names), "{{category_help}}", help);
  const std::size_t pos = base.rfind("\nMessage:\n");
  if (pos == std::string_view::npos) return std::string(base);
  return std::string(base.substr(0, pos)) + text + std::string(base.substr(pos));
}

bool analyzer_expected(const Config& cfg, const std::string& text) {
  const SegmentsPlan plan = segments_plan(cfg);
  if (!plan.wanted) return false;
  const Json active = cfg.get("semantic_analysis");
  if (active.is_boolean() && !active.get<bool>()) return false;
  const Json model = cfg.get("semantic_model");
  if (!model.is_string() || model.get<std::string>().empty()) return false;
  auto pol = Policy::load_cached(cfg.path().parent_path());
  const std::int64_t min_len = pol ? json::get_int(pol->p.value("analyzer", Json::object()), "min_text_codepoints", 20) : 20;
  return static_cast<std::int64_t>(utf8::length(text)) >= min_len;
}

Result<int> write_analysis(Database& db, const Config* cfg, const std::filesystem::path& root, const std::string& message_id, const Json& analysis) {
  if (!analysis.is_object()) return 0;
  const bool has_segments = analysis.contains("segments") && analysis["segments"].is_array();
  const bool has_thread = analysis.contains("thread") && analysis["thread"].is_string();
  const bool llm_source = json::get_string(analysis, "source") == "llm";
  // A regex analysis carries no segments; its thread (the dominant topic) is written only when somebody reads threads.
  bool regex_thread = false;
  if (!has_segments && !has_thread && cfg && analysis.contains("topics")) regex_thread = segments_plan(*cfg).wanted;
  if (!has_segments && !has_thread && !regex_thread) return 0;
  LOOM_TRY_ASSIGN(auto pol, Policy::load_cached(root));
  const Json an = pol.p.value("analyzer", Json::object());
  if (!an.is_object() || an.empty()) return 0;
  LOOM_TRY_ASSIGN(auto msg, db.get_msg(message_id));
  if (!msg) return 0;
  const bool model_made = has_segments || has_thread;
  const std::string producer = !model_made ? "semantic_regex" : (llm_source ? "semantic_llm" : "semantic_batch");
  const std::string method = !model_made ? "regex" : (llm_source ? "llm" : "batch");
  const Json receipt = analysis.value("annotation", Json::object());
  const std::string version = model_made ? json::get_string(receipt, "version", method_version(method)) : method_version("regex:" + json::get_string(analysis, "analyzer_profile_hash"));
  Json cost = receipt.value("cost", Json::object());
  const Json categories = an.value("categories", Json::object());
  const Json qualifiers = pol.p.value("qualifiers", Json::object());
  const std::int64_t max_segments = json::get_int(an, "max_segments", 12);
  int written = 0;
  std::int64_t seen = 0;
  Json asked = Json::array();
  for (auto it = categories.begin(); it != categories.end(); ++it) asked.push_back(it.key());
  if (has_segments) {
    for (const auto& seg : analysis["segments"]) {
      if (!seg.is_object() || ++seen > max_segments) continue;
      const std::string category = json::get_string(seg, "category"), quote = json::get_string(seg, "quote");
      if (quote.empty() || !categories.contains(category)) continue;
      const std::size_t at = msg->text.find(quote);
      if (at == std::string::npos) continue;  // a quote that is not in the message is not a segment of it
      const auto start = static_cast<std::int64_t>(utf8::length(std::string_view(msg->text).substr(0, at)));
      Claim c;
      c.message_id = message_id;
      c.category = category;
      c.start_char = start;
      c.end_char = start + static_cast<std::int64_t>(utf8::length(quote));
      c.producer = producer;
      c.method = method;
      c.method_version = version;
      c.confidence = json::get_number(seg, "confidence", 0.7);
      c.origin = "model";
      const std::string q = json::get_string(seg, "qualifier");
      if (!q.empty() && qualifiers.contains(q)) c.qualifier = q;
      c.cost = cost;  // the receipt of the one analysis call; its calls are counted on the first annotation only
      if (written > 0 && c.cost.is_object() && c.cost.contains("calls")) c.cost["calls"] = 0;
      auto w = write(db, pol, c);
      if (!w) {
        log::write(log::Level::Debug, kLog, "segment not written: " + w.error().message);
        continue;
      }
      if (w->outcome == Outcome::Created || w->outcome == Outcome::Replaced) ++written;
    }
  }
  const Json tcfg = an.value("thread", Json::object());
  if (json::get_bool(tcfg, "write", true)) {
    std::string label;
    double conf = 0.7;
    if (has_thread) label = json::get_string(analysis, "thread");
    else if (analysis.contains("topics") && analysis["topics"].is_array()) {
      for (const auto& t : analysis["topics"]) {
        const double tc = t.is_object() ? json::get_number(t, "confidence", 0.5) : 0.5;
        const std::string l = t.is_object() ? json::get_string(t, "label") : (t.is_string() ? t.get<std::string>() : std::string());
        if (!l.empty() && tc >= json::get_number(tcfg, "min_confidence", 0.3) && (label.empty() || tc > conf)) {
          label = l;
          conf = tc;
        }
      }
    }
    if (!label.empty()) {
      Claim c;
      c.message_id = message_id;
      c.category = json::get_string(tcfg, "category", "thread");
      c.start_char = 0;
      c.end_char = -1;
      c.producer = producer;
      c.method = method;
      c.method_version = version;
      c.confidence = conf;
      c.origin = model_made ? "model" : "recorded";
      c.cost = written > 0 ? Json::object() : cost;
      c.extra = Json{{"thread", label}};
      auto w = write(db, pol, c);
      if (w && (w->outcome == Outcome::Created || w->outcome == Outcome::Replaced)) ++written;
    }
  }
  if (model_made) {
    Json run_cost = cost.is_object() ? cost : Json::object();
    (void)record_run(db, pol, message_id, producer, version, [&] {
      std::vector<std::string> v;
      for (const auto& a : asked) v.push_back(a.get<std::string>());
      return v;
    }(), run_cost);
  }
  return written;
}

}  // namespace loom::annotations
