// Relative graph addresses (K3-1): data (JSON steps), text notation from a preset, composition, resolution on a port graph.
// See include/loom/address.h.
#include "loom/address.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <set>

#include "k3_packs_embedded.inc"
#include "loom/util/fs.h"

namespace loom::address {

namespace {

const std::set<std::string> kReserved = {"int", "double", "name", "word", "datetime", "range", "levels", "pred"};

std::string kind_of(const Json& c) { return c.is_object() && c.size() == 1 ? c.begin().key() : std::string(); }
const Json& payload(const Json& c) { return c.begin().value(); }
Json coord_of(const std::string& kind, Json v) {
  Json c = Json::object();
  c[kind] = std::move(v);
  return c;
}

bool ident_start(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool ident_char(unsigned char c) { return ident_start(c) || std::isdigit(c) || c == '-'; }
bool is_identifier(const std::string& s) {
  if (s.empty() || !ident_start(static_cast<unsigned char>(s[0]))) return false;
  return std::all_of(s.begin(), s.end(), [](char c) { return ident_char(static_cast<unsigned char>(c)); });
}

// ── validation of the JSON form ─────────────────────────────────────────────
std::string check_coord(const Json& c, bool is_part, bool is_end) {
  const std::string kind = kind_of(c);
  if (kind.empty()) return "a coordinate is an object with exactly one key (its kind)";
  const Json& v = payload(c);
  if (kind == "int") return v.is_number_integer() ? "" : "int needs an integer";
  if (kind == "double") return v.is_number() && std::isfinite(v.get<double>()) ? "" : "double needs a finite number";
  if (kind == "name" || kind == "datetime" || kind == "pred") return v.is_string() ? "" : kind + " needs a string";
  if (kind == "word") return v.is_string() && is_identifier(v.get<std::string>()) ? "" : "word needs an identifier";
  if (kind == "range") {
    if (is_part || is_end) return "a range cannot be a level or an end of a range";
    if (!v.is_array() || v.size() != 2) return "range needs two ends";
    for (const auto& e : v)
      if (auto m = check_coord(e, false, true); !m.empty()) return m;
    return "";
  }
  if (kind == "levels") {
    if (is_part) return "levels cannot be nested";
    if (!v.is_array() || v.empty()) return "levels needs at least one part";
    for (const auto& e : v) {
      if (kind_of(e) == "range") return "a range cannot be a level";
      if (auto m = check_coord(e, true, false); !m.empty()) return m;
    }
    return "";
  }
  if (!is_identifier(kind)) return "the kind `" + kind + "` is not an identifier";
  return v.is_string() ? "" : "a coordinate of kind " + kind + " needs a string payload";
}

// ── tokens ──────────────────────────────────────────────────────────────────
struct Lex {
  std::string_view s;
  std::size_t i = 0;
  const Syntax& syn;

  bool starts_at(std::size_t at, const std::string& tok) const { return !tok.empty() && s.compare(at, tok.size(), tok) == 0; }
  bool starts(const std::string& tok) const { return starts_at(i, tok); }
  bool at_end() const { return i >= s.size(); }
  void ws() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
  const std::string* any_token_at(std::size_t at) const {
    for (const std::string* t : {&syn.escape, &syn.quote, &syn.pred_open, &syn.pred_close, &syn.group_open, &syn.group_close, &syn.step,
                                 &syn.dim, &syn.range, &syn.level})
      if (starts_at(at, *t)) return t;
    return nullptr;
  }
  // The character (or whole token) after an escape, copied literally.
  void copy_after_escape(std::string& out) {
    if (i >= s.size()) return;
    if (const std::string* t = any_token_at(i)) {
      out += *t;
      i += t->size();
      return;
    }
    do out += s[i++];
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
  }
  Error err(const std::string& what) const { return Error(Errc::Parse, "address: " + what + " at position " + std::to_string(i)); }

  // after the opening quote; consumes the closing one
  Result<std::string> quoted() {
    std::string out;
    while (i < s.size()) {
      if (starts(syn.escape)) {
        i += syn.escape.size();
        copy_after_escape(out);
      } else if (starts(syn.quote)) {
        i += syn.quote.size();
        return out;
      } else {
        out += s[i++];
      }
    }
    return err("unterminated quote");
  }
  // after the opening bracket; consumes the closing one. Quotes hide brackets, brackets nest, the escape takes the next token literally.
  Result<std::string> predicate() {
    std::string out;
    int depth = 1;
    bool in_quote = false;
    while (i < s.size()) {
      if (starts(syn.escape)) {
        i += syn.escape.size();
        copy_after_escape(out);
      } else if (starts(syn.quote)) {
        in_quote = !in_quote;
        out += syn.quote;
        i += syn.quote.size();
      } else if (!in_quote && starts(syn.pred_open)) {
        ++depth;
        out += syn.pred_open;
        i += syn.pred_open.size();
      } else if (!in_quote && starts(syn.pred_close)) {
        i += syn.pred_close.size();
        if (--depth == 0) return out;
        out += syn.pred_close;
      } else {
        out += s[i++];
      }
    }
    return err("unterminated condition");
  }
};

// An atom with the raw text that built it (the double rule needs the digits).
struct Atom {
  Json coord;
  std::string raw;
  bool plain_int = false;       // an int with no sign
  bool numeric_int = false;     // an int (signed or not)
  bool signed_zero = false;
  bool overflow = false;        // digits that do not fit 64 bits: fine inside a double, an error as an int
};

class Parser {
 public:
  Parser(std::string_view text, const Syntax& syn, const Context& ctx) : lx{text, 0, syn}, syn_(syn), ctx_(ctx) {}

  Result<Address> run(const std::string& original) {
    Address out;
    lx.ws();
    if (lx.at_end()) {
      if (ctx_.advisories) ctx_.advisories->add("address_empty");
      return out;
    }
    for (;;) {
      LOOM_TRY_ASSIGN(auto step, parse_step());
      out.steps.push_back(std::move(step));
      lx.ws();
      if (lx.starts(syn_.step)) {
        lx.i += syn_.step.size();
        continue;
      }
      break;
    }
    if (!lx.at_end()) return lx.err("unexpected text");
    if (ctx_.advisories) {
      for (const auto& z : zeros_) ctx_.advisories->add("address_degenerate", Json{{"address", original}, {"detail", "a sign on zero: " + z + " is the same as 0"}});
      for (std::size_t k = 1; k < out.steps.size(); ++k)
        if (out.steps[k].dim == out.steps[k - 1].dim && out.steps[k].coord == coord_of("word", "all") && out.steps[k - 1].coord == out.steps[k].coord)
          ctx_.advisories->add("address_degenerate", Json{{"address", original}, {"detail", "`all` again on the same dimension adds nothing"}});
      if (ctx_.dims)
        for (const auto& st : out.steps)
          if (!st.dim.empty() && !ctx_.dims->find(st.dim)) ctx_.advisories->add("dimension_unknown", Json{{"dimension", st.dim}});
    }
    return out;
  }

 private:
  Lex lx;
  const Syntax& syn_;
  const Context& ctx_;
  std::vector<std::string> zeros_;

  Result<Step> parse_step() {
    lx.ws();
    Step step;
    const std::size_t save = lx.i;
    if (!lx.at_end() && ident_start(static_cast<unsigned char>(lx.s[lx.i])) && !lx.any_token_at(lx.i)) {
      std::size_t j = lx.i;
      while (j < lx.s.size() && ident_char(static_cast<unsigned char>(lx.s[j])) && !lx.any_token_at(j)) ++j;
      if (lx.starts_at(j, syn_.dim)) {
        step.dim = std::string(lx.s.substr(lx.i, j - lx.i));
        lx.i = j + syn_.dim.size();
      } else {
        lx.i = save;
      }
    }
    LOOM_TRY_ASSIGN(step.coord, parse_coord());
    return step;
  }

  Result<Json> parse_coord() {
    LOOM_TRY_ASSIGN(auto first, parse_body());
    lx.ws();
    if (lx.starts(syn_.range)) {
      lx.i += syn_.range.size();
      lx.ws();
      LOOM_TRY_ASSIGN(auto second, parse_body());
      lx.ws();
      if (lx.starts(syn_.range)) return lx.err("a range has two ends; ranges do not chain");
      return coord_of("range", Json::array({first, second}));
    }
    return first;
  }

  // levels (grouped or dotted), a double, or one atom — never a range
  Result<Json> parse_body() {
    lx.ws();
    if (lx.starts(syn_.group_open)) {
      lx.i += syn_.group_open.size();
      Json parts = Json::array();
      for (;;) {
        LOOM_TRY_ASSIGN(auto a, parse_atom());
        if (a.overflow) return lx.err("the number " + a.raw + " does not fit a 64-bit integer");
        parts.push_back(a.coord);
        lx.ws();
        if (lx.starts(syn_.level) && !lx.starts(syn_.range)) {
          lx.i += syn_.level.size();
          continue;
        }
        break;
      }
      if (!lx.starts(syn_.group_close)) return lx.err("a closing group token was expected");
      lx.i += syn_.group_close.size();
      return coord_of("levels", std::move(parts));
    }
    std::vector<Atom> atoms;
    LOOM_TRY_ASSIGN(auto first, parse_atom());
    atoms.push_back(std::move(first));
    while (lx.starts(syn_.level) && !lx.starts(syn_.range)) {
      lx.i += syn_.level.size();
      LOOM_TRY_ASSIGN(auto a, parse_atom());
      atoms.push_back(std::move(a));
    }
    if (atoms.size() == 1) {
      if (atoms[0].overflow) return lx.err("the number " + atoms[0].raw + " does not fit a 64-bit integer");
      return atoms[0].coord;
    }
    if (atoms.size() == 2 && syn_.level == "." && atoms[0].numeric_int && atoms[1].plain_int) {
      const std::string text = atoms[0].raw + "." + atoms[1].raw;
      return coord_of("double", std::strtod(text.c_str(), nullptr));
    }
    Json parts = Json::array();
    for (auto& a : atoms) {
      if (a.overflow) return lx.err("the number " + a.raw + " does not fit a 64-bit integer");
      parts.push_back(a.coord);
    }
    return coord_of("levels", std::move(parts));
  }

  Result<Atom> parse_atom() {
    Atom a;
    lx.ws();
    if (lx.at_end()) return lx.err("a coordinate was expected");
    if (lx.starts(syn_.quote)) {
      lx.i += syn_.quote.size();
      LOOM_TRY_ASSIGN(auto text, lx.quoted());
      a.coord = coord_of("name", text);
      return a;
    }
    if (lx.starts(syn_.pred_open)) {
      lx.i += syn_.pred_open.size();
      LOOM_TRY_ASSIGN(auto text, lx.predicate());
      a.coord = coord_of("pred", text);
      return a;
    }
    if (!syn_.datetime_pattern.empty()) {
      std::cmatch m;
      if (std::regex_search(lx.s.data() + lx.i, lx.s.data() + lx.s.size(), m, syn_.datetime_re, std::regex_constants::match_continuous) && m.length() > 0) {
        a.coord = coord_of("datetime", m.str());
        lx.i += static_cast<std::size_t>(m.length());
        return a;
      }
    }
    const unsigned char c = static_cast<unsigned char>(lx.s[lx.i]);
    if (std::isdigit(c) || ((c == '+' || c == '-') && lx.i + 1 < lx.s.size() && std::isdigit(static_cast<unsigned char>(lx.s[lx.i + 1])))) {
      std::size_t j = lx.i + ((c == '+' || c == '-') ? 1 : 0);
      const std::size_t digits_from = j;
      while (j < lx.s.size() && std::isdigit(static_cast<unsigned char>(lx.s[j]))) ++j;
      if (syn_.level != "." && j + 1 < lx.s.size() && lx.s[j] == '.' && std::isdigit(static_cast<unsigned char>(lx.s[j + 1])) && !lx.starts_at(j, syn_.range)) {
        std::size_t k = j + 1;
        while (k < lx.s.size() && std::isdigit(static_cast<unsigned char>(lx.s[k]))) ++k;
        a.raw = std::string(lx.s.substr(lx.i, k - lx.i));
        a.coord = coord_of("double", std::strtod(a.raw.c_str(), nullptr));
        lx.i = k;
        return a;
      }
      a.raw = std::string(lx.s.substr(lx.i, j - lx.i));
      std::int64_t v = 0;
      const char* b = a.raw.c_str() + (a.raw[0] == '+' ? 1 : 0);
      const auto r = std::from_chars(b, a.raw.c_str() + a.raw.size(), v);
      if (r.ec == std::errc::result_out_of_range) a.overflow = true;
      else if (r.ec != std::errc() || r.ptr != a.raw.c_str() + a.raw.size()) return lx.err("a number was expected");
      a.coord = a.overflow ? Json(nullptr) : coord_of("int", v);
      a.numeric_int = true;
      a.plain_int = (c != '+' && c != '-');
      a.signed_zero = (c == '+' || c == '-') && v == 0 && j - digits_from >= 1;
      if (a.signed_zero) zeros_.push_back(a.raw);
      lx.i = j;
      return a;
    }
    if (ident_start(c) && !lx.any_token_at(lx.i)) {
      std::size_t j = lx.i;
      while (j < lx.s.size() && ident_char(static_cast<unsigned char>(lx.s[j])) && !lx.any_token_at(j)) ++j;
      const std::string word(lx.s.substr(lx.i, j - lx.i));
      if (lx.starts_at(j, syn_.quote)) {  // typed literal: kind"payload"
        if (kReserved.count(word)) return lx.err("the kind `" + word + "` is written without a prefix");
        lx.i = j + syn_.quote.size();
        LOOM_TRY_ASSIGN(auto text, lx.quoted());
        a.coord = coord_of(word, text);
        return a;
      }
      lx.i = j;
      a.coord = coord_of("word", word);
      return a;
    }
    return lx.err("unexpected character");
  }
};

// Alias pre-pass: replaces the matches found outside quotes and brackets.
std::string apply_aliases(std::string_view text, const Syntax& syn, advisory::Collector* adv) {
  if (syn.aliases.empty()) return std::string(text);
  Lex lx{text, 0, syn};
  std::string out;
  while (lx.i < text.size()) {
    bool hit = false;
    for (const auto& al : syn.aliases)
      if (!al.match.empty() && lx.starts(al.match)) {
        out += al.expands;
        if (adv) adv->add("alias_applied", Json{{"alias", al.id}, {"from", al.match}, {"to", al.expands}});
        lx.i += al.match.size();
        hit = true;
        break;
      }
    if (hit) continue;
    if (lx.starts(syn.range)) {  // never start a match in the middle of a token
      out += syn.range;
      lx.i += syn.range.size();
    } else if (lx.starts(syn.quote)) {
      const std::size_t from = lx.i;
      lx.i += syn.quote.size();
      (void)lx.quoted();  // an unterminated quote is reported by the parser; copy the rest as it is
      out += text.substr(from, lx.i - from);
    } else if (lx.starts(syn.pred_open)) {
      const std::size_t from = lx.i;
      lx.i += syn.pred_open.size();
      (void)lx.predicate();
      out += text.substr(from, lx.i - from);
    } else {
      out += text[lx.i++];
    }
  }
  return out;
}

// ── printer ─────────────────────────────────────────────────────────────────
std::string escape_text(const std::string& text, const Syntax& syn, bool all_tokens) {
  Lex lx{text, 0, syn};
  std::string out;
  while (lx.i < text.size()) {
    bool esc = false;
    for (const std::string* t : {&syn.escape, &syn.quote})
      if (lx.starts(*t)) {
        out += syn.escape + *t;
        lx.i += t->size();
        esc = true;
        break;
      }
    if (!esc && all_tokens)
      for (const std::string* t : {&syn.pred_open, &syn.pred_close})
        if (lx.starts(*t)) {
          out += syn.escape + *t;
          lx.i += t->size();
          esc = true;
          break;
        }
    if (!esc) out += text[lx.i++];
  }
  return out;
}

std::string print_double(double v) {
  char buf[512];
  const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
  std::string s(buf, r.ptr);
  if (s.find('.') == std::string::npos) s += ".0";
  return s;
}

class Printer {
 public:
  Printer(const Syntax& syn, const Context& ctx, bool spaced) : syn_(syn), ctx_(ctx), spaced_(spaced) {}

  std::string address(const Address& a) {
    std::string out;
    for (std::size_t i = 0; i < a.steps.size(); ++i) {
      if (i) out += syn_.step;
      if (!a.steps[i].dim.empty()) out += a.steps[i].dim + syn_.dim;
      out += coord(a.steps[i].coord, false);
    }
    return out;
  }

 private:
  const Syntax& syn_;
  const Context& ctx_;
  bool spaced_;  // a space after every level token: keeps an alias from firing on printed text

  std::string name(const std::string& t) const { return syn_.quote + escape_text(t, syn_, false) + syn_.quote; }
  std::string as_name(const std::string& kind, const std::string& text) {
    if (ctx_.advisories) ctx_.advisories->add("coord_printed_as_name", Json{{"kind", kind}, {"text", text}});
    return name(text);
  }
  std::string pred(const std::string& text) const {
    // the plain form (only quotes escaped when they are unbalanced is not enough; use the plain form when the parser reads it back, else escape everything)
    auto plain = [&](const std::string& t) {
      std::string o;
      Lex lx{t, 0, syn_};
      while (lx.i < t.size()) {
        if (lx.starts(syn_.escape)) {
          o += syn_.escape + syn_.escape;
          lx.i += syn_.escape.size();
        } else {
          o += t[lx.i++];
        }
      }
      return o;
    };
    const std::string candidate = plain(text) + syn_.pred_close;
    Lex back{candidate, 0, syn_};
    auto r = back.predicate();
    if (r && *r == text && back.i == candidate.size()) return syn_.pred_open + plain(text) + syn_.pred_close;
    return syn_.pred_open + escape_text(text, syn_, true) + syn_.pred_close;
  }
  std::string atom(const Json& c) {
    const std::string kind = kind_of(c);
    const Json& v = payload(c);
    if (kind == "int") return std::to_string(v.get<std::int64_t>());
    if (kind == "double") return print_double(v.get<double>());
    if (kind == "name") return name(v.get<std::string>());
    if (kind == "word") return is_identifier(v.get<std::string>()) ? v.get<std::string>() : as_name(kind, v.get<std::string>());
    if (kind == "datetime") {
      const std::string t = v.get<std::string>();
      if (!syn_.datetime_pattern.empty() && std::regex_match(t, syn_.datetime_re)) return t;
      return as_name(kind, t);
    }
    if (kind == "pred") return pred(v.get<std::string>());
    return kind + syn_.quote + escape_text(v.get<std::string>(), syn_, false) + syn_.quote;  // typed literal
  }
  std::string levels(const Json& parts) {
    std::string out;
    bool lookalike = false;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const Json& p = parts[i];
      std::string t;
      if (kind_of(p) == "double" && syn_.level == ".") t = as_name("double", print_double(payload(p).get<double>()));
      else t = atom(p);
      if (i) {
        // A date and time followed by a level token and a digit would read the digit as its fraction of a second: one space apart.
        const bool apart = spaced_ || (kind_of(parts[i - 1]) == "datetime" && !t.empty() && std::isdigit(static_cast<unsigned char>(t[0])));
        out += syn_.level + (apart ? " " : "");
      }
      out += t;
    }
    if (parts.size() == 2 && syn_.level == "." && kind_of(parts[0]) == "int" && kind_of(parts[1]) == "int" && payload(parts[1]).get<std::int64_t>() >= 0)
      lookalike = true;
    if (parts.size() == 1 || lookalike) return syn_.group_open + out + syn_.group_close;
    return out;
  }
  std::string coord(const Json& c, bool) {
    const std::string kind = kind_of(c);
    if (kind == "range") return end(payload(c)[0]) + syn_.range + end(payload(c)[1]);
    if (kind == "levels") return levels(payload(c));
    return atom(c);
  }
  std::string end(const Json& c) { return coord(c, true); }
};

// ── syntaxes ────────────────────────────────────────────────────────────────
Json merge_syntax(const Json& base, const Json& over) {
  Json out = base.is_object() ? base : Json::object();
  for (auto it = over.begin(); it != over.end(); ++it) {
    if (it.key() == "tokens" && it.value().is_object()) {
      Json t = out.value("tokens", Json::object());
      for (auto k = it.value().begin(); k != it.value().end(); ++k) t[k.key()] = k.value();
      out["tokens"] = t;
    } else if (it.key() != "extends") {
      out[it.key()] = it.value();
    }
  }
  return out;
}

Result<Json> effective_syntax(const Json& defs, const std::string& id, int depth) {
  const Json* def = json::find(defs, id);
  if (!def || !def->is_object()) return Error(Errc::NotFound, "address syntax: unknown syntax " + id);
  if (depth > static_cast<int>(defs.size()) + 1) return Error(Errc::InvalidArgument, "address syntax: `extends` cycle at " + id);
  const std::string ext = json::get_string(*def, "extends");
  if (ext.empty()) return *def;
  LOOM_TRY_ASSIGN(auto base, effective_syntax(defs, ext, depth + 1));
  return merge_syntax(base, *def);
}

Result<Syntax> make_syntax(const std::string& id, const Json& e) {
  Syntax s;
  s.id = id;
  s.kind = json::get_string(e, "kind", "preset");
  s.label = e.value("label", Json::object());
  s.why = e.value("why", Json::object());
  const Json tokens = e.value("tokens", Json::object());
  auto tok = [&](const char* key, std::string& into) {
    if (const Json* t = json::find(tokens, key); t && t->is_string() && !t->get<std::string>().empty()) into = t->get<std::string>();
  };
  tok("step", s.step); tok("dim", s.dim); tok("range", s.range); tok("level", s.level); tok("quote", s.quote); tok("escape", s.escape);
  tok("group_open", s.group_open); tok("group_close", s.group_close); tok("pred_open", s.pred_open); tok("pred_close", s.pred_close);
  s.datetime_pattern = json::get_string(e, "datetime_pattern");
  if (!s.datetime_pattern.empty()) {
    try {
      s.datetime_re = std::regex(s.datetime_pattern, std::regex::ECMAScript);
    } catch (const std::regex_error& ex) {
      return Error(Errc::InvalidArgument, "address syntax " + id + ": datetime_pattern: " + ex.what());
    }
  }
  if (const Json* al = json::find(e, "aliases"); al && al->is_array())
    for (const auto& a : *al) s.aliases.push_back(Alias{json::get_string(a, "id"), json::get_string(a, "match"), json::get_string(a, "expands")});
  s.print_with_aliases = json::get_bool(e, "print_with_aliases", false);
  s.examples = e.value("examples", Json::array());
  return s;
}

}  // namespace

// ── Address ─────────────────────────────────────────────────────────────────
Json Address::to_json() const {
  Json list = Json::array();
  for (const auto& s : steps) list.push_back(Json{{"dim", s.dim}, {"coord", s.coord}});
  return Json{{"steps", list}};
}

Result<Address> Address::from_json(const Json& value) {
  const Json* steps = json::find(value, "steps");
  if (!steps || !steps->is_array()) return Error(Errc::InvalidArgument, "address: `steps` must be an array");
  Address out;
  std::size_t n = 0;
  for (const auto& s : *steps) {
    const std::string dim = json::get_string(s, "dim");
    const Json* c = json::find(s, "coord");
    if (!s.is_object() || !c) return Error(Errc::InvalidArgument, "address: step " + std::to_string(n) + " needs `coord`");
    if (!dim.empty() && !is_identifier(dim)) return Error(Errc::InvalidArgument, "address: the dimension `" + dim + "` cannot be written as text (identifier characters only)");
    if (auto m = check_coord(*c, false, false); !m.empty()) return Error(Errc::InvalidArgument, "address: step " + std::to_string(n) + ": " + m);
    out.steps.push_back(Step{dim, *c});
    ++n;
  }
  return out;
}

Address compose(const Address& a, const Address& b) {
  Address out = a;
  out.steps.insert(out.steps.end(), b.steps.begin(), b.steps.end());
  return out;
}

// ── Syntaxes ────────────────────────────────────────────────────────────────
Result<Syntaxes> Syntaxes::from_json(Json pack) {
  if (!pack.is_object() || json::get_string(pack, "schema") != "loom.address_syntax/1")
    return Error(Errc::InvalidArgument, "address syntax: schema must be loom.address_syntax/1");
  const Json* defs = json::find(pack, "syntaxes");
  if (!defs || !defs->is_object()) return Error(Errc::InvalidArgument, "address syntax: `syntaxes` must be an object");
  Syntaxes out;
  out.default_id_ = json::get_string(pack, "default_syntax");
  for (auto it = defs->begin(); it != defs->end(); ++it) {
    LOOM_TRY_ASSIGN(auto eff, effective_syntax(*defs, it.key(), 0));
    LOOM_TRY_ASSIGN(auto syn, make_syntax(it.key(), eff));
    out.list_.push_back(std::move(syn));
  }
  if (out.default_id_.empty() && !out.list_.empty()) out.default_id_ = out.list_.front().id;
  out.pack_ = std::move(pack);
  return out;
}

Result<Syntaxes> Syntaxes::load_file(const std::filesystem::path& file) {
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(file));
  LOOM_TRY_ASSIGN(auto doc, json::parse(bytes));
  return from_json(std::move(doc));
}

const Syntaxes& Syntaxes::builtin() {
  static const Syntaxes syn = [] {
    auto doc = json::parse(kAddressSyntaxPack);
    auto r = doc ? from_json(std::move(*doc)) : Result<Syntaxes>(doc.error());
    if (!r) return Syntaxes{};
    return std::move(*r);
  }();
  return syn;
}

const Syntax& Syntaxes::default_syntax() const {
  static const Syntax fallback = [] {
    Syntax s;
    s.id = "default";
    return s;
  }();
  for (const auto& s : list_)
    if (s.id == default_id_) return s;
  return list_.empty() ? fallback : list_.front();
}

const Syntax& Syntaxes::get(std::string_view id, advisory::Collector* advisories) const {
  for (const auto& s : list_)
    if (s.id == id) return s;
  if (advisories) advisories->add("syntax_unknown", Json{{"syntax", std::string(id)}});
  return default_syntax();
}

std::vector<std::string> Syntaxes::ids() const {
  std::vector<std::string> out;
  for (const auto& s : list_) out.push_back(s.id);
  return out;
}

Result<Address> parse(std::string_view text, const Syntax& syntax, const Context& ctx) {
  const std::string original(text);
  const std::string rewritten = apply_aliases(text, syntax, ctx.advisories);
  Parser p(rewritten, syntax, ctx);
  return p.run(original);
}

std::string print(const Address& address, const Syntax& syntax, const Context& ctx) {
  const std::string plain = Printer(syntax, ctx, false).address(address);
  if (syntax.aliases.empty() || apply_aliases(plain, syntax, nullptr) == plain) return plain;
  // The printed text would be read back through an alias; write it so that it is not.
  const std::string spaced = Printer(syntax, ctx, true).address(address);
  if (apply_aliases(spaced, syntax, nullptr) == spaced) return spaced;
  if (ctx.advisories) ctx.advisories->add("alias_collision", Json{{"address", plain}});
  return plain;
}

}  // namespace loom::address
