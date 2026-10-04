// Typed item extraction (bilingual PL+EN cue phrases) and supersession /
// contradiction detection (MEGA MASTER §4.9 steps 8-9). Deterministic.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "archive/archive_internal.h"
#include "archive/profile.h"
#include "loom/util/utf8.h"

namespace loom::archive {

namespace {

// Tie-break priority (lower index wins).
int type_rank(std::string_view t, const ArchiveProfile& policy) {
  const auto& order = policy.items().at("type_order");
  for (std::size_t i = 0; i < order.size(); ++i) if (order[i].get_ref<const std::string&>() == t) return static_cast<int>(i);
  return static_cast<int>(order.size());
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

bool in_lexicon(const ArchiveProfile& policy, std::string_view name, std::string_view word) {
  for (const auto& item : policy.items().at(std::string(name))) if (item.get_ref<const std::string&>() == word) return true;
  return false;
}

double encode_confidence(double value, const ArchiveProfile& policy) {
  const auto multiplier = policy.number("/text_item_closure/confidence_multiplier");
  if (multiplier == 0) return value;
  const auto scaled = value * multiplier;
  if (std::isfinite(value) && !std::isfinite(scaled))
    throw std::invalid_argument("archive confidence encoder exceeds numeric representation");
  return std::round(scaled) / multiplier;
}

}  // namespace

Classification classify_sentence(std::string_view sentence, std::string_view heading, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  profile = scope.ptr();
  Classification c;
  std::string s = utf8::to_lower(sentence);
  std::string h = utf8::to_lower(heading);
  std::map<std::string, double> score;
  std::map<std::string, std::vector<std::string>> cues;
  for (const auto& cue : policy.items().at("cues")) {
    const std::string phrase = cue.at("phrase").get<std::string>();
    const std::string type = cue.at("type").get<std::string>();
    if (has_phrase(s, phrase)) {
      score[type] += cue.at("weight").get<double>();
      std::string p = phrase;
      if (!p.empty() && p.back() == '*') p.pop_back();
      cues[type].push_back(p);
    }
  }
  // "must not" also matches "must": keep it an invariant, not a requirement.
  bool suppress = false;
  for (const auto& phrase : policy.value("/items/requirement_penalty_phrases")) suppress = suppress || has_phrase(s, phrase.get_ref<const std::string&>());
  if (suppress) score[policy.text("/text_item_closure/penalty_target_type")] -= policy.number("/items/requirement_penalty");
  std::string_view trimmed = utf8::rstrip(sentence);
  bool removed = true;
  while (!trimmed.empty() && removed) {
    removed = false;
    for (const auto& closer : policy.value("/text_item_closure/question_closing_suffixes")) {
      const auto& suffix = closer.get_ref<const std::string&>();
      if (trimmed.ends_with(suffix)) { trimmed.remove_suffix(suffix.size()); removed = true; break; }
    }
  }
  const auto question_type = policy.text("/text_item_closure/question_target_type");
  const auto question_suffix = policy.text("/items/question_suffix");
  if (!question_suffix.empty() && trimmed.size() >= question_suffix.size() && trimmed.substr(trimmed.size() - question_suffix.size()) == question_suffix) {
    score[question_type] += policy.number("/items/question_suffix_score");
    cues[question_type].push_back(policy.text("/items/question_suffix"));
  }
  if (s.rfind(policy.text("/items/question_prefix"), 0) == 0) {
    score[question_type] += policy.number("/items/question_prefix_score");
    cues[question_type].push_back(policy.text("/items/question_prefix_cue"));
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
    for (const auto& hh : policy.items().at("heading_hints")) {
      const std::string phrase = hh.at("phrase").get<std::string>();
      const std::string type = hh.at("type").get<std::string>();
      std::string_view ph(phrase);
      bool anywhere = policy.contains("/items/contains_headings", ph);
      bool hit = anywhere ? lead.find(ph) != std::string_view::npos : lead.substr(0, ph.size()) == ph;
      if (hit) {
        list_heading = list_heading || anywhere;
        score[type] += hh.at("weight").get<double>();
        cues[type].push_back(std::string("§") + phrase);
      }
    }
  }
  std::string best;
  double best_score = 0;
  double second = 0;
  for (const auto& [t, v] : score) {
    if (v > best_score || (v == best_score && v > 0 && type_rank(t, policy) < type_rank(best, policy))) {
      second = std::max(second, best_score);
      best = t;
      best_score = v;
    } else {
      second = std::max(second, v);
    }
  }
  // polarity: sentence-level rejection / negation
  for (const auto& t : tokenize(s)) {
    if (in_lexicon(policy, "negators", t)) {
      c.polarity = -1;
      break;
    }
  }
  if (best.empty() || best_score < policy.number("/items/min_score")) return c;
  // A heading alone classifies only full statements, not noun-phrase bullets.
  if (sentence_best < policy.number("/items/min_sentence_score") && tokenize(s).size() < static_cast<std::size_t>(policy.integer(list_heading ? "/items/min_list_tokens" : "/items/min_heading_tokens"))) return c;
  c.type = best;
  double conf = policy.number("/items/confidence_base") + policy.number("/items/confidence_slope") * best_score;
  if (best_score - second < policy.number("/items/ambiguity_margin")) conf -= policy.number("/items/ambiguity_penalty");
  c.confidence = encode_confidence(std::clamp(conf, policy.number("/items/confidence_min"), policy.number("/items/confidence_max")), policy);
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

void fill_subject(Item& it, std::string_view sentence, const ArchiveProfile& policy) {
  auto toks = tokenize(sentence);
  std::set<std::string> subj;
  auto neg = [&](std::string_view word) { return in_lexicon(policy, "negators", word); };
  for (std::size_t p = 0; p < toks.size(); ++p) {
    const std::string& t = toks[p];
    if (utf8::length(t) < static_cast<std::size_t>(policy.integer("/items/subject_min_token_codepoints")) || is_stopword(t, &policy) || in_lexicon(policy, "generic_subject_words", t) || neg(t)) continue;
    std::string st = stem(t, &policy);
    int pol = 1;
    for (std::size_t back = 1; back <= static_cast<std::size_t>(policy.integer("/items/negation_lookback")) && back <= p; ++back) {
      if (neg(toks[p - back])) {
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

std::vector<Item> extract_items(const Doc& doc, std::string_view theme, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  profile = scope.ptr();
  std::vector<Item> out;
  auto make = [&](std::string type, std::string_view text, double conf, std::vector<std::string> cues, int idx) {
    Item it;
    it.type = std::move(type);
    it.text = clip(text, static_cast<std::size_t>(policy.integer("/items/text_max_codepoints")));
    it.doc = doc.key;
    it.unit = doc.unit;
    it.theme = std::string(theme);
    it.date = doc.date;
    it.confidence = conf;
    it.cues = std::move(cues);
    it.id = "i_" + hash_prefix(doc.key + "#" + std::to_string(idx) + "#" + it.text, 10);
    fill_subject(it, text, policy);
    return it;
  };
  if (policy.contains("/text_item_closure/todo_source_kinds", doc.kind)) {
    if (const Json* todos = json::find(doc.extra, "todos"); todos && todos->is_array()) {
      int idx = 0;
      for (const auto& t : *todos) {
        std::string text = json::get_string(t, "text");
        bool fix = false;
        for (const auto& marker : policy.value("/items/bug_markers")) fix = fix || text.rfind(marker.get_ref<const std::string&>(), 0) == 0;
        const auto& binding = policy.value(fix ? "/text_item_closure/todo_bug_binding" : "/text_item_closure/todo_default_binding");
        out.push_back(make(binding.at("type").get<std::string>(), text, policy.number("/items/todo_confidence"),
                           {binding.at("cue").get<std::string>()}, idx++));
      }
    }
    return out;
  }
  if (policy.contains("/text_item_closure/commit_source_kinds", doc.kind)) {
    std::string subject = json::get_string(doc.extra, "subject", doc.label);
    std::string low = utf8::to_lower(subject);
    bool fix = false;
    for (const auto& phrase : policy.value("/items/commit_bug_phrases")) fix = fix || has_phrase(low, phrase.get_ref<const std::string&>());
    const auto& binding = policy.value(fix ? "/text_item_closure/commit_bug_binding" : "/text_item_closure/commit_default_binding");
    out.push_back(make(binding.at("type").get<std::string>(), subject, policy.number("/items/commit_confidence"),
                       {binding.at("cue").get<std::string>()}, 0));
    return out;
  }
  std::string heading = doc_heading(doc);
  int idx = 0;
  for (const auto& sent : split_sentences(doc.text, profile)) {
    ++idx;
    if (sent.size() > static_cast<std::size_t>(policy.integer("/items/sentence_max_bytes"))) continue;
    // list intros ("Each decision is one of:") and bare noun-phrase bullets
    std::string_view tail = utf8::rstrip(sent);
    bool introduction = false;
    for (const auto& suffix : policy.value("/text_item_closure/list_intro_suffixes"))
      introduction = introduction || tail.ends_with(suffix.get_ref<const std::string&>());
    if (introduction) continue;
    if (tokenize(sent).size() < static_cast<std::size_t>(policy.integer("/items/sentence_min_tokens"))) continue;
    Classification c = classify_sentence(sent, heading, profile);
    if (c.type.empty()) continue;
    out.push_back(make(c.type, sent, c.confidence, c.cues, idx));
  }
  return out;
}

Json ItemEdge::to_json() const { return Json{{"src", src}, {"dst", dst}, {"type", type}, {"reason", reason}}; }

std::vector<ItemEdge> relate_items(std::vector<Item>& items, const CorpusStats* stats, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  std::vector<ItemEdge> edges;
  const double n = stats ? static_cast<double>(std::max<std::size_t>(stats->n, 1)) : 100.0;
  auto idf = [&](const std::string& t) {
    if (!stats) return 1.0;
    auto it = stats->df.find(t);
    if (it == stats->df.end()) it = stats->df.find(t + "s");
    double df = it == stats->df.end() ? 1.0 : it->second;
    return std::log((n + 1.0) / (df + 1.0)) + policy.number("/items/idf_offset");
  };
  auto distinctive = [&](const std::string& t) {
    if (!stats) return true;
    auto it = stats->df.find(t);
    if (it == stats->df.end()) it = stats->df.find(t + "s");
    return it == stats->df.end() || it->second <= std::max(policy.number("/items/distinctive_df_min"), policy.number("/items/distinctive_df_fraction") * n);
  };
  auto in_s = [&](const std::string& t) { return policy.contains("/items/relation_types", t); };
  auto question_role = [&](const std::string& t) { return policy.contains("/text_item_closure/question_role_types", t); };
  auto answer_role = [&](const std::string& t) { return policy.contains("/text_item_closure/answer_role_types", t); };

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
    if (!in_s(it.type) && !question_role(it.type) && !answer_role(it.type)) continue;
    for (const auto& t : it.subject) {
      if (distinctive(t)) inv[t].push_back(i);
    }
  }
  std::set<std::pair<std::size_t, std::size_t>> seen;
  for (const auto& [tok, list] : inv) {
    if (list.size() > static_cast<std::size_t>(policy.integer("/items/max_shared_token_items"))) continue;
    for (std::size_t x = 0; x < list.size(); ++x) {
      for (std::size_t y = x + 1; y < list.size(); ++y) {
        std::size_t a = list[x], b = list[y];
        if (rank[a] > rank[b]) std::swap(a, b);  // a earlier
        if (!seen.insert({a, b}).second) continue;
        Item& A = items[a];
        Item& B = items[b];
        if (A.doc == B.doc) continue;
        double wa = 0, wb = 0, shared = 0;
        int shared_distinct = 0;
        std::vector<std::string> conflict;
        for (const auto& t : A.subject) wa += idf(t);
        for (const auto& t : B.subject) wb += idf(t);
        for (const auto& t : A.subject) {
          if (!std::binary_search(B.subject.begin(), B.subject.end(), t)) continue;
          shared += idf(t);
          if (distinctive(t)) ++shared_distinct;
          if (distinctive(t) && A.term_polarity[t] != B.term_polarity[t]) conflict.push_back(t);
        }
        double sim = shared / std::max(1e-9, std::min(wa, wb));
        // one shared word is coincidence, not the same subject
        if (shared_distinct < policy.integer("/items/min_shared_distinctive")) continue;
        // and it must be a real share of the longer item too
        if (shared / std::max(1e-9, std::max(wa, wb)) < policy.number("/items/min_longer_share")) continue;
        std::string da = date_only(A.date), db = date_only(B.date);
        bool later = !da.empty() && !db.empty() && da < db;
        bool same_unit = !A.unit.empty() && A.unit == B.unit;
        bool supersede = later && policy.contains("/text_item_closure/superseding_role_types", B.type) && sim >= policy.number("/items/supersede_threshold");
        bool contradict = !later && !same_unit && sim >= policy.number("/items/contradict_threshold");
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
        } else if (question_role(A.type) && answer_role(B.type) && later && sim >= policy.number("/items/resolution_threshold")) {
          auto reason = render_profile_template(policy.text("/text_item_closure/resolution_reason_template"),
              Json{{"answer_type", B.type}, {"question_type", A.type}, {"answer_date", db}, {"question_date", da}});
          if (!reason) throw std::invalid_argument(reason.error().to_string());
          edges.push_back({B.id, A.id, policy.text("/text_item_closure/resolution_relation"), std::move(*reason)});
          if (A.status == "active") A.status = policy.text("/text_item_closure/resolution_status");
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
