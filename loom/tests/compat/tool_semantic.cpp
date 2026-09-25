// Compat commands for the regex engine and SemanticAnalyzer.
#include "compat_registry.h"
#include "loom/re/regex.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/utf8.h"

using namespace loom;
using namespace loom::compat;

namespace {

re::Flags flags_from_json(const Json& arr) {
  re::Flags f = re::kNone;
  if (!arr.is_array()) return f;
  for (const auto& v : arr) {
    if (!v.is_string()) continue;
    const auto& s = v.get_ref<const std::string&>();
    if (s == "IGNORECASE") f |= re::kIgnoreCase;
    if (s == "MULTILINE") f |= re::kMultiline;
    if (s == "DOTALL") f |= re::kDotAll;
  }
  return f;
}

Json span_json(const re::Match& m, std::size_t g) {
  auto sp = m.span(g);
  if (!sp.matched()) return nullptr;
  return Json{{"start", sp.start}, {"end", sp.end}, {"text", m.group_utf8(g)}};
}

Json match_json(const re::Regex& rx, const re::Match& m) {
  Json groups = Json::array();
  for (std::size_t g = 0; g <= rx.group_count(); ++g) groups.push_back(span_json(m, g));
  return Json{{"matched", true},
             {"start", m.start(0)},
             {"end", m.end(0)},
             {"groups", groups},
             {"lastindex", m.lastindex() ? Json(*m.lastindex()) : Json(nullptr)}};
}

}  // namespace

// Each case: {"pattern","flags":[...],"op":"search|match|fullmatch|finditer|
// findall|sub","text","pos":0,"repl":"","count":0}. One JSON result per case,
// same order. {"error":"..."} on a compile failure.
LOOM_COMPAT_COMMAND(cmd_re_exec, "re-exec", "@cases.json : run regex ops, one JSON result per case") {
  if (args.empty()) return fail("usage: re-exec @cases.json");
  auto cases = arg_json(args[0]);
  if (!cases || !cases->is_array()) return fail("expected a JSON array");
  Json out = Json::array();
  for (const auto& c : *cases) {
    std::string pattern = json::get_string(c, "pattern");
    const Json* fj = json::find(c, "flags");
    re::Flags flags = fj ? flags_from_json(*fj) : re::kNone;
    std::string op = json::get_string(c, "op", "search");
    std::string text = json::get_string(c, "text");
    auto rx = re::Regex::compile(pattern, flags);
    if (!rx) {
      out.push_back(Json{{"error", rx.error().to_string()}});
      continue;
    }
    std::u32string t32 = utf8::decode(text);
    Json res;
    if (op == "search" || op == "match" || op == "fullmatch") {
      std::size_t pos = static_cast<std::size_t>(json::get_int(c, "pos", 0));
      std::optional<re::Match> m;
      if (op == "search") m = rx->search(t32, pos);
      else if (op == "match") m = rx->match(t32, pos);
      else m = rx->fullmatch(t32, pos);
      res = m ? match_json(*rx, *m) : Json{{"matched", false}};
    } else if (op == "finditer") {
      Json arr = Json::array();
      for (const auto& m : rx->finditer(t32)) arr.push_back(match_json(*rx, m));
      res = Json{{"matches", arr}};
    } else if (op == "findall") {
      Json arr = Json::array();
      for (const auto& s : rx->findall(t32)) arr.push_back(utf8::encode(s));
      res = Json{{"result", arr}};
    } else if (op == "sub") {
      std::string repl = json::get_string(c, "repl");
      int count = static_cast<int>(json::get_int(c, "count", 0));
      res = Json{{"result", rx->sub_utf8(repl, text, static_cast<std::size_t>(count))}};
    } else {
      res = Json{{"error", "unknown op: " + op}};
    }
    out.push_back(res);
  }
  print_json(out);
  return 0;
}

// Each case is a plain text string. Runs SemanticAnalyzer::analyse() and
// prints the unified analysis dict (entities/topics/relations) per case.
LOOM_COMPAT_COMMAND(cmd_semantic_analyse, "semantic-analyse", "@texts.json : analyse() each text") {
  if (args.empty()) return fail("usage: semantic-analyse @texts.json");
  auto texts = arg_json(args[0]);
  if (!texts || !texts->is_array()) return fail("expected a JSON array of strings");
  auto an = SemanticAnalyzer::create();
  if (!an) return fail("SemanticAnalyzer::create failed: " + an.error().to_string());
  Json out = Json::array();
  for (const auto& t : *texts) {
    if (!t.is_string()) {
      out.push_back(Json{{"error", "not a string"}});
      continue;
    }
    std::string text = t.get<std::string>();
    Analysis a = (*an)->analyse(text);
    Json ents = Json::array();
    for (const auto& e : a.entities) {
      ents.push_back(Json{{"text", e.text}, {"entity_type", e.entity_type}, {"start", e.start}, {"end", e.end}});
    }
    out.push_back(Json{{"entities", ents}, {"topics", a.topics}});
  }
  print_json(out);
  return 0;
}
