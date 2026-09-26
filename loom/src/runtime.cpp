// Composition root. See runtime.h for the wiring order.
#include "loom/runtime.h"

#include <atomic>
#include <mutex>

#include "loom/archive.h"
#include "loom/knowledge.h"
#include "loom/batch_api.h"
#include "loom/chat_engine.h"
#include "loom/crypto.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/github_sync.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/importer.h"
#include "loom/loom.h"
#include "loom/media_providers.h"
#include "loom/memory_engine.h"
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
  Result<net::HttpResponse> send(const net::HttpRequest& req, const net::StreamSink* sink,
                                 const CancelToken* cancel) override {
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
  std::unique_ptr<Config> config;
  std::unique_ptr<Secrets> secrets;
  std::unique_ptr<Database> db;
  std::unique_ptr<EventBus> bus;
  std::unique_ptr<EventLog> event_log;
  std::unique_ptr<BlobStore> blobs;
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

  mutable std::mutex embed_mu;
  std::shared_ptr<EmbeddingProvider> embedder;
  std::atomic<bool> shut_down{false};
};

Runtime::Runtime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Runtime::~Runtime() { shutdown(); }

Result<std::unique_ptr<Runtime>> Runtime::open(const RuntimeOptions& opts) {
  if (opts.log_level) log::set_level(*opts.log_level);
  auto impl = std::make_unique<Impl>();

  std::optional<std::string_view> override_dir;
  if (opts.data_dir) override_dir = *opts.data_dir;
  LOOM_TRY_ASSIGN(fs::path root, resolve_data_dir(override_dir, opts.path_env.value_or(PathEnv::from_process())));
  impl->paths = DataPaths::for_root(root);
  const DataPaths& p = impl->paths;

  impl->config = std::make_unique<Config>(p.config);
  impl->secrets = std::make_unique<Secrets>(p.secrets);

  DbOptions dbo;
  dbo.enable_fts = opts.enable_fts;
  dbo.fts_path = p.fts_index;
  LOOM_TRY_ASSIGN(impl->db, Database::open(p.db, dbo));

  impl->bus = std::make_unique<EventBus>();
  impl->event_log = std::make_unique<EventLog>(*impl->db);
  impl->bus->set_persistence(impl->event_log.get(), event_log_types(*impl->config));
  impl->blobs = std::make_unique<BlobStore>(p.blobs, *impl->db);
  impl->provenance = std::make_unique<ProvenanceStore>(*impl->db);
  impl->tasks = std::make_unique<TaskEngine>(*impl->db, *impl->event_log, impl->bus.get());
  impl->relations = std::make_unique<RelationRegistry>(*impl->db);
  LOOM_TRY(impl->relations->seed_builtin());

  LOOM_TRY_ASSIGN(impl->analyzer, SemanticAnalyzer::create());
  impl->http = std::make_shared<ForwardingTransport>(opts.http ? opts.http : net::make_default_transport());

  impl->models = std::make_unique<ModelRegistry>(p.models, *impl->config, *impl->secrets, *impl->http);
  impl->providers = std::make_unique<ProviderRegistry>(*impl->config, *impl->secrets);
  if (auto st = impl->providers->load_builtin(); !st) {
    log::warn(kLog, "provider manifests not loaded: {}", st.error().message);
  }
  impl->semantic_llm = std::make_unique<SemanticLLM>(*impl->config, *impl->secrets, *impl->http, *impl->analyzer);
  impl->memory = std::make_unique<MemoryEngine>(p.memory, impl->analyzer.get());
  impl->graph_memory = std::make_unique<GraphMemorySelector>(*impl->db, *impl->config, *impl->analyzer);
  impl->context =
      std::make_unique<ContextSelector>(*impl->db, *impl->config, *impl->graph_memory, impl->memory.get());
  impl->graph = std::make_unique<GraphEngine>(*impl->db, *impl->bus, *impl->analyzer, impl->semantic_llm.get(),
                                              impl->relations.get());
  impl->chat = std::make_unique<ChatEngine>(*impl->config, *impl->secrets, *impl->db, *impl->bus, *impl->http,
                                            *impl->analyzer, impl->memory.get(), impl->graph_memory.get());
  impl->batch = std::make_unique<SemanticBatchAPI>(*impl->config, *impl->secrets, *impl->db, *impl->http,
                                                   *impl->analyzer, impl->tasks.get());
  impl->worker = std::make_unique<SemanticWorker>(*impl->db, *impl->semantic_llm, *impl->graph, *impl->config,
                                                  *impl->secrets, *impl->bus, *impl->http, *impl->analyzer,
                                                  impl->tasks.get());
  impl->media = std::make_unique<MediaProviders>(*impl->secrets, *impl->http);
  impl->importer = std::make_unique<ConversationImporter>(*impl->db, *impl->bus, impl->blobs.get(),
                                                          impl->provenance.get(), impl->media.get());
  impl->exporter = std::make_unique<ConversationExporter>(*impl->db);
  impl->crypto = std::make_unique<CryptoVault>(p.root / "crypto.json");
  impl->github = std::make_unique<GitHubSyncManager>(p.github_sync, *impl->secrets, *impl->http);

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
    Json workers = impl->config->get("loom_task_workers");
    impl->tasks->start(workers.is_number_integer() ? workers.get<int>() : 1);
    impl->worker->start();
  }
  log::info(kLog, "Loom {} runtime ready (data: {})", version(), root.string());
  auto rt = std::make_unique<Runtime>(std::move(impl));
  // Registers the archive.* task handlers (resumable by the task workers).
  rt->impl_->archive = std::make_unique<archive::ArchiveIntelligence>(*rt);
  rt->impl_->knowledge = std::make_unique<knowledge::KnowledgeEngine>(*rt);
  return rt;
}

void Runtime::shutdown() {
  if (!impl_ || impl_->shut_down.exchange(true)) return;
  if (impl_->worker) impl_->worker->stop();
  if (impl_->tasks) impl_->tasks->stop();
  if (impl_->graph) impl_->graph->stop();
  if (impl_->bus) impl_->bus->set_persistence(nullptr, {});
  if (impl_->crypto) impl_->crypto->lock();
  if (impl_->db) impl_->db->close();
  log::info(kLog, "Loom runtime shut down");
}

const DataPaths& Runtime::paths() const { return impl_->paths; }
Config& Runtime::config() { return *impl_->config; }
Secrets& Runtime::secrets() { return *impl_->secrets; }
Database& Runtime::db() { return *impl_->db; }
EventBus& Runtime::bus() { return *impl_->bus; }
BlobStore& Runtime::blobs() { return *impl_->blobs; }
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
