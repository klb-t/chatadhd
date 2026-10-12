// HTML / MHT: a hand-written tag scanner (ConversationHTMLParser port), a
// label-based regex-shaped fallback (_extract_messages_regex, without a
// regex engine — re::Regex is still a wave-2 stub in this worktree) and
// _strip_html. MHT unwraps the MIME multipart envelope to find the HTML (or
// base64) part and reuses the same HTML parsing.
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

#include "importer_internal.h"
#include "import/import_recognition.h"
#include "loom/importer.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/utf8.h"

namespace loom {

namespace fs = std::filesystem;
namespace idt = importer_detail;

// ── strip_html (Python _strip_html) ─────────────────────────────────
std::string ConversationImporter::strip_html(std::string_view html) {
  std::string no_tags;
  no_tags.reserve(html.size());
  std::size_t i = 0;
  std::size_t n = html.size();
  while (i < n) {
    if (html[i] != '<') {
      no_tags.push_back(html[i]);
      ++i;
      continue;
    }
    std::size_t close = html.find('>', i + 1);
    if (close == std::string_view::npos) {
      // No '>' anywhere ahead: `<[^>]+>` cannot match again; the remainder
      // (including this '<') is literal text, same as Python's re.sub.
      no_tags.append(html.substr(i));
      break;
    }
    if (close == i + 1) {
      // "<>" : `[^>]+` needs >=1 char, so this "<>" is not a tag match.
      no_tags.push_back('<');
      ++i;
      continue;
    }
    no_tags.push_back(' ');
    i = close + 1;
  }
  return idt::collapse_ws(idt::decode_basic_entities(no_tags));
}

// ── ConversationHTMLParser port ─────────────────────────────────────
Json ConversationImporter::parse_html_messages(std::string_view html) {
  Json messages = Json::array();
  std::string current_role;
  std::vector<std::string> current_text;
  bool in_message = false;
  int depth = 0;

  auto finish = [&] {
    if (!current_role.empty() && !current_text.empty()) {
      std::string content;
      for (std::size_t i = 0; i < current_text.size(); ++i) {
        if (i) content += " ";
        content += current_text[i];
      }
      content = std::string(utf8::strip(content));
      if (!content.empty() && utf8::length(content) > 5) {
        messages.push_back(Json{{"role", current_role}, {"content", content}});
      }
    }
    current_role.clear();
    current_text.clear();
    in_message = false;
  };

  std::size_t i = 0, n = html.size();
  while (i < n) {
    if (html[i] != '<') {
      std::size_t start = i;
      while (i < n && html[i] != '<') ++i;
      if (in_message && !current_role.empty()) {
        std::string chunk = idt::decode_basic_entities(html.substr(start, i - start));
        std::string trimmed = std::string(utf8::strip(chunk));
        if (!trimmed.empty()) current_text.push_back(trimmed);
      }
      continue;
    }
    if (html.compare(i, 4, "<!--") == 0) {
      std::size_t close = html.find("-->", i + 4);
      i = (close == std::string_view::npos) ? n : close + 3;
      continue;
    }
    if (i + 1 < n && (html[i + 1] == '!' || html[i + 1] == '?')) {
      std::size_t close = html.find('>', i);
      i = (close == std::string_view::npos) ? n : close + 1;
      continue;
    }
    bool end_tag = (i + 1 < n && html[i + 1] == '/');
    std::size_t p = i + 1 + (end_tag ? 1 : 0);
    std::size_t name_start = p;
    while (p < n && (std::isalnum(static_cast<unsigned char>(html[p])) || html[p] == ':' || html[p] == '-')) ++p;
    if (p == name_start) {
      // Not a real tag ('<' not followed by a name char); emit the '<'
      // literally and resume scanning at the next byte, like an HTML
      // tokenizer falling back to bogus-comment/text handling.
      if (in_message && !current_role.empty()) current_text.push_back("<");
      ++i;
      continue;
    }
    std::string tag_name;
    for (std::size_t k = name_start; k < p; ++k)
      tag_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(html[k]))));

    std::unordered_map<std::string, std::string> attrs;
    bool self_close = false;
    while (p < n && html[p] != '>') {
      while (p < n && std::isspace(static_cast<unsigned char>(html[p]))) ++p;
      if (p < n && html[p] == '/') {
        self_close = true;
        ++p;
        continue;
      }
      if (p >= n || html[p] == '>') break;
      std::size_t an_start = p;
      while (p < n && html[p] != '=' && html[p] != '>' && html[p] != '/' && !std::isspace(static_cast<unsigned char>(html[p])))
        ++p;
      std::string attr_name;
      for (std::size_t k = an_start; k < p; ++k)
        attr_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(html[k]))));
      while (p < n && std::isspace(static_cast<unsigned char>(html[p]))) ++p;
      std::string attr_val;
      if (p < n && html[p] == '=') {
        ++p;
        while (p < n && std::isspace(static_cast<unsigned char>(html[p]))) ++p;
        if (p < n && (html[p] == '"' || html[p] == '\'')) {
          char q = html[p];
          ++p;
          std::size_t vs = p;
          while (p < n && html[p] != q) ++p;
          attr_val.assign(html.substr(vs, p - vs));
          if (p < n) ++p;
        } else {
          std::size_t vs = p;
          while (p < n && !std::isspace(static_cast<unsigned char>(html[p])) && html[p] != '>') ++p;
          attr_val.assign(html.substr(vs, p - vs));
        }
      }
      if (!attr_name.empty()) attrs[attr_name] = attr_val;
    }
    i = (p < n) ? p + 1 : n;

    if (!end_tag) {
      std::string classes = attrs.count("class") ? attrs["class"] : "";
      std::string data_role;
      if (auto it = attrs.find("data-role"); it != attrs.end()) {
        for (char c : it->second) data_role.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      }
      bool is_user = idt::icontains(classes, "human") || idt::icontains(classes, "user") || data_role == "human" ||
                     data_role == "user";
      bool is_assistant = !is_user && (idt::icontains(classes, "assistant") || idt::icontains(classes, "ai") ||
                                       data_role == "assistant" || data_role == "ai");
      if (is_user) {
        finish();
        current_role = "user";
        in_message = true;
        depth = 0;
      } else if (is_assistant) {
        finish();
        current_role = "assistant";
        in_message = true;
        depth = 0;
      }
      if (in_message) ++depth;
      if (self_close && in_message) {
        --depth;
        if (depth <= 0) finish();
      }
    } else if (in_message) {
      --depth;
      if (depth <= 0) finish();
    }
  }
  finish();
  return messages;
}

// ── Label-based fallback (Python _extract_messages_regex) ───────────
namespace {

std::string strip_tag_blocks(std::string_view html, std::string_view tag) {
  std::string out;
  out.reserve(html.size());
  std::string open_prefix = "<" + std::string(tag);
  std::string close_tag = "</" + std::string(tag) + ">";
  std::size_t pos = 0;
  while (pos < html.size()) {
    std::size_t open_pos = idt::ifind(html, open_prefix, pos);
    if (open_pos == std::string_view::npos) {
      out.append(html.substr(pos));
      break;
    }
    std::size_t tag_end = html.find('>', open_pos);
    if (tag_end == std::string_view::npos) {
      out.append(html.substr(pos));
      break;
    }
    std::size_t close_pos = idt::ifind(html, close_tag, tag_end);
    if (close_pos == std::string_view::npos) {
      // No matching close tag: `.*?</tag>` cannot match; leave verbatim and
      // keep scanning after this open tag to avoid re-matching it forever.
      out.append(html.substr(pos, tag_end + 1 - pos));
      pos = tag_end + 1;
      continue;
    }
    out.append(html.substr(pos, open_pos - pos));
    pos = close_pos + close_tag.size();
  }
  return out;
}

std::string strip_script_and_style(std::string_view html) {
  return strip_tag_blocks(strip_tag_blocks(html, "script"), "style");
}

struct LabelHit {
  std::size_t pos;
  std::size_t token_len;  // "Label:".size()
};

std::optional<LabelHit> nearest_label(std::string_view html, const std::vector<std::string>& labels,
                                      std::size_t from) {
  std::optional<LabelHit> best;
  for (const auto& lbl : labels) {
    std::string needle = lbl + ":";
    std::size_t p = idt::ifind(html, needle, from);
    if (p == std::string_view::npos) continue;
    if (!best || p < best->pos) best = LabelHit{p, needle.size()};
  }
  return best;
}

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

// One `for match in re.finditer(pattern, html, DOTALL|IGNORECASE)` pass:
// captures the span from just after each `own` label's ":" (plus the
// whitespace `\s*` eats) up to the next `stop` label or end of string.
std::vector<std::pair<std::size_t, std::string>> scan_labelled_blocks(
    std::string_view html, const std::vector<std::string>& own, const std::vector<std::string>& stop) {
  std::vector<std::pair<std::size_t, std::string>> out;
  std::size_t pos = 0;
  while (pos <= html.size()) {
    auto hit = nearest_label(html, own, pos);
    if (!hit) break;
    std::size_t content_start = hit->pos + hit->token_len;
    while (content_start < html.size() && is_ws(html[content_start])) ++content_start;
    auto stop_hit = nearest_label(html, stop, content_start);
    std::size_t end = stop_hit ? stop_hit->pos : html.size();
    out.emplace_back(hit->pos, html.substr(content_start, end - content_start));
    pos = end;
  }
  return out;
}

const std::vector<std::string>& user_labels() {
  static const std::vector<std::string> v{"Human", "User", "You"};
  return v;
}
const std::vector<std::string>& assistant_labels() {
  static const std::vector<std::string> v{"Assistant", "AI", "Claude", "ChatGPT"};
  return v;
}

}  // namespace

Json ConversationImporter::extract_messages_regex(std::string_view html) {
  std::string stripped = strip_script_and_style(html);
  std::vector<std::pair<std::size_t, Json>> messages;
  for (const auto& [pos, raw] : scan_labelled_blocks(stripped, user_labels(), assistant_labels())) {
    std::string text = strip_html(raw);
    if (!text.empty() && utf8::length(text) > 10)
      messages.emplace_back(pos, Json{{"role", "user"}, {"content", text}});
  }
  for (const auto& [pos, raw] : scan_labelled_blocks(stripped, assistant_labels(), user_labels())) {
    std::string text = strip_html(raw);
    if (!text.empty() && utf8::length(text) > 10)
      messages.emplace_back(pos, Json{{"role", "assistant"}, {"content", text}});
  }
  std::stable_sort(messages.begin(), messages.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  Json out = Json::array();
  for (auto& [pos, message] : messages) {
    (void)pos;
    out.push_back(std::move(message));
  }
  return out;
}

// ── parse_html_conversation / import_html / import_mht ──────────────
Result<std::optional<Conversation>> ConversationImporter::parse_html_conversation(
    std::string_view html, const std::optional<std::string>& title, const std::optional<fs::path>& source) {
  html_text_fallback_ = false;
  Json messages = parse_html_messages(html);
  if (messages.empty()) messages = extract_messages_regex(html);
  if (messages.empty()) {
    std::string text = strip_html(html);
    if (!text.empty()) {
      html_text_fallback_ = true;
      messages = Json::array();
      messages.push_back(Json{{"role", "assistant"}, {"content", std::string(utf8::prefix(text, 50000))}});
    }
  }
  if (messages.empty()) return std::optional<Conversation>{};

  std::optional<std::string> use_title = title;
  if ((!use_title || use_title->empty()) && source) use_title = source->stem().string();
  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, use_title));
  return std::optional<Conversation>(std::move(conv));
}

Result<std::vector<Conversation>> ConversationImporter::html_body(const fs::path& path, const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(std::string raw, fsutil::read_file(path));
  std::string html = utf8::repair(raw);  // Python: open(..., errors='replace')
  const fs::path identity = current_source_ && !current_source_->source_filename.empty()
      ? fs::path(current_source_->source_filename) : path;
  if (ledger_) ledger_->measured("html");
  LOOM_TRY_ASSIGN(auto conv, parse_html_conversation(html, opts.title, identity));
  std::vector<Conversation> out;
  if (conv) {
    LOOM_TRY(keep_html_fallback(raw, 0, *conv));
    out.push_back(std::move(*conv));
  }
  return out;
}

// R44: when neither HTML parser found a conversation structure the page was
// imported as one text message; the markup itself is kept verbatim, partial.
Status ConversationImporter::keep_html_fallback(std::string_view bytes, std::size_t offset,
                                                const Conversation& conversation) {
  if (!ledger_ || !html_text_fallback_) return {};
  html_text_fallback_ = false;
  FragmentRecord record;
  record.status = std::string(recognition::kPartial);
  record.recognizer = "loom.importer.html";
  record.recognizer_version = std::string(kImporterParserVersion);
  record.reason = "no conversation structure recognised; the page text was imported as one assistant message";
  record.label = "unstructured HTML page";
  record.locator = Json{{"byte_start", static_cast<std::int64_t>(offset)},
                        {"byte_end", static_cast<std::int64_t>(offset + bytes.size())}};
  record.conversation_id = conversation.id;
  record.bytes = bytes;
  record.attempts = Json::array({Json{{"recognizer", "loom.importer.html"},
                                       {"version", std::string(kImporterParserVersion)},
                                       {"outcome", "partial"}, {"reason", record.reason}}});
  auto kept = keep_fragment(std::move(record));
  if (!kept) return kept.error();
  return {};
}

Result<std::vector<Conversation>> ConversationImporter::import_html(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "html", opts, "file", [&](const fs::path& input_path) { return html_body(input_path, opts); });
}

Result<std::vector<Conversation>> ConversationImporter::mht_body(const fs::path& path, const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(std::string content, fsutil::read_file(path));

  std::vector<std::string_view> parts;
  {
    std::string_view sep = "------=_";
    std::size_t pos = 0;
    while (true) {
      std::size_t next = content.find(sep, pos);
      if (next == std::string::npos) {
        parts.emplace_back(std::string_view(content).substr(pos));
        break;
      }
      parts.emplace_back(std::string_view(content).substr(pos, next - pos));
      pos = next + sep.size();
    }
  }

  std::optional<std::string> html;
  std::size_t chosen_part = parts.size();
  std::size_t part_index = 0;
  for (auto part : parts) {
    struct Advance { std::size_t& i; ~Advance() { ++i; } } advance{part_index};
    std::string_view head = part.substr(0, std::min<std::size_t>(part.size(), 500));
    if (head.find("text/html") == std::string_view::npos) continue;  // case-sensitive, matches Python `b'text/html' in`

    std::size_t html_start = part.find("<html");
    if (html_start == std::string_view::npos) html_start = part.find("<HTML");
    if (html_start == std::string_view::npos) html_start = part.find("<!DOCTYPE");
    if (html_start != std::string_view::npos) {
      html = utf8::repair(part.substr(html_start));
      chosen_part = part_index;
      break;
    }

    std::string_view head200 = part.substr(0, std::min<std::size_t>(part.size(), 200));
    if (head200.find("base64") != std::string_view::npos) {
      std::size_t b64_start = part.find("\r\n\r\n");
      if (b64_start != std::string_view::npos) {
        std::string b64_data = std::string(utf8::strip(part.substr(b64_start)));
        if (auto decoded = base64::decode(b64_data)) {
          html = utf8::repair(*decoded);
          chosen_part = part_index;
          break;
        }
      }
    }
  }

  std::vector<Conversation> out;
  if (ledger_) ledger_->measured("mht");
  if (html) {
    const fs::path identity = current_source_ && !current_source_->source_filename.empty()
        ? fs::path(current_source_->source_filename) : path;
    LOOM_TRY_ASSIGN(auto conv, parse_html_conversation(*html, opts.title, identity));
    if (conv) {
      LOOM_TRY(keep_html_fallback(*html, static_cast<std::size_t>(parts[chosen_part].data() - content.data()), *conv));
      out.push_back(std::move(*conv));
    }
  }
  if (ledger_) {
    // R44: every MIME part the handler did not use (images, styles, preamble,
    // parts after the page) is kept verbatim with its byte range.
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i == chosen_part) continue;
      const std::string_view part = parts[i];
      if (utf8::strip(part).empty()) continue;
      const auto start = static_cast<std::int64_t>(part.data() - content.data());
      std::string content_type;
      const std::string_view head = part.substr(0, std::min<std::size_t>(part.size(), 500));
      if (auto at = idt::ifind(head, "content-type:"); at != std::string_view::npos) {
        const auto from = at + std::string_view("content-type:").size();
        const auto to = head.find_first_of("\r\n", from);
        content_type = std::string(utf8::strip(head.substr(from, to == std::string_view::npos ? std::string_view::npos : to - from)));
      }
      Json locator{{"part_index", static_cast<std::int64_t>(i)}, {"byte_start", start},
                   {"byte_end", start + static_cast<std::int64_t>(part.size())}};
      if (!content_type.empty()) locator["content_type"] = content_type;
      LOOM_TRY(keep_unmapped(recognition::kUnrecognized, "loom.importer.mht",
                             html ? "MIME part not used by the MHT handler" : "no text/html part found in the MHT",
                             "MHT part " + std::to_string(i) + (content_type.empty() ? "" : " (" + content_type + ")"),
                             std::move(locator), part));
    }
  }
  return out;
}

Result<std::vector<Conversation>> ConversationImporter::import_mht(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "mht", opts, "file", [&](const fs::path& input_path) { return mht_body(input_path, opts); });
}

}  // namespace loom
