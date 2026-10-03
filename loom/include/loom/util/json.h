// loom/util/json.h — JSON type + Python-compatible helpers.
//
// loom::Json is nlohmann::ordered_json: object keys keep insertion order, the
// same as Python dicts, so a C++ round-trip of a Python-written JSON column
// preserves key order. All parsing goes through the non-throwing overload.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "loom/result.h"

namespace loom {

using Json = nlohmann::ordered_json;

namespace json {

// Non-throwing parse. Errors -> Errc::Parse.
Result<Json> parse(std::string_view text);
// Parse, returning `fallback` when the text is empty or invalid (Python
// idiom `json.loads(x or "{}")` with defensive recovery).
Json parse_or(std::string_view text, Json fallback);

// Output that is byte-identical to Python's json.dumps():
//   indent < 0  -> separators (", ", ": ")      [json.dumps(obj)]
//   indent >= 0 -> newline + indent, (",", ": ") [json.dumps(obj, indent=n)]
//   ensure_ascii -> non-ASCII as \uXXXX (surrogate pairs above U+FFFF)
// Floats use Python repr() (shortest round-trip; exponent form when
// exp < -4 or exp >= 16), NaN/Infinity like Python. Invalid UTF-8 in strings
// is replaced by U+FFFD.
struct DumpOptions {
  int indent = -1;
  bool ensure_ascii = true;
};
std::string py_dumps(const Json& value, const DumpOptions& opts = {});

// Compact nlohmann dump that never throws (invalid UTF-8 replaced).
std::string dump(const Json& value, int indent = -1);

// Python repr(float).
std::string format_float_py(double v);

// Python truthiness: null/false/0/""/[]/{} are falsy.
bool truthy(const Json& v) noexcept;

// Safe accessors (never throw). `obj` may be any JSON value.
const Json* find(const Json& obj, std::string_view key) noexcept;
std::string get_string(const Json& obj, std::string_view key, std::string_view fallback = "");
std::optional<std::string> get_opt_string(const Json& obj, std::string_view key);
double get_number(const Json& obj, std::string_view key, double fallback = 0.0);
std::int64_t get_int(const Json& obj, std::string_view key, std::int64_t fallback = 0);
bool get_bool(const Json& obj, std::string_view key, bool fallback = false);

// Python len() of a JSON value: array/object size, string code points, else 0.
std::size_t py_len(const Json& v) noexcept;

// Deterministic canonical serialization (sorted keys, compact) used for
// hashing parameters (input_hash / output_hash).
std::string canonical(const Json& v);

}  // namespace json
}  // namespace loom
