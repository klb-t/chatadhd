// Built-in analysis modes (regex / llm / batch) and their registry.
#include "loom/analysis_mode.h"

#include <algorithm>

#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"

namespace loom {
namespace {

class RegexMode final : public AnalysisMode {
 public:
  std::string_view name() const override { return "regex"; }
  bool available(const AnalysisCapabilities&) const override { return true; }
  bool sends_to_provider() const override { return false; }
  Json analyse(std::string_view text, const SemanticAnalyzer& analyzer) override {
    return analyzer.to_unified_profile(analyzer.analyse(text));
  }
};

class LlmMode final : public AnalysisMode {
 public:
  explicit LlmMode(SemanticLLM* llm) : llm_(llm) {}
  std::string_view name() const override { return "llm"; }
  bool available(const AnalysisCapabilities& caps) const override { return llm_ && caps.llm_ready; }
  bool sends_to_provider() const override { return true; }
  // The model call is SemanticLLM::analyse, which goes through the single use_model action.
  Json analyse(std::string_view text, const SemanticAnalyzer& analyzer) override { return llm_->analyse(text, analyzer); }

 private:
  SemanticLLM* llm_;
};

class BatchMode final : public AnalysisMode {
 public:
  std::string_view name() const override { return "batch"; }
  bool available(const AnalysisCapabilities& caps) const override { return caps.llm_ready && caps.batch_ready; }
  bool sends_to_provider() const override { return true; }
  bool bulk() const override { return true; }
  Json analyse(std::string_view, const SemanticAnalyzer&) override { return nullptr; }
  Result<int> run_bulk(AnalysisBulkHost& host) override { return host.submit_batch(); }
};

}  // namespace

AnalysisModeRegistry::AnalysisModeRegistry() {
  entries_.emplace_back("regex", [](const AnalysisModeContext&) { return std::make_unique<RegexMode>(); });
  entries_.emplace_back("llm", [](const AnalysisModeContext& c) { return std::make_unique<LlmMode>(c.llm); });
  entries_.emplace_back("batch", [](const AnalysisModeContext&) { return std::make_unique<BatchMode>(); });
}

AnalysisModeRegistry& AnalysisModeRegistry::instance() {
  static AnalysisModeRegistry r;
  return r;
}

void AnalysisModeRegistry::add(std::string name, AnalysisModeFactory factory) {
  std::lock_guard lk(mu_);
  for (auto& e : entries_)
    if (e.first == name) { e.second = std::move(factory); return; }
  entries_.emplace_back(std::move(name), std::move(factory));
}

void AnalysisModeRegistry::remove(std::string_view name) {
  std::lock_guard lk(mu_);
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e.first == name; }),
                 entries_.end());
}

std::unique_ptr<AnalysisMode> AnalysisModeRegistry::create(std::string_view name, const AnalysisModeContext& ctx) const {
  AnalysisModeFactory f;
  {
    std::lock_guard lk(mu_);
    for (const auto& e : entries_)
      if (e.first == name) { f = e.second; break; }
  }
  return f ? f(ctx) : nullptr;
}

std::vector<std::string> AnalysisModeRegistry::names() const {
  std::lock_guard lk(mu_);
  std::vector<std::string> out;
  for (const auto& e : entries_) out.push_back(e.first);
  return out;
}

std::vector<std::unique_ptr<AnalysisMode>> make_analysis_modes(const std::vector<std::string>& order,
                                                               const AnalysisModeContext& ctx,
                                                               std::vector<std::string>* unknown) {
  std::vector<std::unique_ptr<AnalysisMode>> out;
  for (const auto& n : order) {
    if (auto m = AnalysisModeRegistry::instance().create(n, ctx)) out.push_back(std::move(m));
    else if (unknown) unknown->push_back(n);
  }
  return out;
}

AnalysisMode* pick_message_mode(const std::vector<std::unique_ptr<AnalysisMode>>& modes, const AnalysisCapabilities& caps,
                                std::string_view text) {
  AnalysisMode* last = nullptr;
  for (const auto& m : modes) {
    if (m->bulk() || !m->available(caps)) continue;
    if (m->accepts(text)) return m.get();
    last = m.get();
  }
  return last;
}

AnalysisMode* pick_bulk_mode(const std::vector<std::unique_ptr<AnalysisMode>>& modes, const AnalysisCapabilities& caps) {
  for (const auto& m : modes)
    if (m->bulk() && m->available(caps)) return m.get();
  return nullptr;
}

}  // namespace loom
