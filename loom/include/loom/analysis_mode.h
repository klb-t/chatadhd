// loom/analysis_mode.h — semantic analysis modes as registry entries.
//
// A mode (regex / llm / batch / anything an embedder registers) is an
// AnalysisMode registered by name in AnalysisModeRegistry. Which modes run, and
// in what preference order, is data: worker runtime profile key
// "analysis_modes" (default ["batch","llm","regex"]). For every drain the
// first available bulk mode may take the whole queue; otherwise each message is
// analysed by the first available per-message mode that accepts it, so two
// modes can share one run. Existing config keys (semantic_model,
// semantic_analysis, the batch secret) still decide availability.
#pragma once

#include "loom/adapter_registry.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class SemanticAnalyzer;
class SemanticLLM;

// Facts about this drain that modes use to say whether they apply.
struct AnalysisCapabilities {
  bool llm_ready = false;    // provider usable and config semantic_analysis on
  bool batch_ready = false;  // batch credentials present and queue above the batch threshold
};

// Implemented by the worker; lets a bulk mode hand the queue to the batch transport.
class AnalysisBulkHost {
 public:
  virtual ~AnalysisBulkHost() = default;
  virtual Result<int> submit_batch() = 0;
};

class AnalysisMode {
 public:
  virtual ~AnalysisMode() = default;
  virtual std::string_view name() const = 0;
  virtual bool available(const AnalysisCapabilities& caps) const = 0;
  // True when the analysis leaves the machine (rate limit and usage guard apply).
  virtual bool sends_to_provider() const = 0;
  // A bulk mode takes the whole queue through run_bulk instead of analyse().
  virtual bool bulk() const { return false; }
  // Per-message modes may decline a message; the next available mode takes it.
  virtual bool accepts(std::string_view /*text*/) const { return true; }
  virtual Json analyse(std::string_view text, const SemanticAnalyzer& analyzer) = 0;
  virtual Result<int> run_bulk(AnalysisBulkHost& /*host*/) {
    return Error(Errc::Unavailable, "analysis mode is not a bulk mode");
  }
};

struct AnalysisModeContext {
  SemanticLLM* llm = nullptr;  // may be null: the llm mode is then never available
};

using AnalysisModeFactory = std::function<std::unique_ptr<AnalysisMode>(const AnalysisModeContext&)>;

class AnalysisModeRegistry {
 public:
  static AnalysisModeRegistry& instance();  // pre-registered: regex, llm, batch
  // Replaces an entry of the same name.
  void add(std::string name, AnalysisModeFactory factory);
  void remove(std::string_view name);
  std::unique_ptr<AnalysisMode> create(std::string_view name, const AnalysisModeContext& ctx) const;
  std::vector<std::string> names() const;

 private:
  AnalysisModeRegistry();
  mutable std::mutex mu_;
  AdapterRegistry<AnalysisMode, AnalysisModeContext> reg_{"mode", "analysis mode"};
};

// Instantiates `order` (unknown names are reported through `unknown`, not dropped silently).
std::vector<std::unique_ptr<AnalysisMode>> make_analysis_modes(const std::vector<std::string>& order,
                                                               const AnalysisModeContext& ctx,
                                                               std::vector<std::string>* unknown = nullptr);
// First available per-message mode accepting `text`; if none accepts, the last
// available per-message mode (the catch-all by convention); nullptr if none is available.
AnalysisMode* pick_message_mode(const std::vector<std::unique_ptr<AnalysisMode>>& modes,
                                const AnalysisCapabilities& caps, std::string_view text);
// First available bulk mode, or nullptr.
AnalysisMode* pick_bulk_mode(const std::vector<std::unique_ptr<AnalysisMode>>& modes,
                             const AnalysisCapabilities& caps);

}  // namespace loom
