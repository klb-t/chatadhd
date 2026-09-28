// Markdown, plain text and screenshot (OCR) import. No regex engine is
// available yet in this worktree (re::Regex is still a wave-2 stub owned by
// another area), so every pattern from engine/importer.py's markdown/text/
// OCR handlers is hand-scanned instead.
#include <algorithm>
#include <cctype>

#include "importer_internal.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/media_providers.h"
#include "loom/util/fs.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom {

namespace fs = std::filesystem;
namespace idt = importer_detail;

namespace {

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::vector<std::string_view> split_lines(std::string_view s) {
  std::vector<std::string_view> out;
  std::size_t pos = 0;
  while (pos <= s.size()) {
    std::size_t nl = s.find('\n', pos);
    out.push_back(s.substr(pos, (nl == std::string_view::npos ? s.size() : nl) - pos));
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }
  return out;
}

}  // namespace

// ── Markdown (Python import_markdown) ───────────────────────────────
Json ConversationImporter::parse_markdown(std::string_view content) {
  Json messages = Json::array();
  std::string role;
  std::vector<std::string> text;

  auto flush = [&] {
    if (!role.empty()) {
      std::string joined;
      for (std::size_t i = 0; i < text.size(); ++i) {
        if (i) joined += "\n";
        joined += text[i];
      }
      joined = std::string(utf8::strip(joined));
      messages.push_back(Json{{"role", role}, {"content", joined}});
    }
  };

  for (auto raw_line : split_lines(content)) {
    std::string lower = std::string(utf8::to_lower(std::string(utf8::strip(raw_line))));
    if (idt::istarts_with(lower, "## human") || idt::istarts_with(lower, "## user") ||
        idt::istarts_with(lower, "**human**") || idt::istarts_with(lower, "**user**")) {
      flush();
      role = "user";
      text.clear();
    } else if (idt::istarts_with(lower, "## assistant") || idt::istarts_with(lower, "## claude") ||
              idt::istarts_with(lower, "**assistant**") || idt::istarts_with(lower, "**claude**")) {
      flush();
      role = "assistant";
      text.clear();
    } else if (idt::istarts_with(lower, "---") || idt::istarts_with(lower, "***")) {
      flush();
      text.clear();
      // Python keeps current_role across a separator; only current_text resets.
    } else if (!role.empty()) {
      text.push_back(std::string(raw_line));
    }
  }
  flush();

  if (messages.empty()) {
    messages.push_back(Json{{"role", "assistant"}, {"content", std::string(content)}});
  }
  return messages;
}

Result<std::vector<Conversation>> ConversationImporter::markdown_body(const fs::path& path,
                                                                       const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(std::string content, fsutil::read_file(path));
  Json messages = parse_markdown(content);
  std::optional<std::string> title = opts.title;
  if (!title || title->empty()) title = path.stem().string();
  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, title));
  return std::vector<Conversation>{std::move(conv)};
}

Result<std::vector<Conversation>> ConversationImporter::import_markdown(const fs::path& path,
                                                                        const ImportOptions& opts) {
  return with_source(path, "markdown", opts, "file", [&] { return markdown_body(path, opts); });
}

// ── Plain text (Python import_text) ─────────────────────────────────
namespace {

struct LineLabelHit {
  std::size_t label_pos;  // index of the label's first character
  std::size_t colon_end;  // index right after "Label:"
};

// `(?:^|\n)(Label1|Label2|...):`  — searched line by line from `from`.
std::optional<LineLabelHit> next_line_label(std::string_view text, const std::vector<std::string>& labels,
                                            std::size_t from) {
  std::size_t pos = from;
  while (pos <= text.size()) {
    bool boundary = (pos == 0) || (pos > 0 && text[pos - 1] == '\n');
    if (boundary) {
      for (const auto& lbl : labels) {
        std::string needle = lbl + ":";
        if (pos + needle.size() <= text.size() &&
            idt::ifind(text.substr(pos, needle.size()), needle, 0) == 0) {
          return LineLabelHit{pos, pos + needle.size()};
        }
      }
    }
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
  }
  return std::nullopt;
}

struct RoleText {
  std::string role;
  std::string text;
};

// One `for match in re.finditer(pattern, content, DOTALL)` pass for
// `(?:^|\n)(own...):\s*(.*?)(?=(?:\n(?:all...):)| $)`.
void scan_line_blocks(std::string_view text, const std::vector<std::string>& own,
                      const std::vector<std::string>& all_labels, std::string_view role,
                      std::vector<RoleText>& out) {
  std::size_t pos = 0;
  while (pos <= text.size()) {
    auto hit = next_line_label(text, own, pos);
    if (!hit) break;
    std::size_t content_start = hit->colon_end;
    while (content_start < text.size() && is_ws(text[content_start])) ++content_start;
    auto stop = next_line_label(text, all_labels, content_start);
    std::size_t end = text.size();
    if (stop) end = stop->label_pos > 0 ? stop->label_pos - 1 : 0;
    end = std::max(end, content_start);
    std::string captured = std::string(utf8::strip(text.substr(content_start, end - content_start)));
    if (!captured.empty()) out.push_back(RoleText{std::string(role), captured});
    pos = stop ? stop->label_pos : text.size() + 1;
  }
}

}  // namespace

Json ConversationImporter::parse_plain_text(std::string_view content) {
  static const std::vector<std::string> kUser = {"Human", "User", "You"};
  static const std::vector<std::string> kAssistant = {"Assistant", "AI", "Claude"};
  std::vector<std::string> all_labels = {"Human", "User", "You", "Assistant", "AI", "Claude"};

  std::vector<RoleText> found;
  scan_line_blocks(content, kUser, all_labels, "user", found);
  scan_line_blocks(content, kAssistant, all_labels, "assistant", found);

  Json messages = Json::array();
  for (auto& rt : found) messages.push_back(Json{{"role", rt.role}, {"content", rt.text}});
  if (messages.empty()) messages.push_back(Json{{"role", "assistant"}, {"content", std::string(content)}});
  return messages;
}

Result<std::vector<Conversation>> ConversationImporter::text_body(const fs::path& path, const ImportOptions& opts) {
  LOOM_TRY_ASSIGN(std::string raw, fsutil::read_file(path));
  std::string content = utf8::repair(raw);  // Python: open(..., errors='replace')
  Json messages = parse_plain_text(content);
  std::optional<std::string> title = opts.title;
  if (!title || title->empty()) title = path.stem().string();
  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, title));
  return std::vector<Conversation>{std::move(conv)};
}

Result<std::vector<Conversation>> ConversationImporter::import_text(const fs::path& path, const ImportOptions& opts) {
  return with_source(path, "text", opts, "file", [&] { return text_body(path, opts); });
}

// ── Screenshot (OCR via MediaProviders) ─────────────────────────────
namespace {

bool starts_word_then_sep(std::string_view line, std::string_view word, std::size_t& consumed) {
  if (line.size() < word.size()) return false;
  for (std::size_t i = 0; i < word.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(line[i])) != std::tolower(static_cast<unsigned char>(word[i]))) return false;
  }
  std::size_t j = word.size();
  std::size_t start = j;
  while (j < line.size() && (std::isspace(static_cast<unsigned char>(line[j])) || line[j] == ':')) ++j;
  if (j == start) return false;
  consumed = j;
  return true;
}

bool is_time_only_line(std::string_view line) {
  std::size_t i = 0, n = line.size(), d1 = 0;
  while (i < n && d1 < 2 && std::isdigit(static_cast<unsigned char>(line[i]))) {
    ++i;
    ++d1;
  }
  if (d1 == 0 || i >= n || line[i] != ':') return false;
  ++i;
  std::size_t d2 = 0;
  while (i < n && d2 < 2 && std::isdigit(static_cast<unsigned char>(line[i]))) {
    ++i;
    ++d2;
  }
  if (d2 != 2) return false;
  while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
  if (i + 1 < n) {
    char a = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));
    char m = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i + 1])));
    if ((a == 'a' || a == 'p') && m == 'm') i += 2;
  }
  while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
  return i == n;
}

bool starts_with_any_of(std::string_view line, std::initializer_list<const char*> prefixes, std::size_t& len) {
  for (const char* p : prefixes) {
    std::string_view pv(p);
    if (line.size() >= pv.size() && line.compare(0, pv.size(), pv) == 0) {
      len = pv.size();
      while (len < line.size() && std::isspace(static_cast<unsigned char>(line[len]))) ++len;
      return true;
    }
  }
  return false;
}

}  // namespace

Json ConversationImporter::parse_chat_text(std::string_view text) {
  static const std::vector<std::string_view> kUserWords = {"You", "Human", "User", "Ja", "Ty"};
  static const std::vector<std::string_view> kAiWords = {"Claude", "Assistant", "AI", "ChatGPT", "GPT", "Gemini", "Bot"};

  Json messages = Json::array();
  std::string current_role;
  std::vector<std::string> current_lines;

  auto flush = [&] {
    if (!current_role.empty() && !current_lines.empty()) {
      std::string joined;
      for (std::size_t i = 0; i < current_lines.size(); ++i) {
        if (i) joined += "\n";
        joined += current_lines[i];
      }
      joined = std::string(utf8::strip(joined));
      if (!joined.empty() && utf8::length(joined) > 3) messages.push_back(Json{{"role", current_role}, {"content", joined}});
    }
  };

  for (auto raw_line : split_lines(text)) {
    std::string line = std::string(utf8::strip(raw_line));
    if (line.empty()) continue;

    bool is_user = false, is_ai = false;
    std::size_t consumed = 0;

    for (auto w : kUserWords) {
      if (starts_word_then_sep(line, w, consumed)) {
        is_user = true;
        break;
      }
    }
    if (!is_user && starts_with_any_of(line, {"►", "▶", "→", ">"}, consumed)) is_user = true;
    if (!is_user && is_time_only_line(line)) {
      is_user = true;
      consumed = 0;
    }
    if (!is_user) {
      for (auto w : kAiWords) {
        if (starts_word_then_sep(line, w, consumed)) {
          is_ai = true;
          break;
        }
      }
    }
    if (!is_user && !is_ai && starts_with_any_of(line, {"◄", "◀", "←", "<"}, consumed)) is_ai = true;

    std::string lower_line = std::string(utf8::to_lower(line));
    bool sidebar_human = lower_line == "h" || lower_line == "human";
    bool sidebar_ai = lower_line == "a" || lower_line == "assistant";
    if (sidebar_human) is_user = true;
    if (sidebar_ai) is_ai = true;

    if (is_user || is_ai) {
      flush();
      current_role = is_user ? "user" : "assistant";
      current_lines.clear();
      std::string remainder = (sidebar_human || sidebar_ai) ? std::string() : line.substr(std::min(consumed, line.size()));
      remainder = std::string(utf8::strip(remainder));
      if (!remainder.empty()) current_lines.push_back(remainder);
    } else if (!current_role.empty()) {
      current_lines.push_back(line);
    } else {
      bool ends_q = !line.empty() && line.back() == '?';
      current_role = (ends_q || utf8::length(line) < 50) ? "user" : "assistant";
      current_lines.push_back(line);
    }
  }
  flush();

  if (!messages.empty()) {
    Json merged = Json::array();
    merged.push_back(messages[0]);
    for (std::size_t i = 1; i < messages.size(); ++i) {
      if (messages[i]["role"] == merged.back()["role"]) {
        merged.back()["content"] =
            merged.back()["content"].get<std::string>() + "\n\n" + messages[i]["content"].get<std::string>();
      } else {
        merged.push_back(messages[i]);
      }
    }
    messages = merged;
  }
  return messages;
}

Result<std::vector<Conversation>> ConversationImporter::screenshot_body(const fs::path& path,
                                                                        const ImportOptions& opts) {
  if (!media_) {
    return Error(Errc::Unavailable, "No OCR provider configured (MediaProviders unavailable)");
  }
  auto ocr = media_->ocr(path);
  if (!ocr) return ocr.error();
  if (ocr->text.empty() || utf8::length(std::string(utf8::strip(ocr->text))) < 10) {
    return Error(Errc::InvalidArgument, "Could not extract text from image. Try a clearer screenshot.");
  }

  Json messages = parse_chat_text(ocr->text);
  if (messages.empty()) messages.push_back(Json{{"role", "assistant"}, {"content", ocr->text}});

  std::optional<std::string> title = opts.title;
  if (!title || title->empty()) title = "Screenshot " + timeutil::local_now_format("%Y-%m-%d %H:%M");
  LOOM_TRY_ASSIGN(Conversation conv, import_message_list(messages, title));
  return std::vector<Conversation>{std::move(conv)};
}

Result<std::vector<Conversation>> ConversationImporter::import_screenshot(const fs::path& path,
                                                                          const ImportOptions& opts) {
  return with_source(path, "screenshot", opts, "file", [&] { return screenshot_body(path, opts); });
}

}  // namespace loom
