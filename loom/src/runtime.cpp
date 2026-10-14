// Composition root. See runtime.h for the wiring order.
#include "util/safety.h"
#include "loom/runtime.h"
#include "loom/cleanup.h"
#include "loom/path_policy.h"
#include "loom/net/host_guard.h"
#include "loom/storage_backend.h"

#include <atomic>
#include <mutex>

#include "loom/action_runner.h"
#include "loom/rom.h"
#include "loom/archive.h"
#include "loom/knowledge.h"
#include "loom/catalog.h"
#include "loom/batch_api.h"
#include "loom/chat_engine.h"
#include "loom/crypto.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/github_sync.h"
#include "loom/graph_engine.h"
#include "loom/guardian.h"
#include "loom/graph_memory.h"
#include "loom/importer.h"
#include "loom/loom.h"
#include "loom/media_providers.h"
#include "loom/memory_engine.h"
#include "loom/port_graph.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/providers.h"
#include "loom/relations.h"
#include "loom/selector.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/semantic_worker.h"
#include "loom/sqlite.h"
#include "loom/tasks.h"
#include "loom/usage_policy.h"
#include "loom/util/unicode.h"

#ifndef LOOM_VERSION_STRING
#define LOOM_VERSION_STRING "0.0.0"
#endif

namespace loom {
namespace {
constexpr std::string_view kLog = "loom.runtime";

// Stable HttpTransport reference handed to subsystems; the real transport
// behind it can be swapped (platform injection through the C API).
class ForwardingTransport final : public net::HttpTransport {
 public:
  explicit ForwardingTransport(std::shared_ptr<net::HttpTransport> target) : target_(std::move(target)) {}
  void set(std::shared_ptr<net::HttpTransport> t) {
    std::lock_guard lk(mu_);
    target_ = std::move(t);
  }
  std::shared_ptr<net::HttpTransport> get() const {
    std::lock_guard lk(mu_);
    return target_;
  }
  // Outbound guard (accounts mode, R18): refuses requests to private/local addresses.
  void set_guard(std::vector<std::string> allow_hosts) {
    guard_ = true;
    allow_hosts_ = std::move(allow_hosts);
  }
  Result<net::HttpResponse> send(const net::HttpRequest& req, const net::StreamSink* sink,
                                 const CancelToken* cancel) override {
    if (guard_) LOOM_TRY(net::check_public_url(req.url, allow_hosts_));
    auto t = get();
    if (!t) return Error(Errc::Unavailable, "no HTTP transport configured");
    return t->send(req, sink, cancel);
  }
  std::string name() const override {
    auto t = get();
    return t ? t->name() : "none";
  }

 private:
  mutable std::mutex mu_;
  std::shared_ptr<net::HttpTransport> target_;
  bool guard_ = false;
  std::vector<std::string> allow_hosts_;
};

std::vector<std::string> event_log_types(const Config& cfg) {
  std::vector<std::string> out;
  Json v = cfg.get("loom_event_log_types");
  if (v.is_array()) {
    for (const auto& t : v) {
      if (t.is_string()) out.push_back(t.get<std::string>());
    }
  }
  return out;
}

template <class T>
Result<std::unique_ptr<T>> build_part(std::string_view name, const RuntimeParts::Make<T>& make, PartsContext& ctx) {
  std::unique_ptr<T> part = make(ctx);
  if (!part) return Error(Errc::Internal, "runtime part factory returned null: " + std::string(name));
  return part;
}

std::shared_ptr<EmbeddingProvider> no_embedding(PartsContext&) { return nullptr; }

std::unique_ptr<storage::StorageService> default_storage(PartsContext& c) {
  storage::BackendEnv senv;
  senv.http = c.http_shared;
  senv.secrets = c.secrets;
  senv.data_dir = c.paths->root;
  senv.path_policy = c.path_policy;
  auto svc = storage::StorageService::load(senv, c.db);
  if (svc) return std::move(*svc);
  // A broken overlay must not stop the app, nor silently move data: fall back to the shipped default
  // placement (today's local layout) and say so.
  log::error(kLog, "storage placement profile rejected, using default local placement: {}", svc.error().message);
  auto base = RuntimeProfile::builtin("storage_security");
  auto out = std::make_unique<storage::StorageService>(base ? base->values() : Json::object(), senv);
  out->set_database(c.db);
  return out;
}

std::unique_ptr<CryptoVault> default_crypto(PartsContext& c) {
  auto vault = std::make_unique<CryptoVault>(c.paths->root / "crypto.json");
  // Crypto suite is a setting (storage_security pack + optional <data>/profiles/storage_security.pack
  // overlay). Re-resolved when the overlay file changes; affects NEW writes only.
  struct Cache {
    std::mutex mu;
    bool have = false;
    std::filesystem::file_time_type mtime{};
    std::uintmax_t size = 0;
    bool exists = false;
    Result<cs::SuiteConfig> cfg = cs::SuiteConfig{};
  };
  auto cache = std::make_shared<Cache>();
  std::filesystem::path data_root = c.paths->root;
  vault->set_suite_provider([cache, data_root]() -> Result<cs::SuiteConfig> {
    std::error_code ec;
    auto ov = data_root / "profiles" / "storage_security.pack";
    bool ex = std::filesystem::exists(ov, ec);
    auto mt = ex ? std::filesystem::last_write_time(ov, ec) : std::filesystem::file_time_type{};
    auto sz = ex ? std::filesystem::file_size(ov, ec) : std::uintmax_t{0};
    std::lock_guard lk(cache->mu);
    if (!cache->have || cache->exists != ex || cache->mtime != mt || cache->size != sz) {
      cache->cfg = cs::suite_config_from_profile(data_root);
      cache->have = true;
      cache->exists = ex;
      cache->mtime = mt;
      cache->size = sz;
    }
    return cache->cfg;
  });
  return vault;
}

// Today's wiring, one factory per part. Empty slots of `p` are filled; set ones are kept.
void fill_default_parts(RuntimeParts& p, std::shared_ptr<net::HttpTransport> opts_http) {
  if (!p.http)
    p.http = [opts_http] { return opts_http ? opts_http : net::make_default_transport(); };
  if (!p.analyzer)
    p.analyzer = [](PartsContext& c) { return SemanticAnalyzer::create_from_data_dir(c.paths->root); };
  if (!p.storage) p.storage = default_storage;
  if (!p.models)
    p.models = [](PartsContext& c) {
      return std::make_unique<ModelRegistry>(c.paths->models, *c.config, *c.secrets, *c.http);
    };
  if (!p.providers)
    p.providers = [](PartsContext& c) {
      auto reg = std::make_unique<ProviderRegistry>(*c.config, *c.secrets);
      if (auto st = reg->load_builtin(); !st) log::warn(kLog, "provider manifests not loaded: {}", st.error().message);
      return reg;
    };
  if (!p.semantic_llm)
    p.semantic_llm = [](PartsContext& c) {
      return std::make_unique<SemanticLLM>(*c.config, *c.secrets, *c.http, *c.analyzer);
    };
  if (!p.memory)
    p.memory = [](PartsContext& c) { return std::make_unique<MemoryEngine>(c.paths->memory, c.analyzer); };
  if (!p.graph_memory)
    p.graph_memory = [](PartsContext& c) {
      return std::make_unique<GraphMemorySelector>(*c.db, *c.config, *c.analyzer);
    };
  if (!p.context)
    p.context = [](PartsContext& c) {
      return std::make_unique<ContextSelector>(*c.db, *c.config, *c.graph_memory, c.memory);
    };
  if (!p.graph)
    p.graph = [](PartsContext& c) {
      return std::make_unique<GraphEngine>(*c.db, *c.bus, *c.analyzer, c.semantic_llm, c.relations,
                                           AnalyzerBinding::RuntimeProfile);
    };
  if (!p.chat)
    p.chat = [](PartsContext& c) {
      return std::make_unique<ChatEngine>(*c.config, *c.secrets, *c.db, *c.bus, *c.http, *c.analyzer, c.memory,
                                          c.graph_memory);
    };
  if (!p.batch)
    p.batch = [](PartsContext& c) {
      return std::make_unique<SemanticBatchAPI>(*c.config, *c.secrets, *c.db, *c.http, *c.analyzer, c.tasks);
    };
  if (!p.worker)
    p.worker = [](PartsContext& c) {
      return std::make_unique<SemanticWorker>(*c.db, *c.semantic_llm, *c.graph, *c.config, *c.secrets, *c.bus,
                                              *c.http, *c.analyzer, c.tasks, std::nullopt, Json::object(),
                                              AnalyzerBinding::RuntimeProfile);
    };
  if (!p.media)
    p.media = [](PartsContext& c) { return std::make_unique<MediaProviders>(*c.secrets, *c.http); };
  if (!p.importer)
    p.importer = [](PartsContext& c) {
      return std::make_unique<ConversationImporter>(*c.db, *c.bus, c.blobs, c.provenance, c.media);
    };
  if (!p.crypto) p.crypto = default_crypto;
  if (!p.github)
    p.github = [](PartsContext& c) {
      return std::make_unique<GitHubSyncManager>(c.paths->github_sync, *c.secrets, *c.http);
    };
  if (!p.embedding) p.embedding = no_embedding;
  if (!p.archive) p.archive = [](Runtime& rt) { return std::make_unique<archive::ArchiveIntelligence>(rt); };
  if (!p.knowledge) p.knowledge = [](Runtime& rt) { return std::make_unique<knowledge::KnowledgeEngine>(rt); };
  if (!p.actions) p.actions = [](Runtime& rt) { return std::make_unique<actions::ActionRunner>(rt); };
}

}  // namespace

std::string_view version() noexcept { return LOOM_VERSION_STRING; }

Result<RuntimeOptions> RuntimeOptions::from_json(const Json& j) {
  RuntimeOptions o;
  if (j.is_null()) return o;
  if (!j.is_object()) return Error(Errc::InvalidArgument, "options must be a JSON object");
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    if (k == "data_dir") {
      if (v.is_string()) o.data_dir = v.get<std::string>();
      else if (!v.is_null()) return Error(Errc::InvalidArgument, "data_dir must be a string");
    } else if (k == "start_workers" && v.is_boolean()) {
      o.start_workers = v.get<bool>();
    } else if (k == "enable_fts" && v.is_boolean()) {
      o.enable_fts = v.get<bool>();
    } else if (k == "block_private_hosts" && v.is_boolean()) {
      o.block_private_hosts = v.get<bool>();
    } else if (k == "allow_hosts" && v.is_array()) {
      for (const auto& r : v) {
        if (!r.is_string()) return Error(Errc::InvalidArgument, "allow_hosts must be an array of strings");
        o.allow_hosts.push_back(r.get<std::string>());
      }
    } else if (k == "zip_max_total_bytes" && v.is_number()) {
      o.zip_max_total_bytes = v.get<std::int64_t>();
    } else if (k == "zip_max_ratio" && v.is_number()) {
      o.zip_max_ratio = v.get<double>();
    } else if (k == "git_allow_repo_config" && v.is_boolean()) {
      o.git_allow_repo_config = v.get<bool>();
    } else if (k == "confine_paths" && v.is_boolean()) {
      o.confine_paths = v.get<bool>();
    } else if (k == "path_roots" && v.is_array()) {
      for (const auto& r : v) {
        if (!r.is_string()) return Error(Errc::InvalidArgument, "path_roots must be an array of strings");
        o.path_roots.push_back(r.get<std::string>());
      }
    } else if (k == "log_level" && v.is_string()) {
      std::string s = v.get<std::string>();
      if (s == "debug") o.log_level = log::Level::Debug;
      else if (s == "info") o.log_level = log::Level::Info;
      else if (s == "warning" || s == "warn") o.log_level = log::Level::Warning;
      else if (s == "error") o.log_level = log::Level::Error;
      else return Error(Errc::InvalidArgument, "unknown log_level: " + s);
    } else {
      return Error(Errc::InvalidArgument, "unknown or invalid option: " + k);
    }
  }
  return o;
}

struct Runtime::Impl {
  // Declaration order == construction order == reverse destruction order.
  DataPaths paths;
  std::shared_ptr<PathPolicy> path_policy = std::make_shared<PathPolicy>();
  std::unique_ptr<Config> config;
  std::unique_ptr<Secrets> secrets;
  std::unique_ptr<Database> db;
  std::unique_ptr<EventBus> bus;
  std::unique_ptr<EventLog> event_log;
  std::unique_ptr<BlobStore> blobs;
  std::unique_ptr<storage::StorageService> storage;
  std::unique_ptr<ProvenanceStore> provenance;
  std::unique_ptr<TaskEngine> tasks;
  std::unique_ptr<RelationRegistry> relations;
  std::unique_ptr<SemanticAnalyzer> analyzer;
  std::shared_ptr<ForwardingTransport> http;
  std::unique_ptr<ModelRegistry> models;
  std::unique_ptr<ProviderRegistry> providers;
  std::unique_ptr<SemanticLLM> semantic_llm;
  std::unique_ptr<MemoryEngine> memory;
  std::unique_ptr<GraphMemorySelector> graph_memory;
  std::unique_ptr<ContextSelector> context;
  std::unique_ptr<GraphEngine> graph;
  std::unique_ptr<ChatEngine> chat;
  std::unique_ptr<SemanticBatchAPI> batch;
  std::unique_ptr<SemanticWorker> worker;
  std::unique_ptr<MediaProviders> media;
  std::unique_ptr<ConversationImporter> importer;
  std::unique_ptr<ConversationExporter> exporter;
  std::unique_ptr<CryptoVault> crypto;
  std::unique_ptr<GitHubSyncManager> github;
  std::unique_ptr<archive::ArchiveIntelligence> archive;  // needs the Runtime; created last
  std::unique_ptr<knowledge::KnowledgeEngine> knowledge;  // needs the Runtime; registers knowledge.* handlers
  rom::MountRegistry mounts;                              // ROM first, then the data dir
  std::unique_ptr<guardian::Guardian> guardian;           // needs the Runtime; destroyed before the actions
  std::unique_ptr<actions::ActionRunner> actions;         // needs the Runtime; destroyed first

  mutable std::mutex embed_mu;
  std::shared_ptr<EmbeddingProvider> embedder;
  std::atomic<bool> shut_down{false};
};

Runtime::Runtime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Runtime::~Runtime() {
  shutdown();
  // Parts built from the Runtime reach the other parts through rt.bus(), rt.event_log() ... in their destructors, i.e. through impl_.
  // std::unique_ptr in libc++ (the Android NDK) nulls impl_ BEFORE it deletes the Impl, so letting ~Impl destroy them reads impl_->bus
  // through a null pointer (SIGSEGV, fault addr = offsetof(Impl, bus) = 0x148). Destroy them here, while impl_ is still valid, in
  // reverse construction order.
  if (impl_) {
    impl_->actions.reset();
    impl_->knowledge.reset();
    impl_->archive.reset();
  }
}

Result<std::unique_ptr<Runtime>> Runtime::open(const RuntimeOptions& opts) {
  if (opts.log_level) log::set_level(*opts.log_level);
  auto impl = std::make_unique<Impl>();

  std::optional<std::string_view> override_dir;
  if (opts.data_dir) override_dir = *opts.data_dir;
  LOOM_TRY_ASSIGN(fs::path root, resolve_data_dir(override_dir, opts.path_env.value_or(PathEnv::from_process())));
  impl->paths = DataPaths::for_root(root);
  const DataPaths& p = impl->paths;
  impl->mounts = rom::MountRegistry::standard(p.root);
  if (opts.zip_max_total_bytes || opts.zip_max_ratio || opts.git_allow_repo_config)
    safety::set_operator_ceiling(p.root, {opts.zip_max_total_bytes.value_or(0), opts.zip_max_ratio.value_or(0),
                                          opts.git_allow_repo_config.value_or(true)});
  if (opts.confine_paths) {
    std::vector<fs::path> roots{p.root};
    for (const auto& r : opts.path_roots) roots.emplace_back(r);
    impl->path_policy->confine(std::move(roots));
  }

  impl->config = std::make_unique<Config>(p.config);
  impl->secrets = std::make_unique<Secrets>(p.secrets);

  DbOptions dbo;
  dbo.enable_fts = opts.enable_fts;
  dbo.fts_path = p.fts_index;
  {
    Json cr = impl->config->get("loom_fts_chunk_rows");
    if (cr.is_number_integer()) dbo.fts_chunk_rows = static_cast<int>(std::max<std::int64_t>(0, cr.get<std::int64_t>()));
  }
  LOOM_TRY_ASSIGN(impl->db, Database::open(p.db, dbo));
  if (opts.start_workers && opts.enable_fts) {
    Json bg = impl->config->get("loom_fts_background_build");
    if (!bg.is_boolean() || bg.get<bool>()) impl->db->fts_start_background();
  }

  impl->bus = std::make_unique<EventBus>();
  impl->event_log = std::make_unique<EventLog>(*impl->db);
  impl->bus->set_persistence(impl->event_log.get(), event_log_types(*impl->config));
  impl->blobs = std::make_unique<BlobStore>(p.blobs, *impl->db);
  impl->provenance = std::make_unique<ProvenanceStore>(*impl->db);
  impl->tasks = std::make_unique<TaskEngine>(*impl->db, *impl->event_log, impl->bus.get());
  impl->relations = std::make_unique<RelationRegistry>(*impl->db);
  LOOM_TRY(impl->relations->seed_builtin());

  RuntimeParts parts = opts.parts;
  fill_default_parts(parts, opts.http);
  PartsContext ctx;
  ctx.paths = &impl->paths;
  ctx.path_policy = impl->path_policy;
  ctx.config = impl->config.get();
  ctx.secrets = impl->secrets.get();
  ctx.db = impl->db.get();
  ctx.bus = impl->bus.get();
  ctx.event_log = impl->event_log.get();
  ctx.blobs = impl->blobs.get();
  ctx.provenance = impl->provenance.get();
  ctx.tasks = impl->tasks.get();
  ctx.relations = impl->relations.get();

  LOOM_TRY_ASSIGN(impl->analyzer, parts.analyzer(ctx));
  if (!impl->analyzer) return Error(Errc::Internal, "runtime part factory returned null: analyzer");
  ctx.analyzer = impl->analyzer.get();
  auto forwarding = std::make_shared<ForwardingTransport>(parts.http());
  if (opts.block_private_hosts) forwarding->set_guard(opts.allow_hosts);
  impl->http = forwarding;
  ctx.http = impl->http.get();
  ctx.http_shared = impl->http;

  LOOM_TRY_ASSIGN(impl->storage, build_part("storage", parts.storage, ctx));
  ctx.storage = impl->storage.get();
  impl->blobs->set_placement(impl->storage.get());
  LOOM_TRY_ASSIGN(impl->models, build_part("models", parts.models, ctx));
  ctx.models = impl->models.get();
  LOOM_TRY_ASSIGN(impl->providers, build_part("providers", parts.providers, ctx));
  ctx.providers = impl->providers.get();
  LOOM_TRY_ASSIGN(impl->semantic_llm, build_part("semantic_llm", parts.semantic_llm, ctx));
  ctx.semantic_llm = impl->semantic_llm.get();
  LOOM_TRY_ASSIGN(impl->memory, build_part("memory", parts.memory, ctx));
  ctx.memory = impl->memory.get();
  LOOM_TRY_ASSIGN(impl->graph_memory, build_part("graph_memory", parts.graph_memory, ctx));
  ctx.graph_memory = impl->graph_memory.get();
  LOOM_TRY_ASSIGN(impl->context, build_part("context", parts.context, ctx));
  ctx.context = impl->context.get();
  LOOM_TRY_ASSIGN(impl->graph, build_part("graph", parts.graph, ctx));
  ctx.graph = impl->graph.get();
  LOOM_TRY_ASSIGN(impl->chat, build_part("chat", parts.chat, ctx));
  ctx.chat = impl->chat.get();
  LOOM_TRY_ASSIGN(impl->batch, build_part("batch", parts.batch, ctx));
  ctx.batch = impl->batch.get();
  LOOM_TRY_ASSIGN(impl->worker, build_part("worker", parts.worker, ctx));
  ctx.worker = impl->worker.get();
  LOOM_TRY_ASSIGN(impl->media, build_part("media", parts.media, ctx));
  ctx.media = impl->media.get();
  // ASR/OCR model_use records go to the provenance store (subject = the usage receipt when there is one).
  impl->media->set_provenance_sink([prov = impl->provenance.get(), db = impl->db.get(), cfg = impl->config.get()](const Json& record) {
    ProvenanceRecord rec;
    rec.subject_id = json::get_string(record, "operation_id");
    if (rec.subject_id.empty()) rec.subject_id = json::get_string(record, "request_sha256");
    rec.subject_kind = "model_use";
    rec.transform = json::get_string(record, "kind", "model_use") + "@1";
    rec.locator = record;
    if (auto r = prov->add(std::move(rec)); !r) log::debug(kLog, "media provenance not recorded: {}", r.error().message);
    // The same call as a port run in the graph (B-MOD-09); switch off with config port_graph_runs=false.
    if (cfg->get("port_graph_runs", Json(true)) == Json(false)) return;
    auto run = port_graph::port_run_from_model_use(record);
    // The registered adapter whose name starts with the provider's id (groq -> groq_whisper).
    const std::string provider = run.provider;
    const auto names = run.port == "asr" ? asr_adapters().names() : run.port == "ocr" ? ocr_adapters().names() : std::vector<std::string>();
    for (const auto& n : names)
      if (!provider.empty() && n.rfind(provider, 0) == 0) { run.adapter = n; break; }
    if (auto g = port_graph::record_port_run(*db, run); !g) log::debug(kLog, "port run not recorded in the graph: {}", g.error().message);
  });
  LOOM_TRY_ASSIGN(impl->importer, build_part("importer", parts.importer, ctx));
  ctx.importer = impl->importer.get();
  impl->exporter = std::make_unique<ConversationExporter>(*impl->db);
  LOOM_TRY_ASSIGN(impl->crypto, build_part("crypto", parts.crypto, ctx));
  LOOM_TRY_ASSIGN(impl->github, build_part("github", parts.github, ctx));
  impl->embedder = parts.embedding(ctx);

  // Python ChatEngine resumed the most recent conversation on construction.
  if (auto st = impl->chat->resume_last(); !st && st.error().code != Errc::NotImplemented) {
    log::warn(kLog, "could not resume last conversation: {}", st.error().message);
  }

  // Start: graph subscription always (real-time graph is core behaviour);
  // background threads only when requested.
  impl->graph->start();
  if (auto rec = impl->tasks->recover_interrupted(); !rec) {
    log::warn(kLog, "task recovery failed: {}", rec.error().message);
  }
  if (opts.start_workers) {
    // Housekeeping of the usage ledger (preset mode "auto"): never creates it, runs at most once per interval.
    if (auto compacted = auto_compact_usage_ledger(*impl->config, root); !compacted)
      log::warn(kLog, "usage ledger compaction failed: {}", compacted.error().message);
    // Housekeeping of scratch files and unreferenced blobs (loom/cleanup.h; blobs are off and dry-run by preset): a failure never stops the runtime.
    if (auto copts = cleanup_options(*impl->config); !copts) {
      log::warn(kLog, "cleanup settings ignored: {}", copts.error().message);
    } else if (auto cleaned = run_cleanup(*impl->db, *impl->blobs, root, *copts); !cleaned) {
      log::warn(kLog, "cleanup failed: {}", cleaned.error().message);
    }
    Json workers = impl->config->get("loom_task_workers");
    impl->tasks->start(workers.is_number_integer() ? workers.get<int>() : 1);
    impl->worker->start();
  }
  log::info(kLog, "Loom {} runtime ready (data: {})", version(), root.string());
  auto rt = std::make_unique<Runtime>(std::move(impl));
  // Registers the archive.* task handlers (resumable by the task workers).
  rt->impl_->archive = parts.archive(*rt);
  rt->impl_->knowledge = parts.knowledge(*rt);
  rt->impl_->actions = parts.actions(*rt);
  if (!rt->impl_->archive || !rt->impl_->knowledge || !rt->impl_->actions)
    return Error(Errc::Internal, "runtime part factory returned null: archive/knowledge/actions");
  rt->impl_->guardian = std::make_unique<guardian::Guardian>(*rt);
  rt->impl_->guardian->start();
  rt->tasks().register_handler("catalog.read_resource", [runtime = rt.get()](TaskContext& task) -> Status {
    const auto& params = task.params();
    if (!params.is_object() || !params.contains("unit_id") ||
        !params["unit_id"].is_string() || params["unit_id"].get<std::string>().empty())
      return Error(Errc::InvalidArgument, "catalog.read_resource requires a nonempty unit_id");
    for (const auto& [key, value] : params.items()) {
      (void)value;
      if (key != "unit_id" && key != "read_options" && key != "read_authorized")
        return Error(Errc::InvalidArgument, "catalog.read_resource has an unknown argument");
    }
    if (params.contains("read_authorized") && !params["read_authorized"].is_boolean())
      return Error(Errc::InvalidArgument, "catalog.read_resource authorization must be boolean");
    if (task.cancelled()) return Error(Errc::Cancelled, "resource read cancelled");
    LOOM_TRY_ASSIGN(auto pack, runtime->knowledge().pack());
    catalog::Catalog catalog(*runtime, pack);
    // Task submission is the existing authenticated host read request. Source
    // bytes and discovered descriptions are never interpreted as task grants.
    LOOM_TRY_ASSIGN(auto result, catalog.execute_resource(params["unit_id"].get<std::string>(),
        params.value("read_options", Json::object()), params.value("read_authorized", true)));
    const auto& effective = result.at("method_manifest").at("trace").at("effective_parameters");
    if (effective.at("read_options").at("projection_storage") == "transient") {
      // The durable queue stores a reference/receipt. Persisting set_result's
      // in-memory graph here would defeat the selected retention policy.
      result.erase("last_successful");
      result["resource_ref"] = effective.at("resource");
      result["payload_status"] = "reference_only";
      if (result.contains("error") && result["error"].is_object()) result["error"].erase("message");
    }
    task.set_result(std::move(result));
    return {};
  });
  rt->chat().set_knowledge_context_builder([runtime = rt.get()](const context::ContextRequest& request) -> Result<Json> {
    auto pack = runtime->knowledge().pack();
    if (!pack) return pack.error();
    auto& store = runtime->knowledge().store();
    auto resolved = request;
    if (resolved.run.empty()) {
      auto runs = store.list_context_runs(1, "done");
      if (!runs) return runs.error();
      for (const auto& run : *runs) {
        if (run.status == "done") { resolved.run = run.id; break; }
      }
      if (resolved.run.empty()) return Error(Errc::NotFound, "no finished knowledge run");
    }
    auto run = store.get_run(resolved.run);
    if (!run) return run.error();
    if (!*run) return Error(Errc::NotFound, "knowledge run not found: " + resolved.run);
    context::ContextEngine engine(*runtime, store, *pack);
    auto compiled = engine.build(resolved);  // offline; no model permission inferred
    if (!compiled) return compiled.error();
    (*compiled)["request"] = resolved.to_json();
    return *compiled;
  });
  return rt;
}

void Runtime::shutdown() {
  if (!impl_ || impl_->shut_down.exchange(true)) return;
  if (impl_->worker) impl_->worker->stop();
  if (impl_->tasks) impl_->tasks->stop();
  if (impl_->guardian) impl_->guardian->stop();
  if (impl_->graph) impl_->graph->stop();
  if (impl_->bus) impl_->bus->set_persistence(nullptr, {});
  if (impl_->crypto) impl_->crypto->lock();
  if (impl_->db) impl_->db->close();
  log::info(kLog, "Loom runtime shut down");
}

const DataPaths& Runtime::paths() const { return impl_->paths; }
const PathPolicy& Runtime::path_policy() const { return *impl_->path_policy; }
const rom::MountRegistry& Runtime::mounts() const { return impl_->mounts; }
Config& Runtime::config() { return *impl_->config; }
Secrets& Runtime::secrets() { return *impl_->secrets; }
Database& Runtime::db() { return *impl_->db; }
EventBus& Runtime::bus() { return *impl_->bus; }
BlobStore& Runtime::blobs() { return *impl_->blobs; }
storage::StorageService& Runtime::storage() { return *impl_->storage; }
ProvenanceStore& Runtime::provenance() { return *impl_->provenance; }
EventLog& Runtime::event_log() { return *impl_->event_log; }
TaskEngine& Runtime::tasks() { return *impl_->tasks; }
RelationRegistry& Runtime::relations() { return *impl_->relations; }
SemanticAnalyzer& Runtime::analyzer() { return *impl_->analyzer; }
net::HttpTransport& Runtime::http() { return *impl_->http; }
ModelRegistry& Runtime::models() { return *impl_->models; }
ProviderRegistry& Runtime::providers() { return *impl_->providers; }
SemanticLLM& Runtime::semantic_llm() { return *impl_->semantic_llm; }
MemoryEngine& Runtime::memory() { return *impl_->memory; }
GraphMemorySelector& Runtime::graph_memory() { return *impl_->graph_memory; }
ContextSelector& Runtime::context() { return *impl_->context; }
GraphEngine& Runtime::graph() { return *impl_->graph; }
ChatEngine& Runtime::chat() { return *impl_->chat; }
SemanticBatchAPI& Runtime::batch() { return *impl_->batch; }
SemanticWorker& Runtime::worker() { return *impl_->worker; }
MediaProviders& Runtime::media() { return *impl_->media; }
ConversationImporter& Runtime::importer() { return *impl_->importer; }
ConversationExporter& Runtime::exporter() { return *impl_->exporter; }
CryptoVault& Runtime::crypto() { return *impl_->crypto; }
GitHubSyncManager& Runtime::github() { return *impl_->github; }
archive::ArchiveIntelligence& Runtime::archive() { return *impl_->archive; }
knowledge::KnowledgeEngine& Runtime::knowledge() { return *impl_->knowledge; }
guardian::Guardian& Runtime::guardian() { return *impl_->guardian; }
actions::ActionRunner& Runtime::actions() { return *impl_->actions; }

void Runtime::set_http_transport(std::shared_ptr<net::HttpTransport> transport) {
  impl_->http->set(transport ? std::move(transport) : net::make_default_transport());
}

void Runtime::set_embedding_provider(std::shared_ptr<EmbeddingProvider> provider) {
  std::lock_guard lk(impl_->embed_mu);
  impl_->embedder = std::move(provider);
}

std::shared_ptr<EmbeddingProvider> Runtime::embedding_provider() const {
  std::lock_guard lk(impl_->embed_mu);
  return impl_->embedder;
}

SelectorEngine Runtime::make_selector() const { return SelectorEngine(std::nullopt, embedding_provider()); }

Json Runtime::build_info() {
  bool fts5 = false;
  if (auto c = sql::Connection::open_memory()) fts5 = c->has_fts5();
#if defined(LOOM_HAVE_OPENSSL)
  constexpr bool kOpenSsl = true;
#else
  constexpr bool kOpenSsl = false;
#endif
  return Json{{"version", std::string(version())},
              {"abi", LOOM_ABI_VERSION},
              {"sqlite", sql::library_version()},
              {"fts5", fts5},
              {"openssl", kOpenSsl},
              {"unicode", std::string(unicode::database_version())}};
}

Json Runtime::info() const {
  Json j = build_info();
  const DataPaths& p = impl_->paths;
  j["data_dir"] = p.root.string();
  j["paths"] = Json{{"db", p.db.string()},           {"fts_index", p.fts_index.string()},
                    {"config", p.config.string()},   {"secrets", p.secrets.string()},
                    {"memory", p.memory.string()},   {"models", p.models.string()},
                    {"blobs", p.blobs.string()},     {"attachments", p.attachments.string()},
                    {"exports", p.exports.string()}, {"logs", p.logs.string()}};
  j["http_transport"] = impl_->http->name();
  return j;
}

}  // namespace loom
