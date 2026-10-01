#include "active_task_spec.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/util/utf8.h"

namespace loom::chat {
namespace {

Error invalid(std::string_view field) {
  // Field names are fixed compiler diagnostics; never include source values.
  return Error(Errc::InvalidArgument, "invalid active_task_spec: " + std::string(field));
}

bool object_shape(const Json& value, std::initializer_list<std::string_view> required,
                  std::initializer_list<std::string_view> optional = {}) {
  if (!value.is_object()) return false;
  for (auto key : required) if (!value.contains(std::string(key))) return false;
  for (auto it = value.begin(); it != value.end(); ++it) {
    if (std::find(required.begin(), required.end(), it.key()) == required.end() &&
        std::find(optional.begin(), optional.end(), it.key()) == optional.end()) return false;
  }
  return true;
}

bool text(const Json& value, bool nonempty = true) {
  return value.is_string() && (!nonempty || !value.get_ref<const std::string&>().empty());
}

bool one_of(const Json& value, std::initializer_list<std::string_view> options) {
  if (!value.is_string()) return false;
  return std::find(options.begin(), options.end(), value.get_ref<const std::string&>()) != options.end();
}

bool integer(const Json& value, std::uint64_t minimum = 0) {
  if (!value.is_number_integer()) return false;
  if (value.is_number_unsigned()) return value.get<std::uint64_t>() >= minimum;
  const auto number = value.get<std::int64_t>();
  return number >= 0 && static_cast<std::uint64_t>(number) >= minimum;
}

// Compare non-negative finite JSON numbers without first rounding integers to
// double. In particular, adjacent integer timestamps above 2^53 must not
// collapse to the same binary64 value.
bool number_less(const Json& left, const Json& right) {
  const bool left_integer = left.is_number_integer();
  const bool right_integer = right.is_number_integer();
  if (left_integer && right_integer) {
    const auto as_unsigned = [](const Json& value) {
      return value.is_number_unsigned() ? value.get<std::uint64_t>()
                                        : static_cast<std::uint64_t>(value.get<std::int64_t>());
    };
    return as_unsigned(left) < as_unsigned(right);
  }
  if (!left_integer && !right_integer) return left.get<double>() < right.get<double>();

  constexpr double kUint64Limit = 18446744073709551616.0;  // 2^64, exactly representable
  if (left_integer) {
    const auto integer_value = left.is_number_unsigned()
        ? left.get<std::uint64_t>()
        : static_cast<std::uint64_t>(left.get<std::int64_t>());
    const double float_value = right.get<double>();
    if (float_value >= kUint64Limit) return true;
    const auto whole = static_cast<std::uint64_t>(float_value);
    return integer_value < whole || (integer_value == whole && std::trunc(float_value) != float_value);
  }

  const double float_value = left.get<double>();
  if (float_value >= kUint64Limit) return false;
  const auto integer_value = right.is_number_unsigned()
      ? right.get<std::uint64_t>()
      : static_cast<std::uint64_t>(right.get<std::int64_t>());
  return static_cast<std::uint64_t>(float_value) < integer_value;
}

bool strings(const Json& value, bool nonempty_array = false, bool unique = true) {
  if (!value.is_array() || (nonempty_array && value.empty())) return false;
  std::set<std::string> seen;
  for (const auto& item : value) {
    if (!text(item)) return false;
    if (unique && !seen.insert(item.get<std::string>()).second) return false;
  }
  return true;
}

// Check even opaque extensions: invalid UTF-8 and nonfinite native JSON numbers
// cannot become valid source bytes merely because the serializer repairs them.
bool finite_utf8_json(const Json& root) {
  std::vector<const Json*> pending{&root};
  while (!pending.empty()) {
    const Json& value = *pending.back();
    pending.pop_back();
    if (value.is_string() && !utf8::is_valid(value.get_ref<const std::string&>())) return false;
    if (value.is_number_float() && !std::isfinite(value.get<double>())) return false;
    if (value.is_binary() || value.is_discarded()) return false;
    if (value.is_object()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (!utf8::is_valid(it.key())) return false;
        pending.push_back(&it.value());
      }
    } else if (value.is_array()) {
      for (const auto& item : value) pending.push_back(&item);
    }
  }
  return true;
}

struct Timestamp {
  std::int64_t seconds = 0;
  std::string fraction;  // trailing zeroes removed; no floating-point rounding
};

bool timestamp(const Json& value, Timestamp& out) {
  if (!value.is_string()) return false;
  const auto& s = value.get_ref<const std::string&>();
  if (s.size() < 20) return false;
  auto digits = [&](std::size_t start, std::size_t count) -> int {
    if (start + count > s.size()) return -1;
    int result = 0;
    for (std::size_t i = start; i < start + count; ++i) {
      if (s[i] < '0' || s[i] > '9') return -1;
      result = result * 10 + s[i] - '0';
    }
    return result;
  };
  if (s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't') ||
      s[13] != ':' || s[16] != ':') return false;
  const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2);
  const int hour = digits(11, 2), minute = digits(14, 2), second = digits(17, 2);
  if (year < 1 || month < 1 || day < 1 || hour < 0 || hour > 23 || minute < 0 ||
      minute > 59 || second < 0 || second > 59) return false;
  const std::chrono::year_month_day date{std::chrono::year{year},
      std::chrono::month{static_cast<unsigned>(month)}, std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok()) return false;
  std::size_t pos = 19;
  out.fraction.clear();
  if (s[pos] == '.') {
    const std::size_t start = ++pos;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
    if (pos == start) return false;
    out.fraction = s.substr(start, pos - start);
    while (!out.fraction.empty() && out.fraction.back() == '0') out.fraction.pop_back();
  }
  if (pos == s.size()) return false;
  int offset = 0;
  if (s[pos] == 'Z' || s[pos] == 'z') {
    if (pos + 1 != s.size()) return false;
  } else {
    if ((s[pos] != '+' && s[pos] != '-') || pos + 6 != s.size() || s[pos + 3] != ':') return false;
    const int oh = digits(pos + 1, 2), om = digits(pos + 4, 2);
    if (oh < 0 || oh > 23 || om < 0 || om > 59) return false;
    offset = (oh * 60 + om) * 60 * (s[pos] == '+' ? 1 : -1);
  }
  out.seconds = std::chrono::sys_days{date}.time_since_epoch().count() * 86400 +
                hour * 3600 + minute * 60 + second - offset;
  return true;
}

bool after(const Timestamp& left, const Timestamp& right) {
  return left.seconds > right.seconds || (left.seconds == right.seconds && left.fraction > right.fraction);
}

bool product_ref(const Json& value) {
  return object_shape(value, {"kind", "id"}) && value["kind"] == "product" && text(value["id"]);
}

bool pointer(const Json& value) {
  if (!text(value, false)) return false;
  const auto& s = value.get_ref<const std::string&>();
  if (!s.empty() && s.front() != '/') return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '~' && (++i == s.size() || (s[i] != '0' && s[i] != '1'))) return false;
  }
  return true;
}

bool locator(const Json& value) {
  if (!object_shape(value, {"source"}, {"member", "json_pointer", "byte_start", "byte_len",
                                      "time_start", "time_end", "line"}) || !text(value["source"])) return false;
  if (value.contains("member") && !text(value["member"], false)) return false;
  if (value.contains("json_pointer") && !pointer(value["json_pointer"])) return false;
  auto present = [&](std::string_view key) { return value.contains(std::string(key)) && !value[std::string(key)].is_null(); };
  for (auto key : {"byte_start", "byte_len", "line"}) {
    if (present(key) && !integer(value[key], std::string_view(key) == "line" ? 1 : 0)) return false;
  }
  if (present("byte_start") != present("byte_len") || present("time_start") != present("time_end")) return false;
  for (auto key : {"time_start", "time_end"}) {
    if (present(key) && (!value[key].is_number() || value[key].get<double>() < 0)) return false;
  }
  return !present("time_start") || !number_less(value["time_end"], value["time_start"]);
}

Status validate(const Json& spec) {
  if (!finite_utf8_json(spec)) return invalid("finite UTF-8 JSON required");
  if (!object_shape(spec, {"schema", "product_ref", "goal_id", "knowledge_run", "scope", "version",
      "previous_product_ref", "known_at", "representation", "materializer", "history_event_ids",
      "source_refs", "statements", "compiled_instruction"}, {"extensions"})) return invalid("envelope");
  if (spec["schema"] != "loom.active_task_spec/1" || spec["representation"] != "derived_product") return invalid("schema/representation");
  if (!product_ref(spec["product_ref"]) || !text(spec["goal_id"]) ||
      (!spec["knowledge_run"].is_null() && !text(spec["knowledge_run"])) || !integer(spec["version"], 1)) return invalid("identity/version");
  if (!spec["previous_product_ref"].is_null()) {
    if (!product_ref(spec["previous_product_ref"]) ||
        spec["previous_product_ref"]["id"] == spec["product_ref"]["id"]) return invalid("previous_product_ref");
  }
  const auto& scope = spec["scope"];
  if (!object_shape(scope, {"conversation_id", "branch_id", "task_id"}) || !text(scope["conversation_id"]) ||
      !text(scope["branch_id"]) || !text(scope["task_id"])) return invalid("scope");
  const auto& method = spec["materializer"];
  if (!object_shape(method, {"id", "version"}) || !text(method["id"]) || !text(method["version"])) return invalid("materializer");
  if (spec.contains("extensions") && !spec["extensions"].is_object()) return invalid("extensions");
  Timestamp cutoff;
  if (!timestamp(spec["known_at"], cutoff)) return invalid("known_at");
  if (!strings(spec["history_event_ids"], true)) return invalid("history_event_ids");
  std::set<std::string> history;
  for (const auto& id : spec["history_event_ids"]) history.insert(id.get<std::string>());
  if (!spec["source_refs"].is_array() || spec["source_refs"].empty()) return invalid("source_refs");
  std::set<std::string> sources;
  for (const auto& ref : spec["source_refs"]) {
    if (!object_shape(ref, {"event_id", "locator", "known_at"}, {"observation_id", "quote"}) ||
        !text(ref["event_id"]) || !locator(ref["locator"]) ||
        (ref.contains("observation_id") && !text(ref["observation_id"])) ||
        (ref.contains("quote") && !text(ref["quote"], false))) return invalid("source_refs entry");
    Timestamp known;
    if (!timestamp(ref["known_at"], known) || after(known, cutoff)) return invalid("source_refs known_at");
    const auto id = ref["event_id"].get<std::string>();
    if (!history.contains(id)) return invalid("source_refs outside history");
    sources.insert(id);
  }
  if (!spec["statements"].is_array() || spec["statements"].empty()) return invalid("statements");
  std::map<std::string, const Json*> statements;
  bool active_goal = false;
  for (const auto& statement : spec["statements"]) {
    if (!object_shape(statement, {"id", "kind", "status", "text", "source_event_ids", "claim_ids", "conditions", "supersedes"}, {"extensions"}) ||
        !text(statement["id"]) || !text(statement["text"]) ||
        !one_of(statement["kind"], {"goal", "required_information", "format", "style", "prohibition", "exception", "alternative", "open_issue", "executor_context"}) ||
        !one_of(statement["status"], {"active", "contested", "superseded", "rejected"}) ||
        !strings(statement["source_event_ids"], true) || !strings(statement["claim_ids"]) ||
        !strings(statement["conditions"], false, false) || !strings(statement["supersedes"]) ||
        (statement.contains("extensions") && !statement["extensions"].is_object())) return invalid("statement entry");
    if (!statements.emplace(statement["id"].get<std::string>(), &statement).second) return invalid("duplicate statement id");
    for (const auto& id : statement["source_event_ids"]) {
      if (!sources.contains(id.get<std::string>())) return invalid("statement source reference");
    }
    if (statement["kind"] == "goal" && one_of(statement["status"], {"active", "contested"})) active_goal = true;
  }
  if (!active_goal) return invalid("active or contested goal required");
  // Kahn's algorithm avoids recursive traversal of caller-supplied chains.
  std::map<std::string, std::size_t> incoming;
  for (const auto& [id, statement] : statements) incoming[id] = 0;
  for (const auto& [id, statement] : statements) {
    for (const auto& old : (*statement)["supersedes"]) {
      const auto old_id = old.get<std::string>();
      auto it = statements.find(old_id);
      if (old_id == id || it == statements.end() || !one_of((*it->second)["status"], {"superseded", "rejected"})) return invalid("supersedes reference/status");
      ++incoming[old_id];
    }
  }
  std::vector<std::string> ready;
  for (const auto& [id, count] : incoming) if (count == 0) ready.push_back(id);
  std::size_t visited = 0;
  while (!ready.empty()) {
    const auto id = ready.back();
    ready.pop_back();
    ++visited;
    for (const auto& old : (*statements.at(id))["supersedes"]) {
      const auto old_id = old.get<std::string>();
      if (--incoming[old_id] == 0) ready.push_back(old_id);
    }
  }
  if (visited != statements.size()) return invalid("cyclic supersession");
  const auto& compiled = spec["compiled_instruction"];
  if (!object_shape(compiled, {"text", "source_map"}) || !text(compiled["text"]) ||
      !compiled["source_map"].is_array() || compiled["source_map"].empty()) return invalid("compiled_instruction");
  const auto& input_text = compiled["text"].get_ref<const std::string&>();
  for (const auto& mapping : compiled["source_map"]) {
    if (!object_shape(mapping, {"span", "statement_ids"}) ||
        !object_shape(mapping["span"], {"byte_start", "byte_len"}) ||
        !integer(mapping["span"]["byte_start"]) || !integer(mapping["span"]["byte_len"], 1) ||
        !strings(mapping["statement_ids"], true)) return invalid("compiled_instruction source_map");
    const auto start = mapping["span"]["byte_start"].get<std::uint64_t>();
    const auto length = mapping["span"]["byte_len"].get<std::uint64_t>();
    if (start > input_text.size() || length > input_text.size() - start) return invalid("compiled_instruction span range");
    if (!utf8::is_valid(std::string_view(input_text).substr(0, static_cast<std::size_t>(start))) ||
        !utf8::is_valid(std::string_view(input_text).substr(static_cast<std::size_t>(start), static_cast<std::size_t>(length)))) return invalid("compiled_instruction span UTF-8");
    for (const auto& id : mapping["statement_ids"]) {
      const auto it = statements.find(id.get<std::string>());
      if (it == statements.end() || !one_of((*it->second)["status"], {"active", "contested"})) return invalid("compiled_instruction inactive/unknown statement");
    }
  }
  return ok_status();
}

}  // namespace

Result<Json> compile_active_task_spec(const Json& spec) {
  LOOM_TRY(validate(spec));
  std::string instruction;
  Json mapping = Json::array();
  for (const auto& statement : spec["statements"]) {
    if (!one_of(statement["status"], {"active", "contested"})) continue;
    std::string line = "[" + statement["kind"].get<std::string>();
    if (statement["status"] == "contested") line += "; contested — do not resolve without clarification";
    if (statement["kind"] == "executor_context") line += "; executor only — not product content";
    line += "] " + statement["text"].get<std::string>();
    if (!statement["conditions"].empty()) {
      line += " [scope: ";
      bool first = true;
      for (const auto& condition : statement["conditions"]) {
        if (!first) line += " | ";
        first = false;
        line += condition.get<std::string>();
      }
      line += ']';
    }
    if (!instruction.empty()) instruction += '\n';
    const auto start = instruction.size();
    instruction += line;
    mapping.push_back(Json{{"span", {{"byte_start", start}, {"byte_len", line.size()}}},
                           {"statement_ids", Json::array({statement["id"]})}});
  }
  Json result = spec;
  result["compiled_instruction"] = Json{{"text", std::move(instruction)}, {"source_map", std::move(mapping)}};
  return result;
}

}  // namespace loom::chat
