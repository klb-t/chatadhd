// Typed item extraction (bilingual PL+EN cue phrases) and supersession /
// contradiction detection (MEGA MASTER §4.9 steps 8-9). Deterministic.
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "archive/archive_internal.h"
#include "loom/util/utf8.h"

namespace loom::archive {

namespace {

struct Cue {
  const char* type;
  const char* phrase;  // lowercase; trailing '*' = word prefix
  double weight;
};

// Policy data: cue phrases per item type (EN + PL).
const Cue kCues[] = {
    // decision
    {"decision", "we decided", 2},
    {"decision", "decided to", 1.5},
    {"decision", "decision:", 2},
    {"decision", "the decision is", 2},
    {"decision", "we chose", 2},
    {"decision", "we choose", 1.5},
    {"decision", "we will use", 1.5},
    {"decision", "we'll use", 1.5},
    {"decision", "let's go with", 2},
    {"decision", "let's use", 1.5},
    {"decision", "going with", 1.5},
    {"decision", "we go with", 2},
    {"decision", "settled on", 2},
    {"decision", "we agreed", 1.5},
    {"decision", "agreed to", 1.5},
    {"decision", "switched to", 1.5},
    {"decision", "we switch to", 1.5},
    {"decision", "we adopt", 1.5},
    {"decision", "we keep", 1},
    {"decision", "chosen", 1},
    {"decision", "zdecydowa*", 2},
    {"decision", "postanowi*", 2},
    {"decision", "decyzja", 1.5},
    {"decision", "decyzję", 1.5},
    {"decision", "wybieramy", 2},
    {"decision", "wybraliśmy", 2},
    {"decision", "wybrałem", 1.5},
    {"decision", "idziemy w", 2},
    {"decision", "zostajemy przy", 2},
    {"decision", "przechodzimy na", 2},
    {"decision", "stawiamy na", 2},
    {"decision", "ustaliliśmy", 2},
    {"decision", "będziemy używać", 1.5},
    {"decision", "używamy", 1},
    {"decision", "definiujemy", 1},
    {"decision", "budujemy", 1},
    // rejected option
    {"rejected_option", "rejected", 2},
    {"rejected_option", "we reject", 2},
    {"rejected_option", "decided against", 2.5},
    {"rejected_option", "ruled out", 2},
    {"rejected_option", "we won't", 1.5},
    {"rejected_option", "we will not", 1.5},
    {"rejected_option", "not going to", 1.5},
    {"rejected_option", "we drop", 2},
    {"rejected_option", "dropped", 1.5},
    {"rejected_option", "abandon*", 1.5},
    {"rejected_option", "no longer", 1},
    {"rejected_option", "instead of", 1},
    {"rejected_option", "rather than", 1},
    {"rejected_option", "don't use", 1.5},
    {"rejected_option", "do not use", 1.5},
    {"rejected_option", "discard*", 1.5},
    {"rejected_option", "not worth", 1},
    {"rejected_option", "odrzuca*", 2},
    {"rejected_option", "odrzucon*", 2},
    {"rejected_option", "rezygnuj*", 2},
    {"rejected_option", "porzuca*", 2},
    {"rejected_option", "porzucon*", 1.5},
    {"rejected_option", "zamiast", 1},
    {"rejected_option", "nie używamy", 1.5},
    {"rejected_option", "nie będziemy", 1.5},
    {"rejected_option", "nie wybieramy", 2},
    {"rejected_option", "odpada", 1.5},
    {"rejected_option", "nie jako", 1.5},
    {"rejected_option", "nie budujemy", 2},
    // open question
    {"open_question", "open question", 2.5},
    {"open_question", "open decision", 2},
    {"open_question", "open issue", 2},
    {"open_question", "tbd", 2},
    {"open_question", "to be decided", 2},
    {"open_question", "to be determined", 2},
    {"open_question", "unresolved", 2},
    {"open_question", "undecided", 2},
    {"open_question", "not yet decided", 2},
    {"open_question", "we need to decide", 2},
    {"open_question", "unclear", 1},
    {"open_question", "should we", 1.5},
    {"open_question", "do ustalenia", 2},
    {"open_question", "do decyzji", 2},
    {"open_question", "nie wiadomo", 1.5},
    {"open_question", "nierozstrzygnięt*", 2},
    {"open_question", "nie zamknięte", 2},
    {"open_question", "otwarte pytanie", 2.5},
    {"open_question", "otwarta kwestia", 2.5},
    {"open_question", "pytanie", 1},
    // requirement
    {"requirement", "must", 1.5},
    {"requirement", "should", 1},
    {"requirement", "needs to", 1.5},
    {"requirement", "need to", 1},
    {"requirement", "has to", 1.5},
    {"requirement", "have to", 1},
    {"requirement", "required", 1.5},
    {"requirement", "requirement", 2},
    {"requirement", "shall", 1.5},
    {"requirement", "musi", 1.5},
    {"requirement", "muszą", 1.5},
    {"requirement", "musimy", 1.5},
    {"requirement", "powinien", 1},
    {"requirement", "powinna", 1},
    {"requirement", "powinno", 1},
    {"requirement", "powinny", 1},
    {"requirement", "wymaga", 1.5},
    {"requirement", "wymóg", 2},
    {"requirement", "wymagani*", 1.5},
    {"requirement", "trzeba", 1},
    {"requirement", "należy", 1},
    {"requirement", "ma mieć", 1.5},
    {"requirement", "mają mieć", 1.5},
    {"requirement", "ma być", 1},
    {"requirement", "mają być", 1},
    {"requirement", "potrzebujemy", 1},
    // invariant
    {"invariant", "invariant", 3},
    {"invariant", "never", 1.5},
    {"invariant", "always", 1},
    {"invariant", "must not", 2},
    {"invariant", "must never", 2.5},
    {"invariant", "immutable", 2},
    {"invariant", "append-only", 2},
    {"invariant", "append only", 2},
    {"invariant", "hard contract", 2},
    {"invariant", "guarantee*", 1},
    {"invariant", "nigdy", 1.5},
    {"invariant", "zawsze", 1},
    {"invariant", "nie wolno", 2},
    {"invariant", "niezmienn*", 2},
    {"invariant", "inwariant*", 3},
    {"invariant", "nie może", 1.5},
    {"invariant", "nie mogą", 1.5},
    // idea
    {"idea", "idea", 1.5},
    {"idea", "what if", 2},
    {"idea", "we could", 1.5},
    {"idea", "maybe", 1},
    {"idea", "perhaps", 1},
    {"idea", "consider", 1},
    {"idea", "proposal", 1.5},
    {"idea", "propose", 1.5},
    {"idea", "it would be nice", 2},
    {"idea", "would be good", 1.5},
    {"idea", "suggest*", 1},
    {"idea", "in the future", 1},
    {"idea", "later", 0.5},
    {"idea", "pomysł", 2},
    {"idea", "pomysły", 2},
    {"idea", "można by", 2},
    {"idea", "warto", 1.5},
    {"idea", "propozycja", 1.5},
    {"idea", "proponuję", 1.5},
    {"idea", "dobrze byłoby", 2},
    {"idea", "fajnie by", 2},
    {"idea", "w przyszłości", 1},
    {"idea", "kiedyś", 1},
    {"idea", "później", 0.5},
    {"idea", "może być", 1},
    // rationale
    {"rationale", "because", 1.5},
    {"rationale", "so that", 1.5},
    {"rationale", "the reason", 2},
    {"rationale", "reason:", 2},
    {"rationale", "rationale", 2.5},
    {"rationale", "in order to", 1},
    {"rationale", "that's why", 1.5},
    {"rationale", "this avoids", 1.5},
    {"rationale", "this allows", 1},
    {"rationale", "to avoid", 1},
    {"rationale", "why:", 1.5},
    {"rationale", "ponieważ", 1.5},
    {"rationale", "bo", 1},
    {"rationale", "dlatego", 1.5},
    {"rationale", "uzasadnienie", 2.5},
    {"rationale", "powód", 2},
    {"rationale", "dzięki temu", 1.5},
    {"rationale", "co pozwala", 1.5},
    {"rationale", "to pozwala", 1.5},
    // implementation
    {"implementation", "implemented", 2},
    {"implementation", "we added", 1.5},
    {"implementation", "added", 1},
    {"implementation", "now supports", 1.5},
    {"implementation", "refactor*", 1.5},
    {"implementation", "ported", 1.5},
    {"implementation", "port of", 1.5},
    {"implementation", "landed", 1},
    {"implementation", "merged", 1},
    {"implementation", "works now", 1.5},
    {"implementation", "zaimplementowa*", 2},
    {"implementation", "dodałem", 1.5},
    {"implementation", "dodano", 1.5},
    {"implementation", "dodaliśmy", 1.5},
    {"implementation", "działa już", 1.5},
    {"implementation", "zrobione", 1.5},
    // bug
    {"bug", "bug", 2},
    {"bug", "crash*", 2},
    {"bug", "fails", 1},
    {"bug", "failing", 1},
    {"bug", "broken", 1.5},
    {"bug", "regression", 2},
    {"bug", "doesn't work", 2},
    {"bug", "does not work", 2},
    {"bug", "leak", 1.5},
    {"bug", "race condition", 2},
    {"bug", "deadlock", 2},
    {"bug", "segfault", 2},
    {"bug", "błąd", 2},
    {"bug", "błędy", 1.5},
    {"bug", "nie działa", 2},
    {"bug", "wysypuje", 2},
    {"bug", "crashuje", 2},
    {"bug", "zawiesza", 1.5},
};

struct HeadingHint {
  const char* phrase;
  const char* type;
  double weight;
};
const HeadingHint kHeadingHints[] = {
    {"open decision", "open_question", 2.5},     {"otwarte decyzje", "open_question", 2.5},
    {"open question", "open_question", 2.5},     {"otwarte pytania", "open_question", 2.5},
    {"unresolved", "open_question", 2},          {"do ustalenia", "open_question", 2},
    {"nie zamknięte", "open_question", 2},       {"rejected", "rejected_option", 2},
    {"odrzucone", "rejected_option", 2},         {"alternatives", "rejected_option", 1},
    {"invariant", "invariant", 2},               {"inwariant", "invariant", 2},
    {"konstytucja", "invariant", 1.5},           {"nie wolno zgubić", "requirement", 2},
    {"must not lose", "requirement", 2},         {"requirement", "requirement", 2},
    {"wymagania", "requirement", 2},             {"decision", "decision", 1.5},
    {"decyzj", "decision", 1.5},                 {"idea", "idea", 1.5},
    {"pomysł", "idea", 1.5},                     {"known issue", "bug", 1.5},
    {"bugs", "bug", 1.5},                        {"rationale", "rationale", 1.5},
    {"uzasadnienie", "rationale", 1.5},          {"zasady", "requirement", 1},
};

// Tie-break priority (lower index wins).
int type_rank(std::string_view t) {
  static const char* kOrder[] = {"invariant", "decision",       "rejected_option", "open_question", "requirement",
                                 "bug",       "implementation", "rationale",       "idea"};
  for (int i = 0; i < 9; ++i) {
    if (t == kOrder[i]) return i;
  }
  return 99;
}

bool boundary_before(std::string_view s, std::size_t p) {
  if (p == 0) return true;
  unsigned char c = static_cast<unsigned char>(s[p - 1]);
  if (c >= 0x80) return false;  // inside a non-ASCII letter
  return !(std::isalnum(c) || c == '_');
}
bool boundary_after(std::string_view s, std::size_t p) {
  if (p >= s.size()) return true;
  unsigned char c = static_cast<unsigned char>(s[p]);
  if (c >= 0x80) return false;
  return !(std::isalnum(c) || c == '_');
}

// Finds `phrase` in lowercase `s` at word boundaries ('*' suffix = prefix).
bool has_phrase(std::string_view s, std::string_view phrase) {
  bool prefix = !phrase.empty() && phrase.back() == '*';
  if (prefix) phrase.remove_suffix(1);
  bool punct_end = !phrase.empty() && !std::isalnum(static_cast<unsigned char>(phrase.back())) &&
                   static_cast<unsigned char>(phrase.back()) < 0x80;
  std::size_t p = 0;
  while ((p = s.find(phrase, p)) != std::string_view::npos) {
    bool ok = boundary_before(s, p) && (prefix || punct_end || boundary_after(s, p + phrase.size()));
    if (ok) return true;
    ++p;
  }
  return false;
}

const std::unordered_set<std::string>& negators() {
  static const std::unordered_set<std::string> k = {
      "not",     "no",       "never",    "don",      "won",       "without", "against",   "drop",
      "drops",   "dropped",  "dropping", "reject",   "rejected",  "rejects", "rejecting", "abandon",
      "abandoned", "remove", "removed",  "instead",  "replace",   "replaced", "nie",      "bez",
      "zamiast", "rezygnujemy", "rezygnuję", "odrzucamy", "odrzucone", "odrzucony", "porzucamy",
      "porzucone", "nigdy", "stop",    "stopped",  "ditch",     "ditched", "deprecated", "deprecate"};
  return k;
}

// Tokens that never define an item's subject.
const std::unordered_set<std::string>& generic_subject_words() {
  static const std::unordered_set<std::string> k = {
      "decided", "decision", "decide", "chose", "choose", "chosen", "going", "agreed", "switch", "switched",
      "adopt", "rejected", "reject", "dropped", "drop", "abandon", "abandoned", "question", "open", "unclear",
      "required", "requirement", "invariant", "idea", "maybe", "perhaps", "consider", "proposal", "propose",
      "reason", "rationale", "implemented", "added", "bug", "system", "thing", "zdecydowaliśmy", "decyzja",
      "decyzję", "wybieramy", "wybraliśmy", "odrzucamy", "rezygnujemy", "zamiast", "pomysł", "warto",
      "pytanie", "ponieważ", "dlatego", "musi", "powinien", "trzeba", "należy", "otwarte", "używamy", "będziemy",
      "używać", "kept", "keep", "later", "because", "should", "could", "would", "will", "instead", "rather",
      "support", "supports", "want", "wanted", "need", "needs", "make", "made", "new", "also", "still"};
  return k;
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }

}  // namespace

Classification classify_sentence(std::string_view sentence, std::string_view heading) {
  Classification c;
  std::string s = utf8::to_lower(sentence);
  std::string h = utf8::to_lower(heading);
  std::map<std::string, double> score;
  std::map<std::string, std::vector<std::string>> cues;
  for (const Cue& cue : kCues) {
    if (has_phrase(s, cue.phrase)) {
      score[cue.type] += cue.weight;
      std::string p = cue.phrase;
      if (!p.empty() && p.back() == '*') p.pop_back();
      cues[cue.type].push_back(p);
    }
  }
  // "must not" also matches "must": keep it an invariant, not a requirement.
  if (has_phrase(s, "must not") || has_phrase(s, "must never")) score["requirement"] -= 1.5;
  std::string_view trimmed = utf8::rstrip(sentence);
  while (!trimmed.empty() && (trimmed.back() == ')' || trimmed.back() == '*' || trimmed.back() == '"')) {
    trimmed.remove_suffix(1);
  }
  if (!trimmed.empty() && trimmed.back() == '?') {
    score["open_question"] += 2.5;
    cues["open_question"].push_back("?");
  }
  if (s.rfind("czy ", 0) == 0) {
    score["open_question"] += 1.0;
    cues["open_question"].push_back("czy");
  }
  bool list_heading = false;  // an explicit list heading ("10. Otwarte decyzje")
  double sentence_best = 0;
  for (const auto& [t, v] : score) sentence_best = std::max(sentence_best, v);
  if (!h.empty()) {
    // Only the innermost heading counts, and a hint must lead it ("Decisions",
    // "10. Otwarte decyzje", "Invariants") - a heading that merely mentions a
    // word ("Next invariant implementation target") is not a list of that type.
    std::string last = h.substr(h.rfind("›") == std::string::npos ? 0 : h.rfind("›") + 3);
    std::size_t k = 0;
    while (k < last.size() && (std::isdigit(static_cast<unsigned char>(last[k])) || last[k] == '.' || last[k] == ' ' ||
                               last[k] == '#' || last[k] == ')')) {
      ++k;
    }
    std::string_view lead(last);
    lead.remove_prefix(k);
    for (const HeadingHint& hh : kHeadingHints) {
      std::string_view ph(hh.phrase);
      bool anywhere = ph == "otwarte decyzje" || ph == "open question" || ph == "open decision" ||
                      ph == "nie wolno zgubić" || ph == "must not lose" || ph == "otwarte pytania";
      bool hit = anywhere ? lead.find(ph) != std::string_view::npos : lead.substr(0, ph.size()) == ph;
      if (hit) {
        list_heading = list_heading || anywhere;
        score[hh.type] += hh.weight;
        cues[hh.type].push_back(std::string("§") + hh.phrase);
      }
    }
  }
  std::string best;
  double best_score = 0;
  double second = 0;
  for (const auto& [t, v] : score) {
    if (v > best_score || (v == best_score && v > 0 && type_rank(t) < type_rank(best))) {
      second = std::max(second, best_score);
      best = t;
      best_score = v;
    } else {
      second = std::max(second, v);
    }
  }
  // polarity: sentence-level rejection / negation
  for (const auto& t : tokenize(s)) {
    if (negators().count(t)) {
      c.polarity = -1;
      break;
    }
  }
  if (best.empty() || best_score < 1.5) return c;
  // A heading alone classifies only full statements, not noun-phrase bullets.
  if (sentence_best < 1.0 && tokenize(s).size() < (list_heading ? 3u : 5u)) return c;
  c.type = best;
  double conf = 0.35 + 0.15 * best_score;
  if (best_score - second < 0.5) conf -= 0.1;
  c.confidence = round2(std::clamp(conf, 0.3, 0.95));
  c.cues = cues[best];
  return c;
}

Json Item::to_json() const {
  Json pol = Json::object();
  for (const auto& [t, p] : term_polarity) {
    if (p < 0) pol[t] = p;
  }
  return Json{{"id", id},         {"type", type},         {"text", text},     {"doc", doc}, {"unit", unit},
              {"theme", theme},   {"date", date},         {"confidence", confidence},
              {"cues", cues},     {"subject", subject},   {"negated", pol},   {"status", status}};
}

Item Item::from_json(const Json& j) {
  Item it;
  it.id = json::get_string(j, "id");
  it.type = json::get_string(j, "type");
  it.text = json::get_string(j, "text");
  it.doc = json::get_string(j, "doc");
  it.unit = json::get_string(j, "unit");
  it.theme = json::get_string(j, "theme");
  it.date = json::get_string(j, "date");
  it.confidence = json::get_number(j, "confidence");
  it.status = json::get_string(j, "status", "active");
  if (const Json* a = json::find(j, "cues"); a && a->is_array()) {
    for (const auto& x : *a) it.cues.push_back(x.get<std::string>());
  }
  if (const Json* a = json::find(j, "subject"); a && a->is_array()) {
    for (const auto& x : *a) {
      it.subject.push_back(x.get<std::string>());
      it.term_polarity[x.get<std::string>()] = 1;
    }
  }
  if (const Json* o = json::find(j, "negated"); o && o->is_object()) {
    for (auto p = o->begin(); p != o->end(); ++p) it.term_polarity[p.key()] = -1;
  }
  return it;
}

namespace {

void fill_subject(Item& it, std::string_view sentence) {
  auto toks = tokenize(sentence);
  std::set<std::string> subj;
  const auto& neg = negators();
  for (std::size_t p = 0; p < toks.size(); ++p) {
    const std::string& t = toks[p];
    if (utf8::length(t) < 3 || is_stopword(t) || generic_subject_words().count(t) || neg.count(t)) continue;
    std::string st = stem(t);
    int pol = 1;
    for (std::size_t back = 1; back <= 3 && back <= p; ++back) {
      if (neg.count(toks[p - back])) {
        pol = -1;
        break;
      }
    }
    subj.insert(st);
    auto [iter, inserted] = it.term_polarity.emplace(st, pol);
    if (!inserted && pol < 0) iter->second = -1;
  }
  it.subject.assign(subj.begin(), subj.end());
}

std::string doc_heading(const Doc& d) {
  if (const Json* h = json::find(d.extra, "heading"); h && h->is_string()) return h->get<std::string>();
  return "";
}

}  // namespace

std::vector<Item> extract_items(const Doc& doc, std::string_view theme) {
  std::vector<Item> out;
  auto make = [&](std::string type, std::string_view text, double conf, std::vector<std::string> cues, int idx) {
    Item it;
    it.type = std::move(type);
    it.text = clip(text, 320);
    it.doc = doc.key;
    it.unit = doc.unit;
    it.theme = std::string(theme);
    it.date = doc.date;
    it.confidence = conf;
    it.cues = std::move(cues);
    it.id = "i_" + hash_prefix(doc.key + "#" + std::to_string(idx) + "#" + it.text, 10);
    fill_subject(it, text);
    return it;
  };
  if (doc.kind == "code") {
    if (const Json* todos = json::find(doc.extra, "todos"); todos && todos->is_array()) {
      int idx = 0;
      for (const auto& t : *todos) {
        std::string text = json::get_string(t, "text");
        bool fix = text.rfind("FIXME", 0) == 0 || text.rfind("XXX", 0) == 0 || text.rfind("HACK", 0) == 0;
        out.push_back(make(fix ? "bug" : "requirement", text, 0.6, {fix ? "fixme" : "todo"}, idx++));
      }
    }
    return out;
  }
  if (doc.kind == "commit") {
    std::string subject = json::get_string(doc.extra, "subject", doc.label);
    std::string low = utf8::to_lower(subject);
    bool fix = has_phrase(low, "fix") || has_phrase(low, "bug") || has_phrase(low, "crash*") ||
               has_phrase(low, "regression");
    out.push_back(make(fix ? "bug" : "implementation", subject, 0.9, {"commit"}, 0));
    return out;
  }
  std::string heading = doc_heading(doc);
  int idx = 0;
  for (const auto& sent : split_sentences(doc.text)) {
    ++idx;
    if (sent.size() > 1200) continue;
    // list intros ("Each decision is one of:") and bare noun-phrase bullets
    std::string_view tail = utf8::rstrip(sent);
    if (!tail.empty() && tail.back() == ':') continue;
    if (tokenize(sent).size() < 3) continue;
    Classification c = classify_sentence(sent, heading);
    if (c.type.empty()) continue;
    out.push_back(make(c.type, sent, c.confidence, c.cues, idx));
  }
  return out;
}

Json ItemEdge::to_json() const { return Json{{"src", src}, {"dst", dst}, {"type", type}, {"reason", reason}}; }

std::vector<ItemEdge> relate_items(std::vector<Item>& items, const CorpusStats* stats) {
  std::vector<ItemEdge> edges;
  const double n = stats ? static_cast<double>(std::max<std::size_t>(stats->n, 1)) : 100.0;
  auto idf = [&](const std::string& t) {
    if (!stats) return 1.0;
    auto it = stats->df.find(t);
    if (it == stats->df.end()) it = stats->df.find(t + "s");
    double df = it == stats->df.end() ? 1.0 : it->second;
    return std::log((n + 1.0) / (df + 1.0)) + 0.1;
  };
  auto distinctive = [&](const std::string& t) {
    if (!stats) return true;
    auto it = stats->df.find(t);
    if (it == stats->df.end()) it = stats->df.find(t + "s");
    return it == stats->df.end() || it->second <= std::max(3.0, 0.3 * n);
  };
  auto in_s = [](const std::string& t) {
    return t == "decision" || t == "rejected_option" || t == "requirement" || t == "invariant";
  };

  // order: by date (undated last), then id
  std::vector<std::size_t> order(items.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const auto& A = items[a];
    const auto& B = items[b];
    std::string da = A.date.empty() ? "9999" : A.date;
    std::string db = B.date.empty() ? "9999" : B.date;
    if (da != db) return da < db;
    return A.id < B.id;
  });
  std::vector<std::size_t> rank(items.size());
  for (std::size_t r = 0; r < order.size(); ++r) rank[order[r]] = r;

  std::map<std::string, std::vector<std::size_t>> inv;  // distinctive subject token -> items
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    if (!in_s(it.type) && it.type != "open_question") continue;
    for (const auto& t : it.subject) {
      if (distinctive(t)) inv[t].push_back(i);
    }
  }
  std::set<std::pair<std::size_t, std::size_t>> seen;
  for (const auto& [tok, list] : inv) {
    if (list.size() > 200) continue;
    for (std::size_t x = 0; x < list.size(); ++x) {
      for (std::size_t y = x + 1; y < list.size(); ++y) {
        std::size_t a = list[x], b = list[y];
        if (rank[a] > rank[b]) std::swap(a, b);  // a earlier
        if (!seen.insert({a, b}).second) continue;
        Item& A = items[a];
        Item& B = items[b];
        if (A.doc == B.doc) continue;
        double wa = 0, wb = 0, shared = 0;
        std::vector<std::string> conflict;
        for (const auto& t : A.subject) wa += idf(t);
        for (const auto& t : B.subject) wb += idf(t);
        for (const auto& t : A.subject) {
          if (!std::binary_search(B.subject.begin(), B.subject.end(), t)) continue;
          shared += idf(t);
          if (distinctive(t) && A.term_polarity[t] != B.term_polarity[t]) conflict.push_back(t);
        }
        double sim = shared / std::max(1e-9, std::min(wa, wb));
        std::string da = date_only(A.date), db = date_only(B.date);
        bool later = !da.empty() && !db.empty() && da < db;
        bool same_unit = !A.unit.empty() && A.unit == B.unit;
        bool supersede = later && (B.type == "decision" || B.type == "rejected_option") && sim >= 0.3;
        bool contradict = !later && !same_unit && sim >= 0.5;
        if (in_s(A.type) && in_s(B.type) && !conflict.empty() && (supersede || contradict)) {
          std::string terms;
          for (const auto& t : conflict) terms += (terms.empty() ? "'" : ", '") + t + "'";
          if (supersede) {
            edges.push_back({B.id, A.id, "supersedes",
                             "later " + B.type + " (" + db + ") reverses " + A.type + " (" + da + ") on " + terms});
            A.status = "superseded";
          } else {
            edges.push_back({B.id, A.id, "contradicts", "opposite polarity on " + terms});
            if (A.status == "active") A.status = "contested";
            if (B.status == "active") B.status = "contested";
          }
        } else if (A.type == "open_question" && B.type == "decision" && later && sim >= 0.35) {
          edges.push_back({B.id, A.id, "resolves", "decision (" + db + ") answers question (" + da + ")"});
          if (A.status == "active") A.status = "resolved";
        }
      }
    }
  }
  std::sort(edges.begin(), edges.end(), [](const ItemEdge& x, const ItemEdge& y) {
    return std::tie(x.type, x.src, x.dst) < std::tie(y.type, y.src, y.dst);
  });
  return edges;
}

}  // namespace loom::archive
