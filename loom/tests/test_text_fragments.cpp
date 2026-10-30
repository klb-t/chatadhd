// R102 G1: text fragments and version alignment. Example cases + generative property tests.
// All texts here are synthetic (written for the test or generated from a fixed seed).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <cstdio>
#include <new>
#include <set>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "loom/text_fragments.h"
#include <fstream>
#include <sstream>
#include "test_helpers.h"

using namespace loom;
using namespace loom::text;
using loom::test::unwrap;

namespace {

std::vector<std::string> cut(const Segmenter& s, std::string_view text, std::string_view level) {
  const Segmentation seg = unwrap(s.segment(text, level));
  return fragment_texts(text, seg);
}

std::vector<std::string> cores(const Segmenter& s, std::string_view text, std::string_view level) {
  const Segmentation seg = unwrap(s.segment(text, level));
  std::vector<std::string> out;
  for (const auto& f : seg.fragments) out.emplace_back(text.substr(f.core_begin, f.core_end - f.core_begin));
  return out;
}

std::string concat(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) out += s;
  return out;
}

const std::vector<std::string> kAtoms{
    "a", "b", "Z", "ą", "ż", "Ł", "é", "e\xCC\x81", "ß", "Σ", "日", "本", "。", "！", "😀", "👍🏽", "\xE2\x80\x8D", " ",
    "  ", "\n", "\n\n", "\t", "\r\n", ".", "!", "?", "…", ",", "'", "’", "-", "„", "”", "\"", "(", ")", "1", "7", "np.",
    "Dr.", "J.", "m.in.", "e.g.", "\xC3", "\xFF", "\x80", "\xE2\x82", "\xF0\x9F\x98"};

std::string random_text(std::mt19937& rng, std::size_t max_atoms) {
  std::uniform_int_distribution<std::size_t> len(0, max_atoms), pick(0, kAtoms.size() - 1);
  std::string s;
  const std::size_t n = len(rng);
  for (std::size_t i = 0; i < n; ++i) s += kAtoms[pick(rng)];
  return s;
}

// a vocabulary of distinct synthetic words (Polish letters included)
std::vector<std::string> vocabulary(std::mt19937& rng, std::size_t n) {
  static const std::vector<std::string> letters{"a", "e", "i", "o", "u", "y", "ą", "ę", "ó", "k", "r", "s", "t",
                                                "ł", "ż", "ś", "ć", "n", "m", "p", "w", "z", "d", "g"};
  std::set<std::string> seen;
  std::vector<std::string> out;
  std::uniform_int_distribution<std::size_t> pick(0, letters.size() - 1), len(4, 9);
  while (out.size() < n) {
    std::string w;
    const std::size_t k = len(rng);
    for (std::size_t i = 0; i < k; ++i) w += letters[pick(rng)];
    if (seen.insert(w).second) out.push_back(w);
  }
  return out;
}

std::string sentence(std::mt19937& rng, const std::vector<std::string>& vocab, std::size_t words) {
  std::uniform_int_distribution<std::size_t> pick(0, vocab.size() - 1);
  std::string s = "Zdanie";  // capitalised start, so the pl rules cut before it
  for (std::size_t i = 0; i < words; ++i) s += " " + vocab[pick(rng)];
  return s + ".";
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) out += (i ? std::string(sep) : "") + v[i];
  return out;
}

}  // namespace

TEST_CASE("text pack: the embedded copy equals loom/data/text/segmentation.pack") {
  const auto path = std::filesystem::path(LOOM_TEST_FIXTURES) / ".." / ".." / "data" / "text" / "segmentation.pack";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::stringstream buf;
  buf << in.rdbuf();
  const std::string disk = buf.str();
  CHECK(Json::parse(disk) == builtin_pack());
  CHECK(builtin_pack_source() == "loom/data/text/segmentation.pack");
}

TEST_CASE("segmentation: Polish preset (abbreviations, ordinals, quotes)") {
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  CHECK(pl.advisories().empty());
  CHECK(cores(pl, "To jest np. kot. Drugie zdanie.", "sentence") ==
        std::vector<std::string>{"To jest np. kot.", "Drugie zdanie."});
  CHECK(cores(pl, "Urodził się 5. maja. Potem wyjechał.", "sentence") ==
        std::vector<std::string>{"Urodził się 5. maja.", "Potem wyjechał."});
  CHECK(cores(pl, "Przyszli m.in. Jan i Ewa. Było ok. 20 osób!", "sentence") ==
        std::vector<std::string>{"Przyszli m.in. Jan i Ewa.", "Było ok. 20 osób!"});
  CHECK(cores(pl, "Powiedział: „Idę.” Potem wyszedł.", "sentence") ==
        std::vector<std::string>{"Powiedział: „Idę.”", "Potem wyszedł."});
  CHECK(cores(pl, "Czy tak?! Tak... Na pewno.", "sentence") ==
        std::vector<std::string>{"Czy tak?!", "Tak...", "Na pewno."});
}

TEST_CASE("segmentation: English, initials, CJK, paragraphs, words") {
  const Segmenter en = unwrap(Segmenter::builtin("en"));
  CHECK(cores(en, "Mr. Smith went to Washington. He said \"Hi!\" Then he left.", "sentence") ==
        std::vector<std::string>{"Mr. Smith went to Washington.", "He said \"Hi!\"", "Then he left."});
  CHECK(cores(en, "J. R. R. Tolkien wrote it. Yes, e.g. this.", "sentence") ==
        std::vector<std::string>{"J. R. R. Tolkien wrote it.", "Yes, e.g. this."});
  CHECK(cores(en, "Pi is 3.14 exactly. Not.", "sentence") == std::vector<std::string>{"Pi is 3.14 exactly.", "Not."});
  const Segmenter ja = unwrap(Segmenter::builtin("ja"));
  CHECK(cores(ja, "今日は晴れ。明日は雨。", "sentence") == std::vector<std::string>{"今日は晴れ。", "明日は雨。"});
  CHECK(cores(en, "First one.\n\nSecond one.\nStill second.", "paragraph") ==
        std::vector<std::string>{"First one.", "Second one.\nStill second."});
  CHECK(cores(en, "A b.\n\nC d.", "sentence") == std::vector<std::string>{"A b.", "C d."});
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  CHECK(cut(pl, "Biało-czerwony don't, stop.", "word") ==
        std::vector<std::string>{"Biało-czerwony ", "don't", ", ", "stop", "."});
  CHECK(cores(pl, "kawa\xCC\x81 dom", "word") == std::vector<std::string>{"kawa\xCC\x81", "dom"});
  CHECK(cut(pl, "line one\nline two\n", "line") == std::vector<std::string>{"line one\n", "line two\n"});
}

TEST_CASE("segmentation: languages are data (unknown code, region code, overlay)") {
  const Segmenter xx = unwrap(Segmenter::builtin("xx"));
  REQUIRE(xx.advisories().size() == 1);
  CHECK(xx.advisories()[0].find("„xx”") != std::string::npos);
  CHECK(cores(xx, "One. Two.", "sentence").size() == 2);
  const Segmenter plpl = unwrap(Segmenter::builtin("pl-PL"));
  CHECK(plpl.advisories().size() == 1);
  CHECK(cores(plpl, "To np. kot. Dwa.", "sentence").size() == 2);
  // a new language and a new level by data only
  const Json overlay = Json::parse(R"({"languages":{"de":{"sentence":{"abbreviations":["z.B","usw"]}}},
     "levels":{"clause":{"method":"sentence","parent":"sentence","terminators":[",",";"],"next_start":[]}}})");
  const Segmenter de = unwrap(Segmenter::from_pack(merge_pack(builtin_pack(), overlay), "de"));
  CHECK(de.advisories().empty());
  CHECK(cores(de, "Es gibt z.B. Hunde. Und Katzen.", "sentence").size() == 2);
  CHECK(cores(de, "Eins, zwei; drei.", "clause") == std::vector<std::string>{"Eins,", "zwei;", "drei."});
  // what this build cannot do is an explicit error, never a silent fallback
  const Json bad = merge_pack(builtin_pack(), Json::parse(R"({"levels":{"x":{"method":"neural","parent":"document"}}})"));
  CHECK_FALSE(Segmenter::from_pack(bad, "pl"));
  const Json badcls = merge_pack(builtin_pack(), Json::parse(R"({"languages":{"*":{"sentence":{"next_start":["vowel"]}}}})"));
  CHECK_FALSE(Segmenter::from_pack(badcls, "pl"));
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  CHECK_FALSE(pl.segment("x", "nope"));
  CHECK(similarity(pl, "vector", "a", "b").error().code == Errc::Unsupported);
}

TEST_CASE("fingerprint: normalization is data") {
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  CHECK(pl.fingerprint("  Żółw   idzie. ") == pl.fingerprint("żółw idzie."));
  CHECK(pl.fingerprint("Żółw idzie.") != pl.fingerprint("Żółw idzie!"));
  CHECK(pl.fingerprint("x").rfind("sha256:", 0) == 0);
  const Json drop = merge_pack(builtin_pack(), Json::parse(R"({"normalization":{"punctuation":"drop"},"fingerprint":{"hex_chars":16}})"));
  const Segmenter d = unwrap(Segmenter::from_pack(drop, "pl"));
  CHECK(d.fingerprint("Żółw idzie.") == d.fingerprint("żółw idzie!"));
  CHECK(d.fingerprint("x").size() == std::string("sha256:").size() + 16);
  const Json keep = merge_pack(builtin_pack(), Json::parse(R"({"normalization":{"case":"keep"}})"));
  const Segmenter k = unwrap(Segmenter::from_pack(keep, "pl"));
  CHECK(k.fingerprint("Ala") != k.fingerprint("ala"));
}

TEST_CASE("property: segmentation never loses a byte (all levels, languages, random Unicode and invalid UTF-8)") {
  std::mt19937 rng(20261030);
  std::vector<Segmenter> segs;
  for (const char* l : {"pl", "en", "ja", "zh", "xx"}) segs.push_back(unwrap(Segmenter::builtin(l)));
  for (int t = 0; t < 1500; ++t) {
    const std::string s = random_text(rng, 40);
    for (const auto& seg : segs) {
      for (const char* level : {"paragraph", "line", "sentence", "word"}) {
        const Segmentation r = unwrap(seg.segment(s, level));
        std::size_t at = 0;
        for (const auto& f : r.fragments) {
          REQUIRE(f.begin == at);
          REQUIRE(f.end > f.begin);
          REQUIRE(f.core_begin >= f.begin);
          REQUIRE(f.core_end <= f.end);
          REQUIRE(f.core_begin <= f.core_end);
          at = f.end;
        }
        REQUIRE(at == s.size());
        REQUIRE(concat(fragment_texts(s, r)) == s);
      }
    }
  }
}

TEST_CASE("myers_script: minimal, reconstructs, symmetric length (random sequences with duplicates)") {
  std::mt19937 rng(7);
  for (int t = 0; t < 3000; ++t) {
    std::uniform_int_distribution<int> len(0, 14), sym(0, t % 2 ? 2 : 6);
    std::vector<int> a(static_cast<std::size_t>(len(rng))), b(static_cast<std::size_t>(len(rng)));
    for (auto& x : a) x = sym(rng);
    for (auto& x : b) x = sym(rng);
    const auto script = myers_script(a, b);
    // reconstruct b and check a is consumed in order
    std::vector<int> rb;
    std::size_t ia = 0, ib = 0;
    for (const auto& o : script) {
      if (o.kind == "equal") {
        REQUIRE(o.a == ia);
        REQUIRE(o.b == ib);
        REQUIRE(a[o.a] == b[o.b]);
        rb.push_back(a[ia++]);
        ++ib;
      } else if (o.kind == "delete") {
        REQUIRE(o.a == ia++);
      } else {
        REQUIRE(o.kind == "insert");
        REQUIRE(o.b == ib);
        rb.push_back(b[ib++]);
      }
    }
    REQUIRE(ia == a.size());
    REQUIRE(rb == b);
    // minimal: D = n + m - 2·LCS (LCS by plain DP)
    std::vector<std::vector<std::size_t>> L(a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
    for (std::size_t i = a.size(); i-- > 0;)
      for (std::size_t j = b.size(); j-- > 0;)
        L[i][j] = a[i] == b[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    REQUIRE(myers_distance(a, b) == a.size() + b.size() - 2 * L[0][0]);
    REQUIRE(myers_distance(a, b) == myers_distance(b, a));
  }
}

TEST_CASE("myers_distance: equals the number of non-equal ops of myers_script (random, duplicates, larger sizes)") {
  std::mt19937 rng(31);
  for (int t = 0; t < 400; ++t) {
    std::uniform_int_distribution<int> len(0, 120), sym(0, t % 3 == 0 ? 2 : (t % 3 == 1 ? 9 : 400));
    std::vector<int> a(static_cast<std::size_t>(len(rng))), b(static_cast<std::size_t>(len(rng)));
    for (auto& x : a) x = sym(rng);
    for (auto& x : b) x = sym(rng);
    std::size_t d = 0;
    for (const auto& o : myers_script(a, b)) d += o.kind != "equal";
    REQUIRE(myers_distance(a, b) == d);
  }
}

#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
// Review 1030 finding 2: the distance needs O(N+M) memory, not the trace (~D^2 longs, ~290 MB here). A child process gets an address
// space limit of its current size + 100 MB; the work must finish inside it. Synthetic sequences: 60000 units, 3000 replaced by fresh ids.
TEST_CASE("myers_distance: linear memory - D = 6000 on 60000 units finishes within +100 MB of address space") {
  std::vector<int> a(60000), b;
  for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<int>(i);
  b = a;
  for (std::size_t i = 0; i < 3000; ++i) b[10 + i * 19] = 1000000 + static_cast<int>(i);
  const pid_t pid = fork();
  REQUIRE(pid >= 0);
  if (pid == 0) {
    long pages = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) { if (std::fscanf(f, "%ld", &pages) != 1) pages = 0; std::fclose(f); }
    struct rlimit rl;
    rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(pages) * static_cast<rlim_t>(sysconf(_SC_PAGESIZE)) + (100u << 20);
    setrlimit(RLIMIT_AS, &rl);
    int code = 2;
    try { code = myers_distance(a, b) == 6000 ? 0 : 3; } catch (const std::bad_alloc&) { code = 4; }
    _exit(code);
  }
  int status = 0;
  REQUIRE(waitpid(pid, &status, 0) == pid);
  REQUIRE(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
}
#endif

TEST_CASE("property: text::apply(align(a, b), a) == b for random texts, both directions") {
  std::mt19937 rng(99);
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  AlignOptions approx = unwrap(AlignOptions::from_pack(builtin_pack()));
  AlignOptions loose = approx;
  loose.move_threshold = 0.3;
  loose.similarity = "chars";
  for (int t = 0; t < 400; ++t) {
    const std::string a = random_text(rng, 30), b = t % 3 == 0 ? a : random_text(rng, 30);
    for (const char* level : {"sentence", "word"}) {
      for (const AlignOptions* opt : {&approx, &loose}) {
        const auto ua = fragment_texts(a, unwrap(pl.segment(a, level)));
        const auto ub = fragment_texts(b, unwrap(pl.segment(b, level)));
        const Alignment ab = unwrap(align_units(pl, ua, ub, *opt));
        REQUIRE(concat(unwrap(text::apply(ab, ua))) == b);
        const Alignment ba = unwrap(align_units(pl, ub, ua, *opt));
        REQUIRE(concat(unwrap(text::apply(ba, ub))) == a);
        // the kept part is symmetric (both directions are optimal)
        REQUIRE(ab.count("equal") == ba.count("equal"));
        for (const auto& o : ab.ops) {
          if (o.kind != "replace" && o.kind != "move") continue;
          if (!o.inner.empty()) {
            const auto ia = fragment_texts(ua[o.a], unwrap(pl.segment(ua[o.a], opt->inner_level)));
            Alignment inner;
            inner.ops = o.inner;
            REQUIRE(concat(unwrap(text::apply(inner, ia))) == ub[o.b]);
          }
        }
        if (a == b) REQUIRE(ab.count("equal") == ua.size());
      }
    }
  }
}

TEST_CASE("edge cases: empty, one sentence, identical, completely different") {
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  const AlignOptions opt = unwrap(AlignOptions::from_pack(builtin_pack()));
  CHECK(unwrap(align_texts(pl, "", "", "sentence", opt)).ops.empty());
  const Alignment ins = unwrap(align_texts(pl, "", "Jedno zdanie.", "sentence", opt));
  REQUIRE(ins.ops.size() == 1);
  CHECK(ins.ops[0].kind == "insert");
  const Alignment del = unwrap(align_texts(pl, "Jedno zdanie.", "", "sentence", opt));
  REQUIRE(del.ops.size() == 1);
  CHECK(del.ops[0].kind == "delete");
  const std::string t = "Ala ma kota. Kot ma Alę. Koniec.";
  const Alignment same = unwrap(align_texts(pl, t, t, "sentence", opt));
  CHECK(same.count("equal") == 3);
  CHECK(same.ops.size() == 3);
  const Alignment diff = unwrap(align_texts(pl, t, "Zupełnie inny tekst. Bez wspólnych słów.", "sentence", opt));
  CHECK(diff.count("equal") == 0);
  CHECK(diff.count("replace") == 0);
  CHECK(diff.count("delete") == 3);
  CHECK(diff.count("insert") == 2);
  const Alignment one = unwrap(align_texts(pl, "Ala ma kota.", "Ala ma psa.", "sentence", opt));
  REQUIRE(one.ops.size() == 1);
  CHECK(one.ops[0].kind == "replace");
  CHECK(one.ops[0].similarity == doctest::Approx(2.0 * 3 / 8));  // Ala, ma, "." of 4 + 4 word units
  CHECK(one.ops[0].inner.size() == 5);  // Ala ma [kota→psa as delete+insert] .
}

TEST_CASE("the R102 example: sentence 5 vanishes, sentence 2 is changed, nothing is called") {
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  const AlignOptions opt = unwrap(AlignOptions::from_pack(builtin_pack()));
  const std::string d1 =
      "Projekt startuje w marcu. Zespół liczy pięć osób. Budżet jest zatwierdzony. Ryzyka są opisane. "
      "Umowę z firmą Nordwind podpisano na 120 000 zł. Harmonogram jest w załączniku. Pytania kierujcie do biura.";
  const std::string d2 =
      "Projekt startuje w marcu. Zespół liczy sześć osób i jednego stażystę. Budżet jest zatwierdzony. Ryzyka są "
      "opisane. Harmonogram jest w załączniku. Pytania kierujcie do biura.";
  const Alignment al = unwrap(align_texts(pl, d1, d2, "sentence", opt));
  CHECK(al.count("delete") == 1);
  CHECK(al.count("replace") == 1);
  CHECK(al.count("insert") == 0);
  for (const auto& o : al.ops) {
    if (o.kind == "delete") CHECK(o.a == 4);
    if (o.kind == "replace") {
      CHECK(o.a == 1);
      CHECK(o.b == 1);
    }
  }
}

TEST_CASE("property: alignment recovers an injected script (delete, insert, change, move)") {
  std::mt19937 rng(4242);
  const auto vocab = vocabulary(rng, 3000);
  const Segmenter pl = unwrap(Segmenter::builtin("pl"));
  const AlignOptions opt = unwrap(AlignOptions::from_pack(builtin_pack()));
  int with_move = 0;
  for (int t = 0; t < 300; ++t) {
    std::uniform_int_distribution<std::size_t> nlen(1, 18), coin(0, 5);
    const std::size_t n = nlen(rng);
    std::vector<std::string> a;
    for (std::size_t i = 0; i < n; ++i) a.push_back(sentence(rng, vocab, 8));
    // b as a list of (origin a index or kNone, text)
    struct Unit {
      std::size_t from;
      std::string text;
      char tag;  // '=', 'c' changed, 'n' new, 'm' moved
    };
    std::vector<Unit> b;
    std::set<std::size_t> deleted, changed;
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t c = coin(rng);
      if (c == 0) {
        deleted.insert(i);
        continue;
      }
      if (c == 1) {
        std::string s = a[i];
        const auto sp = s.find(' ', 7);  // change one word inside: 7 of 9 word units stay
        const auto sp2 = s.find(' ', sp + 1);
        s = s.substr(0, sp + 1) + "zmienione" + s.substr(sp2);
        changed.insert(i);
        b.push_back({i, s, 'c'});
        continue;
      }
      b.push_back({i, a[i], '='});
    }
    std::size_t inserted = 0;
    for (std::size_t k = coin(rng) % 3; k-- > 0;) {
      std::uniform_int_distribution<std::size_t> pos(0, b.size());
      b.insert(b.begin() + static_cast<long>(pos(rng)), Unit{kNone, sentence(rng, vocab, 8), 'n'});
      ++inserted;
    }
    // one move over at least 3 untouched units
    std::size_t moved = kNone;
    for (std::size_t from = 0; from < b.size() && moved == kNone; ++from) {
      if (b[from].tag != '=' || coin(rng) > 1) continue;
      for (std::size_t to = b.size(); to-- > from + 4;) {
        std::size_t kept = 0;
        for (std::size_t q = from + 1; q < to; ++q) kept += b[q].tag == '=';
        if (kept < 3) continue;
        Unit u = b[from];
        u.tag = 'm';
        b.erase(b.begin() + static_cast<long>(from));
        b.insert(b.begin() + static_cast<long>(to - 1), u);
        moved = u.from;
        break;
      }
    }
    with_move += moved != kNone;
    std::vector<std::string> bt;
    for (const auto& u : b) bt.push_back(u.text);
    const std::string ta = join(a, " "), tb = join(bt, " ");
    const Alignment al = unwrap(align_texts(pl, ta, tb, "sentence", opt));
    std::set<std::size_t> got_del, got_rep, got_move;
    std::size_t got_ins = 0;
    for (const auto& o : al.ops) {
      if (o.kind == "delete") got_del.insert(o.a);
      if (o.kind == "replace") got_rep.insert(o.a);
      if (o.kind == "move") got_move.insert(o.a);
      if (o.kind == "insert") ++got_ins;
    }
    INFO("trial " << t << "\nA: " << ta << "\nB: " << tb);
    CHECK(got_del == deleted);
    CHECK(got_rep == changed);
    CHECK(got_ins == inserted);
    CHECK(got_move == (moved == kNone ? std::set<std::size_t>{} : std::set<std::size_t>{moved}));
    const auto ua = fragment_texts(ta, unwrap(pl.segment(ta, "sentence")));
    CHECK(concat(unwrap(text::apply(al, ua))) == tb);
  }
  CHECK(with_move > 30);
}

TEST_CASE("apply refuses a script that does not cover both sides") {
  auto mk = [](const char* kind, std::size_t a, std::size_t b) {
    EditOp o;
    o.kind = kind;
    o.a = a;
    o.b = b;
    return o;
  };
  Alignment al;
  al.ops.push_back(mk("equal", 0, 0));
  CHECK_FALSE(text::apply(al, {"x", "y"}));  // a[1] unaccounted for
  al.ops.push_back(mk("equal", 0, 1));
  CHECK_FALSE(text::apply(al, {"x"}));  // a[0] twice
  Alignment noside;
  noside.ops.push_back(mk("insert", kNone, 0));
  CHECK_FALSE(text::apply(noside, {}));
}
