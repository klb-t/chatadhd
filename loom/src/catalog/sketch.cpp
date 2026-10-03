// catalog_internal.h: Bloom filter, MinHash, Sketch. Pure functions of their
// inputs; no I/O, no clock, no randomness (deterministic seeds only).
#include "catalog_internal.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "loom/util/base64.h"

namespace loom::catalog::internal {

namespace {

std::uint64_t fnv1a(std::string_view s) noexcept {
  std::uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

// Deterministic second hash: fnv1a of the term with a suffix marker, so it
// is independent enough of the first for double hashing.
std::uint64_t fnv1a_h2(std::string_view s) noexcept {
  std::uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  h ^= 0x9e3779b97f4a7c15ULL;
  return h;
}

std::uint64_t splitmix64(std::uint64_t x) noexcept {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

}  // namespace

// ── Bloom ─────────────────────────────────────────────────────────────
Bloom Bloom::sized(int n_estimate, double fpr) {
  Bloom b;
  int n = std::max(1, n_estimate);
  fpr = std::clamp(fpr, 1e-6, 0.5);
  double m = std::ceil(-(static_cast<double>(n) * std::log(fpr)) / (std::log(2.0) * std::log(2.0)));
  b.bits = std::max(64, static_cast<int>(m));
  double k = std::round((static_cast<double>(b.bits) / n) * std::log(2.0));
  b.hashes = std::clamp(static_cast<int>(k), 1, 16);
  b.words.assign(static_cast<std::size_t>((b.bits + 63) / 64), 0ULL);
  return b;
}

void Bloom::add(std::string_view term) {
  if (bits <= 0 || words.empty()) return;
  std::uint64_t h1 = fnv1a(term);
  std::uint64_t h2 = fnv1a_h2(term) | 1ULL;  // odd, so gcd(h2, 2^n) stays small
  for (int i = 0; i < hashes; ++i) {
    std::uint64_t h = (h1 + static_cast<std::uint64_t>(i) * h2) % static_cast<std::uint64_t>(bits);
    words[h / 64] |= (1ULL << (h % 64));
  }
}

bool Bloom::maybe_contains(std::string_view term) const {
  if (bits <= 0 || words.empty()) return false;
  std::uint64_t h1 = fnv1a(term);
  std::uint64_t h2 = fnv1a_h2(term) | 1ULL;
  for (int i = 0; i < hashes; ++i) {
    std::uint64_t h = (h1 + static_cast<std::uint64_t>(i) * h2) % static_cast<std::uint64_t>(bits);
    if (!(words[h / 64] & (1ULL << (h % 64)))) return false;
  }
  return true;
}

std::string Bloom::to_base64() const {
  std::string raw(words.size() * 8, '\0');
  for (std::size_t i = 0; i < words.size(); ++i) {
    std::uint64_t w = words[i];
    for (int b = 0; b < 8; ++b) raw[i * 8 + b] = static_cast<char>((w >> (8 * b)) & 0xff);
  }
  return base64::encode(raw);
}

Bloom Bloom::from_base64(std::string_view b64, int bits, int hashes) {
  Bloom b;
  b.bits = bits;
  b.hashes = hashes;
  auto raw = base64::decode(b64);
  std::size_t n_words = static_cast<std::size_t>((std::max(0, bits) + 63) / 64);
  b.words.assign(n_words, 0ULL);
  if (!raw) return b;
  const std::string& bytes = *raw;
  for (std::size_t i = 0; i < n_words && (i * 8 + 7) < bytes.size(); ++i) {
    std::uint64_t w = 0;
    for (int bi = 0; bi < 8; ++bi) w |= (static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[i * 8 + bi])) << (8 * bi));
    b.words[i] = w;
  }
  return b;
}

// ── MinHash ───────────────────────────────────────────────────────────
MinHash MinHash::build(const std::vector<std::string>& stemmed_tokens, int lanes) {
  MinHash m;
  lanes = std::max(0, lanes);
  m.sig.assign(static_cast<std::size_t>(lanes), UINT64_MAX);
  if (lanes == 0 || stemmed_tokens.size() < 3) return m;
  std::vector<std::uint64_t> lane_seed(static_cast<std::size_t>(lanes));
  for (int i = 0; i < lanes; ++i) lane_seed[static_cast<std::size_t>(i)] = splitmix64(static_cast<std::uint64_t>(i) + 1);
  for (std::size_t i = 0; i + 2 < stemmed_tokens.size(); ++i) {
    std::string shingle = stemmed_tokens[i] + " " + stemmed_tokens[i + 1] + " " + stemmed_tokens[i + 2];
    std::uint64_t base = fnv1a(shingle);
    for (int lane = 0; lane < lanes; ++lane) {
      std::uint64_t h = splitmix64(base ^ lane_seed[static_cast<std::size_t>(lane)]);
      if (h < m.sig[static_cast<std::size_t>(lane)]) m.sig[static_cast<std::size_t>(lane)] = h;
    }
  }
  return m;
}

double MinHash::jaccard(const MinHash& other) const {
  if (sig.empty() || sig.size() != other.sig.size()) return 0.0;
  int agree = 0, valid = 0;
  for (std::size_t i = 0; i < sig.size(); ++i) {
    bool a_set = sig[i] != UINT64_MAX, b_set = other.sig[i] != UINT64_MAX;
    if (!a_set && !b_set) continue;
    ++valid;
    if (a_set && b_set && sig[i] == other.sig[i]) ++agree;
  }
  return valid == 0 ? 0.0 : static_cast<double>(agree) / static_cast<double>(valid);
}

Json MinHash::to_json() const {
  Json arr = Json::array();
  for (auto v : sig) arr.push_back(static_cast<std::int64_t>(v));
  return arr;
}
MinHash MinHash::from_json(const Json& j) {
  MinHash m;
  if (j.is_array()) {
    for (const auto& v : j) m.sig.push_back(static_cast<std::uint64_t>(v.get<std::int64_t>()));
  }
  return m;
}

// ── Sketch ────────────────────────────────────────────────────────────
namespace {
// Misra-Gries with 4*K counters, then an exact recount of the survivors
// against the full token stream (deterministic top-K regardless of the
// (lossy) summary that picked the candidate set).
std::vector<TermCount> misra_gries_topk(const std::vector<std::string>& tokens, int k) {
  if (tokens.empty() || k <= 0) return {};
  std::unordered_map<std::string, int> counters;
  std::size_t cap = static_cast<std::size_t>(std::max(1, 4 * k));
  for (const auto& t : tokens) {
    auto it = counters.find(t);
    if (it != counters.end()) {
      ++it->second;
    } else if (counters.size() < cap) {
      counters.emplace(t, 1);
    } else {
      for (auto cit = counters.begin(); cit != counters.end();) {
        if (--cit->second <= 0) cit = counters.erase(cit); else ++cit;
      }
    }
  }
  std::unordered_map<std::string, int> exact;
  exact.reserve(counters.size());
  for (const auto& t : tokens) {
    if (counters.count(t)) ++exact[t];
  }
  std::vector<TermCount> out(exact.begin(), exact.end());
  std::sort(out.begin(), out.end(), [](const TermCount& a, const TermCount& b) {
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
  });
  if (static_cast<int>(out.size()) > k) out.resize(static_cast<std::size_t>(k));
  return out;
}
}  // namespace

Sketch Sketch::build(std::string_view text, std::string_view code_text, const SketchParams& params,
                     const kb::Normalizer& norm) {
  Sketch s;
  std::string_view capped = text.substr(0, static_cast<std::size_t>(std::min<std::int64_t>(kSketchByteCap, static_cast<std::int64_t>(text.size()))));
  s.truncated = capped.size() < text.size();
  s.n_chars = static_cast<std::int64_t>(text.size());
  s.n_code_chars = static_cast<std::int64_t>(code_text.size());

  kb::Lang lang = norm.guess_lang(capped);
  s.lang = std::string(kb::to_string(lang));

  std::vector<std::string> stems;
  auto add_tokens = [&](std::string_view t) {
    for (auto& tok : norm.tokens(t)) {
      if (norm.is_stopword(tok)) continue;
      stems.push_back(norm.match_key(tok));
    }
  };
  add_tokens(capped);
  std::string_view code_capped = code_text.substr(0, static_cast<std::size_t>(std::min<std::int64_t>(kSketchByteCap, static_cast<std::int64_t>(code_text.size()))));
  add_tokens(code_capped);

  s.top_terms = misra_gries_topk(stems, params.top_k);

  // Bloom over every distinct stemmed token (covers what falls out of top-K).
  std::vector<std::string> distinct;
  {
    std::unordered_map<std::string, char> seen;
    for (auto& t : stems) {
      if (seen.emplace(t, 0).second) distinct.push_back(t);
    }
  }
  s.bloom = Bloom::sized(static_cast<int>(distinct.size()), params.bloom_fpr);
  for (auto& t : distinct) s.bloom.add(t);

  s.minhash = MinHash::build(stems, params.minhash);
  return s;
}

bool Sketch::contains(std::string_view term) const {
  for (auto& [t, _] : top_terms) {
    if (t == term) return true;
  }
  return bloom.maybe_contains(term);
}

int Sketch::topk_tf(std::string_view term) const {
  for (auto& [t, c] : top_terms) {
    if (t == term) return c;
  }
  return 0;
}

Json Sketch::to_json() const {
  Json terms = Json::array();
  for (auto& [t, c] : top_terms) terms.push_back(Json{{"term", t}, {"tf", c}});
  return Json{{"top_terms", terms},
              {"bloom", Json{{"bits", bloom.bits}, {"hashes", bloom.hashes}, {"data", bloom.to_base64()}}},
              {"minhash", minhash.to_json()},
              {"lang", lang},
              {"n_chars", n_chars},
              {"n_code_chars", n_code_chars},
              {"truncated", truncated}};
}

Result<Sketch> Sketch::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "sketch: expected an object");
  Sketch s;
  if (const Json* terms = json::find(j, "top_terms"); terms && terms->is_array()) {
    for (const auto& t : *terms) s.top_terms.emplace_back(json::get_string(t, "term"), static_cast<int>(json::get_int(t, "tf")));
  }
  if (const Json* b = json::find(j, "bloom"); b && b->is_object()) {
    s.bloom = Bloom::from_base64(json::get_string(*b, "data"), static_cast<int>(json::get_int(*b, "bits")),
                                 static_cast<int>(json::get_int(*b, "hashes", 1)));
  }
  if (const Json* mh = json::find(j, "minhash")) s.minhash = MinHash::from_json(*mh);
  s.lang = json::get_string(j, "lang");
  s.n_chars = json::get_int(j, "n_chars");
  s.n_code_chars = json::get_int(j, "n_code_chars");
  s.truncated = json::get_bool(j, "truncated");
  return s;
}

}  // namespace loom::catalog::internal
