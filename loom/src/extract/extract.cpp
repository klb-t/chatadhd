// extract.h — detection, segmentation into located observations, and the
// Extractor facade (the extractors themselves live in extractors.cpp).
// Deterministic: no clock, no randomness; ids are content-derived (I5).
#include "loom/extract.h"

#include <algorithm>
#include <set>

#include "archive/archive_internal.h"
#include "extract/extract_internal.h"
#include "loom/util/utf8.h"

namespace loom::extract {

using model::Observation;
using model::ObservationKind;

// ── JSON ────────────────────────────────────────────────────────────
Json Detection::to_json() const { return Json{{"artifact_type", artifact_type}, {"score", score}, {"reasons", reasons}}; }

Json ClassifiedItem::to_json() const {
  return Json{{"observation", observation},
              {"text", text},
              {"role", role ? Json(std::string(model::to_string(*role))) : Json("")},
              {"kind", kind},
              {"score", score},
              {"area", area}};
}

Json Extraction::to_json() const {
  auto list = [](const auto& v) {
    Json a = Json::array();
    for (const auto& x : v) a.push_back(x.to_json());
    return a;
  };
  return Json{{"observations", list(observations)}, {"entities", list(entities)}, {"claims", list(claims)},
              {"areas", list(areas)},               {"principles", list(principles)}, {"decisions", list(decisions)},
              {"forks", list(forks)},               {"statuses", list(statuses)},     {"items", list(items)},
              {"stats", stats}};
}

namespace detail {

std::string clip(std::string_view s, std::size_t n) {
  std::string out;
  bool space = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  if (utf8::length(out) > n) return std::string(utf8::prefix(out, n)) + "…";
  return out;
}

model::ArtifactType fallback_type() {
  model::ArtifactType t;
  t.header.id = "";
  t.medium = model::Medium::Text;
  t.parse = Json{{"segment", Json::array({"markdown_sections", "list_items", "sentences", "code_blocks"})}};
  t.extract = Json::array();
  for (const char* op : {"items", "decisions", "status_cues", "versions", "normative", "generalizations", "areas",
                         "entities_lexicon", "relation_patterns", "citations"}) {
    t.extract.push_back(Json{{"op", op}});
  }
  return t;
}

std::vector<std::string> extractor_ops(const model::ArtifactType& t) {
  std::vector<std::string> out;
  if (t.extract.is_array()) {
    for (const auto& e : t.extract) {
      std::string op = e.is_string() ? e.get<std::string>() : json::get_string(e, "op");
      if (!op.empty() && std::find(out.begin(), out.end(), op) == out.end()) out.push_back(op);
    }
  }
  return out;
}

namespace {

std::vector<std::string> segmenters_of(const model::ArtifactType& t) {
  std::vector<std::string> out;
  if (const Json* s = json::find(t.parse, "segment"); s && s->is_array()) {
    for (const auto& x : *s) {
      if (x.is_string()) out.push_back(x.get<std::string>());
    }
  }
  return out;
}

void add_string_block(UnitBlocks& ub, const Json& obj, std::string_view key, const std::string& base, const std::string& date) {
  if (const Json* v = json::find(obj, key); v && v->is_string() && !utf8::is_blank(v->get<std::string>())) {
    Block b;
    b.text = v->get<std::string>();
    b.pointer = base + "/" + std::string(key);
    b.speaker = std::string(key);
    b.date = date;
    b.attrs["field"] = std::string(key);
    ub.blocks.push_back(std::move(b));
  }
}

std::string escape_pointer(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c == '~') out += "~0";
    else if (c == '/') out += "~1";
    else out.push_back(c);
  }
  return out;
}

}  // namespace

UnitBlocks unit_blocks(const UnitContent& c) {
  UnitBlocks ub;
  const Json& e = c.structured;
  std::string platform = e.is_object() ? archive::sniff_export_element(e) : "";
  if (platform.empty() && e.is_object() && json::find(e, "hash") && json::find(e, "files")) platform = "git_commit";
  ub.platform = platform.empty() ? (c.text.empty() ? "empty" : "text") : platform;
  if (platform == "chatgpt" || platform == "claude") {
    archive::ChatWalk w = platform == "chatgpt" ? archive::walk_chatgpt(e) : archive::walk_claude(e);
    std::map<std::string, std::size_t> claude_index;
    if (platform == "claude") {
      if (const Json* ms = json::find(e, "chat_messages"); ms && ms->is_array()) {
        for (std::size_t i = 0; i < ms->size(); ++i) {
          std::string id = json::get_string((*ms)[i], "uuid");
          if (id.empty()) id = "#" + std::to_string(i);
          claude_index.emplace(id, i);
        }
      }
    }
    for (const auto& m : w.messages) {
      Block b;
      b.text = json::get_string(m, "text");
      std::string node = json::get_string(m, "node");
      if (platform == "chatgpt") {
        b.pointer = "/mapping/" + escape_pointer(node) + "/message/content/parts/0";
      } else {
        auto it = claude_index.find(node);
        b.pointer = it == claude_index.end() ? "" : "/chat_messages/" + std::to_string(it->second) + "/text";
      }
      b.speaker = json::get_string(m, "role");
      b.date = json::get_string(m, "date");
      b.utterance = true;
      b.attrs = Json{{"node", node},
                     {"parent", json::get_string(m, "parent")},
                     {"branch", json::get_string(m, "branch")},
                     {"current", json::get_bool(m, "current", true)}};
      ub.blocks.push_back(std::move(b));
    }
    ub.forks = w.forks;
    return ub;
  }
  const std::string date = c.unit.date;
  if (platform == "claude_projects") {
    add_string_block(ub, e, "name", "", date);
    add_string_block(ub, e, "description", "", date);
    add_string_block(ub, e, "prompt_template", "", date);
    if (const Json* d = json::find(e, "docs"); d && d->is_array()) {
      for (std::size_t i = 0; i < d->size(); ++i) {
        add_string_block(ub, (*d)[i], "content", "/docs/" + std::to_string(i), date);
      }
    }
    return ub;
  }
  if (platform == "claude_memories") {
    add_string_block(ub, e, "conversations_memory", "", date);
    if (const Json* pm = json::find(e, "project_memories"); pm && pm->is_object()) {
      for (auto it = pm->begin(); it != pm->end(); ++it) {
        if (!it.value().is_string()) continue;
        Block b;
        b.text = it.value().get<std::string>();
        b.pointer = "/project_memories/" + escape_pointer(it.key());
        b.speaker = "project_memories";
        b.date = date;
        b.attrs["field"] = "project_memories";
        b.attrs["project_ext_id"] = it.key();
        ub.blocks.push_back(std::move(b));
      }
    }
    return ub;
  }
  if (platform == "git_commit") {
    for (const char* k : {"hash", "date", "author"}) {
      if (const Json* v = json::find(e, k); v && v->is_string()) {
        Block b;
        b.text = std::string(k) + ": " + v->get<std::string>();
        b.pointer = std::string("/") + k;
        b.speaker = k;
        b.date = date;
        b.field = true;
        b.attrs["field"] = k;
        ub.blocks.push_back(std::move(b));
      }
    }
    add_string_block(ub, e, "subject", "", date);
    add_string_block(ub, e, "body", "", date);
    if (const Json* f = json::find(e, "files"); f && f->is_array()) {
      for (std::size_t i = 0; i < f->size(); ++i) {
        if (!(*f)[i].is_string()) continue;
        Block b;
        b.text = "file: " + (*f)[i].get<std::string>();
        b.pointer = "/files/" + std::to_string(i);
        b.speaker = "files";
        b.date = date;
        b.field = true;
        b.attrs["field"] = "files";
        ub.blocks.push_back(std::move(b));
      }
    }
    return ub;
  }
  if (!c.text.empty()) {
    Block b;
    b.text = c.text;
    b.date = date;
    ub.blocks.push_back(std::move(b));
  }
  return ub;
}

}  // namespace detail

// ── Extractor ───────────────────────────────────────────────────────
Extractor::Extractor(std::shared_ptr<const kb::Pack> pack) : Extractor(std::move(pack), Json::array()) {}
Extractor::Extractor(std::shared_ptr<const kb::Pack> pack, const Json& discovered)
    : pack_(std::move(pack)), lex_(std::make_unique<detail::Lexicons>(*pack_, discovered)) {}
Extractor::~Extractor() = default;

namespace {

// '*' = any run without '/', '**' = anything.
bool glob_match(std::string_view pat, std::string_view s) {
  if (pat.empty()) return s.empty();
  if (pat.substr(0, 2) == "**") {
    std::string_view rest = pat.substr(2);
    if (!rest.empty() && rest[0] == '/') {
      // "**/x": x at the root or after any '/'
      std::string_view r2 = rest.substr(1);
      if (glob_match(r2, s)) return true;
      for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && glob_match(r2, s.substr(i + 1))) return true;
      }
      return false;
    }
    for (std::size_t i = 0; i <= s.size(); ++i) {
      if (glob_match(rest, s.substr(i))) return true;
    }
    return false;
  }
  if (pat[0] == '*') {
    for (std::size_t i = 0; i <= s.size(); ++i) {
      if (glob_match(pat.substr(1), s.substr(i))) return true;
      if (i < s.size() && s[i] == '/') break;
    }
    return false;
  }
  if (s.empty()) return false;
  if (pat[0] != '?' && pat[0] != s[0]) return false;
  return glob_match(pat.substr(1), s.substr(1));
}

std::string lower_ascii(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

}  // namespace

Result<std::vector<Detection>> Extractor::detect(const UnitContent& content) const {
  std::vector<Detection> out;
  const std::string member = content.unit.locator.member.empty() ? content.unit.title : content.unit.locator.member;
  const std::string lmember = lower_ascii(member);
  std::string platform =
      content.structured.is_object() ? archive::sniff_export_element(content.structured) : std::string();
  if (platform.empty() && content.structured.is_object() && json::find(content.structured, "hash") &&
      json::find(content.structured, "files")) {
    platform = "git_log";
  }
  // Text used by heading/cue ops: the unit text or the structured strings.
  std::string text = content.text;
  if (text.empty() && content.structured.is_object()) {
    for (const auto& b : detail::unit_blocks(content).blocks) {
      text += b.text;
      text += "\n";
    }
  }
  const std::string folded = lex_->fold(text);
  for (const auto& id : pack_->ids("artifact_types")) {
    auto t = model::artifact_type(*pack_, id);
    if (!t) continue;
    const Json* any = json::find(t->detect, "any");
    if (!any || !any->is_array()) continue;
    Detection d;
    d.artifact_type = id;
    for (const auto& op : *any) {
      std::string name = json::get_string(op, "op");
      std::string value = json::get_string(op, "value");
      double w = 0.0;
      if (name == "file_ext") {
        std::string v = lower_ascii(value);
        if (!v.empty() && lmember.size() >= v.size() && lmember.compare(lmember.size() - v.size(), v.size(), v) == 0) w = 1.0;
      } else if (name == "path_glob") {
        if (!member.empty() && glob_match(value, member)) w = 1.0;
      } else if (name == "mime") {
        std::string mime = json::get_string(content.unit.attrs, "mime");
        if (!mime.empty()) {
          if (!value.empty() && value.back() == '*') {
            if (mime.rfind(value.substr(0, value.size() - 1), 0) == 0) w = 1.0;
          } else if (mime == value) {
            w = 1.0;
          }
        }
      } else if (name == "json_key") {
        if (content.structured.is_object() && json::find(content.structured, value)) w = 1.0;
      } else if (name == "export_shape") {
        if (!platform.empty() && platform == value) w = 1.0;
      } else if (name == "heading") {
        // A line that starts with the value (markdown heading marks ignored).
        detail::Phrase p = detail::make_phrase(lex_->norm, value, 1.0);
        std::size_t pos = 0;
        while (pos < folded.size()) {
          std::size_t eol = folded.find('\n', pos);
          if (eol == std::string::npos) eol = folded.size();
          std::string_view line(folded.data() + pos, eol - pos);
          while (!line.empty() && (line[0] == '#' || line[0] == ' ' || line[0] == '*')) line.remove_prefix(1);
          if (line.substr(0, p.text.size()) == p.text) {
            w = 0.5;
            break;
          }
          pos = eol + 1;
        }
      } else if (name == "cue") {
        double s = lex_->score(value, folded);
        if (s > 0) w = std::min(0.6, 0.2 * s);
      }
      if (w > 0) {
        d.score += w;
        d.reasons.push_back(Json{{"op", name}, {"value", value}, {"weight", w}});
      }
    }
    d.score = std::min(1.0, d.score);
    double min_score = json::get_number(t->detect, "min_score", 0.5);
    if (d.score >= min_score && d.score > 0) out.push_back(std::move(d));
  }
  std::sort(out.begin(), out.end(), [](const Detection& a, const Detection& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.artifact_type < b.artifact_type;
  });
  return out;
}

namespace {

struct SegCtx {
  const detail::Lexicons& lex;
  const UnitContent& content;
  std::string artifact_type;
  std::set<std::string> seg;
  std::vector<Observation> out;
  int ordinal = 0;
  std::string heading_path;
};

std::string lang_of(const detail::Lexicons& lex, std::string_view text) {
  kb::Lang l = lex.norm.guess_lang(text);
  if (l == kb::Lang::Unknown) return "";
  return std::string(kb::to_string(l));
}

void emit(SegCtx& s, const detail::Block& b, ObservationKind kind, std::size_t start, std::size_t len, int line,
          Json attrs = Json::object()) {
  std::string text(b.text.substr(start, len));
  std::string_view st = utf8::strip(text);
  if (st.empty()) return;
  // keep the exact bytes of the stripped span
  std::size_t lead = static_cast<std::size_t>(st.data() - text.data());
  start += lead;
  len = st.size();
  Observation o;
  o.unit = s.content.unit.id;
  o.kind = kind;
  o.text = std::string(st);
  o.locator.source = s.content.unit.locator.source.empty() ? s.content.unit.source : s.content.unit.locator.source;
  o.locator.member = s.content.unit.locator.member;
  o.locator.json_pointer = s.content.unit.locator.json_pointer + b.pointer;
  if (s.content.unit.locator.byte_start && b.pointer.empty()) {
    o.locator.byte_start = *s.content.unit.locator.byte_start + static_cast<std::int64_t>(start);
  } else {
    o.locator.byte_start = static_cast<std::int64_t>(start);
  }
  o.locator.byte_len = static_cast<std::int64_t>(len);
  if (line > 0) o.locator.line = line;
  o.lang = lang_of(s.lex, o.text);
  o.date = b.date.empty() ? s.content.unit.date : b.date;
  o.ordinal = s.ordinal++;
  o.artifact_type = s.artifact_type;
  o.speaker = b.speaker;
  Json a = b.attrs;
  for (auto it = attrs.begin(); it != attrs.end(); ++it) a[it.key()] = it.value();
  if (!s.heading_path.empty() && kind != ObservationKind::Heading) a["heading"] = s.heading_path;
  o.attrs = std::move(a);
  o.id = Observation::make_id(o.unit, o.locator, o.text);
  s.out.push_back(std::move(o));
}

const std::set<std::string> kAbbrev = {"art", "ust", "np", "tj", "itd", "itp", "pkt", "par", "nr", "dr", "prof",
                                       "godz", "str", "ok", "tzw", "e.g", "i.e", "vs", "etc", "mr", "mrs", "cf",
                                       "fig", "ul", "tel", "wg", "m.in", "k.p.c", "k.p", "k.p.a", "k.p.k", "ds"};

// Sentence spans [start, end) of one line (byte offsets into the line).
std::vector<std::pair<std::size_t, std::size_t>> sentence_spans(std::string_view line) {
  std::vector<std::pair<std::size_t, std::size_t>> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c != '.' && c != '!' && c != '?') continue;
    std::size_t j = i;
    while (j + 1 < line.size() && (line[j + 1] == '.' || line[j + 1] == '!' || line[j + 1] == '?')) ++j;
    if (j + 1 < line.size() && line[j + 1] != ' ') {
      i = j;
      continue;
    }
    std::size_t k = j + 1;
    while (k < line.size() && line[k] == ' ') ++k;
    if (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])) && c == '.') {
      i = j;
      continue;
    }
    if (c == '.' && j == i) {
      // the word before the dot
      std::size_t w = i;
      while (w > start && line[w - 1] != ' ' && line[w - 1] != '(') --w;
      std::string word = lower_ascii(std::string(line.substr(w, i - w)));
      if (kAbbrev.count(word) || (word.size() == 1 && std::isalpha(static_cast<unsigned char>(word[0])))) {
        i = j;
        continue;
      }
    }
    out.emplace_back(start, j + 1);
    start = k;
    i = k == 0 ? 0 : k - 1;
  }
  if (start < line.size()) out.emplace_back(start, line.size());
  return out;
}

// "- x", "* x", "• x", "1. x", "1) x", "a) x", "§ 1 x" -> marker length.
std::size_t list_marker(std::string_view line, bool* numbered) {
  std::size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  std::string_view s = line.substr(i);
  *numbered = false;
  if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') return i + 2;
  if (s.substr(0, 4) == "\xE2\x80\xA2 ") return i + 4;  // •
  std::size_t d = 0;
  while (d < s.size() && d < 3 && std::isdigit(static_cast<unsigned char>(s[d]))) ++d;
  if (d > 0 && d + 1 < s.size() && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') {
    *numbered = true;
    return i + d + 2;
  }
  if (s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ')' && s[2] == ' ') {
    *numbered = true;
    return i + 3;
  }
  if (s.substr(0, 3) == "\xC2\xA7 ") {  // "§ "
    *numbered = true;
    std::size_t k = 3;
    while (k < s.size() && s[k] != ' ') ++k;
    return i + std::min(s.size(), k + 1);
  }
  return 0;
}

bool is_scene_heading(std::string_view line) {
  std::string_view s = utf8::strip(line);
  return s.substr(0, 4) == "INT." || s.substr(0, 4) == "EXT." || s.substr(0, 8) == "INT./EXT";
}

// "00:00:01.000 --> 00:00:04.000" (VTT/SRT) -> seconds.
bool parse_cue_times(std::string_view line, double* a, double* b) {
  std::size_t arrow = line.find("-->");
  if (arrow == std::string_view::npos) return false;
  auto parse = [](std::string_view t, double* out) {
    t = utf8::strip(t);
    double parts[3] = {0, 0, 0};
    int n = 0;
    std::string cur;
    for (char c : t) {
      if (c == ':') {
        if (n >= 2) return false;
        parts[n++] = std::atof(cur.c_str());
        cur.clear();
      } else if (c == ',' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
        cur.push_back(c == ',' ? '.' : c);
      } else if (c == ' ') {
        break;
      } else {
        return false;
      }
    }
    parts[n++] = std::atof(cur.c_str());
    double s = 0;
    for (int i = 0; i < n; ++i) s = s * 60 + parts[i];
    *out = s;
    return true;
  };
  return parse(line.substr(0, arrow), a) && parse(line.substr(arrow + 3), b);
}

void segment_block(SegCtx& s, const detail::Block& b) {
  const bool sentences = s.seg.count("sentences") > 0;
  const bool lists = s.seg.count("list_items") > 0;
  const bool numbered_ok = s.seg.count("numbered_paragraphs") > 0;
  const bool headings = s.seg.count("markdown_sections") > 0;
  const bool code = s.seg.count("code_blocks") > 0;
  const bool scenes = s.seg.count("scene_headings") > 0;
  const bool timed = s.seg.count("timestamped_utterances") > 0;
  const bool email = s.seg.count("email_headers") > 0;
  std::string_view text = b.text;
  std::size_t pos = 0;
  int line_no = 0;
  bool in_code = false;
  std::size_t code_start = 0;
  int code_line = 0;
  std::string code_lang;
  bool in_headers = email && b.pointer.empty();
  std::optional<std::pair<double, double>> cue_time;
  std::string scene_speaker;
  while (pos <= text.size()) {
    std::size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    ++line_no;
    std::string_view raw = text.substr(pos, eol - pos);
    if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
    std::string_view st = utf8::strip(raw);
    std::size_t lead = static_cast<std::size_t>(st.data() - raw.data());
    const int line = b.pointer.empty() ? line_no : 0;
    if (st.substr(0, 3) == "```") {
      if (!in_code) {
        in_code = true;
        code_start = eol + 1;
        code_line = line;
        code_lang = std::string(utf8::strip(st.substr(3)));
      } else {
        in_code = false;
        if (code && pos > code_start) {
          emit(s, b, ObservationKind::CodeBlock, code_start, pos - code_start - 1, code_line + 1, Json{{"language", code_lang}});
        }
      }
    } else if (in_code) {
      // inside a fence
    } else if (in_headers) {
      if (st.empty()) {
        in_headers = false;
      } else if (raw[0] != ' ' && raw[0] != '\t' && raw.find(':') != std::string_view::npos) {
        std::string key(raw.substr(0, raw.find(':')));
        emit(s, b, ObservationKind::Field, pos, raw.size(), line, Json{{"field", lower_ascii(key)}});
      }
    } else if (st.empty()) {
      scene_speaker.clear();
    } else if (timed && [&] {
                 double ta = 0, tb = 0;
                 if (!parse_cue_times(st, &ta, &tb)) return false;
                 cue_time = std::make_pair(ta, tb);
                 return true;
               }()) {
      // the following lines are the cue text
    } else if (timed && cue_time && !st.empty() &&
               !std::all_of(st.begin(), st.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) &&
               st != "WEBVTT") {
      Json a{{"time", Json::array({cue_time->first, cue_time->second})}};
      std::string speaker;
      std::size_t off = lead;
      std::string_view body = st;
      if (body.substr(0, 3) == "<v ") {
        std::size_t gt = body.find('>');
        if (gt != std::string_view::npos) {
          speaker = std::string(body.substr(3, gt - 3));
          off += gt + 1;
          body = body.substr(gt + 1);
        }
      } else if (std::size_t colon = body.find(':'); colon != std::string_view::npos && colon < 30 && colon > 0) {
        speaker = std::string(body.substr(0, colon));
        off += colon + 1;
        body = body.substr(colon + 1);
      }
      if (!speaker.empty()) a["speaker"] = speaker;
      detail::Block bb = b;
      if (!speaker.empty()) bb.speaker = speaker;
      std::size_t before = s.out.size();
      emit(s, bb, ObservationKind::Utterance, pos + off, raw.size() - off, line, a);
      if (s.out.size() > before) {
        s.out.back().locator.time_start = cue_time->first;
        s.out.back().locator.time_end = cue_time->second;
        s.out.back().id = Observation::make_id(s.out.back().unit, s.out.back().locator, s.out.back().text);
      }
    } else if (headings && st[0] == '#') {
      std::size_t h = 0;
      while (h < st.size() && st[h] == '#') ++h;
      std::string title(utf8::strip(st.substr(h)));
      emit(s, b, ObservationKind::Heading, pos + lead + h, st.size() - h, line, Json{{"level", static_cast<int>(h)}});
      s.heading_path = title;
    } else if (scenes && is_scene_heading(st)) {
      emit(s, b, ObservationKind::Heading, pos + lead, st.size(), line, Json{{"scene", true}});
      s.heading_path = std::string(st);
    } else if (scenes && st.size() < 40 && utf8::to_upper(st) == st &&
               std::any_of(st.begin(), st.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); })) {
      scene_speaker = std::string(st);  // character cue
    } else {
      bool numbered = false;
      std::size_t m = (lists || numbered_ok) ? list_marker(raw, &numbered) : 0;
      if (m > 0 && (lists || (numbered && numbered_ok))) {
        emit(s, b, ObservationKind::ListItem, pos + m, raw.size() - m, line,
             Json{{"depth", static_cast<int>(lead / 2)}, {"numbered", numbered}});
      } else if (!scene_speaker.empty()) {
        detail::Block bb = b;
        bb.speaker = scene_speaker;
        emit(s, bb, ObservationKind::Utterance, pos + lead, st.size(), line);
      } else if (sentences) {
        for (auto [a, e] : sentence_spans(raw)) emit(s, b, ObservationKind::Sentence, pos + a, e - a, line);
      }
    }
    if (eol >= text.size()) break;
    pos = eol + 1;
  }
}

}  // namespace

Result<std::vector<Observation>> Extractor::segment(const model::ArtifactType& type, const UnitContent& content) const {
  SegCtx s{*lex_, content, type.header.id, {}, {}, 0, {}};
  for (const auto& x : detail::segmenters_of(type)) s.seg.insert(x);
  detail::UnitBlocks ub = detail::unit_blocks(content);
  const bool messages = s.seg.count("messages") > 0;
  const bool fields = s.seg.count("commit_fields") > 0 || s.seg.count("email_headers") > 0;
  for (const auto& b : ub.blocks) {
    s.heading_path.clear();
    std::size_t utt = SIZE_MAX;
    if (b.utterance && messages) {
      std::size_t before = s.out.size();
      emit(s, b, ObservationKind::Utterance, 0, b.text.size(), 0);
      if (s.out.size() > before) utt = before;
    } else if (b.field && fields) {
      emit(s, b, ObservationKind::Field, 0, b.text.size(), 0);
      continue;
    }
    if (b.field) continue;
    std::size_t first = s.out.size();
    segment_block(s, b);
    if (utt != SIZE_MAX) {
      // A one-sentence message is its own leaf: the sentence would repeat the
      // utterance (same locator and text, hence the same observation id).
      bool same = s.out.size() == first + 1 && s.out.back().id == s.out[utt].id;
      if (same) s.out.pop_back();
      if (same || s.out.size() == first) s.out[utt].attrs["leaf"] = true;
    }
  }
  if (s.seg.count("code_symbols") && content.structured.is_null() && !content.text.empty()) {
    // One Field per declared symbol, located at its first declaring line.
    const std::string& member = content.unit.locator.member.empty() ? content.unit.title : content.unit.locator.member;
    std::string language = archive::code_language(member);
    archive::CodeDigest dg = archive::digest_code(member, language.empty() ? "text" : language, content.text);
    detail::Block whole;
    whole.text = content.text;
    whole.date = content.unit.date;
    for (const auto& sym : dg.symbols) {
      std::size_t p = content.text.find(sym);
      if (p == std::string::npos) continue;
      std::size_t ls = content.text.rfind('\n', p);
      ls = ls == std::string::npos ? 0 : ls + 1;
      std::size_t le = content.text.find('\n', p);
      if (le == std::string::npos) le = content.text.size();
      int line = 1 + static_cast<int>(std::count(content.text.begin(), content.text.begin() + static_cast<std::ptrdiff_t>(ls), '\n'));
      emit(s, whole, ObservationKind::Field, ls, le - ls, line, Json{{"symbol", sym}, {"language", dg.language}});
    }
    // version declarations (__version__ = "x", project(... VERSION x)) as fields
    std::u32string u = utf8::decode(content.text);
    std::set<std::size_t> lines_done;
    for (const auto& d : lex_->version_decls) {
      if (d.id == "changelog_line" || d.id == "commit_subject") continue;
      for (const auto& m : d.re.finditer(u)) {
        std::size_t p = utf8::byte_offset(content.text, static_cast<std::size_t>(m.start(0)));
        std::size_t ls = content.text.rfind('\n', p);
        ls = ls == std::string::npos ? 0 : ls + 1;
        if (!lines_done.insert(ls).second) continue;
        std::size_t le = content.text.find('\n', p);
        if (le == std::string::npos) le = content.text.size();
        int line = 1 + static_cast<int>(std::count(content.text.begin(), content.text.begin() + static_cast<std::ptrdiff_t>(ls), '\n'));
        emit(s, whole, ObservationKind::Field, ls, le - ls, line, Json{{"declaration", d.id}});
      }
    }
  }
  return std::move(s.out);
}

Result<Extraction> Extractor::extract(const model::ArtifactType& type, const UnitContent& content,
                                      const std::vector<Observation>& observations) const {
  return detail::run_extractors(*lex_, *pack_, type.header.id, detail::extractor_ops(type), content, observations);
}

Result<Extraction> Extractor::process(const UnitContent& content) const {
  LOOM_TRY_ASSIGN(auto dets, detect(content));
  model::ArtifactType type = detail::fallback_type();
  std::vector<std::string> ops;
  if (!dets.empty()) {
    auto t = model::artifact_type(*pack_, dets.front().artifact_type);
    if (t) type = std::move(*t);
  }
  ops = detail::extractor_ops(type);
  // Secondary artifact types detected in the same unit contribute their
  // extractors (a brainstorm inside a conversation keeps its areas).
  for (std::size_t i = 1; i < dets.size(); ++i) {
    auto t = model::artifact_type(*pack_, dets[i].artifact_type);
    if (!t || (t->medium != model::Medium::Text && t->medium != type.medium)) continue;
    for (const auto& op : detail::extractor_ops(*t)) {
      if (std::find(ops.begin(), ops.end(), op) == ops.end()) ops.push_back(op);
    }
  }
  if (dets.empty()) {
    // Unknown shape: generic prose extractors over whatever text it has.
    auto t = detail::fallback_type();
    for (const auto& op : detail::extractor_ops(t)) {
      if (std::find(ops.begin(), ops.end(), op) == ops.end()) ops.push_back(op);
    }
  }
  LOOM_TRY_ASSIGN(auto obs, segment(type, content));
  Extraction ex = detail::run_extractors(*lex_, *pack_, type.header.id, ops, content, obs);
  Json dj = Json::array();
  for (const auto& d : dets) dj.push_back(d.to_json());
  ex.stats["detections"] = dj;
  ex.stats["artifact_type"] = type.header.id;
  return ex;
}

Result<std::vector<ClassifiedItem>> Extractor::classify_items(const model::ProjectKind& kind,
                                                              const std::vector<model::Facet>& facets,
                                                              const std::vector<Observation>& items) const {
  std::vector<detail::KindRef> kinds;
  for (const auto& k : kind.domain_kinds) kinds.push_back({&k, kind.header.id});
  for (const auto& f : facets) {
    for (const auto& k : f.domain_kinds) kinds.push_back({&k, f.header.id});
  }
  return detail::classify(*lex_, kinds, items);
}

}  // namespace loom::extract
