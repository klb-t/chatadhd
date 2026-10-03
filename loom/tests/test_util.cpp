#include <doctest/doctest.h>

#include <regex>
#include <sys/stat.h>

#include "loom/log.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/json.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"
#include "test_helpers.h"

using namespace loom;

TEST_SUITE("util") {
  TEST_CASE("Result and Status basics") {
    Result<int> ok = 5;
    CHECK(ok);
    CHECK(*ok == 5);
    Result<int> bad = Error(Errc::NotFound, "nope");
    CHECK(!bad);
    CHECK(bad.error().code == Errc::NotFound);
    CHECK(bad.error().to_string() == "not_found: nope");
    CHECK(bad.value_or(7) == 7);
    Status s;
    CHECK(s);
    Status f = Error(Errc::Io, "x");
    CHECK(!f);
    CHECK(errc_name(Errc::NotImplemented) == "not_implemented");
    CHECK(errc_from_name("rate_limited") == Errc::RateLimited);
    CHECK(!errc_from_name("bogus"));

    auto chain = []() -> Result<int> {
      LOOM_TRY_ASSIGN(int v, Result<int>(Error(Errc::Parse, "p")));
      return v + 1;
    };
    CHECK(chain().error().code == Errc::Parse);
  }

  TEST_CASE("sha256 known vectors (FIPS 180-2)") {
    CHECK(Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 s;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) s.update(chunk);
    CHECK(s.finish_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Incremental update across block boundaries equals one-shot.
    std::string data;
    for (int i = 0; i < 300; ++i) data.push_back(static_cast<char>(i * 7));
    Sha256 inc;
    for (std::size_t i = 0; i < data.size(); i += 13) inc.update(std::string_view(data).substr(i, 13));
    CHECK(inc.finish_hex() == Sha256::hex(data));
  }

  TEST_CASE("hmac-sha256 and pbkdf2 (RFC 4231 / RFC 7914 vectors)") {
    auto mac = hmac_sha256(std::string(20, '\x0b'), "Hi There");
    CHECK(to_hex(mac) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    std::uint8_t out[64];
    pbkdf2_hmac_sha256("passwd", "salt", 1, out, 64);
    CHECK(to_hex(out, 64) ==
          "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
          "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    std::uint8_t out2[32];
    pbkdf2_hmac_sha256("password", "salt", 4096, out2, 32);
    CHECK(to_hex(out2, 32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
  }

  TEST_CASE("base64 (RFC 4648 vectors + Python non-strict decode)") {
    CHECK(base64::encode("") == "");
    CHECK(base64::encode("f") == "Zg==");
    CHECK(base64::encode("fo") == "Zm8=");
    CHECK(base64::encode("foo") == "Zm9v");
    CHECK(base64::encode("foobar") == "Zm9vYmFy");
    CHECK(*base64::decode("Zm9vYmE=") == "fooba");
    CHECK(*base64::decode("Zm9v\nYmFy\r\n") == "foobar");
    CHECK(!base64::decode("Zm9", false));
    CHECK(!base64::decode("Zm9v!", true));
    std::string bin;
    for (int i = 0; i < 256; ++i) bin.push_back(static_cast<char>(i));
    CHECK(*base64::decode(base64::encode(bin)) == bin);
  }

  TEST_CASE("ids") {
    std::string id = gen_id("m_");
    CHECK(id.size() == 14);
    CHECK(is_generated_id(id, "m_"));
    CHECK(!is_generated_id("m_ABCDEF123456", "m_"));
    CHECK(!is_generated_id("c_abc", "c_"));
    CHECK(gen_id("m_") != gen_id("m_"));
    CHECK(gen_id().size() == 12);
  }

  TEST_CASE("time format matches Python isoformat") {
    using namespace std::chrono;
    auto tp = timeutil::Clock::time_point(seconds(1'700'000'000));
    CHECK(timeutil::format_iso_utc(tp) == "2023-11-14T22:13:20Z");  // microsecond == 0 -> no fraction
    CHECK(timeutil::format_iso_utc(tp + microseconds(5)) == "2023-11-14T22:13:20.000005Z");
    CHECK(timeutil::format_iso_utc(tp + microseconds(123456)) == "2023-11-14T22:13:20.123456Z");
    std::regex re(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{6})?Z)");
    CHECK(std::regex_match(timeutil::utc_now_iso(), re));
    auto parsed = timeutil::parse_iso_utc("2023-11-14T22:13:20.000005Z");
    REQUIRE(parsed);
    CHECK(*parsed == tp + microseconds(5));
  }

  TEST_CASE("utf8: Python replace semantics, slicing, strip, lower") {
    CHECK(utf8::is_valid("za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"));
    CHECK(!utf8::is_valid("\xff"));
    CHECK(utf8::repair("a\xffz") == "a\xEF\xBF\xBDz");
    // Truncated 3-byte sequence = one U+FFFD (maximal subpart), then 'A'.
    CHECK(utf8::repair("\xe2\x82" "A") == "\xEF\xBF\xBD" "A");
    // Surrogate encoding ED A0 80: every byte replaced separately.
    CHECK(utf8::decode("\xed\xa0\x80").size() == 3);
    CHECK(utf8::length("zażółć") == 6);
    CHECK(utf8::prefix("zażółć gęślą", 4) == "zażó");
    CHECK(utf8::slice("zażółć", 2, 4) == "żó");
    CHECK(utf8::strip("  \t hi 　\n") == "hi");
    CHECK(utf8::is_blank("  \n"));
    CHECK(!utf8::is_blank(" x "));
    CHECK(utf8::to_lower("ZAŻÓŁĆ GĘŚLĄ JAŹŃ") == "zażółć gęślą jaźń");
    CHECK(utf8::to_lower("İ") == "i̇");
    CHECK(utf8::to_lower("ΟΔΟΣ ΣΑ") == "οδος σα");  // final sigma rule
    CHECK(utf8::to_upper("straße") == "STRASSE");
    CHECK(unicode::is_word(U'ż'));
    CHECK(unicode::is_word(U'_'));
    CHECK(!unicode::is_word(U'-'));
    CHECK(unicode::is_decimal(U'٣'));  // ARABIC-INDIC DIGIT THREE
    CHECK(unicode::is_space(U' '));
    CHECK(!unicode::database_version().empty());
  }

  TEST_CASE("json: Python json.dumps compatibility") {
    Json j = Json::parse(R"({"b": 1, "a": [1.5, true, null, "xé😀"], "e": {}, "f": []})");
    CHECK(json::py_dumps(j) == "{\"b\": 1, \"a\": [1.5, true, null, \"x\\u00e9\\ud83d\\ude00\"], \"e\": {}, \"f\": []}");
    json::DumpOptions o;
    o.indent = 2;
    o.ensure_ascii = false;
    CHECK(json::py_dumps(Json{{"k", Json::array({1, 2})}, {"z", Json::object()}}, o) ==
          "{\n  \"k\": [\n    1,\n    2\n  ],\n  \"z\": {}\n}");
    CHECK(json::py_dumps(Json("a\"b\\c\n\x01\x7f")) == R"("a\"b\\c\n\u0001\u007f")");
    CHECK(json::format_float_py(0.1) == "0.1");
    CHECK(json::format_float_py(1.0) == "1.0");
    CHECK(json::format_float_py(1e16) == "1e+16");
    CHECK(json::format_float_py(1e15) == "1000000000000000.0");
    CHECK(json::format_float_py(1.5e-5) == "1.5e-05");
    CHECK(json::format_float_py(0.0001) == "0.0001");
    CHECK(json::format_float_py(-0.0) == "-0.0");
    CHECK(json::format_float_py(123456789.125) == "123456789.125");
    CHECK(json::canonical(Json{{"b", 1}, {"a", Json{{"d", 2}, {"c", 3}}}}) == R"({"a":{"c":3,"d":2},"b":1})");
    CHECK(!json::parse("{bad"));
    CHECK(json::parse_or("", Json::object()).is_object());
    CHECK(json::py_len(Json("zaż")) == 3);
    CHECK(!json::truthy(Json::array()));
    CHECK(json::truthy(Json(0.5)));
    // Key order is preserved (Python dict semantics).
    Json ord = Json::parse(R"({"z":1,"a":2})");
    CHECK(json::py_dumps(ord) == R"({"z": 1, "a": 2})");
  }

  TEST_CASE("fs: atomic write, perms, temp dir") {
    fsutil::TempDir td;
    REQUIRE(td.valid());
    auto p = td.path() / "sub" / "config.json";
    LOOM_REQUIRE_OK(fsutil::atomic_write(p, "{}", {}));
    CHECK(*fsutil::read_file(p) == "{}");
    CHECK(!std::filesystem::exists(td.path() / "sub" / "config.tmp"));
    fsutil::AtomicWriteOptions o;
    o.owner_only = true;
    LOOM_REQUIRE_OK(fsutil::atomic_write(p, "{\"a\": 1}", o));
    struct stat st{};
    REQUIRE(::stat(p.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    std::filesystem::path kept;
    {
      fsutil::TempDir t2;
      kept = t2.path();
      CHECK(std::filesystem::exists(kept));
    }
    CHECK(!std::filesystem::exists(kept));
  }

  TEST_CASE("log: sinks and ring buffer") {
    log::clear_recent();
    std::vector<std::string> seen;
    int tok = log::add_sink([&](const log::Record& r) { seen.push_back(r.message); });
    log::info("loom.test", "hello {}", 42);
    log::debug("loom.test", "hidden");  // below default Info threshold
    log::remove_sink(tok);
    log::info("loom.test", "after");
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == "hello 42");
    auto lines = log::recent(10);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("[INFO ] loom.test: hello 42") != std::string::npos);
  }
}
