#include "importer_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::importer_detail {

std::string title_or(const std::optional<std::string>& title, const char* prefix, const char* strftime_fmt) {
  if (title && !title->empty()) return *title;
  return std::string(prefix) + timeutil::local_now_format(strftime_fmt);
}

namespace {

std::string py_repr_string(const std::string& s) {
  // Python repr(): single-quoted unless the string has a ' and no ", in
  // which case double quotes are used. Escapes backslash, the quote in use,
  // \n \r \t; other control bytes as \xHH. Good enough for import content
  // (which is rarely re-stringified) without pulling in a full repr engine.
  bool has_sq = s.find('\'') != std::string::npos;
  bool has_dq = s.find('"') != std::string::npos;
  char quote = (has_sq && !has_dq) ? '"' : '\'';
  std::string out;
  out.push_back(quote);
  for (unsigned char c : s) {
    if (c == static_cast<unsigned char>(quote) || c == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c < 0x20 || c == 0x7f) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\x%02x", c);
      out += buf;
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  out.push_back(quote);
  return out;
}

std::string py_repr(const Json& v) {
  if (v.is_null()) return "None";
  if (v.is_boolean()) return v.get<bool>() ? "True" : "False";
  if (v.is_number_integer() || v.is_number_unsigned()) return std::to_string(v.get<std::int64_t>());
  if (v.is_number_float()) return json::format_float_py(v.get<double>());
  if (v.is_string()) return py_repr_string(v.get<std::string>());
  if (v.is_array()) {
    std::string out = "[";
    bool first = true;
    for (const auto& e : v) {
      if (!first) out += ", ";
      first = false;
      out += py_repr(e);
    }
    out += "]";
    return out;
  }
  if (v.is_object()) {
    std::string out = "{";
    bool first = true;
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (!first) out += ", ";
      first = false;
      out += py_repr_string(it.key());
      out += ": ";
      out += py_repr(it.value());
    }
    out += "}";
    return out;
  }
  return "";
}

}  // namespace

std::string py_str(const Json& v) {
  if (v.is_null()) return "None";
  if (v.is_boolean()) return v.get<bool>() ? "True" : "False";
  if (v.is_number_integer() || v.is_number_unsigned()) return std::to_string(v.get<std::int64_t>());
  if (v.is_number_float()) return json::format_float_py(v.get<double>());
  if (v.is_string()) return v.get<std::string>();
  return py_repr(v);
}

std::optional<NormalizedMessage> normalize_message(const Json& msg) {
  if (!msg.is_object()) return std::nullopt;

  // Python: `msg.get('role', msg.get('sender', 'user'))`. When 'role' is
  // absent, fall back to 'sender'; when BOTH are absent the literal default
  // is the string 'user' (not "no role" / falsy) — that default itself then
  // passes the 'user'/'human' check below, unlike a present-but-non-string
  // role (which carries through unchanged and never matches, becoming
  // 'assistant', same as Python's `in` check against a non-string value).
  const Json* role_v = json::find(msg, "role");
  if (!role_v) role_v = json::find(msg, "sender");
  std::string role_str;
  bool role_is_string;
  if (role_v) {
    role_is_string = role_v->is_string();
    if (role_is_string) role_str = role_v->get<std::string>();
  } else {
    role_is_string = true;
    role_str = "user";
  }
  if (role_is_string && role_str == "system") return std::nullopt;
  bool is_user = role_is_string && (role_str == "user" || role_str == "human");
  std::string role = is_user ? "user" : "assistant";

  const Json* content = json::find(msg, "content");
  if (!content) content = json::find(msg, "text");
  std::string text;
  if (content) {
    if (content->is_array()) {
      std::vector<std::string> parts;
      for (const auto& part : *content) {
        if (part.is_object()) {
          std::string type = json::get_string(part, "type");
          if (type == "text") {
            parts.push_back(json::get_string(part, "text"));
          } else if (type == "image_url") {
            parts.emplace_back("[image]");
          }
        } else if (part.is_string()) {
          parts.push_back(part.get<std::string>());
        }
      }
      std::string joined;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) joined += "\n";
        joined += parts[i];
      }
      text = joined;
    } else if (content->is_string()) {
      text = content->get<std::string>();
    } else {
      // `str(content) if content else ''`
      text = json::truthy(*content) ? py_str(*content) : "";
    }
  }

  if (utf8::is_blank(text)) return std::nullopt;
  return NormalizedMessage{role, text};
}

std::string decode_basic_entities(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '&') {
      auto match = [&](std::string_view ent, char rep) {
        if (text.compare(i, ent.size(), ent) == 0) {
          out.push_back(rep);
          i += ent.size();
          return true;
        }
        return false;
      };
      if (text.compare(i, 6, "&nbsp;") == 0) {
        out.push_back(' ');
        i += 6;
        continue;
      }
      if (match("&lt;", '<')) continue;
      if (match("&gt;", '>')) continue;
      if (match("&amp;", '&')) continue;
      if (match("&quot;", '"')) continue;
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

std::string collapse_ws(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool in_ws = false;
  for (unsigned char c : text) {
    bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    if (ws) {
      in_ws = true;
      continue;
    }
    if (in_ws && !out.empty()) out.push_back(' ');
    in_ws = false;
    out.push_back(static_cast<char>(c));
  }
  // strip() equivalent: trailing whitespace never appended; leading handled
  // because in_ws only inserts a separator once out is non-empty.
  return out;
}

bool icontains(std::string_view haystack, std::string_view needle) noexcept { return ifind(haystack, needle) != std::string_view::npos; }

std::size_t ifind(std::string_view haystack, std::string_view needle, std::size_t from) noexcept {
  if (needle.empty()) return from <= haystack.size() ? from : std::string_view::npos;
  if (needle.size() > haystack.size()) return std::string_view::npos;
  auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  for (std::size_t i = from; i + needle.size() <= haystack.size(); ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (lower(static_cast<unsigned char>(haystack[i + j])) != lower(static_cast<unsigned char>(needle[j]))) {
        ok = false;
        break;
      }
    }
    if (ok) return i;
  }
  return std::string_view::npos;
}

bool istarts_with(std::string_view s, std::string_view prefix) noexcept {
  if (prefix.size() > s.size()) return false;
  return ifind(s.substr(0, prefix.size()), prefix) == 0;
}

std::optional<std::string> get_present_str(const Json& obj, std::string_view key) {
  const Json* v = json::find(obj, key);
  if (!v) return std::nullopt;
  if (v->is_string()) return v->get<std::string>();
  return py_str(*v);
}

std::string mime_for_format(std::string_view fmt) {
  if (fmt == "zip") return "application/zip";
  if (fmt == "sqlite") return "application/vnd.sqlite3";
  if (fmt == "json") return "application/json";
  if (fmt == "jsonl") return "application/x-ndjson";
  if (fmt == "html") return "text/html";
  if (fmt == "mht") return "message/rfc822";
  if (fmt == "markdown") return "text/markdown";
  if (fmt == "text") return "text/plain";
  if (fmt == "screenshot") return "image/*";
  return "application/octet-stream";
}

}  // namespace loom::importer_detail
