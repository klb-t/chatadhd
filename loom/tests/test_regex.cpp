#include <doctest/doctest.h>

#include "loom/re/regex.h"
#include "loom/util/utf8.h"

using namespace loom;
using namespace loom::re;

namespace {
std::optional<Match> S(const Regex& r, const char* text, std::size_t pos = 0) {
  static thread_local std::u32string buf;
  buf = utf8::decode(text);
  return r.search(buf, pos);
}
std::string g(const Match& m, std::size_t i = 0) { return m.group_utf8(i); }
}  // namespace

TEST_SUITE("regex") {
  TEST_CASE("literals and basic classes") {
    auto r = Regex::compile("abc");
    REQUIRE(r);
    auto m = S(*r, "xxabcyy");
    REQUIRE(m);
    CHECK(m->start(0) == 2);
    CHECK(m->end(0) == 5);

    auto r2 = Regex::compile("[a-c]+");
    REQUIRE(r2);
    auto m2 = S(*r2, "xxabccbayy");
    REQUIRE(m2);
    CHECK(g(*m2) == "abccba");

    auto r3 = Regex::compile("[^a-c]+");
    REQUIRE(r3);
    auto m3 = S(*r3, "abcXYZabc");
    REQUIRE(m3);
    CHECK(g(*m3) == "XYZ");
  }

  TEST_CASE("shorthand classes are Unicode-aware") {
    auto r = Regex::compile(R"(\w+)");
    REQUIRE(r);
    auto m = S(*r, "  żółw123  ");
    REQUIRE(m);
    CHECK(g(*m) == "żółw123");

    auto rs = Regex::compile(R"(\s+)");
    REQUIRE(rs);
    auto ms = S(*rs, "a\t\n b");
    REQUIRE(ms);
    CHECK(g(*ms) == "\t\n ");
  }

  TEST_CASE("anchors: ^ $ \\A \\Z, MULTILINE") {
    auto r = Regex::compile("^abc$");
    REQUIRE(r);
    CHECK(r->fullmatch(utf8::decode("abc")).has_value());
    CHECK(S(*r, "abc\n").has_value());  // bare $ matches before trailing \n
    CHECK(!S(*r, "xabc").has_value());

    auto rm = Regex::compile("^abc$", kMultiline);
    REQUIRE(rm);
    auto text = utf8::decode("xyz\nabc\ndef");
    auto m = rm->search(text);
    REQUIRE(m);
    CHECK(m->start(0) == 4);
    CHECK(m->end(0) == 7);

    auto ra = Regex::compile(R"(\A\d+\Z)");
    REQUIRE(ra);
    CHECK(ra->search(utf8::decode("123")).has_value());
    CHECK(!ra->search(utf8::decode("123\n")).has_value());
  }

  TEST_CASE("word boundary") {
    auto r = Regex::compile(R"(\bcat\b)");
    REQUIRE(r);
    auto text = utf8::decode("cat cats concatenate a-cat-b");
    auto all = r->finditer(text);
    CHECK(all.size() == 2);  // "cat" alone, and "cat" inside "a-cat-b"
  }

  TEST_CASE("quantifiers: greedy vs lazy, bounded") {
    CHECK(g(*S(*Regex::compile("a.*b"), "axbxb")) == "axbxb");
    CHECK(g(*S(*Regex::compile("a.*?b"), "axbxb")) == "axb");
    CHECK(g(*S(*Regex::compile("a+"), "aaa")) == "aaa");
    CHECK(g(*S(*Regex::compile("a+?"), "aaa")) == "a");
    auto rq = Regex::compile("a?");
    auto mq = rq->search(utf8::decode("b"));
    REQUIRE(mq);
    CHECK(g(*mq) == "");

    auto rb = Regex::compile("a{2,4}");
    CHECK(g(*S(*rb, "aaaaaa")) == "aaaa");
    auto rbl = Regex::compile("a{2,4}?");
    CHECK(g(*S(*rbl, "aaaaaa")) == "aa");
    auto rex = Regex::compile("a{3}");
    CHECK(!S(*rex, "aa").has_value());
    CHECK(g(*S(*rex, "aaaa")) == "aaa");
    auto ropen = Regex::compile("a{2,}");
    CHECK(g(*S(*ropen, "aaaaa")) == "aaaaa");
  }

  TEST_CASE("bounded quantifier does not blow up (was O(2^k))") {
    std::string text(300, 'a');
    text.push_back('b');
    auto r = Regex::compile(R"([\w\s]{1,30}X)");
    REQUIRE(r);
    auto t32 = utf8::decode(text);
    auto m = r->search(t32);
    CHECK(!m.has_value());
    CHECK(!r->last_search_hit_limit());
  }

  TEST_CASE("groups, alternation, non-capturing, lastindex") {
    auto r = Regex::compile("(a)|(b)|(c)");
    REQUIRE(r);
    auto m = S(*r, "xbx");
    REQUIRE(m);
    CHECK(m->lastindex() == 2u);
    CHECK(!m->matched(1));
    CHECK(m->matched(2));
    CHECK(!m->matched(3));

    auto rnc = Regex::compile("(?:ab)+(c)");
    REQUIRE(rnc);
    auto mnc = S(*rnc, "ababc");
    REQUIRE(mnc);
    CHECK(mnc->group_count() == 1);
    CHECK(g(*mnc, 1) == "c");
  }

  TEST_CASE("IGNORECASE literals and classes") {
    auto r = Regex::compile("HeLLo", kIgnoreCase);
    REQUIRE(r);
    CHECK(S(*r, "say hello world").has_value());
    auto rc = Regex::compile("[a-f]+", kIgnoreCase);
    REQUIRE(rc);
    CHECK(g(*S(*rc, "ABCXYZdef")) == "ABC");
  }

  TEST_CASE("lookahead") {
    auto r = Regex::compile(R"(foo(?=bar))");
    REQUIRE(r);
    CHECK(S(*r, "foobar").has_value());
    CHECK(!S(*r, "foobaz").has_value());
    auto rn = Regex::compile(R"(foo(?!bar))");
    REQUIRE(rn);
    CHECK(!S(*rn, "foobar").has_value());
    CHECK(S(*rn, "foobaz").has_value());
  }

  TEST_CASE("finditer non-overlapping, empty matches") {
    auto r = Regex::compile("a*");
    REQUIRE(r);
    auto text = utf8::decode("baab");
    auto all = r->finditer(text);
    // Python: re.findall('a*', 'baab') == ['', 'aa', '', '']
    REQUIRE(all.size() == 4);
    CHECK(g(all[0]) == "");
    CHECK(g(all[1]) == "aa");
    CHECK(g(all[2]) == "");
    CHECK(g(all[3]) == "");
  }

  TEST_CASE("findall with 0 and 1 groups") {
    auto r0 = Regex::compile(R"(\d+)");
    auto all0 = r0->findall(utf8::decode("a1 b22 c333"));
    REQUIRE(all0.size() == 3);
    CHECK(utf8::encode(all0[1]) == "22");

    auto r1 = Regex::compile(R"(\((\w+)\))");
    auto all1 = r1->findall(utf8::decode("(a) (bb) (ccc)"));
    REQUIRE(all1.size() == 3);
    CHECK(utf8::encode(all1[2]) == "ccc");
  }

  TEST_CASE("sub: literal, backreferences, count") {
    auto r = Regex::compile(R"(\s+)");
    CHECK(r->sub_utf8(" ", "a   b\tc") == "a b c");

    auto rg = Regex::compile(R"((\w+)@(\w+))");
    CHECK(rg->sub_utf8(R"(\2@\1)", "user@host") == "host@user");

    auto rc = Regex::compile("a");
    CHECK(rc->sub_utf8("X", "aaaa", 2) == "XXaa");
  }

  TEST_CASE("match vs fullmatch vs search") {
    auto r = Regex::compile(R"(\d+)");
    REQUIRE(r);
    auto text = utf8::decode("123abc");
    CHECK(r->match(text).has_value());
    CHECK(!r->fullmatch(text).has_value());
    CHECK(r->fullmatch(utf8::decode("123")).has_value());
    CHECK(!r->match(utf8::decode("abc123")).has_value());
    CHECK(r->search(utf8::decode("abc123")).has_value());
  }

  TEST_CASE("compile errors are reported, not thrown") {
    auto r = Regex::compile("(abc");
    CHECK(!r);
    auto r2 = Regex::compile("[abc");
    CHECK(!r2);
    auto r3 = Regex::compile("*abc");
    CHECK(!r3);
  }

  TEST_CASE("emoji / astral code points") {
    // '.' must match exactly one code point (not UTF-16 surrogate units).
    auto rd = Regex::compile(".");
    REQUIRE(rd);
    auto text = utf8::decode("😀x");
    auto m = rd->search(text);
    REQUIRE(m);
    CHECK(m->end(0) - m->start(0) == 1);  // one code point, not two UTF-16 units
    CHECK(g(*m) == "😀");
  }
}
