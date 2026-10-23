// K3-1: relative addresses as data, their text notations (presets from the pack), composition and the owner's example alias.
// Every concrete dimension, word and token below is an example of what the registry and the syntax pack must be able to express.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>

#include "address_mock.h"
#include "loom/address.h"
#include "loom/dimensions.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::address;
using test::unwrap;

namespace {

const dimensions::Registry& reg() { return dimensions::Registry::builtin(); }
const Syntaxes& syntaxes() { return Syntaxes::builtin(); }

Json coord(const std::string& kind, Json v) {
  Json c = Json::object();
  c[kind] = std::move(v);
  return c;
}

// ── generator over the registry ──────────────────────────────────────────────
struct Gen {
  std::mt19937_64 rng;
  explicit Gen(std::uint64_t seed) : rng(seed) {}
  int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); }
  bool coin(int percent) { return pick(100) < percent; }

  std::string text(int max_len) {
    static const std::vector<std::string> alphabet = {"a", "B", "7", " ", "\"", "\\", "[", "]", "(", ")", ".", "..", "/", ":", "\xC5\x82", "\xF0\x9F\x99\x82", "-", "+", "'", "00", "\n", "role = \"user\""};
    std::string out;
    const int n = pick(max_len + 1);
    for (int i = 0; i < n; ++i) out += alphabet[static_cast<std::size_t>(pick(static_cast<int>(alphabet.size())))];
    return out;
  }
  std::string ident() {
    static const std::string first = "abcXYZ_\xC5\x82";
    static const std::string rest = "abcXYZ_09-";
    std::string out;
    out += first[static_cast<std::size_t>(pick(7))];
    if (out[0] == '\xC5') out += '\x82';
    const int n = pick(8);
    for (int i = 0; i < n; ++i) out += rest[static_cast<std::size_t>(pick(static_cast<int>(rest.size())))];
    return out;
  }
  std::string datetime() {
    char buf[64];
    int len = std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", 2000 + pick(60), 1 + pick(12), 1 + pick(28));
    std::string out(buf, static_cast<std::size_t>(len));
    if (coin(70)) {
      len = std::snprintf(buf, sizeof buf, "T%02d:%02d", pick(24), pick(60));
      out.append(buf, static_cast<std::size_t>(len));
      if (coin(50)) {
        len = std::snprintf(buf, sizeof buf, ":%02d", pick(60));
        out.append(buf, static_cast<std::size_t>(len));
        if (coin(40)) out += "." + std::to_string(pick(1000));
      }
      switch (pick(5)) {
        case 0: out += "Z"; break;
        case 1: out += "+02:00"; break;
        case 2: out += "-0530"; break;
        case 3: out += "+01"; break;
        default: break;
      }
    }
    return out;
  }
  std::int64_t integer() {
    switch (pick(10)) {
      case 0: return std::numeric_limits<std::int64_t>::min();
      case 1: return std::numeric_limits<std::int64_t>::max();
      case 2: case 3: case 4: return std::uniform_int_distribution<std::int64_t>(-1000000000000LL, 1000000000000LL)(rng);
      default: return pick(41) - 20;
    }
  }
  double real() {
    switch (pick(8)) {
      case 0: return -0.0;
      case 1: return static_cast<double>(pick(100)) - 50;
      case 2: return std::uniform_real_distribution<double>(-1, 1)(rng) * 1e-9;
      case 3: return std::uniform_real_distribution<double>(-1, 1)(rng) * 1e15;
      case 4: return 1e300;
      case 5: return std::uniform_real_distribution<double>(-1, 1)(rng) * 1e-300;
      default: return std::uniform_real_distribution<double>(-1000, 1000)(rng);
    }
  }
  Json part() {  // a level: int, word, name, datetime, pred, typed
    switch (pick(6)) {
      case 0: return coord("int", integer());
      case 1: return coord("word", ident());
      case 2: return coord("name", text(10));
      case 3: return coord("datetime", datetime());
      case 4: return coord("pred", text(10));
      default: return coord(std::string("kind") + std::to_string(pick(3)), text(6));
    }
  }
  Json levels(int max_parts) {
    Json parts = Json::array();
    const int n = 1 + pick(max_parts);
    for (int i = 0; i < n; ++i) parts.push_back(part());
    return coord("levels", parts);
  }
  Json end() { return coin(20) ? levels(4) : (coin(25) ? coord("double", real()) : part()); }
  Json any_coord(int max_parts) {
    switch (pick(5)) {
      case 0: return coord("double", real());
      case 1: return levels(max_parts);
      case 2: return coord("range", Json::array({end(), end()}));
      default: return part();
    }
  }
  Address address(int depth, const std::vector<std::string>& dims, int max_parts) {
    Address a;
    const int n = 1 + pick(depth);
    for (int i = 0; i < n; ++i) {
      Step s;
      if (coin(75)) s.dim = coin(95) ? dims[static_cast<std::size_t>(pick(static_cast<int>(dims.size())))] : ident();
      s.coord = any_coord(max_parts);
      a.steps.push_back(std::move(s));
    }
    return a;
  }
};

bool kind_is_levels(const Json& c) { return c.begin().key() == "levels"; }

constexpr int kStartDepth = 4;  // a preset of the generator, not a limit of the engine (the long-chain case below goes far beyond it)

}  // namespace

TEST_SUITE_BEGIN("address");

TEST_CASE("T1 round trip: parse(print(a)) == a for every syntax of the pack, with and without the registry") {
  Gen gen(20261023);
  const auto dims = reg().ids();
  REQUIRE(syntaxes().ids().size() >= 2);
  std::size_t texts = 0;
  for (const auto& sid : syntaxes().ids()) {
    const Syntax& syn = syntaxes().get(sid);
    for (int depth = 1; depth <= kStartDepth; ++depth)
      for (int i = 0; i < 400; ++i) {
        const Address a = gen.address(depth, dims, 4);
        REQUIRE(unwrap(Address::from_json(a.to_json())) == a);
        const std::string t = print(a, syn);
        auto back = parse(t, syn);
        INFO(sid << ": " << t);
        REQUIRE(back.has_value());
        CHECK(*back == a);
        CHECK(print(*back, syn) == t);  // the printed form is canonical: printing what was read gives the same text
        advisory::Collector adv(&reg().catalogue());
        auto with_registry = parse(t, syn, Context{&reg(), &adv});
        REQUIRE(with_registry.has_value());
        CHECK(*with_registry == a);
        ++texts;
      }
  }
  CHECK(texts == syntaxes().ids().size() * kStartDepth * 400);
}

TEST_CASE("T1 round trip: no hard limit on depth (a long chain of steps and a long hierarchy)") {
  const Syntax& syn = syntaxes().default_syntax();
  Address a;
  for (int i = 0; i < 3000; ++i) {
    Step s;
    s.dim = i % 3 == 0 ? "message" : (i % 3 == 1 ? "version" : "");
    s.coord = i % 2 ? coord("int", i - 1500) : coord("word", "last");
    a.steps.push_back(s);
  }
  Json parts = Json::array();
  for (int i = 0; i < 800; ++i) parts.push_back(i % 2 ? coord("word", "last") : coord("int", i));
  a.steps.push_back(Step{"program_version", coord("levels", parts)});
  const std::string t = print(a, syn);
  CHECK(unwrap(parse(t, syn)) == a);
  CHECK(print(unwrap(parse(t, syn)), syn) == t);
}

TEST_CASE("T1 the examples written in the syntax pack read and print as the pack says") {
  for (const auto& sid : syntaxes().ids()) {
    const Syntax& syn = syntaxes().get(sid);
    for (const auto& ex : syn.examples) {
      const std::string text = ex["text"].get<std::string>(), canonical = ex["canonical"].get<std::string>();
      INFO(sid << ": " << text);
      auto a = parse(text, syn);
      REQUIRE(a.has_value());
      CHECK(print(*a, syn) == canonical);
      CHECK(unwrap(parse(canonical, syn)) == *a);
    }
  }
}

TEST_CASE("the coordinates are not limited to whole numbers (R76, R77): double, name, datetime, range, levels, condition") {
  const Syntax& syn = syntaxes().default_syntax();
  auto s = [&](const char* t) { return unwrap(parse(t, syn)).steps.at(0).coord; };
  CHECK(s("-1") == coord("int", -1));
  CHECK(s("0.5") == coord("double", 0.5));
  CHECK(s("-0.25") == coord("double", -0.25));
  CHECK(s("\"main\"") == coord("name", "main"));
  CHECK(s("last") == coord("word", "last"));
  CHECK(s("-3..0") == coord("range", Json::array({coord("int", -3), coord("int", 0)})));
  CHECK(s("-1.last.last") == coord("levels", Json::array({coord("int", -1), coord("word", "last"), coord("word", "last")})));
  CHECK(s("1.2.3") == coord("levels", Json::array({coord("int", 1), coord("int", 2), coord("int", 3)})));
  CHECK(s("(1.2)") == coord("levels", Json::array({coord("int", 1), coord("int", 2)})));
  CHECK(s("2026-10-22T10:00Z") == coord("datetime", "2026-10-22T10:00Z"));
  CHECK(s("[role = \"user\"]") == coord("pred", "role = \"user\""));
  CHECK(s("ratio\"3:2\"") == coord("ratio", "3:2"));
  CHECK(s("0.5..1.5") == coord("range", Json::array({coord("double", 0.5), coord("double", 1.5)})));
}

TEST_CASE("an address that cannot be read is an error; one that is only useless is accepted with an advisory") {
  const Syntax& syn = syntaxes().default_syntax();
  for (const char* bad : {"\"open", "[open", "1..2..3", "1.", "@", "-", "dim:", "(1.2", "9223372036854775808", "int\"1\""}) {
    INFO(bad);
    auto r = parse(bad, syn);
    CHECK_FALSE(r.has_value());
    if (!r) CHECK(r.error().code == Errc::Parse);
  }
  advisory::Collector adv(&reg().catalogue());
  auto zero = parse("-0", syn, Context{&reg(), &adv});
  REQUIRE(zero.has_value());
  CHECK(*zero == Address{{Step{"", coord("int", 0)}}});
  CHECK(adv.has("address_degenerate"));
  advisory::Collector adv2(&reg().catalogue());
  auto all = parse("all/all", syn, Context{&reg(), &adv2});
  REQUIRE(all.has_value());
  CHECK(all->steps.size() == 2);
  CHECK(adv2.has("address_degenerate"));
  advisory::Collector adv3(&reg().catalogue());
  CHECK(unwrap(parse("", syn, Context{&reg(), &adv3})).steps.empty());
  CHECK(adv3.has("address_empty"));
  advisory::Collector adv4(&reg().catalogue());
  CHECK(unwrap(parse("nowhere:last", syn, Context{&reg(), &adv4})).steps.size() == 1);
  CHECK(adv4.has("dimension_unknown"));
  advisory::Collector adv5(&reg().catalogue());
  CHECK(&syntaxes().get("no_such_syntax", &adv5) == &syntaxes().default_syntax());
  CHECK(adv5.has("syntax_unknown"));
  // every advisory carries the shared shape and a question for the person
  for (const auto& a : adv.items()) {
    CHECK(a.contains("code"));
    CHECK(a.contains("level"));
    CHECK(a["text"].contains("pl"));
    CHECK(a["text"].contains("en"));
    CHECK(a.contains("params"));
    CHECK(a["ask"]["key"] == "advisory.address_degenerate.ask_every");
  }
  // data that no text can carry is refused when it is read as an address
  CHECK_FALSE(Address::from_json(Json{{"steps", Json::array({Json{{"dim", "a dim"}, {"coord", coord("int", 1)}}})}}).has_value());
  CHECK_FALSE(Address::from_json(Json{{"steps", Json::array({Json{{"dim", ""}, {"coord", coord("range", Json::array({coord("range", Json::array({coord("int", 1), coord("int", 2)})), coord("int", 3)}))}}})}}).has_value());
  CHECK_FALSE(Address::from_json(Json{{"steps", "no"}}).has_value());
}

// ── T2: composition ─────────────────────────────────────────────────────────
TEST_CASE("T2 composition is associative as flat-map and not commutative") {
  const auto& r = reg();
  const k3mock::Mock mock = k3mock::Mock::build(r, 32);
  const Graph g = mock.ports();
  Gen gen(77);
  const auto dims = r.ids();
  auto nodes_of = [&](const Address& a, const std::string& x) { return unwrap(resolve(a, x, g, r)).nodes; };
  // resolve(a/b, x) is the union (in order) over y in resolve(a, x) of resolve(b, y)
  auto split = [&](const Address& a, const Address& b, const std::string& x) {
    std::vector<std::string> out;
    for (const auto& y : nodes_of(a, x))
      for (const auto& z : nodes_of(b, y))
        if (std::find(out.begin(), out.end(), z) == out.end()) out.push_back(z);
    return out;
  };
  auto small = [&](int steps) {
    Address a = gen.address(steps, dims, 3);
    for (auto& st : a.steps) {  // keep the generator's coordinates that mean something on a mock: whole offsets, words, ranges
      const int k = gen.pick(6);
      if (k == 0) st.coord = coord("int", gen.pick(7) - 3);
      else if (k == 1) st.coord = coord("word", std::vector<std::string>{"last", "first", "all", "prev", "next", "max", "min"}[static_cast<std::size_t>(gen.pick(7))]);
      else if (k == 2) st.coord = coord("range", Json::array({coord("int", gen.pick(5) - 4), coord("int", gen.pick(5) - 1)}));
      else if (k == 3) st.coord = coord("levels", Json::array({coord("int", gen.pick(5) - 2), coord("word", "last")}));
      // a dimension that suits the coordinate, so that most addresses select something (the property must not hold only for empty sets)
      static const std::vector<std::string> flat = {"message", "branch", "time", "weight", "perspective", "scope", "representation_level", "scale"};
      static const std::vector<std::string> deep = {"version", "program_version"};
      st.dim = kind_is_levels(st.coord) ? deep[static_cast<std::size_t>(gen.pick(2))] : flat[static_cast<std::size_t>(gen.pick(8))];
    }
    return a;
  };
  int nonempty = 0;
  for (int i = 0; i < 600; ++i) {
    const Address a = small(2), b = small(2), c = small(2);
    const std::string x = k3mock::Mock::uid(gen.pick(32));
    const auto left = nodes_of(compose(compose(a, b), c), x);
    const auto right = nodes_of(compose(a, compose(b, c)), x);
    CHECK(left == right);
    CHECK(left == split(compose(a, b), c, x));  // (a/b)/c, split after the second step
    CHECK(left == split(a, compose(b, c), x));  // a/(b/c), split after the first step
    nonempty += !left.empty();
  }
  CHECK(nonempty > 50);  // the property is not satisfied only by empty results
  // not commutative: one counterexample
  const Syntax& syn = syntaxes().default_syntax();
  const Address ab = unwrap(parse("message:-1/message:last", syn)), ba = unwrap(parse("message:last/message:-1", syn));
  CHECK(nodes_of(ab, "u7") != nodes_of(ba, "u7"));
  CHECK(compose(ab, ba) != compose(ba, ab));
  CHECK(compose(compose(ab, ba), ab) == compose(ab, compose(ba, ab)));
}

// ── T5: the owner's example alias ────────────────────────────────────────────
TEST_CASE("T5 the owner's alias -1.\"00\" reads as -1/version:max.last under owner_2026-10-23") {
  const Syntax& owner = syntaxes().get("owner_2026-10-23");
  const Syntax& def = syntaxes().default_syntax();
  REQUIRE(owner.id == "owner_2026-10-23");
  advisory::Collector adv(&reg().catalogue());
  auto a = parse("-1.\"00\"", owner, Context{&reg(), &adv});
  REQUIRE(a.has_value());
  const Json want = Json{{"steps", Json::array({Json{{"dim", ""}, {"coord", coord("int", -1)}},
                                                  Json{{"dim", "version"}, {"coord", coord("levels", Json::array({coord("word", "max"), coord("word", "last")}))}}})}};
  CHECK(a->to_json() == want);
  CHECK(print(*a, owner) == "-1/version:max.last");
  CHECK(print(*a, def) == "-1/version:max.last");
  CHECK(adv.has("alias_applied"));
  CHECK(unwrap(parse("-1/version:max.last", owner)) == *a);
  // the default notation does not know the alias: there the same text is levels
  CHECK(unwrap(parse("-1.\"00\"", def)).steps.size() == 1);
  CHECK(unwrap(parse("-1.\"00\"", def)).steps[0].coord == coord("levels", Json::array({coord("int", -1), coord("name", "00")})));
  // a name that merely contains the alias text, or a levels coordinate that prints like it, is not read as the alias
  const Address tricky{{Step{"", coord("levels", Json::array({coord("int", 5), coord("name", "00")}))}, Step{"branch", coord("name", "x.\"00\"")}}};
  CHECK(unwrap(parse(print(tricky, owner), owner)) == tricky);
  const Address ranged{{Step{"", coord("range", Json::array({coord("int", 1), coord("name", "00")}))}}};
  CHECK(unwrap(parse(print(ranged, owner), owner)) == ranged);
}

TEST_CASE("T5 the alias resolves: the previous message, its highest version line, the last version in it") {
  const auto& r = reg();
  k3mock::Mock m;
  for (int i = 1; i <= 5; ++i) m.add_root("message", "m" + std::to_string(i), i);
  for (int i = 1; i <= 5; ++i) {  // each message: version lines a, b (key 1, 2), each with revisions 1..i
    const std::string line_a = "m" + std::to_string(i) + "/a", line_b = "m" + std::to_string(i) + "/b";
    m.add_root("version", line_a, 1);
    m.add_root("version", line_b, 2);
    for (int k = 1; k <= i; ++k) {
      m.add_kid("version", line_a, line_a + "/" + std::to_string(k), k);
      m.add_kid("version", line_b, line_b + "/" + std::to_string(k), k);
    }
    m.home["version"]["m" + std::to_string(i)] = line_b;  // a message stands for its latest line
  }
  // the version dimension of this mock holds the lines of all messages as one forest, so tell the ports which belong to a message
  address::Graph g = m.ports();
  const auto all_lines = g.neighbours;
  g.neighbours = [&m, all_lines](const std::string& dim, const std::string& node) {
    auto seq = all_lines(dim, node);
    if (dim != "version") return seq;
    const std::string owner_msg = m.place(dim, node).substr(0, m.place(dim, node).find('/'));
    seq.erase(std::remove_if(seq.begin(), seq.end(), [&](const std::string& s) { return s.substr(0, s.find('/')) != owner_msg; }), seq.end());
    return seq;
  };
  const Syntax& owner = syntaxes().get("owner_2026-10-23");
  auto res = unwrap(resolve(unwrap(parse("-1.\"00\"", owner)), "m4", g, r));
  CHECK(res.nodes == std::vector<std::string>{"m3/b/3"});
  CHECK(res.advisories.empty());
  CHECK(res.trace.size() == 2);
}

TEST_SUITE_END();
