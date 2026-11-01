#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "import/video_source_plan/source_plan.h"
#include "loom/util/sha256.h"

#include <fstream>
#include <limits>
#include <sstream>

using namespace loom;
using namespace loom::video;

namespace {
std::string unhex(std::string_view hex) {
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  std::string out;
  for (std::size_t i = 0; i < hex.size(); i += 2)
    out.push_back(static_cast<char>(digit(hex[i]) * 16 + digit(hex[i + 1])));
  return out;
}
struct Fixture {
  std::string original = std::string("SYNTHETIC_VIDEO\0", 16) + "Zażółć";
  // Two locally generated stdlib/zlib RGB PNGs; no ffmpeg/helper rerun.
  std::string red = unhex("89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
  std::string green = unhex("89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de0000000c49444154789c6360f8cf0000020201007b0981780000000049454e44ae426082");
  std::vector<FrameBytes> frames;
  Json policy, doc;
  std::string receipt, pin;

  Fixture() {
    std::ifstream file(VIDEO_PLAN_POLICY);
    REQUIRE(file.good());
    file >> policy;
    frames = {{red, "Klatka czerwona ą.png"}, {green, "Klatka zielona ł.png"}};
    const auto original_hash = Sha256::hex(original);
    doc = Json{{"schema", "loom.video_basic/1"},
      {"original", {{"sha256", original_hash}, {"size", original.size()}}},
      {"metadata", {{"streams", Json::array({Json{{"index", 0}, {"width", 1}, {"height", 1},
          {"time_base", "1/1000"}, {"start_pts", 2000}, {"duration_ts", 1000}}})}}},
      {"method", {{"id", "ffmpeg.video_basic"}, {"version", "1"}, {"evidence_class", "derived"},
          {"ffprobe_version", "synthetic-native-fixture"}, {"ffmpeg_version", nullptr},
          {"autorotate", false}, {"pixel_format", "rgb24"}}},
      {"limits", {{"frame_step", 2}, {"max_frames", 2}, {"max_input_bytes", 8388608},
          {"max_probe_bytes", 262144}, {"max_width", 1920}, {"max_height", 1080},
          {"max_pixels", 2073600}, {"max_output_bytes", 8388608}}},
      {"sampling", {{"kind", "decoded_prefix"}, {"frame_step", 2}, {"max_frames", 2}, {"whole_video_coverage", false}}},
      {"frames", Json::array()}};
    for (std::size_t i = 0; i < frames.size(); ++i) {
      doc["frames"].push_back(Json{{"ordinal", i}, {"decoded_frame_ordinal", i * 2},
          {"file", i == 0 ? "frame-000000.png" : "frame-000001.png"},
          {"sha256", Sha256::hex(frames[i].png)}, {"size", frames[i].png.size()}, {"mime", "image/png"},
          {"width", 1}, {"height", 1}, {"original_sha256", original_hash}, {"stream_index", 0},
          {"timestamp", {{"pts", 2000}, {"time_base", Json::array({1, 1000})}, {"seconds", Json::array({2, 1})}}}});
    }
  }
  void repin() { receipt = doc.dump() + "\n"; pin = Sha256::hex(receipt); }
  SourcePlanInput input() {
    return {original, "Film zażółć.mkv", "video/x-synthetic", frames, receipt, pin};
  }
  Result<SourcePlan> plan() { repin(); return make_source_plan(input(), policy); }
};
void refused(Result<SourcePlan> result, std::string_view reason) {
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Errc::InvalidArgument);
  CHECK(result.error().message == std::string("video_source_plan:") + std::string(reason));
}
}  // namespace

TEST_CASE("owned bytes become existing native contracts with separate equal-PTS occurrences") {
  Fixture f;
  auto result = f.plan();
  REQUIRE(result);
  const auto& plan = *result;
  CHECK(plan.original.bytes == f.original);
  CHECK(plan.receipt.bytes == f.receipt);
  CHECK(plan.original.source.kind == "file");
  CHECK(plan.receipt.source.kind == "derived");
  CHECK(plan.original.source.title == "Film zażółć.mkv");
  CHECK(plan.original.source.blob_hash == Sha256::hex(f.original));
  CHECK(plan.original.source.uri == "sha256:" + Sha256::hex(f.original));
  CHECK(plan.original.source.size == static_cast<std::int64_t>(f.original.size()));
  CHECK(plan.original_unit.locator.byte_start == 0);
  CHECK(plan.original_unit.locator.byte_len == static_cast<std::int64_t>(f.original.size()));
  CHECK(plan.receipt_unit.locator.byte_len == static_cast<std::int64_t>(f.receipt.size()));
  CHECK(plan.method["receipt_sha256"] == f.pin);
  CHECK(plan.method["policy_sha256"] == Sha256::hex(json::canonical(f.policy)));
  REQUIRE(plan.frames.size() == 2);
  CHECK(plan.frames[0].unit.id != plan.frames[1].unit.id);
  CHECK(plan.frames[0].pixels.source.id != plan.frames[1].pixels.source.id);
  CHECK(plan.frames[0].unit.attrs["original_locator"]["timestamp"] ==
        plan.frames[1].unit.attrs["original_locator"]["timestamp"]);
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& frame = plan.frames[i];
    CHECK(frame.pixels.bytes == f.frames[i].png);
    CHECK(frame.pixels.source.kind == "derived");
    CHECK(frame.pixels.source.size == static_cast<std::int64_t>(frame.pixels.bytes.size()));
    CHECK(frame.pixels.source.blob_hash == Sha256::hex(frame.pixels.bytes));
    CHECK(frame.pixels.source.title == f.frames[i].display_name);
    CHECK(frame.unit.source == plan.receipt.source.id);
    CHECK(frame.unit.id == model::Unit::make_id(frame.unit.source, frame.unit.locator));
    CHECK(frame.unit.locator.json_pointer == "/frames/" + std::to_string(i));
    CHECK_FALSE(frame.unit.locator.byte_start);
    CHECK_FALSE(frame.unit.locator.time_start);
    CHECK(frame.unit.attrs["original_locator"]["source"] == plan.original.source.id);
    CHECK(frame.unit.attrs["original_locator"]["decoded_frame_ordinal"] == i * 2);
    CHECK(frame.unit.attrs["frame_source"] == frame.pixels.source.id);
    CHECK(frame.unit.attrs["evidence_class"] == "derived");
    CHECK(frame.observation.kind == model::ObservationKind::Field);
    CHECK(frame.observation.unit == frame.unit.id);
    CHECK(frame.observation.locator.source == plan.receipt.source.id);
    CHECK(frame.observation.text == f.doc.at(Json::json_pointer(frame.observation.locator.json_pointer)).get<std::string>());
    CHECK(frame.observation.id == model::Observation::make_id(frame.unit.id, frame.observation.locator, frame.observation.text));
    CHECK(frame.observation.attrs["content_basis"] == "receipt_field");
    CHECK(frame.genesis.source.id == frame.observation.id);
    CHECK(frame.genesis.source.group == frame.unit.id);
    CHECK(frame.genesis.source.ordinal == 1);
    CHECK(frame.genesis.source.actor == "ffmpeg.video_basic@1");
    CHECK(frame.genesis.text == frame.observation.text);
    CHECK(frame.genesis.source.when.empty());
    REQUIRE(frame.provenance.size() == 3);
    CHECK(frame.provenance[0].subject_id == frame.pixels.source.id);
    CHECK(frame.provenance[0].source_id == plan.original.source.id);
    CHECK(frame.provenance[0].locator["decoded_frame_ordinal"] == i * 2);
    CHECK(frame.provenance[1].source_id == plan.receipt.source.id);
    CHECK(frame.provenance[2].locator == frame.observation.locator.to_json());
    CHECK(frame.provenance[0].transform == "ffmpeg.video_basic@1:" + f.pin);
    CHECK(frame.image.bytes == frame.pixels.bytes);
    CHECK(frame.image.mime == "image/png");
    CHECK(frame.image.name == f.frames[i].display_name);
    CHECK_FALSE(frame.image.has_ocr);
    CHECK(frame.image.parts.empty()); // Host must perform existing admission/rendering.
    CHECK(frame.image.note["original_sha256"] == Sha256::hex(f.original));
  }
  CHECK(plan.receipt_provenance.subject_id == plan.receipt.source.id);
  CHECK(plan.receipt_provenance.source_id == plan.original.source.id);
}

TEST_CASE("receipt pin is checked before JSON; no partial plan on late wrong frame") {
  Fixture f;
  f.repin();
  f.receipt += " ";
  refused(make_source_plan(f.input(), f.policy), "receipt_hash");
  f.doc["original"]["sha256"] = std::string(64, '0');
  refused(f.plan(), "original_hash");
  f.doc["original"]["sha256"] = Sha256::hex(f.original);
  f.doc["frames"][1]["sha256"] = std::string(64, '0');
  refused(f.plan(), "frame_hash");
}

TEST_CASE("wrong original and frame sizes and frame order fail") {
  Fixture f;
  f.doc["original"]["size"] = f.original.size() + 1;
  refused(f.plan(), "original_size");
  f.doc["original"]["size"] = f.original.size();
  f.doc["frames"][1]["size"] = f.green.size() + 1;
  refused(f.plan(), "frame_size");
  f.doc["frames"][1]["size"] = f.green.size();
  std::swap(f.frames[0], f.frames[1]);
  refused(f.plan(), "frame_hash");
}

TEST_CASE("receipt paths cannot choose a file or a host display name") {
  for (const char* path : {"../frame.png", "/tmp/private.png", "https://invalid/p.png", "frame-000002.png"}) {
    Fixture f;
    f.doc["frames"][1]["file"] = path;
    refused(f.plan(), "frame_order_or_path");
  }
  Fixture f;
  f.doc["path"] = "/tmp/not-an-input";
  refused(f.plan(), "unexpected_fields");
  f.doc.erase("path");
  f.frames[1].display_name = "../outside.png";
  refused(f.plan(), "host_display_name_required");
}

TEST_CASE("unknown methods and semantic or OCR claims cannot satisfy the method contract") {
  Fixture f;
  f.doc["method"]["id"] = "unknown.decoder";
  refused(f.plan(), "unknown_method");
  f.doc["method"]["id"] = "ffmpeg.video_basic";
  f.doc["method"]["version"] = "2";
  refused(f.plan(), "unknown_method");
  f.doc["method"]["version"] = "1";
  f.doc["method"]["evidence_class"] = "observed";
  refused(f.plan(), "method_contract");
  f.doc["method"]["evidence_class"] = "derived";
  f.doc["method"]["autorotate"] = true;
  refused(f.plan(), "method_contract");
  f.doc["method"]["autorotate"] = false;
  f.doc["frames"][0]["ocr"] = "invented interpretation";
  refused(f.plan(), "unexpected_fields");
}

TEST_CASE("dimension, count, byte and PNG admission are independent of receipt policy") {
  Fixture f;
  f.doc["frames"][1]["width"] = 2;
  refused(f.plan(), "frame_bounds");
  f.doc["frames"][1]["width"] = 1;
  f.doc["metadata"]["streams"][0]["width"] = 2;
  f.doc["frames"][0]["width"] = 2;
  f.doc["frames"][1]["width"] = 2;
  refused(f.plan(), "frame_bounds"); // PNG says 1x1, not declared 2x1.
  Fixture bounded;
  bounded.policy["limits"]["frames"] = 1;
  refused(bounded.plan(), "frame_count");
  bounded.policy["limits"]["frames"] = 2;
  bounded.policy["limits"]["frame_bytes"] = bounded.red.size();
  refused(bounded.plan(), "frame_size");
  Fixture broken;
  broken.red[30] ^= 1;
  broken.doc["frames"][0]["sha256"] = Sha256::hex(broken.red);
  refused(broken.plan(), "png_crc");
}

TEST_CASE("ordinal, stream and original bindings cannot be changed in a pinned receipt") {
  Fixture f;
  f.doc["frames"][1]["ordinal"] = 0;
  refused(f.plan(), "frame_order_or_path");
  f.doc["frames"][1]["ordinal"] = 1;
  f.doc["frames"][1]["decoded_frame_ordinal"] = 1;
  refused(f.plan(), "decoded_ordinal");
  f.doc["frames"][1]["decoded_frame_ordinal"] = 2;
  f.doc["frames"][1]["stream_index"] = 1;
  refused(f.plan(), "frame_binding");
  f.doc["frames"][1]["stream_index"] = 0;
  f.doc["frames"][1]["original_sha256"] = std::string(64, '0');
  refused(f.plan(), "frame_binding");
}

TEST_CASE("PTS, timebase, exact rational and declared source time bounds are checked") {
  Fixture f;
  f.doc["frames"][1]["timestamp"]["time_base"][1] = 0;
  refused(f.plan(), "positive_integer_required");
  f.doc["frames"][1]["timestamp"]["time_base"][1] = 500;
  refused(f.plan(), "invalid_time_base");
  f.doc["frames"][1]["timestamp"]["time_base"][1] = 1000;
  f.doc["frames"][1]["timestamp"]["seconds"][0] = 3;
  refused(f.plan(), "timestamp_mismatch");
  f.doc["frames"][1]["timestamp"]["seconds"] = Json::array({1999, 1000});
  f.doc["frames"][1]["timestamp"]["pts"] = 1999;
  refused(f.plan(), "frame_time_order");
  f.doc["frames"][1]["timestamp"]["seconds"] = Json::array({3, 1});
  f.doc["frames"][1]["timestamp"]["pts"] = 3000;
  refused(f.plan(), "frame_time_bounds");
}

TEST_CASE("numeric types, rational overflow and JSON ambiguity fail closed") {
  Fixture f;
  f.doc["frames"][0]["ordinal"] = true;
  refused(f.plan(), "integer_required");
  f.doc["frames"][0]["ordinal"] = 0.0;
  refused(f.plan(), "integer_required");
  f.doc["frames"][0]["ordinal"] = 0;
  f.doc["metadata"]["streams"][0]["time_base"] = "9223372036854775807/1";
  f.doc["frames"][0]["timestamp"]["time_base"] = Json::array({(std::numeric_limits<std::int64_t>::max)(), 1});
  refused(f.plan(), "timestamp_overflow");
  Fixture duplicate;
  duplicate.repin();
  duplicate.receipt.insert(1, "\"schema\":\"duplicate\",");
  duplicate.pin = Sha256::hex(duplicate.receipt);
  refused(make_source_plan(duplicate.input(), duplicate.policy), "duplicate_receipt_key");
}

TEST_CASE("plans own bytes; pure identity is stable and a policy change has separate derived identity") {
  Fixture f;
  auto first = f.plan();
  auto second = f.plan();
  REQUIRE(first); REQUIRE(second);
  CHECK(first->frames[0].unit.id == second->frames[0].unit.id);
  CHECK(first->frames[0].provenance[0].id == second->frames[0].provenance[0].id);
  f.policy["kinds"]["frame_unit"] = "klatka_użytkownika";
  auto changed = f.plan();
  REQUIRE(changed);
  CHECK(changed->original.source.id == first->original.source.id);
  CHECK(changed->frames[0].unit.id != first->frames[0].unit.id);
  CHECK(changed->frames[0].unit.kind == "klatka_użytkownika");
  f.original.clear(); f.red.clear(); f.green.clear(); f.receipt.clear();
  CHECK_FALSE(first->original.bytes.empty());
  CHECK(first->frames[0].image.bytes == first->frames[0].pixels.bytes);
  CHECK(first->frames[0].pixels.source.blob_hash == Sha256::hex(first->frames[0].pixels.bytes));
}
