// Internal helpers shared by loom/src/import/*.cpp. Not part of the public
// contract; every free function here mirrors a small piece of
// engine/importer.py so the various *_body() implementations can share it.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/util/json.h"

namespace loom::importer_detail {

// Python f"Import {datetime.now():%Y-%m-%d %H:%M}" / other per-handler
// defaults ("%H:%M", "%Y-%m-%d %H:%M"). `title` overrides when set.
std::string title_or(const std::optional<std::string>& title, const char* prefix, const char* strftime_fmt);

// Python str(x) for a JSON scalar/container reached via a truthy check
// (`str(content) if content else ''`). Containers use Python repr()-shaped
// output (single-quoted strings, True/False/None) since that is what
// Python's str() on a dict/list produces.
std::string py_str(const Json& v);

// One element of engine/importer.py's message normalisation, shared by every
// path that converges on _import_message_list:
//   role = msg.get('role', msg.get('sender', 'user')); 'system' -> dropped;
//   'user'/'human' -> "user", else "assistant"; content from 'content' or
//   'text'; list content -> parts joined by "\n" ("[image]" for image_url);
//   non-string -> str(); blank (after strip) -> dropped.
// Returns nullopt for a dropped message (system role, or blank content).
struct NormalizedMessage {
  std::string role;
  std::string text;
};
std::optional<NormalizedMessage> normalize_message(const Json& msg);

// HTML entity decoding for the small fixed set Python's _strip_html handles
// (&nbsp; &lt; &gt; &amp; &quot;), applied after tag stripping.
std::string decode_basic_entities(std::string_view text);

// Collapse runs of ASCII/Unicode whitespace to a single space and trim, the
// same shape as Python's `re.sub(r'\s+', ' ', text).strip()`.
std::string collapse_ws(std::string_view text);

// Case-insensitive ASCII substring search (used for HTML/MHT/text sniffing
// where the regex engine is unavailable in wave 2).
bool icontains(std::string_view haystack, std::string_view needle) noexcept;
std::size_t ifind(std::string_view haystack, std::string_view needle, std::size_t from = 0) noexcept;
bool istarts_with(std::string_view s, std::string_view prefix) noexcept;

// MIME type recorded on the blob for a detected importer format (best
// effort; not read back by anything, only stored for humans/tools).
std::string mime_for_format(std::string_view fmt);

// `dict.get(key)` presence check (not truthiness): returns the value
// stringified with py_str() when `key` is present with any JSON type, or
// nullopt when the key is absent. Several Python handlers chain these
// (`conv_data.get('name', conv_data.get('title', default))`).
std::optional<std::string> get_present_str(const Json& obj, std::string_view key);

}  // namespace loom::importer_detail
