// Archive ingest: source planning (files, directories, zips, a repository,
// git history, existing conversations) and the adapters that turn them into
// corpus documents, stored as conversations/messages with sources +
// provenance (MEGA MASTER 2.F: raw bytes in the BlobStore, derived rows
// reproducible).
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <unordered_map>

#include "archive/archive_runtime.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/log.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "miniz.h"

namespace loom::archive {
namespace fs = std::filesystem;

namespace {
constexpr std::string_view kLog = "loom.archive";

const std::set<std::string>& skip_dirs() {
  static const std::set<std::string> k = {
      ".git",     "build",        "node_modules", "third_party", "vendor",        "__pycache__", ".venv",
      "venv",     "env",          "dist",         "target",      ".gradle",       ".idea",       ".vscode",
      ".cache",   ".pytest_cache", ".mypy_cache", "Pods",        "DerivedData",   ".tox",        "site-packages",
      "fixtures", "testdata",     ".next",        ".svn",        ".hg",           "out",         ".claude"};
  return k;
}

std::string lower_ext(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

bool is_doc_ext(const std::string& e) {
  return e == ".md" || e == ".markdown" || e == ".txt" || e == ".rst" || e == ".adoc" || e == ".org";
}

bool is_import_ext(const std::string& e) {
  static const std::set<std::string> k = {".jsonl", ".ndjson", ".html", ".htm",  ".mht",  ".mhtml", ".db",
                                          ".sqlite", ".sqlite3", ".png", ".jpg", ".jpeg", ".webp"};
  return k.count(e) > 0;
}

// explicit = the user named this file/dir as a source (chat exports allowed).
std::string adapter_for(const fs::path& p, bool explicit_source) {
  std::string e = lower_ext(p);
  if (!code_language(p).empty()) return "code";
  if (is_doc_ext(e)) return "text";
  if (!explicit_source) return "";
  if (e == ".zip") return "zip";
  if (e == ".json") return "json";
  if (is_import_ext(e)) return "import";
  return "";
}

std::string generic(const fs::path& p) { return p.generic_string(); }

bool under(const fs::path& child, const fs::path& parent) {
  if (parent.empty()) return false;
  auto rel = child.lexically_relative(parent);
  return !rel.empty() && rel.native().rfind("..", 0) != 0 && rel != ".";
}

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  return out + "'";
}

Json locator_of(const std::string& key, const std::string& conv, const std::string& unit, int ordinal) {
  return Json{{"doc_key", key}, {"conv", conv}, {"unit", unit}, {"ordinal", ordinal}};
}

}  // namespace

// ── Plan ────────────────────────────────────────────────────────────
Json PlannedFile::to_json(bool with_path) const {
  Json j{{"uri", uri}, {"adapter", adapter}, {"hash", hash}, {"size", size}};
  if (with_path) j["path"] = path;
  return j;
}

PlannedFile PlannedFile::from_json(const Json& j) {
  PlannedFile f;
  f.path = json::get_string(j, "path");
  f.uri = json::get_string(j, "uri");
  f.adapter = json::get_string(j, "adapter");
  f.hash = json::get_string(j, "hash");
  f.size = json::get_int(j, "size");
  return f;
}

Json Plan::fingerprint() const {
  Json files_j = Json::array();
  for (const auto& f : files) files_j.push_back(f.to_json(false));
  return Json{{"files", files_j},       {"repo", repo_name}, {"git", git},     {"git_head", git_head},
              {"include_db", include_db}, {"db", db_fingerprint}, {"project", project}};
}

Json Plan::to_json() const {
  Json files_j = Json::array();
  for (const auto& f : files) files_j.push_back(f.to_json(true));
  return Json{{"files", files_j},          {"repo_path", repo_path}, {"repo", repo_name},
              {"git", git},                {"git_head", git_head},   {"include_db", include_db},
              {"db", db_fingerprint},      {"project", project},     {"warnings", warnings}};
}

Plan Plan::from_json(const Json& j) {
  Plan p;
  if (const Json* f = json::find(j, "files"); f && f->is_array()) {
    for (const auto& x : *f) p.files.push_back(PlannedFile::from_json(x));
  }
  p.repo_path = json::get_string(j, "repo_path");
  p.repo_name = json::get_string(j, "repo");
  p.git = json::get_bool(j, "git");
  p.git_head = json::get_string(j, "git_head");
  p.include_db = json::get_bool(j, "include_db");
  p.db_fingerprint = json::get_string(j, "db");
  p.project = json::get_string(j, "project");
  if (const Json* w = json::find(j, "warnings"); w && w->is_array()) {
    for (const auto& x : *w) p.warnings.push_back(x.get<std::string>());
  }
  return p;
}

Result<std::string> run_git(const std::string& repo, const std::vector<std::string>& args) {
#if defined(_WIN32) || defined(__ANDROID__)
  (void)repo;
  (void)args;
  return Error(Errc::Unsupported, "git adapter is desktop-only");
#else
  std::string cmd = "git -C " + shell_quote(repo);
  for (const auto& a : args) cmd += " " + shell_quote(a);
  cmd += " 2>/dev/null";
  FILE* f = ::popen(cmd.c_str(), "r");
  if (!f) return Error(Errc::Unavailable, "cannot start git");
  std::string out;
  std::array<char, 65536> buf{};
  std::size_t n;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) out.append(buf.data(), n);
  int rc = ::pclose(f);
  if (rc != 0) return Error(Errc::Unavailable, "git " + (args.empty() ? std::string() : args[0]) + " failed");
  return out;
#endif
}

Result<Plan> make_plan(Runtime& rt, const ArchiveConfig& cfg) {
  Plan plan;
  plan.include_db = cfg.include_db;
  fs::path repo;
  std::error_code ec;
  if (cfg.repo && !cfg.repo->empty()) {
    repo = fs::weakly_canonical(fsutil::expand_user(*cfg.repo), ec);
    if (ec || !fs::is_directory(repo)) return Error(Errc::InvalidArgument, "repo is not a directory: " + *cfg.repo);
    plan.repo_path = repo.string();
    plan.repo_name = repo.filename().string();
    if (plan.repo_name.empty()) plan.repo_name = "repo";
  }
  fs::path out_dir;
  if (!cfg.out_dir.empty()) out_dir = fs::weakly_canonical(fsutil::expand_user(cfg.out_dir), ec);

  struct Cand {
    fs::path path;
    std::string uri;
    std::string adapter;
  };
  std::vector<Cand> cands;
  auto uri_for = [&](const fs::path& p, const fs::path* root) {
    if (!repo.empty() && under(p, repo)) return generic(p.lexically_relative(repo));
    if (root) return generic(root->filename() / p.lexically_relative(*root));
    return p.filename().string();
  };
  auto excluded = [&](const std::string& uri) {
    for (const auto& x : cfg.exclude) {
      if (!x.empty() && uri.find(x) != std::string::npos) return true;
    }
    return false;
  };
  auto walk = [&](const fs::path& root, bool explicit_source) {
    std::vector<Cand> found;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
      if (ec) break;
      const fs::directory_entry& e = *it;
      fs::path p = e.path();
      std::string name = p.filename().string();
      if (e.is_symlink(ec)) {
        if (e.is_directory(ec)) it.disable_recursion_pending();
        continue;
      }
      if (e.is_directory(ec)) {
        std::string uri = uri_for(p, &root);
        if (skip_dirs().count(name) || name.rfind("cmake-build", 0) == 0 || fs::exists(p / ".loom-archive") ||
            (!out_dir.empty() && fs::weakly_canonical(p, ec) == out_dir) || excluded(uri + "/")) {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (!e.is_regular_file(ec)) continue;
      std::string adapter = adapter_for(p, explicit_source);
      if (adapter.empty()) continue;
      std::string uri = uri_for(p, &root);
      if (excluded(uri)) continue;
      found.push_back({p, uri, adapter});
    }
    std::sort(found.begin(), found.end(), [](const Cand& a, const Cand& b) { return a.uri < b.uri; });
    for (auto& f : found) cands.push_back(std::move(f));
  };

  for (const auto& s : cfg.sources) {
    fs::path p = fs::weakly_canonical(fsutil::expand_user(s), ec);
    if (ec || !fs::exists(p)) return Error(Errc::NotFound, "source not found: " + s);
    if (fs::is_directory(p)) {
      walk(p, true);
    } else {
      std::string adapter = adapter_for(p, true);
      if (adapter.empty()) {
        // unknown extension: treat valid UTF-8 text as a document
        auto head = fsutil::read_file(p);
        if (head && head->size() < 2'000'000 && utf8::is_valid(*head) && head->find('\0') == std::string::npos) {
          adapter = "text";
        } else {
          plan.warnings.push_back("skipped unsupported source: " + p.filename().string());
          continue;
        }
      }
      cands.push_back({p, uri_for(p, nullptr), adapter});
    }
  }
  if (!repo.empty() && cfg.code) walk(repo, false);

  std::set<std::string> seen_paths;
  std::set<std::string> seen_text;
  for (auto& c : cands) {
    std::string canon = fs::weakly_canonical(c.path, ec).string();
    if (!seen_paths.insert(canon).second) continue;
    std::int64_t size = static_cast<std::int64_t>(fs::file_size(c.path, ec));
    if (ec) continue;
    bool bounded = c.adapter == "text" || c.adapter == "code";
    if (bounded && size > cfg.max_file_bytes) {
      plan.warnings.push_back("skipped large file: " + c.uri);
      continue;
    }
    LOOM_TRY_ASSIGN(std::string h, sha256_file_hex(c.path));
    if (c.adapter == "text" && !seen_text.insert(h).second) continue;  // same document elsewhere
    plan.files.push_back({canon, c.uri, c.adapter, h, size});
  }
  std::stable_sort(plan.files.begin(), plan.files.end(),
                   [](const PlannedFile& a, const PlannedFile& b) { return a.uri < b.uri; });

  if (!repo.empty() && cfg.git) {
    auto head = run_git(plan.repo_path, {"rev-parse", "HEAD"});
    if (head) {
      plan.git = true;
      plan.git_head = std::string(utf8::strip(*head));
    } else {
      plan.warnings.push_back("git history skipped: " + head.error().message);
    }
  }
  if (cfg.include_db) {
    Sha256 h;
    LOOM_TRY_ASSIGN(auto convs, rt.db().list_convs(1'000'000));
    for (const auto& c : convs) {
      if (c.source.rfind("archive.", 0) == 0) continue;
      h.update(c.id);
      h.update(c.updated);
    }
    plan.db_fingerprint = h.finish_hex();
  }
  plan.project = !cfg.project.empty() ? cfg.project : !plan.repo_name.empty() ? plan.repo_name : "";
  if (plan.project.empty() && !cfg.sources.empty()) plan.project = fs::path(cfg.sources.front()).stem().string();
  if (plan.project.empty()) plan.project = "project";
  return plan;
}

// ── Ingest ──────────────────────────────────────────────────────────
namespace {

struct Unit {
  std::string key;
  std::string title;       // conversation title
  std::string conv_source; // conversations.source
  std::vector<Doc> docs;
};

using PrevBindings = std::unordered_map<std::string, std::pair<std::string, std::string>>;  // key -> (msg, conv)

class Ingestor {
 public:
  Ingestor(Runtime& rt, const Plan& plan, StageControl& ctl, IngestResult& res)
      : rt_(rt), plan_(plan), ctl_(ctl), res_(res) {}

  Status run();

 private:
  Status process_file(const PlannedFile& f);
  Status ingest_text(const PlannedFile& f, std::string_view content, const std::string& blob, const std::string& kind);
  Status ingest_code(const PlannedFile& f, std::string_view content, const std::string& blob);
  Status ingest_json_file(const fs::path& path, const std::string& uri, const std::string& blob,
                          const std::string& source_kind, bool* handled);
  Status ingest_zip(const PlannedFile& f, const std::string& blob);
  Status ingest_import(const fs::path& path, const std::string& uri);
  Status ingest_git();
  Status ingest_db();

  // Finds or registers the source row for (blob, parser, uri); fills prev.
  Result<std::string> source_for(const std::string& blob, const std::string& parser, const std::string& uri,
                                 const std::string& kind, const std::string& format, std::int64_t size,
                                 PrevBindings& prev);
  Status store_unit(Unit& u, const std::string& source_id, const std::string& transform, const PrevBindings& prev);
  void add_source_summary(const std::string& uri, const std::string& adapter, const std::string& format,
                          const std::string& hash, std::size_t docs, std::size_t units);

  Runtime& rt_;
  const Plan& plan_;
  StageControl& ctl_;
  IngestResult& res_;
  std::unordered_map<std::string, std::string> path_date_;
  std::set<std::string> keys_;
  std::size_t created_convs_ = 0;
  std::size_t reused_units_ = 0;
};

Result<std::string> Ingestor::source_for(const std::string& blob, const std::string& parser, const std::string& uri,
                                         const std::string& kind, const std::string& format, std::int64_t size,
                                         PrevBindings& prev) {
  auto& prov = rt_.provenance();
  LOOM_TRY_ASSIGN(auto srcs, prov.find_sources_by_hash(blob));
  for (const auto& s : srcs) {
    if (s.parser != parser || s.parser_version != kPipelineVersion) continue;
    if (json::get_string(s.metadata, "uri") != uri) continue;
    LOOM_TRY_ASSIGN(auto recs, prov.for_source(s.id, 10'000'000));
    for (const auto& r : recs) {
      if (r.subject_kind != "message") continue;
      std::string key = json::get_string(r.locator, "doc_key");
      if (!key.empty()) prev[key] = {r.subject_id, json::get_string(r.locator, "conv")};
    }
    return s.id;
  }
  SourceRecord rec;
  rec.kind = kind;
  rec.uri = uri;
  rec.blob_hash = blob;
  rec.size = size;
  rec.format = format;
  rec.title = uri;
  rec.parser = parser;
  rec.parser_version = std::string(kPipelineVersion);
  rec.metadata = Json{{"uri", uri}, {"project", plan_.project}};
  return prov.add_source(std::move(rec));
}

Status Ingestor::store_unit(Unit& u, const std::string& source_id, const std::string& transform,
                            const PrevBindings& prev) {
  // drop blank / duplicate docs (batch insert skips blanks; keys must be unique)
  std::vector<Doc> docs;
  for (auto& d : u.docs) {
    if (utf8::is_blank(d.text) || keys_.count(d.key)) continue;
    keys_.insert(d.key);
    docs.push_back(std::move(d));
  }
  u.docs.clear();
  if (docs.empty()) return {};
  Database& db = rt_.db();
  bool reuse = true;
  for (const auto& d : docs) {
    if (!prev.count(d.key)) {
      reuse = false;
      break;
    }
  }
  if (reuse) {
    LOOM_TRY_ASSIGN(auto m, db.get_msg(prev.at(docs.front().key).first));
    reuse = m.has_value();
  }
  if (reuse) {
    for (const auto& d : docs) {
      const auto& [msg, conv] = prev.at(d.key);
      res_.bindings[d.key] = Json{{"msg", msg}, {"conv", conv}};
    }
    ++reused_units_;
  } else {
    LOOM_TRY_ASSIGN(Conversation conv, db.create_conv(u.title));
    ConvPatch patch;
    patch.source = u.conv_source;
    patch.metadata = Json{{"archive", Json{{"unit", u.key}, {"uri", docs.front().uri}}}};
    LOOM_TRY(db.update_conv(conv.id, patch));
    std::vector<BatchMessage> batch;
    batch.reserve(docs.size());
    for (const auto& d : docs) {
      BatchMessage bm;
      bm.role = d.role;
      bm.text = d.text;
      bm.metadata = Json{{"archive", Json{{"key", d.key}, {"kind", d.kind}, {"date", d.date}, {"label", d.label}}}};
      batch.push_back(std::move(bm));
    }
    LOOM_TRY(db.batch_create_msgs(conv.id, batch));
    LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(conv.id, true));
    if (msgs.size() != docs.size()) {
      return Error(Errc::Internal, "archive: message count mismatch for " + u.title);
    }
    std::vector<ProvenanceRecord> recs;
    ProvenanceRecord cr;
    cr.subject_id = conv.id;
    cr.subject_kind = "conversation";
    cr.source_id = source_id;
    cr.locator = Json{{"unit", u.key}};
    cr.transform = transform;
    recs.push_back(cr);
    for (std::size_t i = 0; i < docs.size(); ++i) {
      ProvenanceRecord r;
      r.subject_id = msgs[i].id;
      r.subject_kind = "message";
      r.source_id = source_id;
      r.locator = locator_of(docs[i].key, conv.id, u.key, docs[i].ordinal);
      r.transform = transform;
      recs.push_back(std::move(r));
      res_.bindings[docs[i].key] = Json{{"msg", msgs[i].id}, {"conv", conv.id}};
    }
    LOOM_TRY(rt_.provenance().add_many(std::move(recs)));
    ++created_convs_;
  }
  for (auto& d : docs) res_.corpus.docs.push_back(std::move(d));
  return {};
}

void Ingestor::add_source_summary(const std::string& uri, const std::string& adapter, const std::string& format,
                                  const std::string& hash, std::size_t docs, std::size_t units) {
  res_.corpus.sources.push_back(Json{{"uri", uri},
                                     {"adapter", adapter},
                                     {"format", format},
                                     {"hash", hash},
                                     {"units", units},
                                     {"docs", docs}});
}

Status Ingestor::ingest_text(const PlannedFile& f, std::string_view content, const std::string& blob,
                             const std::string& kind) {
  // Chat transcripts saved as markdown/text go through the Importer.
  if (kind == "doc") {
    Json md = ConversationImporter::parse_markdown(content);
    int users = 0, assistants = 0;
    for (const auto& m : md) {
      (json::get_string(m, "role") == "user" ? users : assistants) += 1;
    }
    if (users >= 2 && assistants >= 2) return ingest_import(f.path, f.uri);
  }
  PrevBindings prev;
  LOOM_TRY_ASSIGN(std::string sid, source_for(blob, "loom.archive." + kind, f.uri, "file", kind, f.size, prev));
  std::string date = first_date(utf8::prefix(content, 600));
  if (date.empty()) {
    if (auto it = path_date_.find(f.uri); it != path_date_.end()) date = it->second;
  }
  Unit u;
  u.key = "u" + hash_prefix(blob + "#" + f.uri);
  u.title = "[Doc] " + f.uri;
  u.conv_source = "archive.doc";
  int i = 0;
  for (auto& s : split_markdown(content)) {
    Doc d;
    d.key = "d" + hash_prefix(blob + "#s" + std::to_string(i));
    d.kind = kind;
    d.unit = u.key;
    d.title = f.uri;
    d.label = s.heading_path.empty() ? "L" + std::to_string(s.line) : clip(s.heading_path, 120);
    d.uri = f.uri;
    d.date = date;
    d.role = "document";
    d.text = std::move(s.text);
    d.ordinal = i++;
    d.extra = Json{{"heading", s.heading_path}, {"line", s.line}};
    u.docs.push_back(std::move(d));
  }
  std::size_t n = u.docs.size();
  LOOM_TRY(store_unit(u, sid, "archive.doc@" + std::string(kPipelineVersion), prev));
  add_source_summary(f.uri, "text", kind, blob, n, 1);
  return {};
}

Status Ingestor::ingest_code(const PlannedFile& f, std::string_view content, const std::string& blob) {
  PrevBindings prev;
  LOOM_TRY_ASSIGN(std::string sid, source_for(blob, "loom.archive.code", f.uri, "file", "code", f.size, prev));
  std::string lang = code_language(f.uri);
  CodeDigest dg = digest_code(f.uri, lang, content);
  Unit u;
  u.key = "u" + hash_prefix(blob + "#code#" + f.uri);
  u.title = "[Code] " + f.uri;
  u.conv_source = "archive.code";
  Doc d;
  d.key = "k" + hash_prefix(blob + "#code#" + f.uri);
  d.kind = "code";
  d.unit = u.key;
  d.title = f.uri;
  d.label = f.uri;
  d.uri = f.uri;
  if (auto it = path_date_.find(f.uri); it != path_date_.end()) d.date = it->second;
  d.role = "code";
  d.text = dg.text;
  Json todos = Json::array();
  for (const auto& [ln, t] : dg.todos) todos.push_back(Json{{"line", ln}, {"text", t}});
  Json syms = Json::array();
  for (std::size_t i = 0; i < dg.symbols.size() && i < 400; ++i) syms.push_back(dg.symbols[i]);
  std::int64_t lines = static_cast<std::int64_t>(std::count(content.begin(), content.end(), '\n'));
  Json uses = Json::array();
  for (const auto& used : dg.uses) uses.push_back(used);
  d.extra = Json{{"language", lang}, {"symbols", syms}, {"todos", todos}, {"lines", lines}, {"uses", uses}};
  u.docs.push_back(std::move(d));
  LOOM_TRY(store_unit(u, sid, "archive.code@" + std::string(kPipelineVersion), prev));
  add_source_summary(f.uri, "code", lang, blob, 1, 1);
  return {};
}

// Streams a top-level JSON array element by element (bounded memory); a
// non-array document is parsed whole. fn returns false to stop.
Status for_each_json_element(const fs::path& path, const std::function<bool(const Json&)>& fn) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Error(Errc::Io, "cannot open " + path.string());
  JsonArrayStreamer streamer;
  std::string chunk(1 << 20, '\0');
  bool stop = false;
  bool is_array = true;
  Status err;
  while (!stop && in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    std::streamsize n = in.gcount();
    if (n <= 0) break;
    bool ok = streamer.feed(std::string_view(chunk.data(), static_cast<std::size_t>(n)), [&](std::string_view el) {
      if (stop) return;
      auto j = json::parse(el);
      if (!j) return;
      if (!fn(*j)) stop = true;
    });
    if (!ok) {
      is_array = false;
      break;
    }
  }
  if (is_array) return err;
  LOOM_TRY_ASSIGN(std::string all, fsutil::read_file(path));
  LOOM_TRY_ASSIGN(Json j, json::parse(all));
  if (j.is_object()) {
    // {"conversations":[...]} style wrappers
    for (const char* k : {"conversations", "projects", "memories"}) {
      if (const Json* inner = json::find(j, k); inner && inner->is_array()) {
        for (const auto& e : *inner) {
          if (!fn(e)) return {};
        }
        return {};
      }
    }
  }
  fn(j);
  return {};
}

Status Ingestor::ingest_json_file(const fs::path& path, const std::string& uri, const std::string& blob,
                                  const std::string& source_kind, bool* handled) {
  *handled = false;
  std::string first_kind;
  LOOM_TRY(for_each_json_element(path, [&](const Json& e) {
    first_kind = sniff_export_element(e);
    return false;
  }));
  if (first_kind.empty()) return {};
  *handled = true;
  std::error_code ec;
  std::int64_t size = static_cast<std::int64_t>(fs::file_size(path, ec));
  PrevBindings prev;
  LOOM_TRY_ASSIGN(std::string sid,
                  source_for(blob, "loom.archive." + first_kind, uri, source_kind, first_kind, size, prev));
  std::string transform = "archive." + first_kind + "@" + std::string(kPipelineVersion);
  int idx = -1;
  std::size_t docs_before = res_.corpus.docs.size();
  std::size_t units = 0;
  Status inner;
  LOOM_TRY(for_each_json_element(path, [&](const Json& e) {
    ++idx;
    std::string kind = sniff_export_element(e);
    std::string base = blob + "#" + std::to_string(idx);
    if (kind == "chatgpt" || kind == "claude") {
      ChatWalk w = kind == "chatgpt" ? walk_chatgpt(e) : walk_claude(e);
      if (w.messages.empty()) return true;
      Unit u;
      u.key = "u" + hash_prefix(base);
      std::string title = w.title.empty() ? "(untitled)" : w.title;
      u.title = "[Chat] " + title;
      u.conv_source = "archive.chat";
      std::unordered_map<std::string, std::string> node_key;
      int ord = 0;
      for (const auto& m : w.messages) {
        Doc d;
        std::string node = json::get_string(m, "node");
        d.key = "c" + hash_prefix(base + "/" + node);
        node_key[node] = d.key;
        d.kind = "chat";
        d.unit = u.key;
        d.title = title;
        d.role = json::get_string(m, "role");
        d.label = "#" + std::to_string(ord + 1) + " " + d.role;
        d.uri = uri;
        d.date = json::get_string(m, "date");
        d.text = json::get_string(m, "text");
        d.ordinal = ord++;
        std::string parent = json::get_string(m, "parent");
        d.extra = Json{{"branch", json::get_string(m, "branch")},
                       {"current", json::get_bool(m, "current", true)},
                       {"parent", parent.empty() ? "" : node_key.count(parent) ? node_key[parent] : ""},
                       {"source", kind}};
        u.docs.push_back(std::move(d));
      }
      for (const auto& fk : w.forks) {
        Json alts = Json::array();
        for (const auto& a : fk["alternatives"]) {
          std::string fn = json::get_string(a, "first_node");
          alts.push_back(Json{{"first", node_key.count(fn) ? node_key[fn] : ""},
                              {"messages", json::get_int(a, "messages")},
                              {"current", json::get_bool(a, "current")}});
        }
        std::string after = json::get_string(fk, "node");
        res_.corpus.forks.push_back(Json{{"unit", u.key},
                                         {"title", title},
                                         {"after", node_key.count(after) ? node_key[after] : ""},
                                         {"date", json::get_string(fk, "date")},
                                         {"origin", kind},
                                         {"alternatives", alts}});
      }
      inner = store_unit(u, sid, transform, prev);
      ++units;
    } else if (kind == "claude_projects") {
      Unit u;
      u.key = "u" + hash_prefix(base);
      std::string name = json::get_string(e, "name", "(project)");
      u.title = "[Project] " + name;
      u.conv_source = "archive.project";
      std::string date = normalize_date(json::get_string(e, "created_at"));
      int ord = 0;
      auto add_sections = [&](const std::string& label_prefix, const std::string& content, const std::string& salt) {
        int si = 0;
        for (auto& s : split_markdown(content)) {
          Doc d;
          d.key = "p" + hash_prefix(base + "/" + salt + "/" + std::to_string(si++));
          d.kind = "project";
          d.unit = u.key;
          d.title = "Project: " + name;
          d.label = s.heading_path.empty() ? label_prefix : clip(label_prefix + " › " + s.heading_path, 120);
          d.uri = uri;
          d.date = date;
          d.role = "document";
          d.text = std::move(s.text);
          d.ordinal = ord++;
          d.extra = Json{{"heading", s.heading_path}};
          u.docs.push_back(std::move(d));
        }
      };
      std::string desc = json::get_string(e, "description");
      std::string prompt = json::get_string(e, "prompt_template");
      if (!utf8::is_blank(desc)) add_sections("description", desc, "desc");
      if (!utf8::is_blank(prompt)) add_sections("instructions", prompt, "prompt");
      if (const Json* docs = json::find(e, "docs"); docs && docs->is_array()) {
        int di = 0;
        for (const auto& pd : *docs) {
          add_sections(json::get_string(pd, "filename", "doc"), json::get_string(pd, "content"),
                       "doc" + std::to_string(di++));
        }
      }
      inner = store_unit(u, sid, transform, prev);
      ++units;
    } else if (kind == "claude_memories") {
      Unit u;
      u.key = "u" + hash_prefix(base);
      u.title = "[Memory] Claude memories";
      u.conv_source = "archive.memory";
      int ord = 0;
      auto add = [&](const std::string& label, const std::string& content, const std::string& salt) {
        int si = 0;
        for (auto& s : split_markdown(content)) {
          Doc d;
          d.key = "y" + hash_prefix(base + "/" + salt + "/" + std::to_string(si++));
          d.kind = "memory";
          d.unit = u.key;
          d.title = "Claude memory";
          d.label = s.heading_path.empty() ? label : clip(label + " › " + s.heading_path, 120);
          d.uri = uri;
          d.role = "document";
          d.text = std::move(s.text);
          d.ordinal = ord++;
          d.extra = Json{{"heading", s.heading_path}};
          u.docs.push_back(std::move(d));
        }
      };
      std::string cm = json::get_string(e, "conversations_memory");
      if (!utf8::is_blank(cm)) add("conversations memory", cm, "conv");
      if (const Json* pm = json::find(e, "project_memories"); pm && pm->is_object()) {
        for (auto it = pm->begin(); it != pm->end(); ++it) {
          if (it.value().is_string()) {
            add("project memory " + it.key().substr(0, 8), it.value().get<std::string>(), "p" + it.key());
          }
        }
      }
      inner = store_unit(u, sid, transform, prev);
      ++units;
    }
    return static_cast<bool>(inner);
  }));
  LOOM_TRY(inner);
  add_source_summary(uri, "json", first_kind, blob, res_.corpus.docs.size() - docs_before, units);
  return {};
}

Status Ingestor::ingest_import(const fs::path& path, const std::string& uri) {
  ImportOptions opts;
  auto r = rt_.importer().import_file(path, opts);
  if (!r) {
    res_.stats["warnings"].push_back("import failed for " + uri + ": " + r.error().message);
    return {};
  }
  std::string blob = r->blob_hash;
  std::size_t docs_before = res_.corpus.docs.size();
  int ci = 0;
  for (const auto& conv : r->conversations) {
    LOOM_TRY_ASSIGN(auto msgs, rt_.db().get_msgs(conv.id, true));
    std::string unit = "u" + hash_prefix(blob + "#imp" + std::to_string(ci));
    std::string title = conv.title;
    if (title.rfind("[Import] ", 0) == 0) title = title.substr(9);
    int mi = 0;
    for (const auto& m : msgs) {
      Doc d;
      d.key = "m" + hash_prefix(blob + "#imp" + std::to_string(ci) + "/" + std::to_string(mi));
      if (keys_.count(d.key) || utf8::is_blank(m.text)) {
        ++mi;
        continue;
      }
      keys_.insert(d.key);
      d.kind = "chat";
      d.unit = unit;
      d.title = title;
      d.role = m.role;
      d.label = "#" + std::to_string(mi + 1) + " " + m.role;
      d.uri = uri;
      d.text = m.text;
      d.ordinal = mi++;
      d.extra = Json{{"source", "import"}};
      res_.bindings[d.key] = Json{{"msg", m.id}, {"conv", conv.id}};
      res_.corpus.docs.push_back(std::move(d));
    }
    ++ci;
  }
  add_source_summary(uri, "import", r->format, blob, res_.corpus.docs.size() - docs_before, r->conversations.size());
  return {};
}

Status Ingestor::ingest_zip(const PlannedFile& f, const std::string& blob) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, f.path.c_str(), 0)) {
    res_.stats["warnings"].push_back("not a readable zip: " + f.uri);
    return {};
  }
  struct Member {
    std::string name;
    mz_uint index;
  };
  std::vector<Member> members;
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
    char name[1024];
    mz_zip_reader_get_filename(&zip, i, name, sizeof name);
    members.push_back({name, i});
  }
  std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.name < b.name; });
  fsutil::TempDir tmp("loom_archive_zip_");
  bool any_export = false;
  std::vector<Member> others;
  Status st;
  for (const auto& m : members) {
    fs::path mp(m.name);
    std::string base = mp.filename().string();
    std::string ext = lower_ext(mp);
    if (base.empty() || base[0] == '.' || m.name.find("__MACOSX") != std::string::npos) continue;
    fs::path out = tmp.path() / ("m" + std::to_string(m.index) + ext);
    std::string uri = f.uri + "!" + m.name;
    if (ext == ".json") {
      if (!mz_zip_reader_extract_to_file(&zip, m.index, out.c_str(), 0)) continue;
      LOOM_TRY_ASSIGN(BlobRef ref, rt_.blobs().put_file(out, "application/json"));
      bool handled = false;
      st = ingest_json_file(out, uri, ref.hash, "zip_member", &handled);
      if (!st) break;
      if (handled) {
        any_export = true;
      } else {
        others.push_back(m);
      }
    } else if (is_doc_ext(ext)) {
      std::size_t sz = 0;
      void* p = mz_zip_reader_extract_to_heap(&zip, m.index, &sz, 0);
      if (!p) continue;
      std::string content(static_cast<const char*>(p), sz);
      mz_free(p);
      LOOM_TRY_ASSIGN(BlobRef ref, rt_.blobs().put(content, "text/plain"));
      PlannedFile pf{"", uri, "text", ref.hash, static_cast<std::int64_t>(sz)};
      st = ingest_text(pf, content, ref.hash, "doc");
      if (!st) break;
    } else {
      others.push_back(m);
    }
  }
  mz_zip_reader_end(&zip);
  LOOM_TRY(st);
  if (!any_export && !others.empty()) return ingest_import(f.path, f.uri);  // generic zip -> Importer
  (void)blob;
  return {};
}

Status Ingestor::ingest_git() {
  // chronological (oldest first) so ordinals follow history
  LOOM_TRY_ASSIGN(std::string raw, run_git(plan_.repo_path, {"-c", "core.quotepath=off", "log", "--no-color",
                                                            "--format=" + std::string(kGitLogFormat),
                                                            "--name-status"}));
  auto commits = parse_git_log(raw);
  std::reverse(commits.begin(), commits.end());
  LOOM_TRY_ASSIGN(BlobRef ref, rt_.blobs().put(raw, "text/plain"));
  std::string uri = plan_.repo_name + ".git";
  PrevBindings prev;
  LOOM_TRY_ASSIGN(std::string sid, source_for(ref.hash, "loom.archive.git", uri, "git", "git-log",
                                              static_cast<std::int64_t>(raw.size()), prev));
  Unit u;
  u.key = "u" + hash_prefix("git#" + plan_.repo_name);
  u.title = "[Git] " + plan_.repo_name;
  u.conv_source = "archive.git";
  int ord = 0;
  for (const auto& c : commits) {
    Doc d;
    d.key = "g" + hash_prefix("git#" + c.hash);
    d.kind = "commit";
    d.unit = u.key;
    d.title = "git " + plan_.repo_name;
    d.label = c.hash.substr(0, 7) + " " + clip(c.subject, 72);
    d.uri = uri;
    d.date = c.date;
    d.role = "commit";
    d.text = c.subject;
    if (!c.body.empty()) d.text += "\n\n" + c.body;
    if (!c.files.empty()) {
      d.text += "\n\nFiles:";
      for (std::size_t i = 0; i < c.files.size() && i < 40; ++i) d.text += "\n" + c.files[i];
      if (c.files.size() > 40) d.text += "\n(+" + std::to_string(c.files.size() - 40) + " more)";
    }
    Json files = Json::array();
    for (std::size_t i = 0; i < c.files.size() && i < 200; ++i) files.push_back(c.files[i]);
    d.extra = Json{{"hash", c.hash}, {"subject", c.subject}, {"author", c.author}, {"files", files}};
    d.ordinal = ord++;
    u.docs.push_back(std::move(d));
  }
  std::size_t n = u.docs.size();
  LOOM_TRY(store_unit(u, sid, "archive.git@" + std::string(kPipelineVersion), prev));
  add_source_summary(uri, "git", "git-log", ref.hash, n, 1);
  return {};
}

Status Ingestor::ingest_db() {
  Database& db = rt_.db();
  LOOM_TRY_ASSIGN(auto convs, db.list_convs(1'000'000));
  std::set<std::string> bound_convs;
  for (auto it = res_.bindings.begin(); it != res_.bindings.end(); ++it) bound_convs.insert(json::get_string(it.value(), "conv"));
  std::sort(convs.begin(), convs.end(), [](const Conversation& a, const Conversation& b) {
    return std::tie(a.created, a.id) < std::tie(b.created, b.id);
  });
  std::size_t docs_before = res_.corpus.docs.size();
  for (const auto& c : convs) {
    if (c.source.rfind("archive.", 0) == 0 || bound_convs.count(c.id)) continue;
    LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(c.id, true));
    std::string unit = "db_" + c.id;
    std::map<std::string, std::vector<const Message*>> groups;
    int ord = 0;
    for (const auto& m : msgs) {
      if (m.status == "deleted" || utf8::is_blank(m.text)) continue;
      Doc d;
      d.key = "db_" + m.id;
      d.kind = "chat";
      d.unit = unit;
      d.title = c.title;
      d.role = m.role;
      d.label = "#" + std::to_string(ord + 1) + " " + m.role + (m.status == "version" ? " (earlier version)" : "");
      d.uri = "chatadhd.db";
      d.date = normalize_date(m.created);
      d.text = m.text;
      d.ordinal = ord++;
      d.extra = Json{{"status", m.status}, {"version_num", m.version_num}, {"source", "db"}};
      if (m.version_group_id) groups[*m.version_group_id].push_back(&m);
      res_.bindings[d.key] = Json{{"msg", m.id}, {"conv", c.id}};
      res_.corpus.docs.push_back(std::move(d));
    }
    for (const auto& [g, list] : groups) {
      if (list.size() < 2) continue;
      Json alts = Json::array();
      for (const Message* m : list) {
        alts.push_back(Json{{"first", "db_" + m->id}, {"messages", 1}, {"current", m->status == "active"}});
      }
      const Message* first = list.front();
      std::string after = first->parent_id ? "db_" + *first->parent_id : "";
      res_.corpus.forks.push_back(Json{{"unit", unit},
                                       {"title", c.title},
                                       {"after", after},
                                       {"date", normalize_date(first->created)},
                                       {"origin", "versions"},
                                       {"alternatives", alts}});
    }
  }
  add_source_summary("chatadhd.db", "db", "conversations", plan_.db_fingerprint,
                     res_.corpus.docs.size() - docs_before, 0);
  return {};
}

Status Ingestor::process_file(const PlannedFile& f) {
  if (f.adapter == "import") return ingest_import(f.path, f.uri);
  LOOM_TRY_ASSIGN(BlobRef ref, rt_.blobs().put_file(f.path));
  if (ref.hash != f.hash) {
    res_.stats["warnings"].push_back("file changed while planning: " + f.uri);
  }
  if (f.adapter == "zip") return ingest_zip(f, ref.hash);
  if (f.adapter == "json") {
    bool handled = false;
    LOOM_TRY(ingest_json_file(f.path, f.uri, ref.hash, "file", &handled));
    if (!handled) return ingest_import(f.path, f.uri);
    return {};
  }
  LOOM_TRY_ASSIGN(std::string content, rt_.blobs().read(ref.hash));
  if (!utf8::is_valid(content)) content = utf8::repair(content);
  if (f.adapter == "code") return ingest_code(f, content, ref.hash);
  return ingest_text(f, content, ref.hash, "doc");
}

Status Ingestor::run() {
  res_.stats = Json{{"warnings", Json::array()}};
  for (const auto& w : plan_.warnings) res_.stats["warnings"].push_back(w);
  res_.corpus.project = plan_.project;
  std::string git_raw_err;
  if (plan_.git) {
    auto raw = run_git(plan_.repo_path, {"-c", "core.quotepath=off", "log", "--no-color",
                                         "--format=" + std::string(kGitLogFormat), "--name-status"});
    if (raw) {
      auto commits = parse_git_log(*raw);
      for (auto it = commits.rbegin(); it != commits.rend(); ++it) {
        for (const auto& f : it->files) {
          if (f.size() > 2) path_date_[f.substr(2)] = date_only(it->date);
        }
      }
    }
  }
  std::size_t start = 0;
  if (ctl_.resume_from) {
    const Json& cp = *ctl_.resume_from;
    std::string partial = json::get_string(cp, "partial");
    if (!partial.empty()) {
      LOOM_TRY_ASSIGN(std::string bytes, rt_.blobs().read(partial));
      LOOM_TRY_ASSIGN(Json pj, json::parse(bytes));
      res_.corpus = Corpus::from_json(pj["corpus"]);
      res_.bindings = pj["bindings"];
      Json warnings = res_.stats["warnings"];
      res_.stats = pj["stats"];
      if (!res_.stats.contains("warnings")) res_.stats["warnings"] = warnings;
      for (const auto& d : res_.corpus.docs) keys_.insert(d.key);
      start = static_cast<std::size_t>(json::get_int(cp, "next"));
    }
  }
  auto save = [&](std::size_t next) -> Status {
    if (!ctl_.checkpoint) return {};
    Json pj{{"corpus", res_.corpus.to_json()}, {"bindings", res_.bindings}, {"stats", res_.stats}};
    LOOM_TRY_ASSIGN(BlobRef ref, rt_.blobs().put(json::dump(pj), "application/json"));
    return ctl_.checkpoint(Json{{"next", next}, {"partial", ref.hash}});
  };
  auto last_save = std::chrono::steady_clock::now();
  const std::size_t total = plan_.files.size() + (plan_.git ? 1 : 0) + (plan_.include_db ? 1 : 0);
  for (std::size_t i = start; i < plan_.files.size(); ++i) {
    if (ctl_.should_stop && ctl_.should_stop()) {
      LOOM_TRY(save(i));
      return Error(Errc::Paused, "ingest paused at file " + std::to_string(i));
    }
    const auto& f = plan_.files[i];
    if (ctl_.progress) ctl_.progress(static_cast<std::int64_t>(i), static_cast<std::int64_t>(total), f.uri);
    LOOM_TRY(process_file(f));
    if (std::chrono::steady_clock::now() - last_save > std::chrono::seconds(10)) {
      LOOM_TRY(save(i + 1));
      last_save = std::chrono::steady_clock::now();
    }
  }
  if (ctl_.should_stop && ctl_.should_stop()) {
    LOOM_TRY(save(plan_.files.size()));
    return Error(Errc::Paused, "ingest paused before git history");
  }
  if (plan_.git) {
    if (ctl_.progress) ctl_.progress(static_cast<std::int64_t>(plan_.files.size()), static_cast<std::int64_t>(total), "git history");
    if (auto st = ingest_git(); !st) res_.stats["warnings"].push_back("git history: " + st.error().message);
  }
  if (plan_.include_db) LOOM_TRY(ingest_db());

  // Corpus-derived default seeds: the project name + salient README terms.
  std::vector<std::string> seeds;
  for (const auto& t : content_tokens(plan_.project)) seeds.push_back(t);
  std::map<std::string, int> df;
  for (const auto& d : res_.corpus.docs) {
    std::string base = fs::path(d.uri).filename().string();
    std::string low = utf8::to_lower(base);
    if (d.kind != "doc" || low.rfind("readme", 0) != 0) continue;
    std::set<std::string> ts;
    for (auto& t : content_tokens(d.text)) ts.insert(std::move(t));
    for (const auto& t : ts) ++df[t];
  }
  std::vector<std::pair<int, std::string>> top;
  for (const auto& [t, n] : df) top.emplace_back(-n, t);
  std::sort(top.begin(), top.end());
  for (std::size_t i = 0; i < top.size() && seeds.size() < 6; ++i) {
    if (std::find(seeds.begin(), seeds.end(), top[i].second) == seeds.end()) seeds.push_back(top[i].second);
  }
  res_.corpus.default_seeds = seeds;
  res_.corpus.reindex();
  res_.stats["docs"] = res_.corpus.docs.size();
  res_.stats["sources"] = res_.corpus.sources.size();
  res_.stats["forks"] = res_.corpus.forks.size();
  res_.stats["conversations_created"] = created_convs_;
  res_.stats["units_reused"] = reused_units_;
  std::map<std::string, int> kinds;
  for (const auto& d : res_.corpus.docs) ++kinds[d.kind];
  Json kj = Json::object();
  for (const auto& [k, v] : kinds) kj[k] = v;
  res_.stats["kinds"] = kj;
  if (created_convs_ > 0) {
    rt_.bus().emit(events::kImportDone,
                   Json{{"count", created_convs_}, {"source", "archive"}, {"format", "archive"}});
  }
  return {};
}

}  // namespace

Result<IngestResult> run_ingest(Runtime& rt, const Plan& plan, StageControl& ctl) {
  IngestResult res;
  Ingestor ing(rt, plan, ctl, res);
  LOOM_TRY(ing.run());
  return res;
}

}  // namespace loom::archive
