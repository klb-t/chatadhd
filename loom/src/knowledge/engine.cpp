// knowledge.h: KnowledgeEngine — the knowledge pipeline as resumable
// TaskEngine stages (same contract as the archive pipeline: input hashes,
// cache hits, checkpoints, pause/resume, crash recovery).
#include "loom/knowledge.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>

#include "catalog/catalog_internal.h"
#include "loom/catalog.h"
#include "loom/extract.h"
#include "loom/generalize.h"
#include "loom/log.h"
#include "loom/materialize.h"
#include "loom/provenance.h"
#include "loom/resolve.h"
#include "loom/runtime.h"
#include "loom/tasks.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

namespace loom::knowledge {

namespace {
constexpr std::string_view kLog = "loom.knowledge";

Json str_array(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (const auto& s : v) a.push_back(s);
  return a;
}

// Paths are configuration; bytes are inputs. Hash the source tree as the
// catalogue sees it so edits/additions/deletions invalidate derived state.
// Hashing is streaming even for multi-GB exports. Generated output folders
// carry the same marker understood by the catalogue walker.
Result<Json> source_snapshot(const KnowledgeConfig& cfg, const std::filesystem::path& active_data) {
  namespace fs = std::filesystem;
  std::vector<std::string> roots = cfg.sources;
  if (cfg.repo) roots.push_back(*cfg.repo);
  const Json& cp = cfg.stage_params;
  if (cp.contains("catalog")) {
    const auto& c = cp["catalog"];
    if (c.contains("scan") && c["scan"].contains("sources") && c["scan"]["sources"].is_array()) {
      for (const auto& p : c["scan"]["sources"]) if (p.is_string()) roots.push_back(p.get<std::string>());
    }
    if (c.contains("profile") && c["profile"].contains("repo") && c["profile"]["repo"].is_string()) {
      roots.push_back(c["profile"]["repo"].get<std::string>());
    }
    if (c.contains("profile") && c["profile"].contains("documents") && c["profile"]["documents"].is_array()) {
      for (const auto& p : c["profile"]["documents"]) if (p.is_string()) roots.push_back(p.get<std::string>());
    }
  }
  if (cp.contains("resolve") && cp["resolve"].contains("snapshots") && cp["resolve"]["snapshots"].is_array()) {
    for (const auto& p : cp["resolve"]["snapshots"]) {
      if (p.is_string()) roots.push_back(p.get<std::string>());
      else if (p.is_object() && p.contains("dir") && p["dir"].is_string()) roots.push_back(p["dir"].get<std::string>());
    }
  }
  std::map<std::string, std::string> files;
  auto hidden = [](const fs::path& p) {
    const auto name = p.filename().string();
    return name.empty() || name[0] == '.' || name == "__MACOSX";
  };
  for (const auto& root : roots) {
    const auto path = fsutil::resolve_path(root);
    if (path == active_data) return Error(Errc::InvalidArgument, "source cannot be the active data directory");
    std::error_code ec;
    if (!fs::exists(path, ec)) {
      files[path.string()] = "missing";
      continue;
    }
    std::vector<fs::path> paths;
    if (fs::is_directory(path, ec)) {
      if (fs::exists(path / ".loom-archive", ec)) {
        files[path.string()] = "generated";
        continue;
      }
      for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
           !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code entry_error;
        if (it->is_directory(entry_error)) {
          if (catalog::internal::skip_source_directory(it->path(), active_data)) it.disable_recursion_pending();
        } else if (!entry_error && !hidden(it->path()) && it->is_regular_file(entry_error)) {
          paths.push_back(it->path());
        }
      }
      if (ec) return Error(Errc::Io, "cannot fingerprint source " + path.string() + ": " + ec.message());
      // Keep empty directory identity, and notice when its first file appears.
      files[path.string()] = "directory";
    } else {
      paths.push_back(path);
    }
    for (const auto& file : paths) {
      LOOM_TRY_ASSIGN(auto hash, sha256_file_hex(file));
      files[file.string()] = std::move(hash);
    }
    // Repository analysis also consumes commit/lineage metadata. A new
    // commit can leave the working tree bytes unchanged, so record HEAD's
    // revision without walking other refs, object databases or branches.
    auto git_dir = path / ".git";
    if (fs::is_regular_file(git_dir, ec)) {
      LOOM_TRY_ASSIGN(auto link, fsutil::read_file(git_dir));
      if (link.rfind("gitdir: ", 0) == 0) {
        auto target = link.substr(8);
        while (!target.empty() && (target.back() == '\n' || target.back() == '\r')) target.pop_back();
        git_dir = (path / target).lexically_normal();
      }
    }
    if (fs::is_regular_file(git_dir / "HEAD", ec)) {
      LOOM_TRY_ASSIGN(auto head, fsutil::read_file(git_dir / "HEAD"));
      auto common = git_dir;
      if (fs::is_regular_file(git_dir / "commondir", ec)) {
        LOOM_TRY_ASSIGN(auto dir, fsutil::read_file(git_dir / "commondir"));
        while (!dir.empty() && (dir.back() == '\n' || dir.back() == '\r')) dir.pop_back();
        common = (git_dir / dir).lexically_normal();
      }
      std::string revision = head;
      if (head.rfind("ref: ", 0) == 0) {
        auto ref = head.substr(5);
        while (!ref.empty() && (ref.back() == '\n' || ref.back() == '\r')) ref.pop_back();
        if (fs::is_regular_file(common / ref, ec)) {
          LOOM_TRY_ASSIGN(revision, fsutil::read_file(common / ref));
        } else if (fs::is_regular_file(common / "packed-refs", ec)) {
          LOOM_TRY_ASSIGN(auto packed, fsutil::read_file(common / "packed-refs"));
          std::istringstream lines(packed);
          std::string line;
          while (std::getline(lines, line)) {
            const auto space = line.find(' ');
            if (space != std::string::npos && line.substr(space + 1) == ref) {
              revision = line.substr(0, space);
              break;
            }
          }
        }
      }
      files[(path / ".git/HEAD").string()] = Sha256::hex(revision);
    }
  }
  Json out = Json::object();
  for (const auto& [path, hash] : files) out[path] = hash;
  return out;
}

// A cached materialization still needs to fulfil this invocation's export
// request (possibly a new directory, or files deleted since the last run).
Status export_cached_products(Runtime& rt, const KnowledgeConfig& cfg, const Json& result) {
  if (cfg.out_dir.empty()) return {};
  const auto* artifacts = json::find(result, "artifacts");
  if (!artifacts || !artifacts->is_array()) return {};
  for (const auto& artifact : *artifacts) {
    const auto name = json::get_string(artifact, "name");
    const std::filesystem::path relative(name);
    if (name.empty() || relative.has_parent_path() || relative.is_absolute()) {
      return Error(Errc::InvalidArgument, "invalid materialized product name: " + name);
    }
    LOOM_TRY_ASSIGN(auto bytes, rt.blobs().read(json::get_string(artifact, "hash")));
    LOOM_TRY(fsutil::atomic_write(fsutil::expand_user(cfg.out_dir) / relative, bytes));
  }
  return {};
}
}  // namespace

bool is_stage(std::string_view name) noexcept {
  return std::find(kStages.begin(), kStages.end(), name) != kStages.end();
}

std::string_view stage_input(std::string_view stage) noexcept {
  for (std::size_t i = 1; i < kStages.size(); ++i) {
    if (kStages[i] == stage) return kStages[i - 1];
  }
  return {};
}

// ── config ──────────────────────────────────────────────────────────
Result<KnowledgeConfig> KnowledgeConfig::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "knowledge config must be a JSON object");
  KnowledgeConfig c;
  auto strings = [](const Json& v, std::vector<std::string>& out, const std::string& key) -> Status {
    if (!v.is_array()) return Error(Errc::InvalidArgument, key + " must be an array of strings");
    for (const auto& x : v) {
      if (!x.is_string()) return Error(Errc::InvalidArgument, key + " must be an array of strings");
      out.push_back(x.get<std::string>());
    }
    return {};
  };
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    auto want_string = [&](std::string& dst) -> Status {
      if (!v.is_string()) return Error(Errc::InvalidArgument, k + " must be a string");
      dst = v.get<std::string>();
      return {};
    };
    if (k == "sources") {
      LOOM_TRY(strings(v, c.sources, k));
    } else if (k == "repo") {
      if (v.is_null()) continue;
      std::string r;
      LOOM_TRY(want_string(r));
      if (!r.empty()) c.repo = r;
    } else if (k == "stages") {
      LOOM_TRY(strings(v, c.stages, k));
      for (const auto& s : c.stages) {
        if (!is_stage(s)) return Error(Errc::InvalidArgument, "unknown stage '" + s + "'");
      }
    } else if (k == "prior_cut") {
      LOOM_TRY(want_string(c.prior_cut));
    } else if (k == "priors") {
      if (!v.is_boolean()) return Error(Errc::InvalidArgument, "priors must be a boolean");
      c.priors = v.get<bool>();
    } else if (k == "llm") {
      LOOM_TRY(want_string(c.llm));
      if (c.llm != "off" && c.llm != "auto") return Error(Errc::InvalidArgument, "llm must be 'off' or 'auto'");
    } else if (k == "out_dir") {
      LOOM_TRY(want_string(c.out_dir));
    } else if (k == "project") {
      LOOM_TRY(want_string(c.project));
    } else if (k == "force") {
      if (!v.is_boolean()) return Error(Errc::InvalidArgument, "force must be a boolean");
      c.force = v.get<bool>();
    } else if (k == "stage_params") {
      if (!v.is_object()) return Error(Errc::InvalidArgument, "stage_params must be an object");
      for (auto s = v.begin(); s != v.end(); ++s) {
        if (!is_stage(s.key())) return Error(Errc::InvalidArgument, "stage_params: unknown stage '" + s.key() + "'");
        if (!s.value().is_object()) return Error(Errc::InvalidArgument, "stage_params." + s.key() + " must be an object");
      }
      c.stage_params = v;
    } else {
      return Error(Errc::InvalidArgument, "unknown knowledge config key: " + k);
    }
  }
  // Stages always run in pipeline order.
  std::vector<std::string> ordered;
  for (auto s : kStages) {
    if (std::find(c.stages.begin(), c.stages.end(), s) != c.stages.end()) ordered.emplace_back(s);
  }
  c.stages = ordered;
  return c;
}

Json KnowledgeConfig::to_json() const {
  return Json{{"sources", str_array(sources)},
              {"repo", repo ? Json(*repo) : Json(nullptr)},
              {"stages", str_array(stages)},
              {"prior_cut", prior_cut},
              {"priors", priors},
              {"llm", llm},
              {"out_dir", out_dir},
              {"project", project},
              {"force", force},
              {"stage_params", stage_params}};
}

Json KnowledgeConfig::fingerprint() const {
  Json j = to_json();
  j.erase("out_dir");
  j.erase("force");
  j.erase("stages");
  return j;
}

model::PriorFilter KnowledgeConfig::prior_filter() const {
  if (!priors) return model::PriorFilter::none();
  return model::PriorFilter{true, prior_cut};
}

Json StageRun::to_json() const {
  return Json{{"stage", stage},       {"task_id", task_id},     {"input_hash", input_hash}, {"output_hash", output_hash},
              {"cache_hit", cache_hit}, {"resumed", resumed},   {"stats", stats}};
}

Json RunResult::to_json() const {
  Json st = Json::array();
  for (const auto& s : stages) st.push_back(s.to_json());
  return Json{{"task_id", task_id}, {"run", run},       {"pack_hash", pack_hash}, {"status", status},
              {"error", error},     {"stages", st},     {"summary", summary}};
}

// ── engine state ────────────────────────────────────────────────────
struct KnowledgeEngine::State {
  std::mutex mu;       // pack, stages, progress, cancel
  std::mutex run_mu;   // one run at a time
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<kb::Normalizer> normalizer;
  std::unique_ptr<kb::KnowledgeStore> store;
  std::map<std::string, StageFn, std::less<>> stages;
  ProgressFn progress;
  const CancelToken* cancel = nullptr;

  StageFn stage(std::string_view name) {
    std::lock_guard lk(mu);
    auto it = stages.find(name);
    return it == stages.end() ? StageFn{} : it->second;
  }
  void notify(std::string_view stage, std::int64_t cur, std::int64_t total, std::string_view msg) {
    ProgressFn fn;
    {
      std::lock_guard lk(mu);
      fn = progress;
    }
    if (fn) fn(stage, cur, total, msg);
  }
  bool stop_requested() {
    std::lock_guard lk(mu);
    return cancel && cancel->cancelled();
  }
};

KnowledgeEngine::KnowledgeEngine(Runtime& rt) : rt_(rt), st_(std::make_unique<State>()) {
  st_->store = std::make_unique<kb::KnowledgeStore>(rt_.db());
  st_->stages["catalog"] = [](StageContext& c) { return catalog::run_stage(c); };
  st_->stages["extract"] = [](StageContext& c) { return extract::run_stage(c); };
  st_->stages["resolve"] = [](StageContext& c) { return resolve::run_resolve_stage(c); };
  st_->stages["assess"] = [](StageContext& c) { return resolve::run_assess_stage(c); };
  st_->stages["generalize"] = [](StageContext& c) { return generalize::run_stage(c); };
  st_->stages["materialize"] = [](StageContext& c) { return materialize::run_stage(c); };

  for (auto s : kStages) {
    std::string stage(s);
    rt_.tasks().register_handler("knowledge." + stage, [this, stage](TaskContext& ctx) -> Status {
      auto cfg = KnowledgeConfig::from_json(ctx.params()["config"]);
      if (!cfg) return cfg.error();
      LOOM_TRY_ASSIGN(auto pk, pack());
      StageFn fn = st_->stage(stage);
      if (!fn) return Error(Errc::NotImplemented, "no implementation for stage " + stage);
      StageContext sc{rt_,
                      *st_->store,
                      pk,
                      *st_->normalizer,
                      *cfg,
                      json::get_string(ctx.params(), "run"),
                      stage,
                      cfg->stage_params.contains(stage) ? cfg->stage_params[stage] : Json::object(),
                      ctx.params().contains("input") ? ctx.params()["input"] : Json(nullptr),
                      cfg->prior_filter(),
                      {},
                      {},
                      {},
                      ctx.checkpoint()};
      sc.progress = [&](std::int64_t cur, std::int64_t total, std::string_view msg) {
        ctx.progress(cur, total, msg);
        st_->notify(stage, cur, total, msg);
      };
      sc.should_stop = [&] { return ctx.should_stop() || st_->stop_requested(); };
      sc.checkpoint = [&](const Json& j) { return ctx.save_checkpoint(j); };
      if (ctx.cancelled()) return Error(Errc::Cancelled, "cancelled");
      if (sc.should_stop()) return Error(Errc::Paused, "stopped before start");
      auto r = fn(sc);
      if (!r) {
        if (ctx.cancelled()) return Error(Errc::Cancelled, r.error().message);
        return r.error();
      }
      Json res = std::move(*r);
      if (!res.is_object() || !json::find(res, "output")) {
        return Error(Errc::Internal, "stage " + stage + " returned no \"output\" hash");
      }
      ctx.set_result(std::move(res));
      return {};
    });
  }
  // The run: drives the stages above (resumable as a whole).
  rt_.tasks().register_handler("knowledge.run", [this](TaskContext& ctx) -> Status {
    auto cfg = KnowledgeConfig::from_json(ctx.params()["config"]);
    if (!cfg) return cfg.error();
    auto r = orchestrate(*cfg, ctx.id(), json::get_string(ctx.params(), "run"));
    if (!r) return r.error();
    Json res = r->to_json();
    if (r->status == "paused") {
      (void)ctx.save_checkpoint(res);
      return Error(ctx.cancelled() ? Errc::Cancelled : Errc::Paused, "paused");
    }
    if (r->status == "failed" || r->status == "cancelled") {
      LOOM_TRY(ctx.save_checkpoint(res));
      return Error(r->status == "cancelled" ? Errc::Cancelled : Errc::Internal, r->error);
    }
    ctx.set_result(std::move(res));
    return {};
  });
}

KnowledgeEngine::~KnowledgeEngine() = default;

Result<std::shared_ptr<const kb::Pack>> KnowledgeEngine::pack() {
  std::lock_guard lk(st_->mu);
  if (st_->pack) return st_->pack;
  LOOM_TRY_ASSIGN(auto p, kb::Pack::load_with_overlay(rt_.paths().root / "kb"));
  st_->normalizer = std::make_unique<kb::Normalizer>(*p);
  st_->pack = p;
  return p;
}

kb::KnowledgeStore& KnowledgeEngine::store() { return *st_->store; }

void KnowledgeEngine::set_stage(std::string_view stage, StageFn fn) {
  std::lock_guard lk(st_->mu);
  st_->stages[std::string(stage)] = std::move(fn);
}

// ── running ─────────────────────────────────────────────────────────
namespace {
Result<RunResult> parse_run_result(const Json& j) {
  RunResult r;
  r.task_id = json::get_string(j, "task_id");
  r.run = json::get_string(j, "run");
  r.pack_hash = json::get_string(j, "pack_hash");
  r.status = json::get_string(j, "status");
  r.error = json::get_string(j, "error");
  if (const Json* st = json::find(j, "stages"); st && st->is_array()) {
    for (const auto& x : *st) {
      StageRun sr;
      sr.stage = json::get_string(x, "stage");
      sr.task_id = json::get_string(x, "task_id");
      sr.input_hash = json::get_string(x, "input_hash");
      sr.output_hash = json::get_string(x, "output_hash");
      sr.cache_hit = json::get_bool(x, "cache_hit");
      sr.resumed = json::get_bool(x, "resumed");
      sr.stats = x.contains("stats") ? x["stats"] : Json::object();
      r.stages.push_back(std::move(sr));
    }
  }
  if (const Json* sm = json::find(j, "summary")) r.summary = *sm;
  return r;
}
}  // namespace

// Drives the stages of one knowledge.run task (called by its handler).
Result<RunResult> KnowledgeEngine::orchestrate(const KnowledgeConfig& cfg, const std::string& run_task, const std::string& krun) {
  LOOM_TRY_ASSIGN(auto pk, pack());
  RunResult out;
  out.task_id = run_task;
  out.run = krun;
  out.pack_hash = pk->hash();
  std::vector<std::string> todo = cfg.stages;
  if (todo.empty()) {
    for (auto s : kStages) todo.emplace_back(s);
  }
  Json prev_result = nullptr;
  std::string prev_output;
  const Json fp = cfg.fingerprint();
  for (const auto& stage : todo) {
    StageRun sr;
    sr.stage = stage;
    Json params = cfg.stage_params.contains(stage) ? cfg.stage_params[stage] : Json::object();
    Json hp{{"pack", pk->hash()}, {"run", krun}, {"config", fp}, {"params", params}, {"input", prev_output}};
    sr.input_hash = Sha256::hex("knowledge." + stage + "|" + std::string(kPipelineVersion) + "|" + json::canonical(hp));
    SubmitOptions so;
    so.dedupe = !cfg.force;
    so.input_hash = sr.input_hash;
    so.parent_id = run_task;
    so.max_attempts = 2;
    LOOM_TRY_ASSIGN(sr.task_id, rt_.tasks().submit("knowledge." + stage,
                                                   Json{{"config", cfg.to_json()}, {"run", krun}, {"input", prev_result}}, so));
    bool first = true;
    while (true) {
      LOOM_TRY_ASSIGN(auto rec, rt_.tasks().get(sr.task_id));
      if (!rec) return Error(Errc::NotFound, "stage task vanished: " + sr.task_id);
      const std::string& s = rec->status;
      if (s == task_status::kDone) {
        sr.cache_hit = first;
        Json res = rec->result ? *rec->result : Json::object();
        if (sr.cache_hit && stage == "materialize") LOOM_TRY(export_cached_products(rt_, cfg, res));
        sr.output_hash = json::get_string(res, "output");
        sr.stats = res.contains("stats") ? res["stats"] : Json::object();
        if (sr.cache_hit) st_->notify(stage, 1, 1, "cache hit");
        prev_result = res;
        prev_output = sr.output_hash;
        break;
      }
      first = false;
      if (s == task_status::kFailed || s == task_status::kCancelled) {
        out.status = s == task_status::kFailed ? "failed" : "cancelled";
        out.error = stage + ": " + rec->error;
        out.stages.push_back(sr);
        log::info(kLog, "knowledge run stopped at {}: {}", stage, rec->error);
        return out;
      }
      if (s == task_status::kPaused) {
        if (st_->stop_requested()) {
          sr.resumed = rec->checkpoint.has_value();
          out.stages.push_back(sr);
          out.status = "paused";
          return out;
        }
        sr.resumed = true;
        LOOM_TRY(rt_.tasks().resume(sr.task_id));
        continue;
      }
      if (s == task_status::kPending) {
        if (rec->checkpoint) sr.resumed = true;
        auto r = rt_.tasks().run_sync(sr.task_id);
        if (!r && r.error().code != Errc::Busy) return r.error();
        if (!r) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));  // running on a worker
    }
    out.stages.push_back(sr);
  }
  out.status = "done";
  LOOM_TRY_ASSIGN(Json counts, st_->store->stats(krun));
  out.summary = Json{{"run", krun}, {"counts", counts}};
  return out;
}

Result<RunResult> KnowledgeEngine::run(const KnowledgeConfig& requested, const ProgressFn& progress, const CancelToken* cancel) {
  std::unique_lock run_lock(st_->run_mu, std::try_to_lock);
  if (!run_lock.owns_lock()) return Error(Errc::Busy, "a knowledge run is already in progress");
  // Native callers construct KnowledgeConfig directly; validate them too.
  LOOM_TRY_ASSIGN(auto cfg, KnowledgeConfig::from_json(requested.to_json()));
  LOOM_TRY_ASSIGN(auto pk, pack());
  {
    std::lock_guard lk(st_->mu);
    st_->progress = progress;
    st_->cancel = cancel;
  }
  struct Reset {
    State& s;
    ~Reset() {
      std::lock_guard lk(s.mu);
      s.progress = nullptr;
      s.cancel = nullptr;
    }
  } reset{*st_};

  (void)rt_.tasks().recover_interrupted();
  if (!cfg.out_dir.empty()) {
    const auto output = fsutil::resolve_path(cfg.out_dir);
    for (const auto& source : cfg.sources) {
      if (fsutil::resolve_path(source) == output) return Error(Errc::InvalidArgument, "output directory cannot replace a source");
    }
    if (cfg.repo && fsutil::resolve_path(*cfg.repo) == output) {
      return Error(Errc::InvalidArgument, "output directory cannot replace the repository source");
    }
    LOOM_TRY(fsutil::atomic_write(fsutil::expand_user(cfg.out_dir) / ".loom-archive", "knowledge\n"));
  }
  LOOM_TRY_ASSIGN(auto inputs, source_snapshot(cfg, rt_.paths().root));
  Json fingerprint = cfg.fingerprint();
  fingerprint["source_contents"] = std::move(inputs);
  LOOM_TRY_ASSIGN(auto krun, st_->store->begin_run(pk->hash(), fingerprint));
  SubmitOptions ro;
  ro.max_attempts = 1;
  LOOM_TRY_ASSIGN(std::string id, rt_.tasks().submit("knowledge.run", Json{{"config", cfg.to_json()}, {"run", krun.id}}, ro));
  TaskRecord rec;
  while (true) {
    LOOM_TRY_ASSIGN(auto r, rt_.tasks().get(id));
    if (!r) return Error(Errc::NotFound, "knowledge run task vanished");
    if (r->status == task_status::kPending) {
      auto x = rt_.tasks().run_sync(id);
      if (!x && x.error().code != Errc::Busy) return x.error();
      if (!x) std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    if (r->status == task_status::kRunning) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    rec = *r;
    break;
  }
  const Json src = rec.result ? *rec.result : rec.checkpoint ? *rec.checkpoint : Json::object();
  LOOM_TRY_ASSIGN(RunResult out, parse_run_result(src));
  out.task_id = id;
  out.run = krun.id;
  out.pack_hash = pk->hash();
  if (rec.status == task_status::kPaused) {
    out.status = "paused";
    out.summary["message"] = "paused; run again with the same inputs to resume";
  } else if (rec.status == task_status::kCancelled) {
    out.status = "cancelled";
  } else if (rec.status == task_status::kFailed) {
    out.status = "failed";
    if (out.error.empty()) out.error = rec.error;
  }
  LOOM_TRY(st_->store->finish_run(krun.id, out.status, out.status == "done" ? out.summary : Json{{"error", out.error}}));
  return out;
}

Result<Json> KnowledgeEngine::status(std::string_view task_id) {
  std::optional<TaskRecord> run;
  if (task_id.empty()) {
    TaskFilter f;
    f.kind = "knowledge.run";
    f.limit = 1;
    LOOM_TRY_ASSIGN(auto v, rt_.tasks().list(f));
    if (v.empty()) return Json{{"run", nullptr}, {"stages", Json::array()}};
    run = v.front();
  } else {
    LOOM_TRY_ASSIGN(run, rt_.tasks().get(task_id));
    if (!run || run->kind != "knowledge.run") return Error(Errc::NotFound, "no knowledge run task " + std::string(task_id));
  }
  TaskFilter f;
  f.parent_id = run->id;
  f.limit = 100;
  LOOM_TRY_ASSIGN(auto children, rt_.tasks().list(f));
  // Cache hits retain the original task's parent. Reconstruct a completed
  // invocation from its recorded stage IDs instead of showing an empty run.
  const Json* result = run->result ? &*run->result : run->checkpoint ? &*run->checkpoint : nullptr;
  if (result && result->contains("stages") && (*result)["stages"].is_array()) {
    children.clear();
    for (const auto& stage : (*result)["stages"]) {
      const auto id = json::get_string(stage, "task_id");
      if (id.empty()) continue;
      LOOM_TRY_ASSIGN(auto record, rt_.tasks().get(id));
      if (record) children.push_back(*record);
    }
  }
  Json stages = Json::array();
  for (const auto& c : children) {
    stages.push_back(Json{{"task_id", c.id}, {"kind", c.kind}, {"status", c.status}, {"input_hash", c.input_hash},
                          {"output_hash", c.output_hash}, {"error", c.error}});
  }
  std::string kr = json::get_string(run->params, "run");
  Json kj = nullptr;
  if (!kr.empty()) {
    LOOM_TRY_ASSIGN(auto k, st_->store->get_run(kr));
    if (k) kj = k->to_json();
  }
  return Json{{"run", Json{{"task_id", run->id}, {"knowledge_run", kj}, {"config", run->params["config"]}}}, {"stages", stages}};
}

}  // namespace loom::knowledge
