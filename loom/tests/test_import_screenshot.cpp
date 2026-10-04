#include <doctest/doctest.h>
#include <sqlite3.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/sqlite.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
constexpr std::string_view screenshot_text = "Offline screenshot transcription: żółw and branches.";
constexpr std::string_view png_base64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jM/oAAAAASUVORK5CYII=";
constexpr std::string_view binary_base64 = "AP+AAQoNKy89ACJ7fVtdXAABAgMEBQYHCAn+AA==";

// This deliberately contains embedded NULs, high bytes and form-sensitive bytes.
// It is a transport fixture, not a claim that a live OCR engine decodes it.
std::string binary_image() {
  constexpr std::array<unsigned char, 28> bytes{
      0x00, 0xff, 0x80, 0x01, 0x0a, 0x0d, 0x2b, 0x2f, 0x3d, 0x00,
      0x22, 0x7b, 0x7d, 0x5b, 0x5d, 0x5c, 0x00, 0x01, 0x02, 0x03,
      0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xfe, 0x00};
  std::string result;
  for (const auto byte : bytes) result.push_back(static_cast<char>(byte));
  return result;
}

struct ImageFormat {
  std::string extension;
  std::string declared;
};
const std::array<ImageFormat, 8> image_formats{{
    {"png", "png"}, {"jpg", "jpg"}, {"jpeg", "jpeg"}, {"webp", "webp"},
    {"PNG", "png"}, {"JpG", "jpg"}, {"JPEG", "jpeg"}, {"WeBp", "webp"}}};

// A malformed URI cannot be hidden by a successful scripted OCR response.
// This transport rejects any request other than the exact offline expectation.
class StrictScreenshotTransport final : public net::HttpTransport {
 public:
  StrictScreenshotTransport(std::string_view format, std::string_view expected_base64)
      : expected_body_("apikey=offline-test-key&base64Image=data%3Aimage%2F" + std::string(format) +
          "%3Bbase64%2C" + net::url_encode(expected_base64) +
          "&language=eng&OCREngine=2&isTable=True&scale=True") {}

  Result<net::HttpResponse> send(const net::HttpRequest& request, const net::StreamSink* = nullptr,
                                const CancelToken* = nullptr) override {
    requests.push_back(request);
    if (request.method != "POST" || request.url != "https://api.ocr.space/parse/image" ||
        net::header_value(request.headers, "Content-Type") != "application/x-www-form-urlencoded" ||
        request.body != expected_body_) {
      return Error(Errc::InvalidArgument, "unexpected offline screenshot MIME or image bytes");
    }
    ++accepted;
    net::HttpResponse response;
    response.status = 200;
    response.body = Json{{"IsErroredOnProcessing", false},
        {"ParsedResults", Json::array({Json{{"ParsedText", std::string(screenshot_text)}}})}}.dump();
    return response;
  }
  std::string name() const override { return "strict-offline-screenshot"; }
  const std::string& expected_body() const { return expected_body_; }
  std::vector<net::HttpRequest> requests;
  int accepted = 0;

 private:
  std::string expected_body_;
};

struct ScreenshotFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "source.db");
  EventBus bus;
  BlobStore blobs{temporary.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  Secrets secrets{temporary.path() / "secrets.json"};
  StrictScreenshotTransport transport;
  MediaProviders media;
  ConversationImporter importer;

  ScreenshotFixture(std::string_view format, std::string_view expected_base64)
      : transport(format, expected_base64), media(secrets, transport),
        importer(*db, bus, &blobs, &provenance, &media) {
    secrets.set("ocr_space_api_key", "offline-test-key");
    media.refresh();
  }

  std::filesystem::path write(std::string_view bytes, std::string_view extension) {
    const auto path = temporary.path() / ("capture." + std::string(extension));
    LOOM_REQUIRE_OK(fsutil::write_file(path, bytes));
    return path;
  }

  void check_transcription(const Conversation& conversation) {
    const auto messages = unwrap(db->get_msgs(conversation.id, true));
    REQUIRE(messages.size() == 1);
    CHECK(messages.front().role == "assistant");
    CHECK(messages.front().text == screenshot_text);
    REQUIRE(transport.requests.size() == 1);
    CHECK(transport.accepted == 1);
    CHECK(transport.requests.front().body == transport.expected_body());
  }

  SourceRecord check_source(const Conversation& conversation, const std::filesystem::path& original,
                            std::string_view bytes, std::string_view format) {
    check_transcription(conversation);
    const auto sources = unwrap(provenance.list_sources());
    REQUIRE(sources.size() == 1);
    const auto& source = sources.front();
    CHECK(source.uri == original.string());
    CHECK(source.kind == "file");
    CHECK(source.format == "screenshot");
    CHECK(source.mime == "image/" + std::string(format));
    CHECK(source.metadata["declared_image_format"] == format);
    CHECK(source.blob_hash == Sha256::hex(bytes));
    CHECK(source.size == static_cast<std::int64_t>(bytes.size()));
    CHECK(blobs.path_for(source.blob_hash).extension().empty());
    CHECK(unwrap(blobs.read(source.blob_hash)) == bytes);
    LOOM_REQUIRE_OK(blobs.verify(source.blob_hash));
    const auto blob_mime = unwrap(db->conn().query_text("SELECT mime FROM loom_blobs WHERE hash=?", source.blob_hash));
    REQUIRE(blob_mime);
    CHECK(*blob_mime == "image/" + std::string(format));
    const auto conversation_provenance = unwrap(provenance.for_subject(conversation.id));
    REQUIRE(conversation_provenance.size() == 1);
    CHECK(conversation_provenance.front().subject_kind == "conversation");
    CHECK(conversation_provenance.front().source_id == source.id);
    const auto messages = unwrap(db->get_msgs(conversation.id, true));
    REQUIRE(messages.size() == 1);
    const auto message_provenance = unwrap(provenance.for_subject(messages.front().id));
    REQUIRE(message_provenance.size() == 1);
    CHECK(message_provenance.front().subject_kind == "message");
    CHECK(message_provenance.front().source_id == source.id);
    return source;
  }
};

// The source INSERT follows the completed blob copy and admission checks but
// precedes body dispatch. Mutating only the filesystem here is deterministic;
// the callback never reenters SQLite or invokes a provider.
class SourceMutation {
 public:
  SourceMutation(Database& db, std::filesystem::path original, std::filesystem::path snapshot,
                 std::string expected_bytes, bool remove_original)
      : connection_(db.conn().handle()), original_(std::move(original)), snapshot_(std::move(snapshot)),
        expected_bytes_(std::move(expected_bytes)), remove_original_(remove_original) {
    sqlite3_update_hook(connection_, &SourceMutation::updated, this);
  }
  ~SourceMutation() { sqlite3_update_hook(connection_, nullptr, nullptr); }
  SourceMutation(const SourceMutation&) = delete;
  SourceMutation& operator=(const SourceMutation&) = delete;
  bool fired = false;
  bool snapshot_matches = false;
  std::string error;

 private:
  static void updated(void* context, int operation, const char*, const char* table, sqlite3_int64) {
    auto& self = *static_cast<SourceMutation*>(context);
    if (operation != SQLITE_INSERT || std::string_view(table) != "loom_sources" || self.fired) return;
    self.fired = true;
    const auto snapshot = fsutil::read_file(self.snapshot_);
    self.snapshot_matches = snapshot && *snapshot == self.expected_bytes_;
    if (self.remove_original_) {
      std::error_code failure;
      if (!std::filesystem::remove(self.original_, failure)) self.error = failure ? failure.message() : "original absent";
    } else {
      std::string changed = self.expected_bytes_;
      changed.front() = 'X';
      const auto written = fsutil::write_file(self.original_, changed);
      if (!written) self.error = written.error().to_string();
    }
  }
  sqlite3* connection_;
  std::filesystem::path original_;
  std::filesystem::path snapshot_;
  std::string expected_bytes_;
  bool remove_original_;
};
}  // namespace

TEST_SUITE("import_screenshot") {
  TEST_CASE("import_file preserves declared screenshot formats with source provenance") {
    for (const auto& format : image_formats) {
      SUBCASE(format.extension.c_str()) {
        ScreenshotFixture fixture(format.declared, binary_base64);
        const auto bytes = binary_image();
        const auto path = fixture.write(bytes, format.extension);
        const auto result = unwrap(fixture.importer.import_file(path));
        REQUIRE(result.conversations.size() == 1);
        CHECK(result.format == "screenshot");
        CHECK(result.messages == 1);
        const auto source = fixture.check_source(result.conversations.front(), path, bytes, format.declared);
        CHECK(result.source_id == source.id);
        CHECK(result.blob_hash == source.blob_hash);
      }
    }
  }

  TEST_CASE("direct screenshot import preserves declared format with source provenance") {
    for (const auto& format : image_formats) {
      SUBCASE(format.extension.c_str()) {
        ScreenshotFixture fixture(format.declared, binary_base64);
        const auto bytes = binary_image();
        const auto path = fixture.write(bytes, format.extension);
        const auto conversations = unwrap(fixture.importer.import_screenshot(path));
        REQUIRE(conversations.size() == 1);
        fixture.check_source(conversations.front(), path, bytes, format.declared);
      }
    }
  }

  TEST_CASE("no-provenance screenshot controls preserve declared format and bytes") {
    for (const bool direct : {false, true}) {
      SUBCASE(direct ? "import_screenshot" : "import_file") {
        for (const auto& format : image_formats) {
          SUBCASE(format.extension.c_str()) {
            ScreenshotFixture fixture(format.declared, binary_base64);
            const auto bytes = binary_image();
            const auto path = fixture.write(bytes, format.extension);
            ImportOptions options;
            options.record_provenance = false;
            std::vector<Conversation> conversations;
            if (direct) conversations = unwrap(fixture.importer.import_screenshot(path, options));
            else {
              const auto result = unwrap(fixture.importer.import_file(path, options));
              CHECK(result.source_id.empty()); CHECK(result.blob_hash.empty());
              conversations = result.conversations;
            }
            REQUIRE(conversations.size() == 1);
            fixture.check_transcription(conversations.front());
            CHECK(unwrap(fixture.provenance.list_sources()).empty());
            CHECK(unwrap(fixture.provenance.for_subject(conversations.front().id)).empty());
            CHECK_FALSE(fixture.blobs.has(Sha256::hex(bytes)));
          }
        }
      }
    }
  }

  TEST_CASE("tiny PNG screenshot replay preserves exact bytes on all original routes") {
    bool direct = false;
    bool record_provenance = true;
    SUBCASE("import_file with provenance") {}
    SUBCASE("import_screenshot with provenance") { direct = true; }
    SUBCASE("import_file without provenance") { record_provenance = false; }
    ScreenshotFixture fixture("png", png_base64);
    const auto bytes = unwrap(base64::decode(png_base64, true));
    const auto path = fixture.write(bytes, "png");
    ImportOptions options;
    options.record_provenance = record_provenance;
    const auto conversations = direct ? unwrap(fixture.importer.import_screenshot(path, options))
        : unwrap(fixture.importer.import_file(path, options)).conversations;
    REQUIRE(conversations.size() == 1);
    if (record_provenance) fixture.check_source(conversations.front(), path, bytes, "png");
    else fixture.check_transcription(conversations.front());
  }

  TEST_CASE("screenshot dispatch reads admitted snapshot after original replacement or removal") {
    bool direct = false;
    bool remove_original = false;
    SUBCASE("import_file original overwritten") {}
    SUBCASE("import_file original removed") { remove_original = true; }
    SUBCASE("import_screenshot original overwritten") { direct = true; }
    SUBCASE("import_screenshot original removed") { direct = true; remove_original = true; }
    ScreenshotFixture fixture("jpeg", binary_base64);
    const auto bytes = binary_image();
    const auto path = fixture.write(bytes, "JpEg");
    ImportOptions options;
    options.expected_source_hash = Sha256::hex(bytes);
    options.expected_source_bytes = static_cast<std::int64_t>(bytes.size());
    SourceMutation mutation(*fixture.db, path, fixture.blobs.path_for(*options.expected_source_hash), bytes, remove_original);
    const auto conversations = direct ? unwrap(fixture.importer.import_screenshot(path, options))
        : unwrap(fixture.importer.import_file(path, options)).conversations;
    CHECK(mutation.fired); CHECK(mutation.snapshot_matches); CHECK(mutation.error.empty());
    if (remove_original) CHECK_FALSE(std::filesystem::exists(path));
    else CHECK(unwrap(fsutil::read_file(path)) != bytes);
    REQUIRE(conversations.size() == 1);
    fixture.check_source(conversations.front(), path, bytes, "jpeg");
  }

  TEST_CASE("screenshot preflight mutation rejects stale admission before OCR dispatch") {
    ScreenshotFixture fixture("png", binary_base64);
    const auto bytes = binary_image();
    const auto path = fixture.write(bytes, "png");
    ImportOptions options;
    options.expected_source_hash = Sha256::hex(bytes);
    options.expected_source_bytes = static_cast<std::int64_t>(bytes.size());
    bool preflight_called = false;
    options.preflight = [&](const auto& admitted_path) -> Status {
      CHECK(admitted_path == path);
      preflight_called = true;
      std::string changed = bytes;
      changed.front() = 'X';
      return fsutil::write_file(admitted_path, changed);
    };
    const auto result = fixture.importer.import_file(path, options);
    CHECK(preflight_called);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Conflict);
    CHECK(fixture.transport.requests.empty());
    CHECK(unwrap(fixture.provenance.list_sources()).empty());
    CHECK(unwrap(fixture.db->conn().query_int("SELECT COUNT(*) FROM conversations")).value_or(-1) == 0);
  }
}
