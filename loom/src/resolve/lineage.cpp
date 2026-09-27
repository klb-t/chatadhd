// Code lineage (resolve.h): for a recovered snapshot and a history of
// revisions, the nearest revision per file by content (fewest changed lines;
// ties -> earlier date, then id), then a vote over files weighted by size in
// which unchanged files count most. Deterministic.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>

#include "archive/archive_internal.h"
#include "loom/resolve.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::resolve {

namespace fs = std::filesystem;

Json Revision::to_json() const {
  Json f = Json::object();
  for (const auto& [p, c] : files) {
    f[p] = c.rfind("sha256:", 0) == 0 ? c : "sha256:" + Sha256::hex(c);
  }
  return Json{{"id", id}, {"label", label}, {"date", date}, {"files", f}};
}

Result<Revision> Revision::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "revision: an object is required");
  Revision r;
  r.id = json::get_string(j, "id");
  if (r.id.empty()) return Error(Errc::InvalidArgument, "revision: id is required");
  r.label = json::get_string(j, "label", r.id);
  r.date = json::get_string(j, "date");
  if (const Json* f = json::find(j, "files")) {
    if (!f->is_object()) return Error(Errc::InvalidArgument, "revision: files must be an object");
    for (auto it = f->begin(); it != f->end(); ++it) {
      if (!it.value().is_string()) return Error(Errc::InvalidArgument, "revision: file contents are strings");
      r.files[it.key()] = it.value().get<std::string>();
    }
  }
  return r;
}

Json LineageResult::to_json() const {
  return Json{{"snapshot", snapshot}, {"base", base}, {"confidence", confidence}, {"votes", votes}, {"per_file", per_file}};
}

namespace {

std::vector<std::string_view> split_lines(std::string_view s) {
  std::vector<std::string_view> out;
  std::size_t pos = 0;
  while (pos < s.size()) {
    std::size_t e = s.find('\n', pos);
    if (e == std::string_view::npos) e = s.size();
    std::string_view l = s.substr(pos, e - pos);
    if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
    out.push_back(l);
    pos = e + 1;
  }
  return out;
}

std::size_t line_count(std::string_view s) { return split_lines(s).size(); }

// Myers O((N+M)D): length of the shortest edit script over line ids.
std::size_t myers(const std::vector<int>& a, const std::vector<int>& b) {
  const long n = static_cast<long>(a.size()), m = static_cast<long>(b.size());
  const long max = n + m;
  if (max == 0) return 0;
  std::vector<long> v(static_cast<std::size_t>(2 * max + 2), 0);
  const long off = max;
  for (long d = 0; d <= max; ++d) {
    for (long k = -d; k <= d; k += 2) {
      long x;
      if (k == -d || (k != d && v[static_cast<std::size_t>(k - 1 + off)] < v[static_cast<std::size_t>(k + 1 + off)])) {
        x = v[static_cast<std::size_t>(k + 1 + off)];
      } else {
        x = v[static_cast<std::size_t>(k - 1 + off)] + 1;
      }
      long y = x - k;
      while (x < n && y < m && a[static_cast<std::size_t>(x)] == b[static_cast<std::size_t>(y)]) {
        ++x;
        ++y;
      }
      v[static_cast<std::size_t>(k + off)] = x;
      if (x >= n && y >= m) return static_cast<std::size_t>(d);
    }
  }
  return static_cast<std::size_t>(max);
}


}  // namespace

std::size_t changed_lines(std::string_view a, std::string_view b) {
  if (a == b) return 0;
  auto la = split_lines(a), lb = split_lines(b);
  // common prefix / suffix
  std::size_t p = 0;
  while (p < la.size() && p < lb.size() && la[p] == lb[p]) ++p;
  std::size_t s = 0;
  while (s < la.size() - p && s < lb.size() - p && la[la.size() - 1 - s] == lb[lb.size() - 1 - s]) ++s;
  std::unordered_map<std::string_view, int> ids;
  std::vector<int> ia, ib;
  for (std::size_t i = p; i < la.size() - s; ++i) ia.push_back(ids.emplace(la[i], static_cast<int>(ids.size())).first->second);
  for (std::size_t i = p; i < lb.size() - s; ++i) ib.push_back(ids.emplace(lb[i], static_cast<int>(ids.size())).first->second);
  return myers(ia, ib);
}

Result<LineageResult> code_lineage(const Revision& snapshot, const std::vector<Revision>& history) {
  if (history.empty()) return Error(Errc::InvalidArgument, "code_lineage: an empty history");
  if (snapshot.files.empty()) return Error(Errc::InvalidArgument, "code_lineage: an empty snapshot");
  for (const auto& [p, c] : snapshot.files) {
    if (c.rfind("sha256:", 0) == 0) return Error(Errc::InvalidArgument, "code_lineage: the snapshot needs file contents (" + p + ")");
  }
  // revisions in (date, id) order: the tie-break order
  std::vector<const Revision*> revs;
  for (const auto& r : history) revs.push_back(&r);
  auto norm_date = [](const Revision* r) { return archive::normalize_date(r->date); };
  std::sort(revs.begin(), revs.end(), [&](const Revision* a, const Revision* b) {
    std::string da = norm_date(a), db = norm_date(b);
    if (da != db) return da < db;
    return a->id < b->id;
  });
  LineageResult out;
  out.snapshot = snapshot.id;
  std::map<std::string, double> score;       // revision -> vote
  std::map<std::string, double> sim_sum;     // revision -> weighted similarity
  std::map<std::string, int> nearest_files;  // revision -> files where it is nearest (argmin set)
  std::map<std::string, double> nearest_w;
  double total_w = 0;
  std::map<std::pair<std::string, std::string>, std::size_t> cache;  // (path|hash, hash) -> changed
  for (const auto& [path, content] : snapshot.files) {
    std::size_t ns = line_count(content);
    double w = static_cast<double>(std::max<std::size_t>(ns, 1));
    total_w += w;
    std::string hs = Sha256::hex(content);
    std::size_t best = SIZE_MAX;
    const Revision* best_rev = nullptr;
    std::vector<std::pair<const Revision*, std::size_t>> changed;
    for (const Revision* r : revs) {
      auto it = r->files.find(path);
      std::size_t d;
      std::size_t nr = 0;
      double sim = 0.0;
      if (it == r->files.end() || it->second.rfind("sha256:", 0) == 0) {
        if (it != r->files.end() && it->second == "sha256:" + hs) {
          d = 0;
          sim = 1.0;
        } else {
          d = ns + (it == r->files.end() ? 0 : ns);  // absent: every line is added
        }
      } else {
        std::string hr = Sha256::hex(it->second);
        auto key = std::make_pair(path + "|" + hs, hr);
        auto c = cache.find(key);
        d = c != cache.end() ? c->second : cache.emplace(key, changed_lines(content, it->second)).first->second;
        nr = line_count(it->second);
        sim = ns + nr > 0 ? 1.0 - static_cast<double>(d) / static_cast<double>(ns + nr) : 1.0;
      }
      changed.emplace_back(r, d);
      score[r->id] += w * sim + (d == 0 ? w : 0.0);  // unchanged files count most
      sim_sum[r->id] += w * sim;
      if (d < best) {
        best = d;
        best_rev = r;
      }
    }
    for (const auto& [r, d] : changed) {
      if (d == best) {
        ++nearest_files[r->id];
        nearest_w[r->id] += w;
      }
    }
    std::size_t nb = best_rev && best_rev->files.count(path) ? line_count(best_rev->files.at(path)) : 0;
    double bsim = ns + nb > 0 ? 1.0 - static_cast<double>(best) / static_cast<double>(ns + nb) : 0.0;
    out.per_file[path] = Json{{"revision", best_rev ? best_rev->id : ""},
                              {"changed_lines", best},
                              {"similarity", std::round(std::max(0.0, bsim) * 1e4) / 1e4}};
  }
  const Revision* win = nullptr;
  for (const Revision* r : revs) {
    if (!win || score[r->id] > score[win->id] + 1e-9) win = r;  // revs are in (date, id) order: ties keep the earlier
  }
  out.base = win->id;
  out.confidence = total_w > 0 ? std::round(nearest_w[win->id] / total_w * 1e4) / 1e4 : 0.0;
  std::vector<const Revision*> ranked = revs;
  std::stable_sort(ranked.begin(), ranked.end(), [&](const Revision* a, const Revision* b) { return score[a->id] > score[b->id] + 1e-9; });
  for (const Revision* r : ranked) {
    out.votes.push_back(Json{{"revision", r->id},
                             {"label", r->label},
                             {"date", r->date},
                             {"files", nearest_files[r->id]},
                             {"weight", std::round(score[r->id] * 1e3) / 1e3},
                             {"similarity", total_w > 0 ? std::round(sim_sum[r->id] / total_w * 1e4) / 1e4 : 0.0}});
  }
  return out;
}

std::string revision_entity(std::string_view label) {
  std::string v = kb::normalize_version(label);
  return model::Entity::make_id("version", v.empty() ? std::string(label) : v);
}

Result<std::vector<model::Claim>> lineage_claims(const LineageResult& lineage, std::string_view project_entity) {
  if (lineage.base.empty()) return Error(Errc::InvalidArgument, "lineage_claims: no base revision");
  std::string label = json::get_string(lineage.votes.empty() ? Json::object() : lineage.votes.front(), "label", lineage.base);
  std::string snap = lineage.snapshot.rfind("snapshot:", 0) == 0 ? lineage.snapshot.substr(9) : lineage.snapshot;
  std::vector<model::Claim> out;
  for (const char* pred : {"forked_from", "based_on"}) {
    model::Claim c;
    c.subject = revision_entity(snap);
    c.predicate = pred;
    c.value = Json(lineage.base);
    c.qualifiers.scope = std::string(project_entity);
    c.qualifiers.extra = Json{{"base_label", label}, {"snapshot", lineage.snapshot}};
    c.assessment.evidence = model::EvidenceClass::Derived;
    c.assessment.origin = model::Origin::Repo;
    c.assessment.derivation = model::Derivation{"resolve.code_lineage", 1, "", 0};
    c.assessment.confidence = std::clamp(lineage.confidence, 0.0, 1.0);
    // the per-file votes are the premises of the derivation
    std::vector<std::string> files;
    for (auto it = lineage.per_file.begin(); it != lineage.per_file.end(); ++it) {
      if (json::get_string(it.value(), "revision") == lineage.base) files.push_back(it.key());
    }
    c.assessment.premises.assumptions.push_back("nearest revision by content diff per file, then a size-weighted vote; " +
                                                std::to_string(files.size()) + " of " +
                                                std::to_string(lineage.per_file.size()) + " files nearest to the base");
    c.id = model::Claim::make_id(c.subject, c.predicate, c.object, c.value, c.qualifiers);
    LOOM_TRY(c.validate());
    out.push_back(std::move(c));
  }
  return out;
}

model::Fork lineage_fork(const LineageResult& lineage, const std::vector<Revision>& history, std::string_view project_entity) {
  model::Fork f;
  f.kind = model::ForkKind::CodeLineage;
  f.subject = std::string(project_entity);
  f.base = lineage.base;
  std::string base_date;
  for (const auto& r : history) {
    if (r.id == lineage.base) base_date = archive::normalize_date(r.date);
  }
  model::ForkSide snap;
  snap.ref = lineage.snapshot;
  snap.label = lineage.snapshot.rfind("snapshot:", 0) == 0 ? lineage.snapshot.substr(9) : lineage.snapshot;
  f.sides.push_back(snap);
  // the main line: the first revision after the base
  const Revision* next = nullptr;
  for (const auto& r : history) {
    std::string d = archive::normalize_date(r.date);
    if (r.id == lineage.base || d < base_date || (d == base_date && r.id <= lineage.base)) continue;
    if (!next || d < archive::normalize_date(next->date) || (d == archive::normalize_date(next->date) && r.id < next->id)) next = &r;
  }
  if (next) {
    model::ForkSide main;
    main.ref = next->id;
    main.label = next->label;
    main.date = next->date;
    f.sides.push_back(main);
  }
  f.date = base_date;
  if (f.subject.empty()) f.subject = revision_entity(snap.label);
  f.id = model::Fork::make_id(f.kind, f.subject, f.base, f.sides);
  return f;
}

// ── inputs: git history and snapshots ───────────────────────────────
namespace {

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  return out + "'";
}

Result<std::string> run_git(const fs::path& repo, const std::vector<std::string>& args) {
#if defined(_WIN32) || defined(__ANDROID__)
  (void)repo;
  (void)args;
  return Error(Errc::Unsupported, "git history is desktop-only");
#else
  std::string cmd = "git -C " + shell_quote(repo.string());
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

bool text_file(const std::string& bytes) {
  return bytes.find('\0') == std::string::npos && utf8::is_valid(bytes);
}

}  // namespace

Result<std::vector<Revision>> git_revisions(const fs::path& repo, const std::vector<std::string>& paths) {
  std::vector<std::string> args{"log", "--full-history", "--no-merges", "--format=%H%x1f%h%x1f%aI%x1f%s", "--"};
  for (const auto& p : paths) args.push_back(p);
  LOOM_TRY_ASSIGN(std::string log, run_git(repo, args));
  std::vector<Revision> out;
  std::map<std::string, std::string> blobs;
  std::size_t pos = 0;
  while (pos < log.size()) {
    std::size_t e = log.find('\n', pos);
    if (e == std::string::npos) e = log.size();
    std::string line = log.substr(pos, e - pos);
    pos = e + 1;
    std::vector<std::string> f;
    std::size_t s = 0;
    while (s <= line.size()) {
      std::size_t t = line.find('\x1f', s);
      if (t == std::string::npos) t = line.size();
      f.push_back(line.substr(s, t - s));
      s = t + 1;
    }
    if (f.size() < 4 || f[0].size() < 7) continue;
    Revision r;
    r.id = f[0];
    r.date = archive::normalize_date(f[2]);
    std::string subject = f[3];
    std::string label = f[1];
    for (const char* lead : {"Version ", "version ", "Release ", "Wersja "}) {
      if (subject.rfind(lead, 0) == 0) {
        std::string rest = subject.substr(std::char_traits<char>::length(lead));
        std::string v = kb::normalize_version(rest.substr(0, rest.find_first_of(": ")));
        if (!v.empty()) label = v;
      }
    }
    r.label = label;
    std::vector<std::string> ls{"ls-tree", "-r", r.id, "--"};
    for (const auto& p : paths) ls.push_back(p);
    LOOM_TRY_ASSIGN(std::string tree, run_git(repo, ls));
    std::size_t tp = 0;
    while (tp < tree.size()) {
      std::size_t te = tree.find('\n', tp);
      if (te == std::string::npos) te = tree.size();
      std::string tl = tree.substr(tp, te - tp);
      tp = te + 1;
      std::size_t tab = tl.find('\t');
      if (tab == std::string::npos) continue;
      std::string meta = tl.substr(0, tab), path = tl.substr(tab + 1);
      std::size_t sp1 = meta.find(' '), sp2 = meta.find(' ', sp1 + 1);
      if (sp2 == std::string::npos || meta.substr(sp1 + 1, sp2 - sp1 - 1) != "blob") continue;
      std::string sha = meta.substr(sp2 + 1);
      auto it = blobs.find(sha);
      if (it == blobs.end()) {
        LOOM_TRY_ASSIGN(std::string content, run_git(repo, {"cat-file", "-p", sha}));
        it = blobs.emplace(sha, std::move(content)).first;
      }
      r.files[path] = it->second;
    }
    out.push_back(std::move(r));
  }
  std::sort(out.begin(), out.end(), [](const Revision& a, const Revision& b) {
    if (a.date != b.date) return a.date < b.date;
    return a.id < b.id;
  });
  return out;
}

Result<Revision> snapshot_revision(const fs::path& dir, std::string_view label) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return Error(Errc::NotFound, "not a directory: " + dir.string());
  Revision r;
  r.label = label.empty() ? dir.filename().string() : std::string(label);
  r.id = "snapshot:" + r.label;
  std::string newest;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end;
       it.increment(ec)) {
    if (ec) break;
    std::string name = it->path().filename().string();
    if (it->is_directory(ec)) {
      if (name == ".git" || name == "__pycache__" || name == "node_modules") it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    auto bytes = fsutil::read_file(it->path());
    if (!bytes || !text_file(*bytes)) continue;
    r.files[it->path().lexically_relative(dir).generic_string()] = std::move(*bytes);
  }
  if (r.files.empty()) return Error(Errc::NotFound, "no text files in " + dir.string());
  return r;
}

}  // namespace loom::resolve
