// catalog_internal.h: extract_text — turns one catalogued JSON element
// (a ChatGPT/Claude conversation, a Claude project, the Claude memories
// object, or an unrecognised record) into a prose blob (sketched) and a code
// blob (fenced ``` bodies, sketched separately so identifiers pasted into
// code count as stronger evidence, proposal_scale.md §5.3 `code_evidence`).
// Reuses the archive area's structural walkers (archive_internal.h) instead
// of re-parsing ChatGPT/Claude JSON shapes from scratch.
#include "catalog_internal.h"

#include "archive/archive_internal.h"

namespace loom::catalog::internal {

namespace {

// Splits `text` into prose (fenced code stripped) and the concatenated code
// bodies, on plain ``` fences (a light heuristic; unmatched fences treat the
// remainder as prose, never crash on malformed markdown).
void split_fences(std::string_view text, std::string& prose, std::string& code) {
  std::size_t i = 0;
  bool in_code = false;
  while (i < text.size()) {
    std::size_t fence = text.find("```", i);
    std::string_view chunk = fence == std::string_view::npos ? text.substr(i) : text.substr(i, fence - i);
    (in_code ? code : prose).append(chunk);
    if (in_code) code.push_back('\n');
    if (fence == std::string_view::npos) break;
    in_code = !in_code;
    i = fence + 3;
  }
}

void append_message(const Json& m, std::string& prose, std::string& code) {
  std::string role = json::get_string(m, "role");
  std::string text = json::get_string(m, "text");
  if (text.empty()) return;
  prose.append(role).push_back(':');
  split_fences(text, prose, code);
  prose.push_back('\n');
}

void collect_strings(const Json& j, std::string& out, int depth, std::int64_t& budget) {
  if (budget <= 0 || depth > 12) return;
  if (j.is_string()) {
    const std::string& s = j.get_ref<const std::string&>();
    if (static_cast<std::int64_t>(s.size()) > budget) {
      out.append(s.substr(0, static_cast<std::size_t>(budget)));
      budget = 0;
    } else {
      out.append(s);
      budget -= static_cast<std::int64_t>(s.size());
    }
    out.push_back('\n');
  } else if (j.is_object()) {
    for (auto it = j.begin(); it != j.end() && budget > 0; ++it) collect_strings(it.value(), out, depth + 1, budget);
  } else if (j.is_array()) {
    for (const auto& v : j) {
      if (budget <= 0) break;
      collect_strings(v, out, depth + 1, budget);
    }
  }
}

}  // namespace

ExtractedText extract_text(const Json& element, std::string_view kind) {
  ExtractedText out;
  if (kind == "chatgpt" || kind == "claude") {
    archive::ChatWalk w = kind == "chatgpt" ? archive::walk_chatgpt(element) : archive::walk_claude(element);
    out.title = w.title;
    out.date = w.date;
    out.n_msgs = static_cast<int>(w.messages.size());
    out.n_forks = static_cast<int>(w.forks.size());
    for (const auto& m : w.messages) {
      append_message(m, out.prose, out.code);
      if (out.head.empty() && json::get_string(m, "role") == "user") out.head = archive::clip(json::get_string(m, "text"), 200);
    }
    return out;
  }
  if (kind == "claude_projects") {
    out.title = json::get_string(element, "name");
    out.date = archive::normalize_date(json::get_string(element, "created_at"));
    std::string desc = json::get_string(element, "description");
    std::string prompt = json::get_string(element, "prompt_template");
    if (!desc.empty()) out.prose.append(desc).push_back('\n');
    if (!prompt.empty()) split_fences(prompt, out.prose, out.code);
    if (const Json* docs = json::find(element, "docs"); docs && docs->is_array()) {
      for (const auto& d : *docs) {
        split_fences(json::get_string(d, "content"), out.prose, out.code);
        out.prose.push_back('\n');
      }
    }
    return out;
  }
  if (kind == "claude_memories") {
    std::string cm = json::get_string(element, "conversations_memory");
    if (!cm.empty()) out.prose.append(cm).push_back('\n');
    if (const Json* pm = json::find(element, "project_memories"); pm && pm->is_object()) {
      for (auto it = pm->begin(); it != pm->end(); ++it) {
        if (it.value().is_string()) out.prose.append(it.value().get<std::string>()).push_back('\n');
      }
    }
    return out;
  }
  // Unrecognised record: collect every string leaf, bounded.
  std::int64_t budget = kSketchByteCap;
  collect_strings(element, out.prose, 0, budget);
  return out;
}

}  // namespace loom::catalog::internal
