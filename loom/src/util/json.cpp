#include "loom/util/json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <vector>

#include "loom/util/utf8.h"

namespace loom::json {
namespace {

void append_hex4(std::string& out, unsigned v) {
  static constexpr char kHex[] = "0123456789abcdef";
  out += "\\u";
  out.push_back(kHex[(v >> 12) & 0xF]);
  out.push_back(kHex[(v >> 8) & 0xF]);
  out.push_back(kHex[(v >> 4) & 0xF]);
  out.push_back(kHex[v & 0xF]);
}

void append_py_string(std::string& out, const std::string& s, bool ensure_ascii) {
  out.push_back('"');
  const std::string fixed = utf8::is_valid(s) ? std::string() : utf8::repair(s);
  std::string_view v = fixed.empty() ? std::string_view(s) : std::string_view(fixed);
  std::size_t i = 0;
  while (i < v.size()) {
    auto c = static_cast<unsigned char>(v[i]);
    if (c < 0x80) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
          if (c < 0x20 || (ensure_ascii && c == 0x7F)) {
            append_hex4(out, c);
          } else {
            out.push_back(static_cast<char>(c));
          }
      }
      ++i;
      continue;
    }
    // Multi-byte sequence (valid by construction).
    std::size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    if (!ensure_ascii) {
      out.append(v.substr(i, len));
    } else {
      std::u32string cp = utf8::decode(v.substr(i, len));
      char32_t x = cp.empty() ? utf8::kReplacement : cp[0];
      if (x >= 0x10000) {
        x -= 0x10000;
        append_hex4(out, 0xD800 + static_cast<unsigned>(x >> 10));
        append_hex4(out, 0xDC00 + static_cast<unsigned>(x & 0x3FF));
      } else {
        append_hex4(out, static_cast<unsigned>(x));
      }
    }
    i += len;
  }
  out.push_back('"');
}

void dump_py(std::string& out, const Json& j, const DumpOptions& o, int level, bool sort_keys,
             const char* item_sep, const char* key_sep) {
  using V = Json::value_t;
  auto newline = [&](int lvl) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(o.indent) * static_cast<std::size_t>(lvl), ' ');
  };
  switch (j.type()) {
    case V::null:
    case V::discarded:
    case V::binary:
      out += "null";
      return;
    case V::boolean:
      out += j.get<bool>() ? "true" : "false";
      return;
    case V::number_integer:
      out += std::to_string(j.get<std::int64_t>());
      return;
    case V::number_unsigned:
      out += std::to_string(j.get<std::uint64_t>());
      return;
    case V::number_float: {
      double d = j.get<double>();
      if (std::isnan(d)) {
        out += "NaN";
      } else if (std::isinf(d)) {
        out += d > 0 ? "Infinity" : "-Infinity";
      } else {
        out += format_float_py(d);
      }
      return;
    }
    case V::string:
      append_py_string(out, j.get_ref<const std::string&>(), o.ensure_ascii);
      return;
    case V::array: {
      if (j.empty()) {
        out += "[]";
        return;
      }
      out.push_back('[');
      bool first = true;
      for (const auto& el : j) {
        if (!first) out += item_sep;
        first = false;
        if (o.indent >= 0) newline(level + 1);
        dump_py(out, el, o, level + 1, sort_keys, item_sep, key_sep);
      }
      if (o.indent >= 0) newline(level);
      out.push_back(']');
      return;
    }
    case V::object: {
      if (j.empty()) {
        out += "{}";
        return;
      }
      std::vector<std::pair<const std::string*, const Json*>> items;
      items.reserve(j.size());
      for (auto it = j.begin(); it != j.end(); ++it) items.emplace_back(&it.key(), &it.value());
      if (sort_keys) {
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return *a.first < *b.first; });
      }
      out.push_back('{');
      bool first = true;
      for (const auto& [k, v] : items) {
        if (!first) out += item_sep;
        first = false;
        if (o.indent >= 0) newline(level + 1);
        append_py_string(out, *k, o.ensure_ascii);
        out += key_sep;
        dump_py(out, *v, o, level + 1, sort_keys, item_sep, key_sep);
      }
      if (o.indent >= 0) newline(level);
      out.push_back('}');
      return;
    }
  }
}

}  // namespace

Result<Json> parse(std::string_view text) {
  Json j = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) return Error(Errc::Parse, "invalid JSON");
  return j;
}

Json parse_or(std::string_view text, Json fallback) {
  if (text.empty()) return fallback;
  Json j = Json::parse(text.begin(), text.end(), nullptr, false);
  if (j.is_discarded()) return fallback;
  return j;
}

std::string format_float_py(double v) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
  if (v == 0.0) return std::signbit(v) ? "-0.0" : "0.0";
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
  std::string sci(buf, res.ptr);
  // sci = [-]d[.ddd]e[+-]XX
  bool neg = sci[0] == '-';
  std::size_t p = neg ? 1 : 0;
  std::size_t epos = sci.find('e');
  std::string digits;
  for (std::size_t i = p; i < epos; ++i) {
    if (sci[i] != '.') digits.push_back(sci[i]);
  }
  int exp10 = std::atoi(sci.c_str() + epos + 1);
  int decpt = exp10 + 1;  // value = 0.DIGITS * 10^decpt
  std::string out = neg ? "-" : "";
  if (decpt <= -4 || decpt > 16) {
    out.push_back(digits[0]);
    if (digits.size() > 1) {
      out.push_back('.');
      out.append(digits, 1);
    }
    char eb[16];
    int e = decpt - 1;
    std::snprintf(eb, sizeof eb, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
    out += eb;
  } else if (decpt <= 0) {
    out += "0.";
    out.append(static_cast<std::size_t>(-decpt), '0');
    out += digits;
  } else if (static_cast<std::size_t>(decpt) >= digits.size()) {
    out += digits;
    out.append(static_cast<std::size_t>(decpt) - digits.size(), '0');
    out += ".0";
  } else {
    out.append(digits, 0, static_cast<std::size_t>(decpt));
    out.push_back('.');
    out.append(digits, static_cast<std::size_t>(decpt));
  }
  return out;
}

std::string py_dumps(const Json& value, const DumpOptions& opts) {
  std::string out;
  if (opts.indent >= 0) {
    dump_py(out, value, opts, 0, false, ",", ": ");
  } else {
    dump_py(out, value, opts, 0, false, ", ", ": ");
  }
  return out;
}

std::string canonical(const Json& v) {
  std::string out;
  DumpOptions o;
  o.indent = -1;
  o.ensure_ascii = false;
  dump_py(out, v, o, 0, true, ",", ":");
  return out;
}

std::string dump(const Json& value, int indent) {
  return value.dump(indent, ' ', false, Json::error_handler_t::replace);
}

bool truthy(const Json& v) noexcept {
  using V = Json::value_t;
  switch (v.type()) {
    case V::null:
    case V::discarded:
      return false;
    case V::boolean:
      return v.get<bool>();
    case V::number_integer:
      return v.get<std::int64_t>() != 0;
    case V::number_unsigned:
      return v.get<std::uint64_t>() != 0;
    case V::number_float:
      return v.get<double>() != 0.0;
    case V::string:
      return !v.get_ref<const std::string&>().empty();
    case V::array:
    case V::object:
    case V::binary:
      return !v.empty();
  }
  return false;
}

const Json* find(const Json& obj, std::string_view key) noexcept {
  if (!obj.is_object()) return nullptr;
  auto it = obj.find(key);
  if (it == obj.end()) return nullptr;
  return &*it;
}

std::string get_string(const Json& obj, std::string_view key, std::string_view fallback) {
  const Json* v = find(obj, key);
  if (v && v->is_string()) return v->get<std::string>();
  return std::string(fallback);
}

std::optional<std::string> get_opt_string(const Json& obj, std::string_view key) {
  const Json* v = find(obj, key);
  if (v && v->is_string()) return v->get<std::string>();
  return std::nullopt;
}

double get_number(const Json& obj, std::string_view key, double fallback) {
  const Json* v = find(obj, key);
  if (v && v->is_number()) return v->get<double>();
  return fallback;
}

std::int64_t get_int(const Json& obj, std::string_view key, std::int64_t fallback) {
  const Json* v = find(obj, key);
  if (!v) return fallback;
  if (v->is_number_integer()) return v->get<std::int64_t>();
  if (v->is_number_float()) return static_cast<std::int64_t>(v->get<double>());
  return fallback;
}

bool get_bool(const Json& obj, std::string_view key, bool fallback) {
  const Json* v = find(obj, key);
  if (v && v->is_boolean()) return v->get<bool>();
  return fallback;
}

std::size_t py_len(const Json& v) noexcept {
  if (v.is_array() || v.is_object()) return v.size();
  if (v.is_string()) return utf8::length(v.get_ref<const std::string&>());
  return 0;
}

}  // namespace loom::json
