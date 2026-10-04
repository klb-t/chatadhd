// Offline public-API probe, compiled unchanged against before and after libraries.
// Keep .cc: the compatibility dispatcher automatically compiles only *.cpp.
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "loom/re/regex.h"
#include "loom/util/json.h"
#include "loom/util/utf8.h"

using namespace loom;
using namespace loom::re;

namespace {
Json match_json(const std::optional<Match>& match) {
  if (!match) return nullptr;
  Json groups = Json::array();
  for (std::size_t i = 0; i <= match->group_count(); ++i)
    groups.push_back(Json{{"start", match->start(i)}, {"end", match->end(i)},
                          {"matched", match->matched(i)}, {"text", match->group_utf8(i)}});
  return Json{{"groups", groups},
              {"lastindex", match->lastindex() ? Json(*match->lastindex()) : Json(nullptr)}};
}
}  // namespace

int main() {
  const std::vector<std::pair<std::string, Flags>> patterns{
      {"abc", kNone}, {R"(\w+)", kNone}, {R"((a)|(b)|(c))", kNone},
      {"^abc$", kNone}, {"^abc$", kMultiline}, {"a.*?b", kNone},
      {"a*", kNone}, {R"(foo(?=bar))", kNone}, {R"(foo(?!bar))", kNone},
      {"[a-f]+", kIgnoreCase}, {R"((\w+)@(\w+))", kNone}, {".", kDotAll},
      {R"([\w\s]{1,30}X)", kNone}, {"(abc", kNone}, {"[abc", kNone}, {"*abc", kNone}};
  const std::vector<std::string> texts{
      "", "xxabcyy", "żółw123😀", "xyz\nabc\ndef", "baab", "foobar foobaz",
      "ABCXYZdef", "user@host", "a1 b22 c333", std::string(300, 'a') + "b"};
  Json rows = Json::array();
  for (const auto& [pattern, flags] : patterns) {
    auto regex = Regex::compile(pattern, flags);
    if (!regex) {
      rows.push_back(Json{{"pattern", pattern}, {"flags", flags}, {"error", regex.error().to_string()}});
      continue;
    }
    for (const auto& text : texts) {
      const auto decoded = utf8::decode(text);
      for (std::size_t pos : {std::size_t{0}, std::size_t{1}, decoded.size() + 1}) {
        Json row{{"pattern", pattern}, {"flags", flags}, {"text", text}, {"pos", pos},
                 {"groups", regex->group_count()}, {"effective_flags", regex->flags()}};
        row["search"] = match_json(regex->search(decoded, pos));
        row["search_limit"] = regex->last_search_hit_limit();
        row["match"] = match_json(regex->match(decoded, pos));
        row["match_limit"] = regex->last_search_hit_limit();
        row["fullmatch"] = match_json(regex->fullmatch(decoded, pos));
        row["fullmatch_limit"] = regex->last_search_hit_limit();
        rows.push_back(std::move(row));
      }
      Json matches = Json::array();
      for (const auto& match : regex->finditer(decoded)) matches.push_back(match_json(match));
      Json found = Json::array();
      for (const auto& value : regex->findall(decoded)) found.push_back(utf8::encode(value));
      rows.push_back(Json{{"pattern", pattern}, {"flags", flags}, {"text", text},
                         {"iter", matches}, {"findall", found},
                         {"sub", regex->sub_utf8(R"(\1-X)", text)},
                         {"sub_count2", regex->sub_utf8("X", text, 2)}});
    }
  }
  Json budgets = Json::array();
  for (std::uint64_t limit : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{10}, std::uint64_t{1000}}) {
    auto regex = Regex::compile("(a+)+b");
    if (!regex) return 1;
    regex->set_step_limit(limit);
    const auto text = utf8::decode("aaaaaaaab");
    Json row{{"limit", limit}, {"match", match_json(regex->search(text))}};
    row["hit_limit"] = regex->last_search_hit_limit();
    auto copy = *regex;
    copy.set_step_limit(0);
    row["copy_shared_match"] = match_json(regex->search(text));
    row["copy_shared_hit_limit"] = regex->last_search_hit_limit();
    budgets.push_back(std::move(row));
  }
  std::cout << Json{{"schema", "loom.regex.profile_parity/1"}, {"rows", rows}, {"budgets", budgets}}.dump() << '\n';
}
