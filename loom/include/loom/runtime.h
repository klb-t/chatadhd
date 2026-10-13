// loom/runtime.h — composition root: one Runtime per data directory.
//
// Owns and wires every subsystem (construction order = dependency order,
// destruction in reverse). Nothing else in Loom creates subsystems or holds
// global state (except the process-wide logger). The C API (loom.h) is a thin
// JSON layer over a Runtime.
//
// Wiring (Python main.py equivalent):
//   paths -> Config, Secrets -> Database -> EventBus (+EventLog persistence
//   policy) -> BlobStore, ProvenanceStore, EventLog -> TaskEngine ->
//   RelationRegistry (seeded) -> SemanticAnalyzer -> HttpTransport
//   (forwarding proxy, swappable at runtime) -> ModelRegistry,
//   ProviderRegistry -> SemanticLLM -> MemoryEngine -> GraphMemorySelector,
//   ContextSelector -> GraphEngine (subscribed to message:created) ->
//   ChatEngine -> SemanticBatchAPI -> SemanticWorker (wakes on import:done)
//   -> MediaProviders -> ConversationImporter, ConversationExporter ->
//   CryptoVault -> GitHubSyncManager.
// start_workers=false keeps every background thread off (tests, CLI tools).
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Runtime;
class Database;
class EventBus;
class BlobStore;
namespace storage { class StorageService; }
class ProvenanceStore;
class EventLog;
class TaskEngine;
class RelationRegistry;
class SemanticAnalyzer;
class SemanticLLM;
class GraphEngine;
class GraphMemorySelector;
class ContextSelector;
class SelectorEngine;
class EmbeddingProvider;
class MemoryEngine;
class ModelRegistry;
class ProviderRegistry;
class ChatEngine;
class SemanticBatchAPI;
class SemanticWorker;
class ConversationImporter;
class ConversationExporter;
class CryptoVault;
class MediaProviders;
class GitHubSyncManager;
namespace net {
class HttpTransport;
}
namespace actions {
class ActionRunner;
}
namespace rom {
class MountRegistry;
}
namespace archive {
class ArchiveIntelligence;
}
namespace knowledge {
class KnowledgeEngine;
}

// Everything already built when a part factory runs. Pointers are null until the part that owns them has been
// created (construction order = the wiring list above); factories may only use what precedes their own part.
struct PartsContext {
  const DataPaths* paths = nullptr;
  Config* config = nullptr;
  Secrets* secrets = nullptr;
  Database* db = nullptr;
  EventBus* bus = nullptr;
  EventLog* event_log = nullptr;
  BlobStore* blobs = nullptr;
  ProvenanceStore* provenance = nullptr;
  TaskEngine* tasks = nullptr;
  RelationRegistry* relations = nullptr;
  SemanticAnalyzer* analyzer = nullptr;
  net::HttpTransport* http = nullptr;  // the stable forwarding proxy, never the raw transport
  std::shared_ptr<net::HttpTransport> http_shared;  // same object, for parts that must keep it alive
  storage::StorageService* storage = nullptr;
  ModelRegistry* models = nullptr;
  ProviderRegistry* providers = nullptr;
  SemanticLLM* semantic_llm = nullptr;
  MemoryEngine* memory = nullptr;
  GraphMemorySelector* graph_memory = nullptr;
  ContextSelector* context = nullptr;
  GraphEngine* graph = nullptr;
  ChatEngine* chat = nullptr;
  SemanticBatchAPI* batch = nullptr;
  SemanticWorker* worker = nullptr;
  MediaProviders* media = nullptr;
  ConversationImporter* importer = nullptr;
};

// Injectable composition root. Every empty factory is replaced by the default, which reproduces the wiring
// Runtime::open always had; a caller (test, server, phone shell) sets only the parts it wants to replace,
// without editing runtime.cpp. A factory returning null makes open() fail with Errc::Internal.
// The remaining subsystems (config, secrets, database, bus, event log, blobs, provenance, tasks, relations,
// exporter) are the kernel itself and are not replaceable.
struct RuntimeParts {
  template <class T>
  using Make = std::function<std::unique_ptr<T>(PartsContext&)>;
  // Raw HTTP stack under the forwarding proxy (opts.http, when set, wins over the default; this wins over both).
  std::function<std::shared_ptr<net::HttpTransport>()> http;
  std::function<Result<std::unique_ptr<SemanticAnalyzer>>(PartsContext&)> analyzer;
  Make<storage::StorageService> storage;
  Make<ModelRegistry> models;
  Make<ProviderRegistry> providers;
  Make<SemanticLLM> semantic_llm;  // the LLM service used by graph/worker
  Make<MemoryEngine> memory;
  Make<GraphMemorySelector> graph_memory;
  Make<ContextSelector> context;
  Make<GraphEngine> graph;
  Make<ChatEngine> chat;
  Make<SemanticBatchAPI> batch;
  Make<SemanticWorker> worker;
  Make<MediaProviders> media;  // ASR / OCR
  Make<ConversationImporter> importer;
  Make<CryptoVault> crypto;
  Make<GitHubSyncManager> github;
  // Initial embedding provider for SelectorEngine tier 1 (null = none; set_embedding_provider still works).
  std::function<std::shared_ptr<EmbeddingProvider>(PartsContext&)> embedding;
  // Built last: they need the finished Runtime.
  std::function<std::unique_ptr<archive::ArchiveIntelligence>(Runtime&)> archive;
  std::function<std::unique_ptr<knowledge::KnowledgeEngine>(Runtime&)> knowledge;
  std::function<std::unique_ptr<actions::ActionRunner>(Runtime&)> actions;
};

struct RuntimeOptions {
  std::optional<std::string> data_dir;   // default: resolve_data_dir()
  std::optional<PathEnv> path_env;       // tests: injected environment
  bool start_workers = true;             // SemanticWorker + TaskEngine pool
  bool enable_fts = true;
  std::shared_ptr<net::HttpTransport> http;  // default: net::make_default_transport()
  std::optional<log::Level> log_level;
  RuntimeParts parts;  // factory overrides; empty = defaults (see RuntimeParts)

  // loom_init_ex options: {"data_dir", "start_workers", "enable_fts",
  // "log_level": "debug|info|warning|error"}. Unknown keys -> InvalidArgument.
  static Result<RuntimeOptions> from_json(const Json& j);
};

class Runtime {
 public:
  static Result<std::unique_ptr<Runtime>> open(const RuntimeOptions& opts = {});
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // Stops workers and closes the database. Idempotent; called by the destructor.
  void shutdown();

  const DataPaths& paths() const;
  const rom::MountRegistry& mounts() const;  // memories in mount order: the read-only ROM first, then the data dir
  Config& config();
  Secrets& secrets();
  Database& db();
  EventBus& bus();
  BlobStore& blobs();
  // Backends + per-category placement (storage_security pack + profiles overlay). Never null.
  storage::StorageService& storage();
  ProvenanceStore& provenance();
  EventLog& event_log();
  TaskEngine& tasks();
  RelationRegistry& relations();
  SemanticAnalyzer& analyzer();
  net::HttpTransport& http();  // stable forwarding proxy
  ModelRegistry& models();
  ProviderRegistry& providers();
  SemanticLLM& semantic_llm();
  MemoryEngine& memory();
  GraphMemorySelector& graph_memory();
  ContextSelector& context();
  GraphEngine& graph();
  ChatEngine& chat();
  SemanticBatchAPI& batch();
  SemanticWorker& worker();
  MediaProviders& media();
  ConversationImporter& importer();
  ConversationExporter& exporter();
  CryptoVault& crypto();
  GitHubSyncManager& github();
  archive::ArchiveIntelligence& archive();  // Project Compiler (archive.h)
  knowledge::KnowledgeEngine& knowledge();  // knowledge pipeline (knowledge.h)
  actions::ActionRunner& actions();         // runs actions: the interpreter bound to this runtime (action_runner.h)

  // Explicit source I/O boundary. Existing get_msgs/get_messages remain storage
  // reads. options = {read_options?: resource_read overrides}; grant belongs to
  // the native host, never to source/descriptor/message data. Always all-status.
  Result<Json> read_conversation_view(std::string_view conversation_id, const Json& options,
                                      bool host_local_read_authorized);

  // Swap the HTTP stack at runtime (platform injection). nullptr restores
  // the default transport. In-flight requests finish on the old transport.
  void set_http_transport(std::shared_ptr<net::HttpTransport> transport);
  // Embedding capability for SelectorEngine tier 1 (nullptr = none).
  void set_embedding_provider(std::shared_ptr<EmbeddingProvider> provider);
  std::shared_ptr<EmbeddingProvider> embedding_provider() const;
  // A fresh selector using the best available tier.
  SelectorEngine make_selector() const;

  // {"version","abi","sqlite","fts5","openssl","data_dir","unicode"}
  static Json build_info();
  Json info() const;

  struct Impl;  // defined in src/runtime.cpp only
  // Use Runtime::open(); public only so std::make_unique can reach it.
  explicit Runtime(std::unique_ptr<Impl> impl);

 private:
  std::unique_ptr<Impl> impl_;
};

// "0.1.0" (CMake project version).
std::string_view version() noexcept;

}  // namespace loom
