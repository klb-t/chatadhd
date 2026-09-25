// ConversationExporter — Loom addition backing loom_export_conversation.
// Every non-JSON format is re-importable by its matching import_<format>().
#include "loom/importer.h"

#include <algorithm>
#include <cctype>

#include "loom/db.h"
#include "loom/util/fs.h"

namespace loom {

namespace fs = std::filesystem;

namespace {

std::string html_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

std::string sanitize_filename(std::string_view title) {
  std::string out;
  out.reserve(title.size());
  for (unsigned char c : title) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == ' ') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('_');
    }
  }
  // Collapse whitespace runs and trim, then cap length so the final
  // "<title>_<conv_id>.<ext>" stays a sane filesystem name.
  std::string trimmed;
  bool prev_space = false;
  for (char c : out) {
    if (c == ' ') {
      if (!prev_space && !trimmed.empty()) trimmed.push_back('_');
      prev_space = true;
    } else {
      trimmed.push_back(c);
      prev_space = false;
    }
  }
  while (!trimmed.empty() && trimmed.back() == '_') trimmed.pop_back();
  if (trimmed.size() > 80) trimmed.resize(80);
  return trimmed.empty() ? std::string("conversation") : trimmed;
}

std::string role_label(std::string_view role) { return role == "user" ? "User" : "Assistant"; }

}  // namespace

std::vector<std::string> ConversationExporter::formats() { return {"json", "markdown", "text", "html"}; }

Result<std::string> ConversationExporter::export_conversation(std::string_view conv_id, std::string_view format,
                                                               bool include_all) {
  LOOM_TRY_ASSIGN(auto conv_opt, db_.get_conv(conv_id));
  if (!conv_opt) return Error(Errc::NotFound, "conversation not found: " + std::string(conv_id));
  const Conversation& conv = *conv_opt;
  LOOM_TRY_ASSIGN(std::vector<Message> msgs, db_.get_msgs(conv_id, include_all));

  if (format == "json") {
    Json marr = Json::array();
    for (const auto& m : msgs) marr.push_back(m.to_json());
    Json doc = Json{{"conversation", conv.to_json()}, {"messages", marr}};
    json::DumpOptions opts;
    opts.indent = 2;
    return json::py_dumps(doc, opts);
  }
  if (format == "markdown") {
    std::string out = "# " + conv.title + "\n\n";
    for (const auto& m : msgs) {
      out += "## " + role_label(m.role) + "\n" + m.text + "\n\n";
    }
    return out;
  }
  if (format == "text") {
    std::string out;
    for (const auto& m : msgs) {
      out += role_label(m.role) + ": " + m.text + "\n\n";
    }
    return out;
  }
  if (format == "html") {
    std::string out = "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + html_escape(conv.title) +
                      "</title></head><body>\n";
    for (const auto& m : msgs) {
      out += "<div data-role=\"" + html_escape(m.role) + "\">" + html_escape(m.text) + "</div>\n";
    }
    out += "</body></html>\n";
    return out;
  }
  return Error(Errc::InvalidArgument, "unknown export format: " + std::string(format));
}

Result<fs::path> ConversationExporter::export_to_file(std::string_view conv_id, std::string_view format,
                                                       const fs::path& dir, bool include_all) {
  LOOM_TRY_ASSIGN(auto conv_opt, db_.get_conv(conv_id));
  if (!conv_opt) return Error(Errc::NotFound, "conversation not found: " + std::string(conv_id));
  LOOM_TRY_ASSIGN(std::string content, export_conversation(conv_id, format, include_all));

  std::string ext = "json";
  if (format == "markdown") ext = "md";
  else if (format == "text") ext = "txt";
  else if (format == "html") ext = "html";

  LOOM_TRY(fsutil::ensure_dir(dir));
  fs::path out = dir / (sanitize_filename(conv_opt->title) + "_" + std::string(conv_id) + "." + ext);
  LOOM_TRY(fsutil::atomic_write(out, content));
  return out;
}

}  // namespace loom
