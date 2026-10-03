// Foundation compat commands: JSON formatting, UTF-8/Unicode semantics, time
// format, IDs, config/secrets/paths, embedded contract data.
#include <chrono>

#include "compat_registry.h"
#include "loom/config.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

using namespace loom;
using namespace loom::compat;

namespace {
std::string from_hex(const std::string& h) {
  std::string out;
  for (std::size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return out;
}
std::string hex(std::string_view s) { return to_hex(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
}  // namespace

LOOM_COMPAT_COMMAND(cmd_json_dumps, "json-dumps", "@values.json : py_dumps / indent=2 / canonical for each value") {
  if (args.empty()) return fail("usage: json-dumps @values.json");
  auto vals = arg_json(args[0]);
  if (!vals || !vals->is_array()) return fail("expected a JSON array");
  Json out = Json::array();
  json::DumpOptions indent2;
  indent2.indent = 2;
  indent2.ensure_ascii = false;
  for (const auto& v : *vals) {
    out.push_back(Json{{"compact", json::py_dumps(v)}, {"indent2", json::py_dumps(v, indent2)}, {"canonical", json::canonical(v)}});
  }
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_float_repr, "float-repr", "@floats.json : Python repr() of each double") {
  if (args.empty()) return fail("usage: float-repr @floats.json");
  auto vals = arg_json(args[0]);
  if (!vals || !vals->is_array()) return fail("expected a JSON array");
  Json out = Json::array();
  for (const auto& v : *vals) out.push_back(json::format_float_py(v.get<double>()));
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_utf8, "utf8", "@hex_strings.json : decode(replace)/len/lower/upper/strip per byte string") {
  if (args.empty()) return fail("usage: utf8 @hex_strings.json");
  auto vals = arg_json(args[0]);
  if (!vals || !vals->is_array()) return fail("expected a JSON array of hex strings");
  Json out = Json::array();
  for (const auto& v : *vals) {
    std::string bytes = from_hex(v.get<std::string>());
    std::string fixed = utf8::repair(bytes);
    out.push_back(Json{{"repaired", hex(fixed)},
                       {"valid", utf8::is_valid(bytes)},
                       {"length", utf8::length(fixed)},
                       {"lower", hex(utf8::to_lower(fixed))},
                       {"upper", hex(utf8::to_upper(fixed))},
                       {"strip", hex(utf8::strip(fixed))},
                       {"prefix5", hex(utf8::prefix(fixed, 5))},
                       {"blank", utf8::is_blank(fixed)}});
  }
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_time_format, "time-format", "@micros.json : utcnow-style ISO string for epoch microseconds") {
  if (args.empty()) return fail("usage: time-format @micros.json");
  auto vals = arg_json(args[0]);
  if (!vals || !vals->is_array()) return fail("expected a JSON array");
  Json out = Json::array();
  for (const auto& v : *vals) {
    auto tp = timeutil::Clock::time_point(std::chrono::microseconds(v.get<std::int64_t>()));
    out.push_back(timeutil::format_iso_utc(tp));
  }
  out.push_back(timeutil::utc_now_iso());
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_gen_ids, "gen-ids", "<prefix> <n> : generate ids") {
  if (args.size() < 2) return fail("usage: gen-ids <prefix> <n>");
  Json out = Json::array();
  int n = std::stoi(args[1]);
  for (int i = 0; i < n; ++i) out.push_back(gen_id(args[0]));
  print_json(out);
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_config_load, "config-load", "<dir> : load config.json + secrets.json like the Runtime") {
  if (args.empty()) return fail("usage: config-load <dir>");
  fs::path dir = args[0];
  Config cfg(dir / "config.json");
  Secrets sec(dir / "secrets.json");
  print_json(Json{{"config", cfg.all()},
                  {"upgraded_on_load", cfg.upgraded_on_load()},
                  {"load_error", cfg.load_error()},
                  {"secret_keys", sec.keys()},
                  {"has_api_key", sec.has("api_key")}});
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_config_set, "config-set", "<dir> @patch.json : {config:{...}, secrets:{...}} then save") {
  if (args.size() < 2) return fail("usage: config-set <dir> @patch.json");
  fs::path dir = args[0];
  auto patch = arg_json(args[1]);
  if (!patch || !patch->is_object()) return fail("patch must be an object");
  Config cfg(dir / "config.json");
  Secrets sec(dir / "secrets.json");
  if (const Json* c = json::find(*patch, "config"); c && c->is_object()) {
    for (auto it = c->begin(); it != c->end(); ++it) cfg.set(it.key(), it.value());
    if (auto st = cfg.save(); !st) return fail(st.error().to_string());
  }
  if (const Json* s = json::find(*patch, "secrets"); s && s->is_object()) {
    for (auto it = s->begin(); it != s->end(); ++it) sec.set(it.key(), it.value());
    if (auto st = sec.save(); !st) return fail(st.error().to_string());
  }
  print_json(Json{{"ok", true}});
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_paths_resolve, "paths-resolve", "[override] : resolve_data_dir with the process env") {
  std::optional<std::string_view> ov;
  if (!args.empty() && !args[0].empty()) ov = args[0];
  auto r = resolve_data_dir(ov);
  if (!r) return fail(r.error().to_string());
  print_json(Json{{"path", r->string()}});
  return 0;
}

LOOM_COMPAT_COMMAND(cmd_contract_data, "contract-data", ": embedded policy data (analyzer rules, LLM prompt, config defaults)") {
  print_json(Json{{"analyzer_rules", AnalyzerRules::builtin().to_json()},
                  {"analysis_prompt", std::string(kAnalysisPrompt)},
                  {"config_defaults", config_defaults()}});
  return 0;
}
