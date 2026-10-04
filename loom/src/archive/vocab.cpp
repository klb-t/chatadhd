// Corpus statistics, salient terms and vocabulary expansion (MEGA MASTER
// §4.9 steps 3-4, §13 "kolejne przebiegi wyszukiwania mają wynikać z
// terminów znalezionych w poprzednich").
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "archive/archive_internal.h"
#include "archive/profile.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::archive {

namespace {
double encode_score(double value, const ArchiveProfile& policy) {
  const auto multiplier = policy.number("/vocab_closure/score_multiplier");
  if (multiplier == 0) return value;
  const auto scaled = value * multiplier;
  if (std::isfinite(value) && !std::isfinite(scaled))
    throw std::invalid_argument("archive score encoder exceeds numeric representation");
  return std::round(scaled) / multiplier;
}

std::string fmt(double v, int prec) {
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(prec);
  os << v;
  return os.str();
}

}  // namespace

std::vector<std::string> camel_identifiers(std::string_view text, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  std::vector<std::string> out;
  std::u32string s = utf8::decode(text);
  std::u32string cur;
  auto flush = [&] {
    if (cur.empty()) return;
    if (cur.size() >= static_cast<std::size_t>(policy.integer("/vocabulary/camel_min_length"))) {
      int upper = 0, lower = 0;
      bool inner_upper = false;
      for (std::size_t i = 0; i < cur.size(); ++i) {
        char32_t c = cur[i];
        bool up = unicode::simple_lower(c) != c;
        bool lo = unicode::simple_upper(c) != c;
        upper += up;
        lower += lo;
        if (i > 0 && up) inner_upper = true;
      }
      if ((!policy.value("/vocab_closure/camel_require_inner_uppercase").get<bool>() || inner_upper) &&
          lower >= policy.integer("/vocab_closure/camel_min_lowercase") &&
          upper >= policy.integer("/vocabulary/camel_min_uppercase")) out.push_back(utf8::encode(cur));
    }
    cur.clear();
  };
  for (char32_t c : s) {
    if (unicode::is_alnum(c)) {
      cur.push_back(c);
    } else {
      flush();
    }
  }
  flush();
  return out;
}

CorpusStats compute_stats(const Corpus& corpus, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  profile = scope.ptr();
  CorpusStats st;
  st.n = corpus.docs.size();
  st.docs.resize(corpus.docs.size());
  for (std::size_t i = 0; i < corpus.docs.size(); ++i) {
    DocTerms& dt = st.docs[i];
    for (auto& t : candidate_terms(corpus.docs[i].text, profile)) {
      auto [it, inserted] = dt.tf.emplace(t, 0);
      ++it->second;
      if (inserted) dt.terms.push_back(t);
    }
    for (const auto& t : dt.terms) ++st.df[t];
  }
  return st;
}

Json TermRecord::to_json(const ArchiveProfile* profile) const {
  ProfileScope scope(profile);
  return Json{{"term", term},       {"pass", pass},         {"origin", origin},
              {"score", encode_score(score, scope.get())}, {"reasons", reasons}, {"evidence", evidence}};
}

Json TermRecord::to_json() const { return to_json(nullptr); }

TermRecord TermRecord::from_json(const Json& j) {
  TermRecord r;
  r.term = json::get_string(j, "term");
  r.pass = static_cast<int>(json::get_int(j, "pass"));
  r.origin = json::get_string(j, "origin");
  r.score = json::get_number(j, "score");
  if (const Json* a = json::find(j, "reasons"); a && a->is_array()) {
    for (const auto& x : *a) r.reasons.push_back(x.get<std::string>());
  }
  if (const Json* a = json::find(j, "evidence"); a && a->is_array()) {
    for (const auto& x : *a) r.evidence.push_back(x.get<std::string>());
  }
  return r;
}

std::vector<std::pair<std::string, double>> salient_terms(const CorpusStats& stats, std::size_t i, int k, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  std::vector<std::pair<std::string, double>> v;
  if (i >= stats.docs.size()) return v;
  const DocTerms& dt = stats.docs[i];
  double n = static_cast<double>(stats.n);
  for (const auto& t : dt.terms) {
    auto df_it = stats.df.find(t);
    int df = df_it == stats.df.end() ? 1 : df_it->second;
    if (df < policy.integer("/vocabulary/salient_min_df") || df > std::max(static_cast<double>(policy.integer("/vocabulary/salient_min_df")), policy.number("/vocabulary/salient_max_df_fraction") * n)) continue;
    double tf = dt.tf.at(t);
    double w = (1.0 + std::log(tf)) * std::log((n + 1.0) / (df + 1.0));
    if (t.find(' ') != std::string::npos) w *= policy.number("/vocabulary/salient_phrase_boost");  // phrases are more specific
    if (w <= 0) continue;
    v.emplace_back(t, w);
  }
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
  });
  if (static_cast<int>(v.size()) > k) v.resize(static_cast<std::size_t>(k));
  return v;
}

std::vector<TermRecord> expand_vocabulary(const Corpus& corpus, const CorpusStats& stats,
                                          const std::vector<std::size_t>& hits, const std::set<std::string>& vocab,
                                          int pass, int max_new, const SemanticAnalyzer* analyzer, const ArchiveProfile* profile) {
  ProfileScope scope(profile);
  const auto& policy = scope.get();
  profile = scope.ptr();
  std::vector<TermRecord> out;
  if (hits.empty() || max_new <= 0) return out;
  const double n = static_cast<double>(stats.n);
  const double h = static_cast<double>(hits.size());

  std::set<std::string> vocab_stems;
  std::set<std::string> vocab_words;
  for (const auto& v : vocab) {
    for (const auto& t : tokenize(v)) {
      vocab_stems.insert(stem(t, profile));
      vocab_words.insert(t);
    }
    vocab_stems.insert(stem(v, profile));
  }

  std::unordered_map<std::string, int> df_hits;
  for (std::size_t i : hits) {
    for (const auto& t : stats.docs[i].terms) ++df_hits[t];
  }

  // NER / identifier evidence in hit docs.
  std::unordered_map<std::string, std::string> ner;  // term -> reason
  for (std::size_t i : hits) {
    const std::string& text = corpus.docs[i].text;
    for (const auto& id : camel_identifiers(text, profile)) {
      std::string low = utf8::to_lower(id);
      if (!ner.count(low)) ner[low] = "identifier " + id;
    }
    if (analyzer) {
      for (const auto& e : analyzer->extract_entities(utf8::prefix(text, static_cast<std::size_t>(policy.integer("/vocabulary/ner_max_codepoints"))))) {
        if (!policy.contains("/vocabulary/ner_types", e.entity_type)) {
          continue;
        }
        auto toks = tokenize(e.text);
        if (toks.empty() || toks.size() > static_cast<std::size_t>(policy.integer("/vocabulary/ner_max_tokens"))) continue;
        std::string low;
        for (const auto& token : toks) low += (low.empty() ? "" : " ") + token;
        if (!ner.count(low)) ner[low] = "ner:" + e.entity_type;
      }
    }
  }

  struct Cand {
    std::string term;
    int dfh = 0;
    int df = 0;
    double lift = 0;
    double base = 0;
    double score = 0;
  };
  std::vector<Cand> cands;
  const int min_h = policy.integer(h > policy.number("/vocabulary/large_hit_count") ? "/vocabulary/large_min_hit_df" : "/vocabulary/min_hit_df");
  for (const auto& [t, dfh] : df_hits) {
    if (dfh < min_h) continue;
    if (vocab.count(t) || vocab_stems.count(stem(t, profile))) continue;
    bool bigram = t.find(' ') != std::string::npos;
    if (bigram) {
      auto parts = tokenize(t);
      if (parts.size() == 2 && vocab_words.count(parts[0]) && vocab_words.count(parts[1])) continue;
    }
    int df = stats.df.count(t) ? stats.df.at(t) : dfh;
    if (df > policy.number("/vocabulary/generic_df_fraction") * n && n > policy.number("/vocabulary/generic_min_corpus")) continue;  // generic in this corpus
    double ph = dfh / h;
    double pc = df / n;
    double lift = pc > 0 ? ph / pc : 0;
    if (lift < policy.number("/vocabulary/min_lift")) continue;
    double base = ph * std::log(1.0 + n / df);
    if (bigram) base *= policy.number("/vocabulary/phrase_boost");
    cands.push_back({t, dfh, df, lift, base, base});
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.base != b.base) return a.base > b.base;
    return a.term < b.term;
  });
  std::size_t pool = std::min<std::size_t>(cands.size(), static_cast<std::size_t>(max_new) * static_cast<std::size_t>(policy.integer("/vocabulary/pool_multiplier")));
  cands.resize(pool);

  // Sentence-level co-occurrence with the current vocabulary (pool only).
  std::unordered_map<std::string, std::size_t> pool_index;
  for (std::size_t i = 0; i < cands.size(); ++i) pool_index[cands[i].term] = i;
  std::vector<int> cooc(cands.size(), 0);
  std::vector<std::map<std::string, int>> partners(cands.size());
  for (std::size_t i : hits) {
    for (const auto& sent : split_sentences(corpus.docs[i].text, profile)) {
      auto terms = candidate_terms(sent, profile);
      std::set<std::string> ts(terms.begin(), terms.end());
      std::string partner;
      for (const auto& t : ts) {
        if (vocab.count(t)) {
          partner = t;
          break;
        }
      }
      if (partner.empty()) continue;
      for (const auto& t : ts) {
        auto it = pool_index.find(t);
        if (it == pool_index.end()) continue;
        ++cooc[it->second];
        ++partners[it->second][partner];
      }
    }
  }

  for (std::size_t i = 0; i < cands.size(); ++i) {
    Cand& c = cands[i];
    double ratio = std::min(1.0, static_cast<double>(cooc[i]) / std::max(1, c.dfh));
    c.score = c.base * (policy.number("/vocabulary/cooccurrence_base") + ratio) * (ner.count(c.term) ? policy.number("/vocabulary/ner_boost") : 1.0);
  }
  std::vector<std::size_t> order(cands.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    if (cands[a].score != cands[b].score) return cands[a].score > cands[b].score;
    return cands[a].term < cands[b].term;
  });

  std::set<std::string> chosen_stems;
  std::vector<std::string> chosen_bigrams;
  for (std::size_t oi : order) {
    if (static_cast<int>(out.size()) >= max_new) break;
    const Cand& c = cands[oi];
    std::string st = stem(c.term, profile);
    if (chosen_stems.count(st)) continue;
    bool bigram = c.term.find(' ') != std::string::npos;
    if (!bigram) {
      bool covered = false;
      for (const auto& b : chosen_bigrams) {
        auto parts = tokenize(b);
        if (std::find(parts.begin(), parts.end(), c.term) != parts.end()) covered = true;
      }
      if (covered) continue;
    }
    TermRecord r;
    r.term = c.term;
    r.pass = pass;
    r.origin = "expansion";
    r.score = c.score;
    r.reasons.push_back("tfidf-contrast " + fmt(c.base, policy.integer("/vocab_closure/reason_score_precision")) + ": in " + std::to_string(c.dfh) + "/" +
                        std::to_string(hits.size()) + " hits vs " + std::to_string(c.df) + "/" +
                        std::to_string(stats.n) + " docs (lift " + fmt(c.lift, policy.integer("/vocab_closure/reason_lift_precision")) + ")");
    if (auto it = ner.find(c.term); it != ner.end()) r.reasons.push_back(it->second);
    if (cooc[oi] > 0) {
      std::string best;
      int bestn = 0;
      for (const auto& [p, k] : partners[oi]) {
        if (k > bestn) {
          best = p;
          bestn = k;
        }
      }
      r.reasons.push_back("co-occurs with '" + best + "' in " + std::to_string(bestn) + " sentence(s)");
    }
    // evidence: hit docs with the highest tf
    std::vector<std::pair<int, std::string>> ev;
    for (std::size_t i : hits) {
      auto it = stats.docs[i].tf.find(c.term);
      if (it != stats.docs[i].tf.end()) ev.emplace_back(-it->second, corpus.docs[i].key);
    }
    std::sort(ev.begin(), ev.end());
    for (std::size_t k = 0; k < ev.size() && k < static_cast<std::size_t>(policy.integer("/vocabulary/evidence_limit")); ++k) r.evidence.push_back(ev[k].second);
    chosen_stems.insert(st);
    if (bigram) chosen_bigrams.push_back(c.term);
    out.push_back(std::move(r));
  }
  return out;
}

}  // namespace loom::archive
