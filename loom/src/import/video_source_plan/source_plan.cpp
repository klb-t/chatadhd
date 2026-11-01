#include "source_plan.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::video {
namespace {
using I64 = std::int64_t;
struct Invalid { const char* code; };
void need(bool value, const char* code) { if (!value) throw Invalid{code}; }

I64 integer(const Json& v) {
  need(v.is_number_integer(), "integer_required"); // Reject floats and bools, including 0.0/true.
  if (v.is_number_unsigned())
    need(v.get<std::uint64_t>() <= static_cast<std::uint64_t>((std::numeric_limits<I64>::max)()), "integer_overflow");
  return v.get<I64>();
}
I64 positive(const Json& v) {
  const auto n = integer(v);
  need(n > 0, "positive_integer_required");
  return n;
}
std::string string(const Json& v) {
  need(v.is_string(), "string_required");
  return v.get<std::string>();
}
void keys(const Json& v, std::initializer_list<const char*> names) {
  need(v.is_object() && v.size() == names.size(), "unexpected_fields");
  for (const auto* name : names) need(v.contains(name), "missing_field");
}
bool hash_valid(std::string_view s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
void display_name(std::string_view s, I64 max) {
  need(!s.empty() && s.size() <= static_cast<std::size_t>(max) && utf8::is_valid(s) &&
       s != "." && s != ".." && s.find_first_of("/\\\r\n") == s.npos && s.find('\0') == s.npos,
       "host_display_name_required");
}
std::string frame_tag(std::size_t ordinal) {
  auto n = std::to_string(ordinal);
  return "frame-" + std::string(n.size() < 6 ? 6 - n.size() : 0, '0') + n + ".png";
}

Json parse_receipt(std::string_view bytes, I64 max_depth) {
  std::vector<std::set<std::string>> objects;
  auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
    need(depth <= max_depth, "receipt_depth_limit");
    if (event == Json::parse_event_t::object_start) objects.emplace_back();
    if (event == Json::parse_event_t::key)
      need(!objects.empty() && objects.back().insert(string(value)).second, "duplicate_receipt_key");
    if (event == Json::parse_event_t::object_end) objects.pop_back();
    return true;
  };
  return Json::parse(bytes.begin(), bytes.end(), callback);
}

std::uint32_t u32(std::string_view s, std::size_t offset) {
  std::uint32_t out = 0;
  for (std::size_t i = 0; i < 4; ++i) out = (out << 8) | static_cast<unsigned char>(s[offset + i]);
  return out;
}
std::uint32_t crc(std::string_view s) {
  std::uint32_t out = 0xffffffffU;
  for (unsigned char byte : s) {
    out ^= byte;
    for (int bit = 0; bit < 8; ++bit) out = (out >> 1) ^ ((out & 1) ? 0xedb88320U : 0U);
  }
  return out ^ 0xffffffffU;
}
void dimensions(I64 w, I64 h, const Json& limits) {
  need(w > 0 && h > 0 && w <= positive(limits.at("width")) && h <= positive(limits.at("height")) &&
       w <= positive(limits.at("pixels")) / h, "frame_bounds");
}
void png_admission(std::string_view png, I64 w, I64 h) {
  // Container/header/CRC admission only. Pixel decoding remains the existing
  // image path's job; this does not assert visual meaning or OCR accuracy.
  need(png.size() >= 33 && png.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8), "invalid_png");
  std::size_t pos = 8;
  bool header = false, data = false;
  while (pos <= png.size() - 12) {
    const auto len = u32(png, pos);
    need(len <= png.size() - pos - 12, "invalid_png");
    const auto type = png.substr(pos + 4, 4);
    need(crc(png.substr(pos + 4, static_cast<std::size_t>(len) + 4)) == u32(png, pos + 8 + len), "png_crc");
    if (!header) {
      need(type == "IHDR" && len == 13, "invalid_png");
      need(u32(png, pos + 8) == w && u32(png, pos + 12) == h, "frame_bounds");
      need(static_cast<unsigned char>(png[pos + 16]) == 8 && png[pos + 17] == 2 &&
           png[pos + 18] == 0 && png[pos + 19] == 0 && png[pos + 20] == 0, "png_rgb24_required");
      header = true;
    } else need(type != "IHDR", "invalid_png");
    if (type == "IDAT") data = true;
    pos += static_cast<std::size_t>(len) + 12;
    if (type == "IEND") {
      need(len == 0 && data && pos == png.size(), "invalid_png");
      return;
    }
  }
  throw Invalid{"invalid_png"};
}

I64 decimal_positive(std::string_view s) {
  I64 n = 0;
  const auto result = std::from_chars(s.data(), s.data() + s.size(), n);
  need(result.ec == std::errc{} && result.ptr == s.data() + s.size() && n > 0, "invalid_time_base");
  return n;
}
std::pair<I64, I64> stream_base(const Json& stream) {
  const auto s = string(stream.at("time_base"));
  const auto slash = s.find('/');
  need(slash != s.npos, "invalid_time_base");
  const auto num = decimal_positive(std::string_view(s).substr(0, slash));
  const auto den = decimal_positive(std::string_view(s).substr(slash + 1));
  const auto divisor = std::gcd(num, den);
  return {num / divisor, den / divisor};
}
std::pair<I64, I64> seconds(I64 pts, I64 num, I64 den) {
  const auto magnitude = pts < 0 ? static_cast<std::uint64_t>(-(pts + 1)) + 1 : static_cast<std::uint64_t>(pts);
  const auto a = static_cast<I64>(std::gcd(magnitude, static_cast<std::uint64_t>(den)));
  pts /= a; den /= a;
  const auto b = std::gcd(num, den);
  num /= b; den /= b;
  need(pts <= (std::numeric_limits<I64>::max)() / num && pts >= (std::numeric_limits<I64>::min)() / num,
       "timestamp_overflow");
  return {pts * num, den};
}
struct Validated {
  Json doc;
  std::string original_hash, receipt_hash, policy_hash;
};
Validated validate(const SourcePlanInput& in, const Json& policy) {
  need(policy.at("schema") == "loom.video_source_plan_policy/1", "invalid_policy");
  const auto& limits = policy.at("limits");
  for (const auto* k : {"original_bytes", "receipt_bytes", "frame_bytes", "frames", "width", "height",
                        "pixels", "decoded_ordinal", "name_bytes", "json_depth"}) (void)positive(limits.at(k));
  for (const auto* k : {"original_source", "derived_source", "original_unit", "receipt_unit", "frame_unit", "genesis"})
    need(!string(policy.at("kinds").at(k)).empty(), "invalid_policy");
  for (const auto* k : {"original", "receipt", "frame", "provenance"}) {
    const auto prefix = string(policy.at("prefixes").at(k));
    need(!prefix.empty() && std::all_of(prefix.begin(), prefix.end(), [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }), "invalid_policy");
  }
  need(!in.original_bytes.empty() && in.original_bytes.size() <= static_cast<std::size_t>(positive(limits.at("original_bytes"))), "original_size");
  need(!in.receipt_bytes.empty() && in.receipt_bytes.size() <= static_cast<std::size_t>(positive(limits.at("receipt_bytes"))), "receipt_size");
  need(hash_valid(in.receipt_sha256) && Sha256::hex(in.receipt_bytes) == in.receipt_sha256, "receipt_hash");
  display_name(in.original_name, positive(limits.at("name_bytes")));
  need(in.original_mime.starts_with("video/") && in.original_mime.size() <= static_cast<std::size_t>(positive(limits.at("name_bytes"))) &&
       in.original_mime.find_first_of("\r\n") == in.original_mime.npos && in.original_mime.find('\0') == in.original_mime.npos, "video_mime_required");
  Validated out{parse_receipt(in.receipt_bytes, positive(limits.at("json_depth"))), Sha256::hex(in.original_bytes),
                std::string(in.receipt_sha256), Sha256::hex(json::canonical(policy))};
  const auto& doc = out.doc;
  keys(doc, {"schema", "original", "metadata", "method", "limits", "sampling", "frames"});
  need(doc.at("schema") == "loom.video_basic/1", "receipt_schema");
  keys(doc.at("original"), {"sha256", "size"});
  need(doc.at("original").at("sha256") == out.original_hash, "original_hash");
  need(positive(doc.at("original").at("size")) == static_cast<I64>(in.original_bytes.size()), "original_size");
  const auto& method = doc.at("method");
  keys(method, {"id", "version", "evidence_class", "ffprobe_version", "ffmpeg_version", "autorotate", "pixel_format"});
  const auto id = string(method.at("id")), version = string(method.at("version"));
  need(policy.at("methods").is_array(), "invalid_policy");
  bool known = false;
  for (const auto& allowed : policy.at("methods"))
    if (allowed.at("id") == id && allowed.at("version") == version) known = true;
  need(known, "unknown_method");
  need(method.at("evidence_class") == "derived" && method.at("autorotate").is_boolean() &&
       method.at("autorotate") == false && method.at("pixel_format") == "rgb24", "method_contract");
  for (const auto* field : {"ffprobe_version", "ffmpeg_version"})
    need(method.at(field).is_null() || (method.at(field).is_string() && !string(method.at(field)).empty()), "method_contract");
  const auto& sampling = doc.at("sampling");
  keys(sampling, {"kind", "frame_step", "max_frames", "whole_video_coverage"});
  need(sampling.at("kind") == "decoded_prefix" && sampling.at("whole_video_coverage").is_boolean() &&
       sampling.at("whole_video_coverage") == false, "sampling_contract");
  const auto step = positive(sampling.at("frame_step"));
  const auto count_limit = positive(sampling.at("max_frames"));
  const auto& frames = doc.at("frames");
  need(frames.is_array() && !frames.empty() && frames.size() == in.frames.size() &&
       frames.size() <= static_cast<std::size_t>(count_limit) &&
       frames.size() <= static_cast<std::size_t>(positive(limits.at("frames"))) &&
       frames.size() <= static_cast<std::size_t>((std::numeric_limits<int>::max)()), "frame_count");
  const auto& declared = doc.at("limits");
  need(positive(declared.at("frame_step")) == step && positive(declared.at("max_frames")) == count_limit, "sampling_contract");
  need(in.original_bytes.size() <= static_cast<std::size_t>(positive(declared.at("max_input_bytes"))) &&
       in.receipt_bytes.size() <= static_cast<std::size_t>(positive(declared.at("max_probe_bytes"))), "declared_byte_limit");
  const auto& streams = doc.at("metadata").at("streams");
  need(streams.is_array() && streams.size() == 1, "stream_contract");
  const auto& stream = streams.at(0);
  const auto stream_index = integer(stream.at("index"));
  need(stream_index >= 0, "stream_contract");
  const auto w = positive(stream.at("width")), h = positive(stream.at("height"));
  dimensions(w, h, limits);
  need(w <= positive(declared.at("max_width")) && h <= positive(declared.at("max_height")) &&
       w <= positive(declared.at("max_pixels")) / h, "frame_bounds");
  const auto normalized_base = stream_base(stream);
  if (stream.contains("start_pts")) (void)integer(stream.at("start_pts"));
  if (stream.contains("duration_ts")) (void)positive(stream.at("duration_ts"));
  std::pair<I64, I64> base{};
  std::optional<I64> previous;
  I64 total = 0;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto& f = frames.at(i);
    keys(f, {"ordinal", "decoded_frame_ordinal", "file", "sha256", "size", "mime", "width", "height",
             "original_sha256", "stream_index", "timestamp"});
    need(integer(f.at("ordinal")) == static_cast<I64>(i) && f.at("file") == frame_tag(i), "frame_order_or_path");
    need(i == 0 || step <= (std::numeric_limits<I64>::max)() / static_cast<I64>(i), "ordinal_overflow");
    const auto decoded = static_cast<I64>(i) * step;
    need(integer(f.at("decoded_frame_ordinal")) == decoded && decoded <= positive(limits.at("decoded_ordinal")), "decoded_ordinal");
    need(f.at("original_sha256") == out.original_hash && integer(f.at("stream_index")) == stream_index &&
         f.at("mime") == "image/png", "frame_binding");
    need(integer(f.at("width")) == w && integer(f.at("height")) == h, "frame_bounds");
    display_name(in.frames[i].display_name, positive(limits.at("name_bytes")));
    const auto size = positive(f.at("size"));
    need(in.frames[i].png.size() == static_cast<std::size_t>(size) && size <= positive(limits.at("frame_bytes")) - total, "frame_size");
    total += size;
    need(f.at("sha256") == Sha256::hex(in.frames[i].png), "frame_hash");
    png_admission(in.frames[i].png, w, h);
    const auto& t = f.at("timestamp");
    keys(t, {"pts", "time_base", "seconds"});
    const auto pts = integer(t.at("pts"));
    need(t.at("time_base").is_array() && t.at("time_base").size() == 2 &&
         t.at("seconds").is_array() && t.at("seconds").size() == 2, "invalid_time_base");
    const std::pair<I64, I64> here{positive(t.at("time_base").at(0)), positive(t.at("time_base").at(1))};
    const auto divisor = std::gcd(here.first, here.second);
    need(std::pair<I64, I64>{here.first / divisor, here.second / divisor} == normalized_base &&
         (i == 0 || base == here), "invalid_time_base");
    base = here;
    const auto exact = seconds(pts, here.first, here.second);
    need(integer(t.at("seconds").at(0)) == exact.first && positive(t.at("seconds").at(1)) == exact.second, "timestamp_mismatch");
    need(!previous || pts >= *previous, "frame_time_order");
    previous = pts;
    if (stream.contains("start_pts")) {
      const auto start = integer(stream.at("start_pts"));
      need(pts >= start, "frame_time_bounds");
      if (stream.contains("duration_ts")) {
        const auto duration = positive(stream.at("duration_ts"));
        need(start <= (std::numeric_limits<I64>::max)() - duration && pts < start + duration, "frame_time_bounds");
      }
    }
  }
  const auto declared_output = positive(declared.at("max_output_bytes"));
  need(in.receipt_bytes.size() <= static_cast<std::size_t>(declared_output) &&
       total <= declared_output - static_cast<I64>(in.receipt_bytes.size()), "declared_byte_limit");
  return out;
}

SourceSnapshot snapshot(std::string_view bytes, const std::string& id, const std::string& hash,
                        const std::string& kind, std::string_view name, std::string_view mime,
                        std::string_view format, const Json& metadata) {
  SourceSnapshot out;
  out.bytes = bytes;
  auto& s = out.source;
  s.id = id; s.kind = kind; s.uri = "sha256:" + hash; s.blob_hash = hash;
  s.size = static_cast<I64>(bytes.size()); s.title = name; s.mime = mime; s.format = format;
  s.parser = "loom.video_source_plan"; s.parser_version = "1"; s.metadata = metadata;
  return out;
}
model::Unit whole(const SourceRecord& source, const std::string& kind) {
  model::Unit u;
  u.source = source.id; u.kind = kind; u.title = source.title; u.bytes = source.size;
  u.locator.source = source.id; u.locator.byte_start = 0; u.locator.byte_len = source.size;
  u.id = model::Unit::make_id(u.source, u.locator);
  u.attrs = Json{{"source_sha256", source.blob_hash}};
  return u;
}
ProvenanceRecord provenance(const std::string& prefix, const std::string& subject,
                            std::string_view kind, const std::string& source, const Json& locator,
                            const std::string& transform) {
  ProvenanceRecord p;
  p.subject_id = subject; p.subject_kind = kind; p.source_id = source;
  p.locator = locator; p.transform = transform;
  p.id = prefix + Sha256::hex(json::canonical(Json{{"subject", subject}, {"kind", kind},
             {"source", source}, {"locator", locator}, {"transform", transform}}));
  // 1.0 = exact byte/field binding, not confidence in pixels, OCR or a claim.
  p.confidence = 1.0;
  return p;
}
}  // namespace

Result<SourcePlan> make_source_plan(const SourcePlanInput& in, const Json& policy) {
  try {
    const auto verified = validate(in, policy); // No SourcePlan exists until every frame passed.
    const auto& doc = verified.doc;
    const auto& kinds = policy.at("kinds");
    const auto& prefixes = policy.at("prefixes");
    auto kind = [&](const char* key) { return string(kinds.at(key)); };
    auto prefix = [&](const char* key) { return string(prefixes.at(key)); };
    SourcePlan plan;
    const auto actor = string(doc.at("method").at("id")) + "@" + string(doc.at("method").at("version"));
    const auto transform = actor + ":" + verified.receipt_hash;
    const auto identity = Sha256::hex(json::canonical(Json{{"receipt", verified.receipt_hash},
                                                          {"policy", verified.policy_hash}}));
    plan.method = Json{{"schema", "loom.video_source_plan/1"}, {"helper", doc.at("method")},
                       {"receipt_sha256", verified.receipt_hash}, {"policy_sha256", verified.policy_hash},
                       {"plan_sha256", identity}};
    plan.original = snapshot(in.original_bytes, prefix("original") + verified.original_hash,
                            verified.original_hash, kind("original_source"), in.original_name,
                            in.original_mime, "video", Json{{"source_sha256", verified.original_hash}});
    plan.receipt = snapshot(in.receipt_bytes, prefix("receipt") + identity,
                           verified.receipt_hash, kind("derived_source"), in.original_name,
                           "application/json", "loom.video_basic/1",
                           Json{{"original_source", plan.original.source.id}, {"original_sha256", verified.original_hash},
                                {"method", plan.method}, {"evidence_class", "derived"}});
    plan.original_unit = whole(plan.original.source, kind("original_unit"));
    plan.receipt_unit = whole(plan.receipt.source, kind("receipt_unit"));
    plan.receipt_provenance = provenance(prefix("provenance"), plan.receipt.source.id, "source",
        plan.original.source.id, plan.original_unit.locator.to_json(), transform);
    for (std::size_t i = 0; i < in.frames.size(); ++i) {
      const auto& f = doc.at("frames").at(i);
      FrameSourcePlan frame;
      const auto frame_hash = string(f.at("sha256"));
      model::Locator temporal;
      temporal.source = plan.original.source.id;
      temporal.time_start = static_cast<double>(integer(f.at("timestamp").at("seconds").at(0))) /
                            static_cast<double>(positive(f.at("timestamp").at("seconds").at(1)));
      Json origin = temporal.to_json();
      origin["stream_index"] = f.at("stream_index");
      origin["decoded_frame_ordinal"] = f.at("decoded_frame_ordinal");
      origin["timestamp"] = f.at("timestamp");
      Json binding{{"original_source", plan.original.source.id}, {"original_sha256", verified.original_hash},
                   {"receipt_source", plan.receipt.source.id}, {"receipt_sha256", verified.receipt_hash},
                   {"frame_sha256", frame_hash}, {"original_locator", origin},
                   {"method", plan.method}, {"evidence_class", "derived"}};
      frame.pixels = snapshot(in.frames[i].png, prefix("frame") + identity + "_" +
          std::to_string(integer(f.at("decoded_frame_ordinal"))), frame_hash, kind("derived_source"),
          in.frames[i].display_name, "image/png", "png", binding);
      auto& unit = frame.unit;
      unit.source = plan.receipt.source.id; unit.kind = kind("frame_unit"); unit.title = in.frames[i].display_name;
      unit.locator.source = unit.source; unit.locator.json_pointer = "/frames/" + std::to_string(i);
      unit.id = model::Unit::make_id(unit.source, unit.locator);
      unit.attrs = binding; unit.attrs["frame_source"] = frame.pixels.source.id;
      auto& observation = frame.observation;
      observation.unit = unit.id; observation.kind = model::ObservationKind::Field;
      observation.locator = unit.locator; observation.locator.json_pointer += "/sha256";
      observation.text = frame_hash; observation.ordinal = static_cast<int>(i);
      observation.id = model::Observation::make_id(unit.id, observation.locator, observation.text);
      observation.attrs = unit.attrs; observation.attrs["content_basis"] = "receipt_field";
      frame.provenance.push_back(provenance(prefix("provenance"), frame.pixels.source.id, "source",
                                            plan.original.source.id, origin, transform));
      frame.provenance.push_back(provenance(prefix("provenance"), unit.id, "unit", unit.source,
                                            unit.locator.to_json(), transform));
      frame.provenance.push_back(provenance(prefix("provenance"), observation.id, "observation", unit.source,
                                            observation.locator.to_json(), transform));
      frame.genesis.source.id = observation.id; frame.genesis.source.kind = kind("genesis");
      frame.genesis.source.group = unit.id; frame.genesis.source.ordinal = 1; frame.genesis.source.actor = actor;
      frame.genesis.text = observation.text;
      frame.image.bytes = frame.pixels.bytes; frame.image.mime = frame.pixels.source.mime;
      frame.image.name = in.frames[i].display_name; frame.image.has_ocr = false;
      frame.image.note = unit.attrs;
      frame.image.note["unit_id"] = unit.id; frame.image.note["observation_id"] = observation.id;
      plan.frames.push_back(std::move(frame));
    }
    return plan;
  } catch (const Invalid& error) {
    return Error(Errc::InvalidArgument, std::string("video_source_plan:") + error.code);
  } catch (const Json::exception&) {
    return Error(Errc::InvalidArgument, "video_source_plan:invalid_contract");
  } catch (const std::bad_alloc&) {
    return Error(Errc::Unavailable, "video_source_plan:resource_limit");
  }
}
}  // namespace loom::video
