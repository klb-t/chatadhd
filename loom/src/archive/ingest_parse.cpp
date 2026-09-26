// Pure parsers used by the ingest stage: markdown sections, code digests,
// git log records, and structure-preserving walks of ChatGPT / Claude
// exports (timestamps, branch forks, current path).
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "archive/archive_internal.h"
#include "loom/util/utf8.h"

namespace loom::archive {
namespace fs = std::filesystem;

// ── Doc / Corpus JSON ───────────────────────────────────────────────
Json Doc::to_json() const {
  return Json{{"key", key},     {"kind", kind}, {"unit", unit}, {"title", title}, {"label", label},
              {"uri", uri},     {"date", date}, {"role", role}, {"ordinal", ordinal}, {"text", text},
              {"extra", extra}};
}

Doc Doc::from_json(const Json& j) {
  Doc d;
  d.key = json::get_string(j, "key");
  d.kind = json::get_string(j, "kind");
  d.unit = json::get_string(j, "unit");
  d.title = json::get_string(j, "title");
  d.label = json::get_string(j, "label");
  d.uri = json::get_string(j, "uri");
  d.date = json::get_string(j, "date");
  d.role = json::get_string(j, "role");
  d.ordinal = static_cast<int>(json::get_int(j, "ordinal"));
  d.text = json::get_string(j, "text");
  if (const Json* e = json::find(j, "extra"); e && e->is_object()) d.extra = *e;
  return d;
}

void Corpus::reindex() {
  index.clear();
  for (std::size_t i = 0; i < docs.size(); ++i) index.emplace(docs[i].key, i);
}

const Doc* Corpus::find(std::string_view key) const {
  auto it = index.find(std::string(key));
  return it == index.end() ? nullptr : &docs[it->second];
}

Json Corpus::to_json() const {
  Json d = Json::array();
  for (const auto& doc : docs) d.push_back(doc.to_json());
  return Json{{"version", 1}, {"project", project}, {"default_seeds", default_seeds}, {"sources", sources},
              {"forks", forks},       {"docs", std::move(d)}};
}

Corpus Corpus::from_json(const Json& j) {
  Corpus c;
  c.project = json::get_string(j, "project");
  if (const Json* s = json::find(j, "default_seeds"); s && s->is_array()) {
    for (const auto& t : *s) {
      if (t.is_string()) c.default_seeds.push_back(t.get<std::string>());
    }
  }
  if (const Json* s = json::find(j, "sources"); s && s->is_array()) c.sources = *s;
  if (const Json* f = json::find(j, "forks"); f && f->is_array()) c.forks = *f;
  if (const Json* d = json::find(j, "docs"); d && d->is_array()) {
    c.docs.reserve(d->size());
    for (const auto& x : *d) c.docs.push_back(Doc::from_json(x));
  }
  c.reindex();
  return c;
}

// ── Markdown sections ───────────────────────────────────────────────
std::vector<Section> split_markdown(std::string_view content, std::size_t max_chars) {
  std::vector<Section> out;
  std::vector<std::pair<int, std::string>> stack;  // (level, heading)
  Section cur;
  bool body_nonblank = false;
  bool in_fence = false;
  int line_no = 0;

  auto path_of = [&] {
    std::string p;
    bool deeper = std::any_of(stack.begin(), stack.end(), [](const auto& e) { return e.first >= 2; });
    for (const auto& [lvl, h] : stack) {
      if (deeper && lvl == 1) continue;
      if (!p.empty()) p += " › ";
      p += h;
    }
    return p;
  };
  auto push_piece = [&](Section s) {
    if (utf8::is_blank(s.text)) return;
    // split long sections at blank lines
    if (s.text.size() <= max_chars) {
      out.push_back(std::move(s));
      return;
    }
    std::string piece;
    int piece_line = s.line;
    int ln = s.line;
    std::size_t pos = 0;
    std::string_view t = s.text;
    while (pos <= t.size()) {
      std::size_t nl = t.find('\n', pos);
      if (nl == std::string_view::npos) nl = t.size();
      std::string_view line = t.substr(pos, nl - pos);
      bool blank = utf8::is_blank(line);
      if (blank && piece.size() >= max_chars / 2) {
        Section p = s;
        p.text = piece;
        p.line = piece_line;
        if (!utf8::is_blank(p.text)) out.push_back(std::move(p));
        piece.clear();
        piece_line = ln + 1;
      } else if (piece.size() + line.size() > max_chars && !piece.empty()) {
        Section p = s;
        p.text = piece;
        p.line = piece_line;
        out.push_back(std::move(p));
        piece.clear();
        piece_line = ln;
      }
      if (!(blank && piece.empty())) {
        piece.append(line);
        piece.push_back('\n');
      }
      pos = nl + 1;
      ++ln;
      if (nl == t.size()) break;
    }
    if (!utf8::is_blank(piece)) {
      Section p = s;
      p.text = piece;
      p.line = piece_line;
      out.push_back(std::move(p));
    }
  };
  auto finish = [&] {
    if (body_nonblank) push_piece(cur);
    cur = Section{};
    body_nonblank = false;
  };

  std::size_t pos = 0;
  while (pos <= content.size()) {
    std::size_t nl = content.find('\n', pos);
    if (nl == std::string_view::npos) nl = content.size();
    std::string_view line = content.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    pos = nl + 1;
    ++line_no;
    std::string_view st = utf8::lstrip(line);
    if (st.substr(0, 3) == "```" || st.substr(0, 3) == "~~~") in_fence = !in_fence;
    int level = 0;
    if (!in_fence && line.size() > 1 && line[0] == '#') {
      while (level < static_cast<int>(line.size()) && line[level] == '#') ++level;
      if (level > 6 || level >= static_cast<int>(line.size()) || line[level] != ' ') level = 0;
    }
    if (level > 0) {
      finish();
      std::string h(utf8::strip(line.substr(level)));
      while (!h.empty() && h.back() == '#') h.pop_back();
      h = std::string(utf8::strip(h));
      // strip markdown emphasis from headings
      h.erase(std::remove(h.begin(), h.end(), '*'), h.end());
      while (!stack.empty() && stack.back().first >= level) stack.pop_back();
      stack.emplace_back(level, h);
      cur.heading = h;
      cur.heading_path = path_of();
      cur.line = line_no;
      cur.text.append(line);
      cur.text.push_back('\n');
      continue;
    }
    if (cur.text.empty()) {
      cur.line = line_no;
      cur.heading = stack.empty() ? "" : stack.back().second;
      cur.heading_path = path_of();
    }
    cur.text.append(line);
    cur.text.push_back('\n');
    if (!utf8::is_blank(line)) body_nonblank = true;
    if (nl == content.size()) break;
  }
  finish();
  return out;
}

// ── Code digests ────────────────────────────────────────────────────
std::string code_language(const fs::path& p) {
  static const std::unordered_map<std::string, std::string> kExt = {
      {".c", "c"},          {".h", "cpp"},        {".cc", "cpp"},      {".cpp", "cpp"},     {".cxx", "cpp"},
      {".hh", "cpp"},       {".hpp", "cpp"},      {".hxx", "cpp"},     {".ipp", "cpp"},     {".py", "python"},
      {".pyi", "python"},   {".kt", "kotlin"},    {".kts", "kotlin"},  {".java", "java"},   {".js", "javascript"},
      {".mjs", "javascript"}, {".jsx", "javascript"}, {".ts", "typescript"}, {".tsx", "typescript"},
      {".go", "go"},        {".rs", "rust"},      {".swift", "swift"}, {".m", "objc"},      {".mm", "objc"},
      {".cs", "csharp"},    {".rb", "ruby"},      {".php", "php"},     {".sh", "shell"},    {".bash", "shell"},
      {".cmake", "cmake"},  {".gradle", "gradle"}, {".proto", "proto"}, {".sql", "sql"},   {".dart", "dart"},
      {".scala", "scala"},  {".lua", "lua"},      {".r", "r"},         {".jl", "julia"},    {".zig", "zig"},
      {".vue", "vue"},      {".svelte", "svelte"},
  };
  std::string name = p.filename().string();
  if (name == "CMakeLists.txt") return "cmake";
  if (name == "Makefile" || name == "makefile") return "make";
  if (name == "Dockerfile") return "docker";
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto it = kExt.find(ext);
  return it == kExt.end() ? "" : it->second;
}

namespace {

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string read_ident(std::string_view s, std::size_t& i) {
  while (i < s.size() && s[i] == ' ') ++i;
  std::size_t b = i;
  while (i < s.size() && is_ident_char(s[i])) ++i;
  return std::string(s.substr(b, i - b));
}

bool starts_with_word(std::string_view s, std::string_view w) {
  return s.size() > w.size() && s.substr(0, w.size()) == w && !is_ident_char(s[w.size()]);
}

const std::unordered_set<std::string>& cpp_non_functions() {
  static const std::unordered_set<std::string> k = {"if",     "for",    "while",  "switch", "return", "catch",
                                                    "sizeof", "decltype", "static_assert", "alignof", "defined",
                                                    "else",   "do",     "new",    "delete", "throw", "assert",
                                                    "LOOM_TRY", "LOOM_TRY_ASSIGN", "TEST_CASE", "SUBCASE",
                                                    "CHECK", "REQUIRE", "extern"};
  return k;
}

// The identifier right before the first '(' of a declaration line, with an
// optional "Qualifier::" prefix. "" when the line is not a declaration.
std::string cpp_function_name(std::string_view s) {
  std::size_t paren = s.find('(');
  if (paren == std::string_view::npos || paren == 0) return "";
  std::size_t e = paren;
  while (e > 0 && s[e - 1] == ' ') --e;
  std::size_t b = e;
  while (b > 0 && (is_ident_char(s[b - 1]) || s[b - 1] == ':' || s[b - 1] == '~')) --b;
  std::string name(s.substr(b, e - b));
  if (name.empty() || name.find("operator") != std::string::npos) return "";
  if (b == 0) return "";  // needs a return type (or qualifier) before the name
  std::string last = name.substr(name.rfind(':') == std::string::npos ? 0 : name.rfind(':') + 1);
  if (last.empty() || cpp_non_functions().count(last) || std::isdigit(static_cast<unsigned char>(last[0]))) return "";
  // the text before the name must look like a type ("Result<int> ", "void ", "const char* ")
  std::string_view before = utf8::rstrip(s.substr(0, b));
  if (before.empty()) return "";
  char lc = before.back();
  if (!(is_ident_char(lc) || lc == '*' || lc == '&' || lc == '>')) return "";
  if (before.find('=') != std::string_view::npos || before.find("return") == 0) return "";
  if (name.size() > 2 && name.substr(0, 2) == "::") name = name.substr(2);
  return name;
}

void add_unique(std::vector<std::string>& v, std::unordered_set<std::string>& seen, std::string s) {
  if (s.empty() || !seen.insert(s).second) return;
  v.push_back(std::move(s));
}

std::string normalise_comment(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& l : lines) {
    std::string_view s = utf8::strip(l);
    if (s.empty()) {
      if (!out.empty() && out.back() != '\n') out.push_back('\n');
      continue;
    }
    // drop decoration-only lines ("// ── Title ──────", "/*****")
    bool alnum = std::any_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
    if (!alnum) continue;
    if (!out.empty() && out.back() != '\n') out.push_back('\n');
    out.append(s);
  }
  return out;
}

}  // namespace

CodeDigest digest_code(std::string_view rel_path, std::string_view language, std::string_view content) {
  CodeDigest d;
  d.language = std::string(language);
  const bool c_like = language == "c" || language == "cpp" || language == "java" || language == "kotlin" ||
                      language == "javascript" || language == "typescript" || language == "go" ||
                      language == "rust" || language == "swift" || language == "objc" || language == "csharp" ||
                      language == "php" || language == "gradle" || language == "proto" || language == "dart" ||
                      language == "scala" || language == "zig" || language == "vue" || language == "svelte";
  const bool hash_comments = language == "python" || language == "shell" || language == "cmake" ||
                             language == "ruby" || language == "make" || language == "docker" || language == "r" ||
                             language == "julia";
  std::unordered_set<std::string> seen;
  std::unordered_set<std::string> used_seen;
  auto note_uses = [&](std::string_view line) {
    if (d.uses.size() >= 500) return;
    // identifiers inside string literals are data, not uses
    std::string code;
    char quote = 0;
    for (std::size_t k = 0; k < line.size(); ++k) {
      char ch = line[k];
      if (quote) {
        if (ch == '\\') ++k;
        else if (ch == quote) quote = 0;
        continue;
      }
      if (ch == '"' || ch == '\'' || ch == '`') {
        quote = ch;
        code.push_back(' ');
        continue;
      }
      code.push_back(ch);
    }
    for (auto& id : camel_identifiers(code)) {
      if (used_seen.insert(id).second) d.uses.push_back(std::move(id));
    }
  };
  std::vector<std::string> block;
  bool in_block_comment = false;
  bool in_docstring = false;
  std::string doc_quote;
  std::string py_class;
  int py_class_indent = -1;
  int line_no = 0;

  auto end_block = [&] {
    if (!block.empty()) {
      std::string c = normalise_comment(block);
      if (tokenize(c).size() >= 3) d.comments.push_back(std::move(c));
    }
    block.clear();
  };
  auto scan_todo = [&](std::string_view comment_text) {
    static const char* kMarks[] = {"TODO", "FIXME", "XXX", "HACK"};
    for (const char* m : kMarks) {
      std::string_view body = utf8::lstrip(comment_text);
      if (body.substr(0, std::string_view(m).size()) != m) continue;
      std::size_t p = comment_text.size() - body.size();
      std::size_t ml = std::string_view(m).size();
      if (p > 0 && is_ident_char(comment_text[p - 1])) continue;
      if (p + ml < comment_text.size() && is_ident_char(comment_text[p + ml])) continue;
      std::string rest(utf8::strip(comment_text.substr(p)));
      d.todos.emplace_back(line_no, clip(rest, 200));
      return;
    }
  };

  std::size_t pos = 0;
  while (pos <= content.size()) {
    std::size_t nl = content.find('\n', pos);
    if (nl == std::string_view::npos) nl = content.size();
    std::string_view raw = content.substr(pos, nl - pos);
    if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
    pos = nl + 1;
    ++line_no;
    std::string_view s = utf8::strip(raw);
    int indent = static_cast<int>(raw.find_first_not_of(" \t") == std::string_view::npos
                                      ? raw.size()
                                      : raw.find_first_not_of(" \t"));

    if (c_like) {
      if (in_block_comment) {
        std::size_t endc = s.find("*/");
        std::string_view body = endc == std::string_view::npos ? s : s.substr(0, endc);
        while (!body.empty() && body.front() == '*') body.remove_prefix(1);
        block.emplace_back(body);
        scan_todo(body);
        if (endc != std::string_view::npos) {
          in_block_comment = false;
          end_block();
        }
        if (nl == content.size()) break;
        continue;
      }
      if (s.substr(0, 2) == "//") {
        std::string_view body = s.substr(2);
        while (!body.empty() && (body.front() == '/' || body.front() == '!')) body.remove_prefix(1);
        block.emplace_back(body);
        scan_todo(body);
        if (nl == content.size()) break;
        continue;
      }
      if (s.substr(0, 2) == "/*") {
        end_block();
        std::string_view body = s.substr(2);
        while (!body.empty() && (body.front() == '*' || body.front() == '!')) body.remove_prefix(1);
        std::size_t endc = body.find("*/");
        if (endc != std::string_view::npos) {
          block.emplace_back(body.substr(0, endc));
          scan_todo(body.substr(0, endc));
          end_block();
        } else {
          block.emplace_back(body);
          scan_todo(body);
          in_block_comment = true;
        }
        if (nl == content.size()) break;
        continue;
      }
      end_block();
      note_uses(s.substr(0, s.find("//")));
      // trailing // comment on a code line: TODO scan only
      if (std::size_t tc = s.find("//"); tc != std::string_view::npos &&
                                          std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(tc), '"') % 2 == 0) {
        scan_todo(s.substr(tc + 2));
      }
      // symbols
      std::string_view t = s;
      static const char* kPrefixes[] = {"export ", "public ", "private ", "internal ", "open ", "abstract ",
                                        "final ", "data ", "sealed ", "static ", "pub ", "async ", "inline ",
                                        "template<", "extern \"C\" "};
      bool stripped = true;
      while (stripped) {
        stripped = false;
        for (const char* p : kPrefixes) {
          std::string_view pv(p);
          if (t.substr(0, pv.size()) == pv) {
            if (pv == "template<") {
              std::size_t close = t.find('>');
              if (close == std::string_view::npos) break;
              t = utf8::lstrip(t.substr(close + 1));
            } else {
              t = utf8::lstrip(t.substr(pv.size()));
            }
            stripped = true;
          }
        }
      }
      static const char* kTypeWords[] = {"class", "struct", "interface", "enum class", "enum struct", "enum",
                                         "object", "trait", "protocol", "union", "namespace", "fun", "func", "fn",
                                         "function", "typealias", "type"};
      bool found = false;
      for (const char* kw : kTypeWords) {
        if (!starts_with_word(t, kw)) continue;
        std::size_t i = std::string_view(kw).size();
        std::string name = read_ident(t, i);
        std::string_view rest = utf8::strip(t.substr(i));
        bool fwd = !rest.empty() && rest.front() == ';';
        std::string_view kwv(kw);
        if ((kwv == "type" || kwv == "typealias") && language == "cpp") break;
        if (!name.empty() && !fwd && name != "final") {
          if (kwv == "namespace") break;  // namespaces are not components
          add_unique(d.symbols, seen, name);
        }
        found = true;
        break;
      }
      if (!found && (language == "cpp" || language == "c") && indent == 0 && !s.empty() && s[0] != '#' &&
          s[0] != '}' && s.find('(') != std::string_view::npos) {
        std::string fn = cpp_function_name(t);
        if (!fn.empty()) add_unique(d.symbols, seen, fn);
      }
    } else if (hash_comments) {
      if (language == "python") {
        if (in_docstring) {
          std::size_t endq = s.find(doc_quote);
          std::string_view body = endq == std::string_view::npos ? s : s.substr(0, endq);
          block.emplace_back(body);
          scan_todo(body);
          if (endq != std::string_view::npos) {
            in_docstring = false;
            end_block();
          }
          if (nl == content.size()) break;
          continue;
        }
        if (s.substr(0, 3) == "\"\"\"" || s.substr(0, 3) == "'''") {
          end_block();
          doc_quote = std::string(s.substr(0, 3));
          std::string_view body = s.substr(3);
          std::size_t endq = body.find(doc_quote);
          if (endq != std::string_view::npos) {
            block.emplace_back(body.substr(0, endq));
            scan_todo(body.substr(0, endq));
            end_block();
          } else {
            block.emplace_back(body);
            scan_todo(body);
            in_docstring = true;
          }
          if (nl == content.size()) break;
          continue;
        }
      }
      if (!s.empty() && s[0] == '#' && s.substr(0, 2) != "#!") {
        std::string_view body = s.substr(1);
        block.emplace_back(body);
        scan_todo(body);
        if (nl == content.size()) break;
        continue;
      }
      end_block();
      note_uses(s.substr(0, s.find(" #")));
      if (std::size_t tc = s.find(" #"); tc != std::string_view::npos) scan_todo(s.substr(tc + 2));
      if (language == "python") {
        if (py_class_indent >= 0 && indent <= py_class_indent && !s.empty()) {
          py_class.clear();
          py_class_indent = -1;
        }
        std::string_view t = s;
        if (starts_with_word(t, "async")) t = utf8::lstrip(t.substr(5));
        if (starts_with_word(t, "class")) {
          std::size_t i = 5;
          std::string name = read_ident(t, i);
          add_unique(d.symbols, seen, name);
          py_class = name;
          py_class_indent = indent;
        } else if (starts_with_word(t, "def")) {
          std::size_t i = 3;
          std::string name = read_ident(t, i);
          if (!name.empty() && !(name.size() > 4 && name.substr(0, 2) == "__" && name != "__init__")) {
            if (!py_class.empty() && indent > py_class_indent) {
              if (name != "__init__") add_unique(d.symbols, seen, py_class + "." + name);
            } else if (indent == 0) {
              add_unique(d.symbols, seen, name);
            }
          }
        }
      } else if (language == "cmake") {
        static const char* kCmd[] = {"add_library(", "add_executable(", "option(", "project("};
        for (const char* c : kCmd) {
          std::string_view cv(c);
          if (s.substr(0, cv.size()) == cv) {
            std::size_t i = cv.size();
            add_unique(d.symbols, seen, read_ident(s, i));
          }
        }
      } else if (language == "shell") {
        if (std::size_t p = s.find("()"); p != std::string_view::npos && p > 0 && s.find('{') != std::string_view::npos) {
          std::string_view name = s.substr(0, p);
          if (starts_with_word(name, "function")) name = utf8::lstrip(name.substr(8));
          if (std::all_of(name.begin(), name.end(), is_ident_char)) add_unique(d.symbols, seen, std::string(name));
        }
      }
    }
    if (nl == content.size()) break;
  }
  end_block();

  // Assemble the digest (bounded).
  std::string text = std::string(rel_path) + " [" + d.language + "]\n";
  if (!d.symbols.empty()) {
    text += "Symbols:";
    std::size_t n = 0;
    for (const auto& s : d.symbols) {
      if (++n > 200) break;
      text += (n == 1 ? " " : ", ") + s;
    }
    text += "\n";
  }
  std::size_t budget = 12000;
  for (const auto& c : d.comments) {
    if (c.size() + 1 > budget) break;
    text += c + "\n";
    budget -= c.size() + 1;
  }
  for (const auto& [ln, t] : d.todos) text += "L" + std::to_string(ln) + ": " + t + "\n";
  d.text = std::move(text);
  return d;
}

// ── git log ─────────────────────────────────────────────────────────
std::vector<GitCommit> parse_git_log(std::string_view raw) {
  std::vector<GitCommit> out;
  std::size_t pos = 0;
  while (true) {
    std::size_t rs = raw.find('\x1e', pos);
    if (rs == std::string_view::npos) break;
    std::size_t next = raw.find('\x1e', rs + 1);
    std::string_view rec = raw.substr(rs + 1, (next == std::string_view::npos ? raw.size() : next) - rs - 1);
    pos = next == std::string_view::npos ? raw.size() : next;
    std::vector<std::string_view> f;
    std::size_t fp = 0;
    for (int k = 0; k < 5; ++k) {
      std::size_t us = rec.find('\x1f', fp);
      if (us == std::string_view::npos) break;
      f.push_back(rec.substr(fp, us - fp));
      fp = us + 1;
    }
    if (f.size() < 5) continue;
    GitCommit c;
    c.hash = std::string(utf8::strip(f[0]));
    c.date = normalize_date(f[1]);
    c.author = std::string(utf8::strip(f[2]));
    c.subject = std::string(utf8::strip(f[3]));
    {
      // drop trailers (Co-Authored-By, Signed-off-by, session links)
      std::string body;
      std::size_t bp = 0;
      std::string_view b = f[4];
      while (bp <= b.size()) {
        std::size_t e = b.find('\n', bp);
        if (e == std::string_view::npos) e = b.size();
        std::string_view line = b.substr(bp, e - bp);
        bp = e + 1;
        std::size_t colon = line.find(':');
        bool trailer = colon != std::string_view::npos && colon > 0 && colon < 30 &&
                       line.substr(0, colon).find(' ') == std::string_view::npos &&
                       line.substr(0, colon).find('-') != std::string_view::npos;
        if (!trailer) {
          body.append(line);
          body.push_back('\n');
        }
        if (e == b.size()) break;
      }
      c.body = std::string(utf8::strip(body));
    }
    std::string_view files = rec.substr(fp);
    std::size_t lp = 0;
    while (lp < files.size()) {
      std::size_t e = files.find('\n', lp);
      if (e == std::string_view::npos) e = files.size();
      std::string_view line = utf8::strip(files.substr(lp, e - lp));
      lp = e + 1;
      if (line.empty()) continue;
      std::size_t tab = line.find('\t');
      if (tab == std::string_view::npos) continue;
      std::string st(line.substr(0, 1));
      std::string_view rest = line.substr(tab + 1);
      std::size_t tab2 = rest.find('\t');
      std::string path = tab2 == std::string_view::npos ? std::string(rest)
                                                        : std::string(rest.substr(tab2 + 1));
      c.files.push_back(st + " " + path);
    }
    if (!c.hash.empty()) out.push_back(std::move(c));
  }
  return out;
}

// ── Chat export walks ───────────────────────────────────────────────
namespace {

std::string chatgpt_text(const Json& msg) {
  const Json* content = json::find(msg, "content");
  if (!content) return "";
  if (content->is_string()) return content->get<std::string>();
  if (!content->is_object()) return "";
  std::string text;
  if (const Json* p = json::find(*content, "parts"); p && p->is_array()) {
    bool first = true;
    for (const auto& part : *p) {
      std::string s;
      if (part.is_string()) {
        s = part.get<std::string>();
      } else if (part.is_object() && json::get_string(part, "content_type") == "text") {
        s = json::get_string(part, "text");
      } else {
        continue;
      }
      if (!first) text += "\n";
      text += s;
      first = false;
    }
  } else if (const Json* t = json::find(*content, "text"); t && t->is_string()) {
    text = t->get<std::string>();
  }
  return text;
}

double num_or(const Json& j, std::string_view key) {
  const Json* v = json::find(j, key);
  return v && v->is_number() ? v->get<double>() : 0.0;
}

// Generic tree walk shared by both exports. nodes: id -> (parent, emitted?,
// payload). order: ids in document order (children keep this order).
struct TreeNode {
  std::string parent;  // "" = root
  bool emit = false;
  Json msg;            // {"role","text","date"} when emit
};

void walk_tree(const std::vector<std::string>& order, const std::unordered_map<std::string, TreeNode>& nodes,
               const std::string& current_leaf, ChatWalk& w) {
  std::unordered_map<std::string, std::vector<std::string>> children;
  std::vector<std::string> roots;
  for (const auto& id : order) {
    const TreeNode& n = nodes.at(id);
    if (n.parent.empty() || nodes.find(n.parent) == nodes.end()) {
      roots.push_back(id);
    } else {
      children[n.parent].push_back(id);
    }
  }
  // Pre-order (iterative) to compute subtree emitted counts and "contains current".
  std::vector<std::string> pre;
  {
    std::vector<std::string> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
      std::string id = std::move(stack.back());
      stack.pop_back();
      pre.push_back(id);
      auto it = children.find(id);
      if (it != children.end()) {
        for (auto c = it->second.rbegin(); c != it->second.rend(); ++c) stack.push_back(*c);
      }
    }
  }
  std::unordered_map<std::string, int> count;
  std::unordered_set<std::string> on_current;
  for (std::string cur = current_leaf; !cur.empty() && nodes.count(cur);) {
    if (!on_current.insert(cur).second) break;
    cur = nodes.at(cur).parent;
  }
  for (auto it = pre.rbegin(); it != pre.rend(); ++it) {
    int c = nodes.at(*it).emit ? 1 : 0;
    if (auto ch = children.find(*it); ch != children.end()) {
      for (const auto& k : ch->second) c += count[k];
    }
    count[*it] = c;
  }
  // Emission walk: (id, branch path, last emitted ancestor node id).
  struct Frame {
    std::string id;
    std::string branch;
    std::string last;
  };
  std::vector<Frame> stack;
  for (auto r = roots.rbegin(); r != roots.rend(); ++r) stack.push_back({*r, "", ""});
  auto first_emitted = [&](const std::string& start) -> std::string {
    std::vector<std::string> st{start};
    while (!st.empty()) {
      std::string id = st.back();
      st.pop_back();
      if (nodes.at(id).emit) return id;
      if (auto ch = children.find(id); ch != children.end()) {
        for (auto c = ch->second.rbegin(); c != ch->second.rend(); ++c) st.push_back(*c);
      }
    }
    return "";
  };
  while (!stack.empty()) {
    Frame f = std::move(stack.back());
    stack.pop_back();
    const TreeNode& n = nodes.at(f.id);
    std::string last = f.last;
    if (n.emit) {
      Json m = n.msg;
      m["node"] = f.id;
      m["parent"] = f.last;
      m["branch"] = f.branch;
      m["current"] = current_leaf.empty() ? true : on_current.count(f.id) > 0;
      w.messages.push_back(std::move(m));
      last = f.id;
    }
    auto ch = children.find(f.id);
    if (ch == children.end()) continue;
    std::vector<std::string> live;
    for (const auto& c : ch->second) {
      if (count[c] > 0) live.push_back(c);
    }
    if (live.size() >= 2) {
      Json alts = Json::array();
      for (const auto& c : live) {
        std::string fe = first_emitted(c);
        alts.push_back(Json{{"first_node", fe},
                            {"messages", count[c]},
                            {"current", current_leaf.empty() ? false : on_current.count(c) > 0}});
      }
      std::string date = n.emit ? json::get_string(n.msg, "date") : "";
      w.forks.push_back(Json{{"node", last}, {"date", date}, {"alternatives", std::move(alts)}});
    }
    for (std::size_t k = live.size(); k-- > 0;) {
      std::string branch = f.branch;
      if (live.size() >= 2) branch += "/" + std::to_string(k);
      stack.push_back({live[k], branch, last});
    }
  }
}

}  // namespace

ChatWalk walk_chatgpt(const Json& conv) {
  ChatWalk w;
  if (!conv.is_object()) return w;
  w.title = json::get_string(conv, "title");
  w.date = iso_from_epoch(num_or(conv, "create_time"));
  const Json* mapping = json::find(conv, "mapping");
  if (!mapping || !mapping->is_object()) return w;
  std::vector<std::string> order;
  std::unordered_map<std::string, TreeNode> nodes;
  for (auto it = mapping->begin(); it != mapping->end(); ++it) {
    TreeNode n;
    if (const Json* p = json::find(it.value(), "parent"); p && p->is_string()) n.parent = p->get<std::string>();
    if (const Json* msg = json::find(it.value(), "message"); msg && msg->is_object()) {
      const Json* author = json::find(*msg, "author");
      std::string role = author ? json::get_string(*author, "role", "unknown") : "unknown";
      std::string text = chatgpt_text(*msg);
      if ((role == "user" || role == "assistant") && !utf8::is_blank(text)) {
        std::string date = iso_from_epoch(num_or(*msg, "create_time"));
        if (date.empty()) date = w.date;
        n.emit = true;
        n.msg = Json{{"role", role}, {"text", text}, {"date", date}};
      }
    }
    order.push_back(it.key());
    nodes.emplace(it.key(), std::move(n));
  }
  walk_tree(order, nodes, json::get_string(conv, "current_node"), w);
  return w;
}

ChatWalk walk_claude(const Json& conv) {
  ChatWalk w;
  if (!conv.is_object()) return w;
  w.title = json::get_string(conv, "name", json::get_string(conv, "title"));
  w.date = normalize_date(json::get_string(conv, "created_at"));
  const Json* msgs = json::find(conv, "chat_messages");
  if (!msgs || !msgs->is_array()) return w;
  std::vector<std::string> order;
  std::unordered_map<std::string, TreeNode> nodes;
  bool has_parents = false;
  std::string prev;
  std::string last_id;
  int idx = 0;
  for (const auto& m : *msgs) {
    if (!m.is_object()) continue;
    std::string id = json::get_string(m, "uuid");
    if (id.empty() || nodes.count(id)) id = "#" + std::to_string(idx);
    ++idx;
    TreeNode n;
    std::string parent = json::get_string(m, "parent_message_uuid");
    if (!parent.empty()) has_parents = true;
    n.parent = parent;
    std::string sender = json::get_string(m, "sender", "human");
    std::string role = sender == "human" ? "user" : "assistant";
    std::string text = json::get_string(m, "text");
    if (utf8::is_blank(text)) {
      if (const Json* c = json::find(m, "content"); c && c->is_array()) {
        text.clear();
        for (const auto& part : *c) {
          if (part.is_object() && json::get_string(part, "type") == "text") {
            if (!text.empty()) text += "\n";
            text += json::get_string(part, "text");
          }
        }
      }
    }
    if (!utf8::is_blank(text)) {
      std::string date = normalize_date(json::get_string(m, "created_at"));
      if (date.empty()) date = w.date;
      n.emit = true;
      n.msg = Json{{"role", role}, {"text", text}, {"date", date}};
    }
    order.push_back(id);
    nodes.emplace(id, std::move(n));
    last_id = id;
    prev = id;
  }
  if (!has_parents) {
    // linear conversation: chain in array order
    for (std::size_t i = 1; i < order.size(); ++i) nodes[order[i]].parent = order[i - 1];
  }
  std::string leaf = json::get_string(conv, "current_leaf_message_uuid");
  if (leaf.empty() || !nodes.count(leaf)) leaf = last_id;
  walk_tree(order, nodes, leaf, w);
  return w;
}

std::string sniff_export_element(const Json& e) {
  if (!e.is_object()) return "";
  if (json::find(e, "mapping")) return "chatgpt";
  if (json::find(e, "chat_messages")) return "claude";
  if ((json::find(e, "docs") || json::find(e, "prompt_template")) && (json::find(e, "name") || json::find(e, "uuid"))) {
    return "claude_projects";
  }
  if (json::find(e, "conversations_memory") || json::find(e, "project_memories")) return "claude_memories";
  return "";
}

}  // namespace loom::archive
