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
#include <memory>
#include <optional>
#include <string>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class Database;
class EventBus;
class BlobStore;
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

struct RuntimeOptions {
  std::optional<std::string> data_dir;   // default: resolve_data_dir()
  std::optional<PathEnv> path_env;       // tests: injected environment
  bool start_workers = true;             // SemanticWorker + TaskEngine pool
  bool enable_fts = true;
  std::shared_ptr<net::HttpTransport> http;  // default: net::make_default_transport()
  std::optional<log::Level> log_level;

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
  Config& config();
  Secrets& secrets();
  Database& db();
  EventBus& bus();
  BlobStore& blobs();
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
