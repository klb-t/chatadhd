// Archive Intelligence orchestration: every stage is a TaskEngine task
// ("archive.<stage>") whose input hash covers its parameters and the content
// hashes of the stage outputs it reads, so an unchanged re-run is a cache hit
// and an interrupted stage resumes from its checkpoint.
#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <stdexcept>

#include "archive/archive_runtime.h"
#include "archive/profile.h"
#include "loom/archive.h"
#include "loom/db.h"
#include "loom/graph_engine.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/relations.h"
#include "loom/runtime.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/tasks.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::archive {
namespace fs = std::filesystem;

namespace {
constexpr std::string_view kLog = "loom.archive";

Json str_array(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (const auto& s : v) a.push_back(s);
  return a;
}

std::vector<std::string> json_strings(const Json& j) {
  std::vector<std::string> v;
  if (j.is_array()) {
    for (const auto& x : j) {
      if (x.is_string()) v.push_back(x.get<std::string>());
    }
  }
  return v;
}

double round4(double v) { return std::round(v * 10000.0) / 10000.0; }

std::string mime_for(const std::string& name, const ArchiveProfile& profile) {
  for (const auto& rule : profile.value("/output/mime_rules")) {
    const auto suffix = rule.at("suffix").get<std::string>();
    if (name.ends_with(suffix) && (!rule.at("require_prefix").get<bool>() || name.size() > suffix.size()))
      return rule.at("mime").get<std::string>();
  }
  return profile.text("/output/default_mime");
}

const Json& builtin_config_defaults() {
  static const Json defaults = [] {
    auto profile = ArchiveProfile::builtin();
    if (!profile) throw std::logic_error(profile.error().to_string());
    return profile->value("/config_defaults");
  }();
  return defaults;
}
}  // namespace

// ── Config ──────────────────────────────────────────────────────────
ArchiveConfig::ArchiveConfig() {
  const auto& d = builtin_config_defaults();
  sources = json_strings(d.at("sources"));
  if (d.at("repo").is_string()) repo = d.at("repo").get<std::string>();
  code = d.at("code").get<bool>();
  git = d.at("git").get<bool>();
  seed_terms = json_strings(d.at("seed_terms"));
  out_dir = d.at("out_dir").get<std::string>();
  project = d.at("project").get<std::string>();
  max_passes = d.at("max_passes").get<int>();
  max_new_terms = d.at("max_new_terms").get<int>();
  max_hits_per_term = d.at("max_hits_per_term").get<int>();
  max_synthesis_rounds = d.at("max_synthesis_rounds").get<int>();
  llm = d.at("llm").get<std::string>();
  include_db = d.at("include_db").get<bool>();
  exclude = json_strings(d.at("exclude"));
  max_file_bytes = d.at("max_file_bytes").get<std::int64_t>();
  force = d.at("force").get<bool>();
}

Result<ArchiveConfig> ArchiveConfig::from_json(const Json& j) {
  LOOM_TRY_ASSIGN(auto profile, ArchiveProfile::builtin());
  return from_json_with_profile(j, profile);
}

Result<ArchiveConfig> ArchiveConfig::from_json_with_profile(const Json& j, const ArchiveProfile& profile) {
  if (!j.is_null() && !j.is_object()) return Error(Errc::InvalidArgument, "archive config must be a JSON object");
  Json merged = profile.value("/config_defaults");
  if (j.is_object()) for (auto it = j.begin(); it != j.end(); ++it) merged[it.key()] = it.value();
  ArchiveConfig c;
  for (auto it = merged.begin(); it != merged.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    auto strings = [&](std::vector<std::string>& out) -> Status {
      out.clear();
      if (v.is_string()) {
        out.push_back(v.get<std::string>());
        return {};
      }
      if (!v.is_array()) return Error(Errc::InvalidArgument, k + " must be an array of strings");
      for (const auto& x : v) {
        if (!x.is_string()) return Error(Errc::InvalidArgument, k + " must be an array of strings");
        out.push_back(x.get<std::string>());
      }
      return {};
    };
    auto integer = [&](int& out) -> Status {
      if (!v.is_number_integer()) return Error(Errc::InvalidArgument, k + " must be an integer");
      if (v.is_number_unsigned()) {
        if (v.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
          return Error(Errc::InvalidArgument, k + " exceeds native integer representation");
      } else if (const auto n = v.get<std::int64_t>();
                 n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) {
        return Error(Errc::InvalidArgument, k + " exceeds native integer representation");
      }
      out = v.get<int>();
      return {};
    };
    if (k == "sources") {
      LOOM_TRY(strings(c.sources));
    } else if (k == "repo") {
      if (v.is_string()) c.repo = v.get<std::string>();
      else if (v.is_null()) c.repo.reset();
      else return Error(Errc::InvalidArgument, "repo must be a string");
    } else if (k == "code" && v.is_boolean()) {
      c.code = v.get<bool>();
    } else if (k == "git" && v.is_boolean()) {
      c.git = v.get<bool>();
    } else if (k == "seed_terms") {
      LOOM_TRY(strings(c.seed_terms));
    } else if (k == "out_dir" && (v.is_string() || v.is_null())) {
      c.out_dir = v.is_string() ? v.get<std::string>() : "";
    } else if (k == "project" && v.is_string()) {
      c.project = v.get<std::string>();
    } else if (k == "max_passes") {
      LOOM_TRY(integer(c.max_passes));
    } else if (k == "max_new_terms") {
      LOOM_TRY(integer(c.max_new_terms));
    } else if (k == "max_hits_per_term") {
      LOOM_TRY(integer(c.max_hits_per_term));
    } else if (k == "max_synthesis_rounds") {
      LOOM_TRY(integer(c.max_synthesis_rounds));
    } else if (k == "llm" && v.is_string()) {
      c.llm = v.get<std::string>();
      if (c.llm != "off" && c.llm != "auto") return Error(Errc::InvalidArgument, "llm must be \"off\" or \"auto\"");
    } else if (k == "include_db" && v.is_boolean()) {
      c.include_db = v.get<bool>();
    } else if (k == "exclude") {
      LOOM_TRY(strings(c.exclude));
    } else if (k == "max_file_bytes" && v.is_number_integer()) {
      if (v.is_number_unsigned() && v.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return Error(Errc::InvalidArgument, k + " exceeds native integer representation");
      c.max_file_bytes = v.get<std::int64_t>();
    } else if (k == "force" && v.is_boolean()) {
      c.force = v.get<bool>();
    } else {
      return Error(Errc::InvalidArgument, "unknown or invalid archive option: " + k);
    }
  }
  auto bounds = [&](std::string_view name, int value) {
    const auto& bound = profile.value("/config_bounds").at(std::string(name));
    value = std::max(value, bound.at("minimum").get<int>());
    if (!bound.at("maximum").is_null()) value = std::min(value, bound.at("maximum").get<int>());
    return value;
  };
  c.max_passes = bounds("max_passes", c.max_passes);
  c.max_new_terms = bounds("max_new_terms", c.max_new_terms);
  c.max_hits_per_term = bounds("max_hits_per_term", c.max_hits_per_term);
  c.max_synthesis_rounds = bounds("max_synthesis_rounds", c.max_synthesis_rounds);
  return c;
}

Json ArchiveConfig::to_json() const {
  return Json{{"sources", str_array(sources)},
              {"repo", repo ? Json(*repo) : Json(nullptr)},
              {"code", code},
              {"git", git},
              {"seed_terms", str_array(seed_terms)},
              {"out_dir", out_dir},
              {"project", project},
              {"max_passes", max_passes},
              {"max_new_terms", max_new_terms},
              {"max_hits_per_term", max_hits_per_term},
              {"max_synthesis_rounds", max_synthesis_rounds},
              {"llm", llm},
              {"include_db", include_db},
              {"exclude", str_array(exclude)},
              {"max_file_bytes", max_file_bytes},
              {"force", force}};
}

Json StageRun::to_json() const {
  return Json{{"stage", stage},           {"task_id", task_id}, {"input_hash", input_hash},
              {"output_hash", output_hash}, {"cache_hit", cache_hit}, {"resumed", resumed}, {"stats", stats}};
}

Json ArchiveRunResult::to_json() const {
  Json s = Json::array();
  for (const auto& st : stages) s.push_back(st.to_json());
  return Json{{"run_id", run_id}, {"status", status}, {"stages", s}, {"summary", summary}};
}

// ── State ───────────────────────────────────────────────────────────
struct ArchiveIntelligence::State {
  std::optional<ArchiveProfile> profile;
  std::optional<Error> profile_error;
  std::mutex run_mu;  // one orchestration at a time
  std::mutex mu;      // guards the fields below
  ArchiveProgressFn progress;
  const CancelToken* cancel = nullptr;
  TaskContext* run_ctx = nullptr;
  std::optional<ArchiveConfig> pending_cfg;
  std::map<std::string, std::shared_ptr<const Corpus>> corpus_cache;
  std::map<std::string, std::shared_ptr<const CorpusStats>> stats_cache;
  std::map<std::string, std::shared_ptr<const std::unordered_map<std::string, std::string>>> msg_key_cache;

  void notify(std::string_view stage, std::int64_t cur, std::int64_t total, std::string_view msg) {
    ArchiveProgressFn fn;
    {
      std::lock_guard lk(mu);
      fn = progress;
    }
    if (fn) fn(stage, cur, total, msg);
  }
  bool stop_requested() {
    std::lock_guard lk(mu);
    if (cancel && cancel->cancelled()) return true;
    return run_ctx && run_ctx->should_stop();
  }
};

namespace {

Result<std::string> put_json(Runtime& rt, const Json& j) {
  LOOM_TRY_ASSIGN(BlobRef ref, rt.blobs().put(json::dump(j), "application/json"));
  return ref.hash;
}

Result<Json> get_json(Runtime& rt, std::string_view hash) {
  LOOM_TRY_ASSIGN(std::string bytes, rt.blobs().read(hash));
  return json::parse(bytes);
}

// Optional LLM refinement of low-confidence items (never required; changes
// the output, so only when llm = "auto" and a key + model are configured).
int refine_items_llm(Runtime& rt, std::vector<Item>& items, const ArchiveProfile& policy) {
  std::string key = rt.secrets().get_string("api_key");
  Json base_j = rt.config().get("base_url");
  std::string base = base_j.is_string() ? base_j.get<std::string>() : "";
  std::string model;
  for (const auto& key_name : policy.value("/refinement/model_keys")) {
    const auto& k = key_name.get_ref<const std::string&>();
    Json v = rt.config().get(k);
    if (v.is_string() && !v.get<std::string>().empty()) {
      model = v.get<std::string>();
      break;
    }
  }
  if (key.empty() || base.empty() || model.empty()) return 0;
  while (!base.empty() && base.back() == '/') base.pop_back();
  std::vector<std::size_t> low;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].confidence < policy.number("/refinement/confidence_threshold") && !policy.contains("/refinement/excluded_types", items[i].type)) low.push_back(i);
  }
  int changed = 0;
  for (std::size_t b = 0; b < low.size() && b < static_cast<std::size_t>(policy.integer("/refinement/max_items")); b += static_cast<std::size_t>(policy.integer("/refinement/batch_size"))) {
    std::string prompt = policy.text("/refinement/prompt");
    for (std::size_t k = b; k < low.size() && k < b + static_cast<std::size_t>(policy.integer("/refinement/batch_size")) && k < static_cast<std::size_t>(policy.integer("/refinement/max_items")); ++k) {
      prompt += std::to_string(k) + ". " + clip(items[low[k]].text, static_cast<std::size_t>(policy.integer("/refinement/max_item_codepoints"))) + "\n";
    }
    net::HttpRequest req;
    req.method = "POST";
    req.url = base + "/chat/completions";
    req.headers = {{"Authorization", "Bearer " + key}, {"Content-Type", "application/json"},
                   {"X-Title", policy.text("/refinement/title")}};
    req.body = json::dump(Json{{"model", model},
                               {"messages", Json::array({Json{{"role", "user"}, {"content", prompt}}})},
                               {"temperature", policy.value("/refinement/temperature")},
                               {"max_tokens", policy.integer("/refinement/max_tokens")}});
    req.timeout_ms = policy.integer("/refinement/timeout_ms");
    auto resp = rt.http().send(req);
    if (!resp || resp->status != 200) {
      log::warn(kLog, "item refinement skipped: {}", resp ? "HTTP " + std::to_string(resp->status) : resp.error().message);
      break;
    }
    auto j = resp->json();
    if (!j) break;
    std::string raw;
    try {
      raw = j->at("choices").at(0).at("message").at("content").get<std::string>();
    } catch (const std::exception&) {
      break;
    }
    auto parsed = SemanticLLM::parse_response_json(raw);
    if (!parsed || !parsed->is_array()) continue;
    for (const auto& e : *parsed) {
      std::int64_t i = json::get_int(e, "i", -1);
      std::string type = json::get_string(e, "type");
      if (i < 0 || static_cast<std::size_t>(i) >= low.size()) continue;
      Item& it = items[low[static_cast<std::size_t>(i)]];
      if (type == "none") {
        it.type = "";
      } else if (policy.contains("/items/types", type)) {
        if (type != it.type) ++changed;
        it.type = type;
        it.confidence = std::max(it.confidence, policy.number("/refinement/confidence_floor"));
        it.cues.push_back("llm");
      }
    }
  }
  items.erase(std::remove_if(items.begin(), items.end(), [](const Item& it) { return it.type.empty(); }), items.end());
  return changed;
}

}  // namespace

// ── Stage implementations ───────────────────────────────────────────
class StageRunner {
 public:
  StageRunner(Runtime& rt, ArchiveIntelligence::State& st) : rt_(rt), st_(st), policy_(*st.profile) {}
  const ArchiveProfile& policy() const { return policy_; }

  Result<std::shared_ptr<const Corpus>> corpus(const std::string& hash) {
    {
      std::lock_guard lk(st_.mu);
      if (auto it = st_.corpus_cache.find(hash); it != st_.corpus_cache.end()) return it->second;
    }
    LOOM_TRY_ASSIGN(Json j, get_json(rt_, hash));
    auto c = std::make_shared<const Corpus>(Corpus::from_json(j));
    std::lock_guard lk(st_.mu);
    if (st_.corpus_cache.size() > static_cast<std::size_t>(policy_.integer("/pipeline/corpus_cache_entries"))) st_.corpus_cache.clear();
    st_.corpus_cache[hash] = c;
    return c;
  }
  Result<std::shared_ptr<const CorpusStats>> stats(const std::string& hash) {
    {
      std::lock_guard lk(st_.mu);
      if (auto it = st_.stats_cache.find(hash); it != st_.stats_cache.end()) return it->second;
    }
    LOOM_TRY_ASSIGN(auto c, corpus(hash));
    auto s = std::make_shared<const CorpusStats>(compute_stats(*c, &policy_));
    std::lock_guard lk(st_.mu);
    if (st_.stats_cache.size() > static_cast<std::size_t>(policy_.integer("/pipeline/stats_cache_entries"))) st_.stats_cache.clear();
    st_.stats_cache[hash] = s;
    return s;
  }
  Result<std::shared_ptr<const std::unordered_map<std::string, std::string>>> msg_keys(const std::string& bindings) {
    {
      std::lock_guard lk(st_.mu);
      if (auto it = st_.msg_key_cache.find(bindings); it != st_.msg_key_cache.end()) return it->second;
    }
    LOOM_TRY_ASSIGN(Json b, get_json(rt_, bindings));
    auto m = std::make_shared<std::unordered_map<std::string, std::string>>();
    for (auto it = b.begin(); it != b.end(); ++it) (*m)[json::get_string(it.value(), "msg")] = it.key();
    std::lock_guard lk(st_.mu);
    st_.msg_key_cache[bindings] = m;
    return std::shared_ptr<const std::unordered_map<std::string, std::string>>(m);
  }

  // Each returns {"output": blob, "log": {deterministic}, "stats": {...}, ...}
  Result<Json> ingest(const Json& p, StageControl& ctl);
  Result<Json> retrieve(const Json& p, StageControl& ctl);
  Result<Json> expand(const Json& p, StageControl& ctl);
  Result<Json> graph(const Json& p, StageControl& ctl);
  Result<Json> cluster(const Json& p, StageControl& ctl);
  Result<Json> timeline(const Json& p, StageControl& ctl);
  Result<Json> items(const Json& p, StageControl& ctl);
  Result<Json> relate(const Json& p, StageControl& ctl);
  Result<Json> synthesize_stage(const Json& p, StageControl& ctl);
  Result<Json> materialize(const Json& p, StageControl& ctl, const std::string& task_id);

 private:
  Runtime& rt_;
  ArchiveIntelligence::State& st_;
  const ArchiveProfile& policy_;
};

Result<Json> StageRunner::ingest(const Json& p, StageControl& ctl) {
  Plan plan = Plan::from_json(p["plan"]);
  LOOM_TRY_ASSIGN(IngestResult res, run_ingest(rt_, plan, ctl, &policy_));
  LOOM_TRY_ASSIGN(std::string ch, put_json(rt_, res.corpus.to_json()));
  LOOM_TRY_ASSIGN(std::string bh, put_json(rt_, res.bindings));
  Json log{{"docs", res.corpus.docs.size()}, {"sources", res.corpus.sources.size()}, {"forks", res.corpus.forks.size()}};
  return Json{{"output", ch}, {"bindings", bh}, {"log", log}, {"stats", res.stats}};
}

Result<Json> StageRunner::retrieve(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(auto keys, msg_keys(json::get_string(p, "bindings")));
  auto terms = json_strings(p["terms"]);
  int max_hits = static_cast<int>(json::get_int(p, "max_hits", policy_.integer("/pipeline/standalone_hits")));
  struct Agg {
    double score = 0;
    std::vector<std::string> terms;
  };
  std::map<std::string, Agg> agg;
  std::string mode;
  Json per_term = Json::object();
  std::int64_t done = 0;
  for (const auto& term : terms) {
    if (ctl.should_stop && ctl.should_stop()) return Error(Errc::Paused, "retrieval paused");
    if (ctl.progress) ctl.progress(done++, static_cast<std::int64_t>(terms.size()), term);
    SearchOptions so;
    so.limit = policy_.integer("/pipeline/search_query_limit");
    so.include_inactive = true;
    LOOM_TRY_ASSIGN(SearchResult r, rt_.db().search_messages(term, so));
    mode = r.mode;
    std::vector<std::pair<double, std::string>> hits;
    std::set<std::string> seen;
    for (const auto& h : r.hits) {
      auto it = keys->find(h.message.id);
      if (it == keys->end() || !c->find(it->second) || !seen.insert(it->second).second) continue;
      hits.emplace_back(round4(h.score), it->second);
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first > b.first;
      return a.second < b.second;
    });
    if (static_cast<int>(hits.size()) > max_hits) hits.resize(static_cast<std::size_t>(max_hits));
    double top = hits.empty() ? 1.0 : std::max(hits.front().first, 1e-9);
    for (const auto& [s, k] : hits) {
      auto& a = agg[k];
      a.score += mode == "fts5" ? s / top : 1.0;
      a.terms.push_back(term);
    }
    per_term[term] = hits.size();
  }
  // Unit completion: when a large share of a conversation / document is
  // relevant, the rest of it is context for the hits (decisions refer back).
  std::size_t completed = 0;
  {
    std::map<std::string, std::vector<const Doc*>> units;
    for (const auto& d : c->docs) units[d.unit].push_back(&d);
    for (const auto& [u, ds] : units) {
      if (ds.size() < static_cast<std::size_t>(policy_.integer("/pipeline/unit_min_docs")) || ds.size() > static_cast<std::size_t>(policy_.integer("/pipeline/unit_max_docs"))) continue;
      std::size_t n = 0;
      for (const Doc* d : ds) n += agg.count(d->key);
      if (n == 0 || static_cast<double>(n) < static_cast<double>(ds.size()) * policy_.number("/pipeline/unit_relevance_fraction")) continue;
      for (const Doc* d : ds) {
        if (agg.count(d->key)) continue;
        agg[d->key].terms.push_back("(context)");
        ++completed;
      }
    }
  }
  std::vector<std::pair<double, std::string>> order;
  for (const auto& [k, a] : agg) order.emplace_back(round4(a.score), k);
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first > b.first;
    return a.second < b.second;
  });
  Json hits = Json::array();
  for (const auto& [s, k] : order) hits.push_back(Json{{"key", k}, {"score", s}, {"terms", str_array(agg[k].terms)}});
  Json out{{"pass", json::get_int(p, "pass")}, {"terms", str_array(terms)}, {"per_term", per_term}, {"hits", hits}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh},
              {"log", Json{{"terms", terms.size()}, {"hits", hits.size()}, {"unit_context", completed}}},
              {"stats", Json{{"mode", mode}}}};
}

Result<Json> StageRunner::expand(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(auto s, stats(ch));
  LOOM_TRY_ASSIGN(Json r, get_json(rt_, json::get_string(p, "hits")));
  std::vector<std::size_t> hits;
  for (const auto& h : r["hits"]) {
    auto it = c->index.find(json::get_string(h, "key"));
    if (it != c->index.end()) hits.push_back(it->second);
  }
  std::sort(hits.begin(), hits.end());
  // Vocabulary comes from prose (docs, conversations) when there is enough of
  // it; code and commits are evidence, not the project's language.
  std::vector<std::size_t> prose;
  for (std::size_t i : hits) {
    const std::string& k = c->docs[i].kind;
    if (!policy_.contains("/pipeline/prose_excluded_kinds", k)) prose.push_back(i);
  }
  if (prose.size() >= static_cast<std::size_t>(policy_.integer("/pipeline/prose_preference_threshold"))) hits = prose;
  auto vocab_v = json_strings(p["vocab"]);
  std::set<std::string> vocab(vocab_v.begin(), vocab_v.end());
  if (ctl.progress) ctl.progress(0, 1, "salient terms of " + std::to_string(hits.size()) + " hits");
  auto added = expand_vocabulary(*c, *s, hits, vocab, static_cast<int>(json::get_int(p, "pass")),
                                 static_cast<int>(json::get_int(p, "max_new", policy_.integer("/pipeline/standalone_max_new_terms"))), &rt_.analyzer(), &policy_);
  Json a = Json::array();
  for (const auto& t : added) a.push_back(t.to_json(&policy_));
  // provenance: why each term was added (evidence = source messages)
  LOOM_TRY_ASSIGN(auto keys, msg_keys(json::get_string(p, "bindings")));
  std::unordered_map<std::string, std::string> key_msg;
  for (const auto& [m, k] : *keys) key_msg[k] = m;
  for (const auto& t : added) {
    ProvenanceRecord pr;
    pr.subject_id = "term:" + t.term;
    pr.subject_kind = "archive_term";
    pr.locator = Json{{"pass", t.pass}, {"reasons", str_array(t.reasons)}, {"evidence", str_array(t.evidence)}};
    Json msgs = Json::array();
    for (const auto& e : t.evidence) {
      if (key_msg.count(e)) msgs.push_back(key_msg[e]);
    }
    pr.locator["messages"] = msgs;
    pr.transform = "archive.expand@" + std::string(kPipelineVersion);
    pr.confidence = std::min(1.0, t.score);
    if (auto st = rt_.provenance().add(pr); !st) log::warn(kLog, "term provenance: {}", st.error().message);
  }
  Json out{{"pass", json::get_int(p, "pass")}, {"added", a}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh}, {"log", Json{{"added", added.size()}}}, {"stats", Json::object()}};
}

Result<Json> StageRunner::graph(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(auto s, stats(ch));
  LOOM_TRY_ASSIGN(Json r, get_json(rt_, json::get_string(p, "hits")));
  LOOM_TRY_ASSIGN(Json bindings, get_json(rt_, json::get_string(p, "bindings")));
  auto vocab_v = json_strings(p["vocab"]);
  bool use_llm = json::get_string(p, "llm") == "auto" && rt_.semantic_llm().enabled();
  Json docs = Json::array();
  std::int64_t analysed = 0, skipped = 0, i = 0;
  const std::int64_t total = static_cast<std::int64_t>(r["hits"].size());
  std::vector<std::string> keys;
  for (const auto& h : r["hits"]) keys.push_back(json::get_string(h, "key"));
  std::sort(keys.begin(), keys.end());
  for (const auto& key : keys) {
    if (i % policy_.integer("/pipeline/graph_poll_interval") == 0) {
      if (ctl.should_stop && ctl.should_stop()) return Error(Errc::Paused, "graph build paused");
      if (ctl.progress) ctl.progress(i, total, key);
    }
    ++i;
    auto it = c->index.find(key);
    if (it == c->index.end()) continue;
    const Doc& d = c->docs[it->second];
    Analysis a = rt_.analyzer().analyse(utf8::prefix(d.text, static_cast<std::size_t>(policy_.integer("/pipeline/analysis_max_codepoints"))));
    // knowledge graph (DB): idempotent upserts, skipped when already analysed
    std::string msg = json::get_string(bindings[key], "msg"), conv = json::get_string(bindings[key], "conv");
    if (!msg.empty()) {
      auto m = rt_.db().get_msg(msg);
      if (m && *m && (*m)->semantic_status != "done") {
        Json unified = use_llm ? rt_.semantic_llm().analyse(d.text) : SemanticAnalyzer::to_unified(a);
        rt_.graph().ingest_analysis(msg, conv, unified);
        (void)rt_.db().mark_analysed(msg, unified);
        ++analysed;
      } else {
        ++skipped;
      }
    }
    Json terms = Json::array();
    std::set<std::string> have;
    for (const auto& [t, w] : salient_terms(*s, it->second, policy_.integer("/pipeline/graph_salient_terms"), &policy_)) {
      terms.push_back(Json::array({t, round4(w)}));
      have.insert(t);
    }
    const auto& tf = s->docs[it->second].tf;
    for (const auto& v : vocab_v) {
      if (have.count(v) || !tf.count(v)) continue;
      terms.push_back(Json::array({v, policy_.number("/pipeline/fallback_term_weight")}));
    }
    Json ents = Json::array();
    std::set<std::string> seen;
    for (const auto& e : a.entities) {
      if (ents.size() >= static_cast<std::size_t>(policy_.integer("/pipeline/graph_max_entities"))) break;
      std::string k = e.entity_type + "|" + utf8::to_lower(e.text);
      if (!seen.insert(k).second) continue;
      ents.push_back(Json::array({e.text, e.entity_type}));
    }
    docs.push_back(Json{{"key", key}, {"terms", terms}, {"entities", ents}});
  }
  Json out{{"docs", docs}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh},
              {"log", Json{{"docs", docs.size()}}},
              {"stats", Json{{"analysed", analysed}, {"already_analysed", skipped}, {"llm", use_llm}}}};
}

Result<Json> StageRunner::cluster(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(Json g, get_json(rt_, json::get_string(p, "graph")));
  LOOM_TRY_ASSIGN(Json r, get_json(rt_, json::get_string(p, "hits")));
  if (ctl.progress) ctl.progress(0, 1, "community detection");
  std::map<std::string, double> hit_score;
  for (const auto& h : r["hits"]) hit_score[json::get_string(h, "key")] = json::get_number(h, "score");
  const Json& docs = g["docs"];
  const std::size_t h = docs.size();
  std::map<std::string, int> df;
  for (const auto& d : docs) {
    for (const auto& t : d["terms"]) ++df[t[0].get<std::string>()];
  }
  std::vector<std::pair<int, std::string>> global;
  std::map<std::string, int> node;
  for (const auto& [t, n] : df) {
    if (h >= static_cast<std::size_t>(policy_.integer("/pipeline/global_min_docs")) && n > policy_.number("/pipeline/global_df_fraction") * static_cast<double>(h)) {
      global.emplace_back(-n, t);
    } else if (n >= policy_.integer("/pipeline/cluster_min_df")) {
      node.emplace(t, 0);
    }
  }
  int idx = 0;
  std::vector<std::string> names;
  for (auto& [t, i] : node) {
    i = idx++;
    names.push_back(t);
  }
  std::map<std::pair<int, int>, double> w;
  for (const auto& d : docs) {
    std::vector<int> ids;
    for (const auto& t : d["terms"]) {
      auto it = node.find(t[0].get<std::string>());
      if (it != node.end()) ids.push_back(it->second);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (std::size_t a = 0; a < ids.size(); ++a) {
      for (std::size_t b = a + 1; b < ids.size(); ++b) w[{ids[a], ids[b]}] += 1.0;
    }
  }
  std::vector<std::tuple<int, int, double>> edges;
  for (const auto& [ab, x] : w) edges.emplace_back(ab.first, ab.second, x);
  std::vector<int> comm = louvain(idx, edges, -1, &policy_);
  int ncomm = comm.empty() ? 0 : *std::max_element(comm.begin(), comm.end()) + 1;
  // weighted degree inside the community
  std::vector<double> deg(static_cast<std::size_t>(idx), 0.0);
  for (const auto& [a, b, x] : edges) {
    if (comm[a] == comm[b]) {
      deg[a] += x;
      deg[b] += x;
    }
  }
  // assign docs
  std::vector<std::vector<std::string>> cdocs(static_cast<std::size_t>(ncomm));
  std::vector<std::string> other;
  for (const auto& d : docs) {
    std::vector<double> score(static_cast<std::size_t>(ncomm), 0.0);
    for (const auto& t : d["terms"]) {
      auto it = node.find(t[0].get<std::string>());
      if (it != node.end()) score[comm[it->second]] += t[1].get<double>();
    }
    int best = -1;
    double bs = 0;
    for (int k = 0; k < ncomm; ++k) {
      if (score[k] > bs + policy_.number("/clustering/gain_tolerance")) {
        bs = score[k];
        best = k;
      }
    }
    std::string key = json::get_string(d, "key");
    if (best < 0) other.push_back(key);
    else cdocs[best].push_back(key);
  }
  struct Theme {
    std::string label;
    std::vector<std::string> terms;
    std::vector<std::string> docs;
  };
  std::vector<Theme> themes;
  for (int k = 0; k < ncomm; ++k) {
    if (cdocs[k].size() < static_cast<std::size_t>(policy_.integer("/pipeline/cluster_min_docs"))) {
      for (auto& x : cdocs[k]) other.push_back(std::move(x));
      continue;
    }
    std::vector<std::pair<double, std::string>> tv;
    for (int i = 0; i < idx; ++i) {
      if (comm[i] == k) tv.emplace_back(-deg[i], names[i]);
    }
    std::sort(tv.begin(), tv.end());
    Theme t;
    for (std::size_t i = 0; i < tv.size() && i < static_cast<std::size_t>(policy_.integer("/pipeline/theme_terms")); ++i) t.terms.push_back(tv[i].second);
    // label: top terms, skipping words already contained in a chosen phrase
    std::vector<std::string> lab;
    for (const auto& term : t.terms) {
      if (lab.size() >= static_cast<std::size_t>(policy_.integer("/pipeline/label_terms"))) break;
      bool dup = false;
      for (const auto& l : lab) {
        if (l.find(term) != std::string::npos || term.find(l) != std::string::npos) dup = true;
      }
      if (!dup) lab.push_back(term);
      if (lab.size() == static_cast<std::size_t>(policy_.integer("/pipeline/label_terms"))) break;
    }
    for (std::size_t i = 0; i < lab.size(); ++i) t.label += (i ? " · " : "") + lab[i];
    t.docs = std::move(cdocs[k]);
    themes.push_back(std::move(t));
  }
  std::sort(themes.begin(), themes.end(), [](const Theme& a, const Theme& b) {
    if (a.docs.size() != b.docs.size()) return a.docs.size() > b.docs.size();
    return a.label < b.label;
  });
  const std::size_t kMaxThemes = static_cast<std::size_t>(policy_.integer("/pipeline/max_themes"));
  while (themes.size() > kMaxThemes) {
    for (auto& x : themes.back().docs) other.push_back(std::move(x));
    themes.pop_back();
  }
  if (!other.empty()) {
    Theme o;
    o.label = "other";
    o.docs = std::move(other);
    themes.push_back(std::move(o));
  }
  Json tj = Json::array();
  Json doc_theme = Json::object();
  for (std::size_t i = 0; i < themes.size(); ++i) {
    auto& t = themes[i];
    std::string id = "T" + std::to_string(i + 1);
    std::sort(t.docs.begin(), t.docs.end(), [&](const std::string& a, const std::string& b) {
      if (hit_score[a] != hit_score[b]) return hit_score[a] > hit_score[b];
      return a < b;
    });
    for (const auto& k : t.docs) doc_theme[k] = id;
    tj.push_back(Json{{"id", id}, {"label", t.label}, {"terms", str_array(t.terms)}, {"size", t.docs.size()},
                      {"docs", str_array(t.docs)}});
  }
  std::sort(global.begin(), global.end());
  Json gj = Json::array();
  for (std::size_t i = 0; i < global.size() && i < static_cast<std::size_t>(policy_.integer("/pipeline/global_terms")); ++i) gj.push_back(global[i].second);
  (void)c;
  Json out{{"themes", tj}, {"doc_theme", doc_theme}, {"global_terms", gj}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh}, {"log", Json{{"themes", tj.size()}, {"terms", idx}}}, {"stats", Json::object()}};
}

Result<Json> StageRunner::timeline(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(Json cl, get_json(rt_, json::get_string(p, "cluster")));
  if (ctl.progress) ctl.progress(0, 1, "timelines");
  Json themes = Json::array();
  for (const auto& t : cl["themes"]) {
    std::vector<const Doc*> ds;
    std::set<std::string> keys, units;
    for (const auto& k : t["docs"]) {
      if (const Doc* d = c->find(k.get<std::string>())) {
        ds.push_back(d);
        keys.insert(d->key);
        units.insert(d->unit);
      }
    }
    std::sort(ds.begin(), ds.end(), [](const Doc* a, const Doc* b) {
      std::string da = a->date.empty() ? "9999" : a->date;
      std::string db = b->date.empty() ? "9999" : b->date;
      return std::tie(da, a->unit, a->ordinal, a->key) < std::tie(db, b->unit, b->ordinal, b->key);
    });
    Json ev = Json::array();
    std::string first, last;
    int commits = 0;
    for (const Doc* d : ds) {
      std::string day = date_only(d->date);
      ev.push_back(Json{{"date", day}, {"key", d->key}, {"kind", d->kind}});
      if (!day.empty()) {
        if (first.empty()) first = day;
        last = day;
      }
      commits += d->kind == "commit";
    }
    Json forks = Json::array();
    for (const auto& f : c->forks) {
      bool in = keys.count(json::get_string(f, "after")) > 0;
      for (const auto& a : f["alternatives"]) in = in || keys.count(json::get_string(a, "first"));
      if (in) forks.push_back(f);
    }
    themes.push_back(Json{{"id", t["id"]}, {"label", t["label"]}, {"first", first}, {"last", last},
                          {"commits", commits}, {"events", ev}, {"forks", forks}});
  }
  Json out{{"themes", themes}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh}, {"log", Json{{"themes", themes.size()}}}, {"stats", Json::object()}};
}

Result<Json> StageRunner::items(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(Json cl, get_json(rt_, json::get_string(p, "cluster")));
  const Json& dt = cl["doc_theme"];
  std::vector<Item> all;
  std::int64_t i = 0;
  const std::int64_t total = static_cast<std::int64_t>(c->docs.size());
  for (const auto& d : c->docs) {
    if (i++ % policy_.integer("/pipeline/items_poll_interval") == 0) {
      if (ctl.should_stop && ctl.should_stop()) return Error(Errc::Paused, "item extraction paused");
      if (ctl.progress) ctl.progress(i, total, d.key);
    }
    if (!dt.contains(d.key)) continue;  // only retrieved (relevant) documents
    for (auto& it : extract_items(d, dt[d.key].get<std::string>(), &policy_)) all.push_back(std::move(it));
  }
  int refined = 0;
  if (json::get_string(p, "llm") == "auto") refined = refine_items_llm(rt_, all, policy_);
  Json a = Json::array();
  std::map<std::string, int> by_type;
  for (const auto& it : all) {
    a.push_back(it.to_json());
    ++by_type[it.type];
  }
  Json bt = Json::object();
  for (const auto& [t, n] : by_type) bt[t] = n;
  Json out{{"items", a}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh}, {"log", Json{{"items", a.size()}, {"by_type", bt}}}, {"stats", Json{{"llm_refined", refined}}}};
}

Result<Json> StageRunner::relate(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto s, stats(ch));
  LOOM_TRY_ASSIGN(Json ij, get_json(rt_, json::get_string(p, "items")));
  if (ctl.progress) ctl.progress(0, 1, "supersession / contradiction");
  std::vector<Item> items;
  for (const auto& x : ij["items"]) items.push_back(Item::from_json(x));
  auto edges = relate_items(items, s.get(), &policy_);
  Json a = Json::array(), e = Json::array();
  for (const auto& it : items) a.push_back(it.to_json());
  std::map<std::string, int> by_type;
  for (const auto& ed : edges) {
    e.push_back(ed.to_json());
    ++by_type[ed.type];
  }
  Json bt = Json::object();
  for (const auto& [t, n] : by_type) bt[t] = n;
  Json out{{"items", a}, {"edges", e}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh}, {"log", Json{{"edges", e.size()}, {"by_type", bt}}}, {"stats", Json::object()}};
}

Result<Json> StageRunner::synthesize_stage(const Json& p, StageControl& ctl) {
  std::string ch = json::get_string(p, "corpus");
  LOOM_TRY_ASSIGN(auto c, corpus(ch));
  LOOM_TRY_ASSIGN(auto s, stats(ch));
  if (ctl.progress) ctl.progress(0, 1, "rendering artifacts");
  LOOM_TRY_ASSIGN(Json vocab, get_json(rt_, json::get_string(p, "vocab")));
  LOOM_TRY_ASSIGN(Json hits, get_json(rt_, json::get_string(p, "hits")));
  LOOM_TRY_ASSIGN(Json cl, get_json(rt_, json::get_string(p, "cluster")));
  LOOM_TRY_ASSIGN(Json tl, get_json(rt_, json::get_string(p, "timeline")));
  LOOM_TRY_ASSIGN(Json rel, get_json(rt_, json::get_string(p, "relate")));
  SynthesisInput in;
  in.profile = &policy_;
  in.corpus = c.get();
  in.stats = s.get();
  in.project = json::get_string(p, "project");
  for (const auto& v : vocab["terms"]) in.vocab.push_back(TermRecord::from_json(v));
  in.passes = vocab["passes"];
  in.hits = hits["hits"];
  in.themes = cl["themes"];
  in.doc_theme = cl["doc_theme"];
  in.global_terms = cl["global_terms"];
  in.timeline = tl["themes"];
  for (const auto& x : rel["items"]) in.items.push_back(Item::from_json(x));
  for (const auto& x : rel["edges"]) {
    in.edges.push_back({json::get_string(x, "src"), json::get_string(x, "dst"), json::get_string(x, "type"),
                        json::get_string(x, "reason")});
  }
  in.round = static_cast<int>(json::get_int(p, "round"));
  in.settings = p["settings"];
  SynthesisOutput so = synthesize(in);
  Json files = Json::object();
  for (const auto& [name, content] : so.files) {
    LOOM_TRY_ASSIGN(BlobRef ref, rt_.blobs().put(content, mime_for(name, policy_)));
    files[name] = ref.hash;
  }
  Json out{{"files", files}, {"discovered_terms", str_array(so.discovered_terms)}, {"gap", so.gap["summary"]}};
  LOOM_TRY_ASSIGN(std::string oh, put_json(rt_, out));
  return Json{{"output", oh},
              {"log", Json{{"files", files.size()}, {"discovered_terms", so.discovered_terms.size()}}},
              {"stats", Json::object()}};
}

Result<Json> StageRunner::materialize(const Json& p, StageControl& ctl, const std::string& task_id) {
  LOOM_TRY_ASSIGN(Json syn, get_json(rt_, json::get_string(p, "synthesis")));
  LOOM_TRY_ASSIGN(Json rel, get_json(rt_, json::get_string(p, "relate")));
  LOOM_TRY_ASSIGN(Json cl, get_json(rt_, json::get_string(p, "cluster")));
  LOOM_TRY_ASSIGN(Json bindings, get_json(rt_, json::get_string(p, "bindings")));
  std::string run_id = json::get_string(p, "run_id");
  std::string project = json::get_string(p, "project");
  std::string out_dir = json::get_string(p, "out_dir");
  Json files = syn["files"];
  files[policy_.text("/output/task_log_file")] = json::get_string(p, "task_log");
  if (ctl.progress) ctl.progress(0, 2, "artifacts");
  fs::path od;
  if (!out_dir.empty()) {
    od = fsutil::expand_user(out_dir);
    LOOM_TRY(fsutil::ensure_dir(od));
    LOOM_TRY(fsutil::atomic_write(od / policy_.text("/output/marker_file"), policy_.text("/output/marker_text")));
  }
  Json artifacts = Json::array();
  for (auto it = files.begin(); it != files.end(); ++it) {
    const std::string& name = it.key();
    std::string hash = it.value().get<std::string>();
    ArtifactRecord ar;
    ar.kind = "archive." + name.substr(0, name.find('.'));
    ar.title = name;
    ar.blob_hash = hash;
    ar.mime = mime_for(name, policy_);
    ar.task_id = run_id.empty() ? task_id : run_id;
    ar.metadata = Json{{"project", project}, {"run_id", run_id}, {"file", name}};
    LOOM_TRY_ASSIGN(std::string aid, rt_.provenance().add_artifact(ar));
    ProvenanceRecord pr;
    pr.subject_id = aid;
    pr.subject_kind = "artifact";
    pr.locator = Json{{"run_id", run_id}, {"file", name}};
    pr.transform = "archive.synthesize@" + std::string(kPipelineVersion);
    (void)rt_.provenance().add(pr);
    if (!od.empty()) {
      LOOM_TRY_ASSIGN(std::string content, rt_.blobs().read(hash));
      LOOM_TRY(fsutil::atomic_write(od / name, content, {.fsync = false, .owner_only = false}));
    }
    artifacts.push_back(Json{{"id", aid}, {"name", name}, {"hash", hash}});
  }
  // Knowledge graph: themes and typed items as nodes, typed edges between items.
  if (ctl.progress) ctl.progress(1, 2, "graph nodes");
  Database& db = rt_.db();
  std::map<std::string, std::string> theme_node, item_node;
  for (const auto& t : cl["themes"]) {
    std::string label = json::get_string(t, "label");
    NodeOptions no;
    no.metadata = Json{{"archive_theme", t["id"]}, {"project", project}};
    no.tags = t["terms"];
    LOOM_TRY_ASSIGN(std::string nid, db.get_or_create_node("theme: " + label, "theme", no));
    theme_node[json::get_string(t, "id")] = nid;
  }
  int n_items = 0;
  for (const auto& x : rel["items"]) {
    std::string type = json::get_string(x, "type");
    if (policy_.contains("/pipeline/materialize_excluded_types", type)) continue;
    std::string id = json::get_string(x, "id");
    NodeOptions no;
    no.content = json::get_string(x, "text");
    no.metadata = Json{{"archive_item", id}, {"status", x["status"]}, {"confidence", x["confidence"]}};
    LOOM_TRY_ASSIGN(std::string nid, db.get_or_create_node(clip(json::get_string(x, "text"), static_cast<std::size_t>(policy_.integer("/pipeline/item_node_label_max_codepoints"))), type, no));
    item_node[id] = nid;
    std::string doc = json::get_string(x, "doc");
    std::string msg = bindings.contains(doc) ? json::get_string(bindings[doc], "msg") : "";
    if (!msg.empty()) (void)db.create_link(nid, msg, "derived_from", policy_.number("/pipeline/derived_from_weight"));
    std::string th = json::get_string(x, "theme");
    if (theme_node.count(th)) (void)db.create_link(nid, theme_node[th], "part_of", policy_.number("/pipeline/theme_link_weight"));
    ++n_items;
  }
  for (const auto& e : rel["edges"]) {
    std::string type = json::get_string(e, "type");
    (void)rt_.relations().ensure(type);
    auto a = item_node.find(json::get_string(e, "src"));
    auto b = item_node.find(json::get_string(e, "dst"));
    if (a != item_node.end() && b != item_node.end()) {
      (void)db.create_link(a->second, b->second, type, policy_.number("/pipeline/item_relation_weight"), Json{{"reason", e["reason"]}});
    }
  }
  return Json{{"output", ""},
              {"artifacts", artifacts},
              {"out_dir", od.empty() ? "" : fs::absolute(od).string()},
              {"log", Json::object()},
              {"stats", Json{{"graph_items", n_items}, {"graph_themes", theme_node.size()}}}};
}

// ── Orchestration ───────────────────────────────────────────────────
namespace {

struct Orchestrator {
  Runtime& rt;
  ArchiveIntelligence::State& st;
  StageRunner& runner;
  ArchiveConfig cfg;
  std::string run_id;
  TaskContext* run_ctx = nullptr;
  std::vector<StageRun> stages;

  // Submits (dedupe unless force) and drives one stage task to completion.
  Result<Json> stage(const std::string& label, const std::string& kind, const Json& params, const Json& hash_params) {
    StageRun sr;
    sr.stage = label;
    Json scoped_hash_params = hash_params;
    if (!runner.policy().is_builtin()) scoped_hash_params["archive_profile_hash"] = runner.policy().hash();
    sr.input_hash = Sha256::hex(kind + "|" + std::string(kPipelineVersion) + "|" + json::canonical(scoped_hash_params));
    SubmitOptions so;
    so.dedupe = !cfg.force;
    so.input_hash = sr.input_hash;
    so.parent_id = run_id;
    so.max_attempts = runner.policy().integer("/pipeline/stage_attempts");
    LOOM_TRY_ASSIGN(sr.task_id, rt.tasks().submit(kind, params, so));
    bool first = true;
    while (true) {
      LOOM_TRY_ASSIGN(auto rec, rt.tasks().get(sr.task_id));
      if (!rec) return Error(Errc::NotFound, "stage task vanished: " + sr.task_id);
      const std::string& s = rec->status;
      if (s == task_status::kDone) {
        sr.cache_hit = first;
        Json res = rec->result ? *rec->result : Json::object();
        sr.output_hash = json::get_string(res, "output");
        sr.stats = res["log"];
        if (sr.cache_hit) {
          st.notify(label, 1, 1, "cache hit");
        }
        stages.push_back(sr);
        if (run_ctx) (void)run_ctx->save_checkpoint(Json{{"stages", stages_json()}});
        return res;
      }
      first = false;
      if (s == task_status::kFailed) return Error(Errc::Internal, label + " failed: " + rec->error);
      if (s == task_status::kCancelled) return Error(Errc::Cancelled, label + " cancelled");
      if (s == task_status::kPaused) {
        if (st.stop_requested()) {
          sr.resumed = rec->checkpoint.has_value();
          stages.push_back(sr);
          return Error(Errc::Paused, label + " paused");
        }
        sr.resumed = true;
        LOOM_TRY(rt.tasks().resume(sr.task_id));
        continue;
      }
      if (s == task_status::kPending) {
        if (rec->checkpoint) sr.resumed = true;
        auto r = rt.tasks().run_sync(sr.task_id);
        if (!r && r.error().code != Errc::Busy) return r.error();
        if (!r) std::this_thread::sleep_for(std::chrono::milliseconds(runner.policy().integer("/pipeline/run_poll_interval_ms")));
        continue;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(runner.policy().integer("/pipeline/run_poll_interval_ms")));  // running on a worker
    }
  }

  Json stages_json() const {
    Json a = Json::array();
    for (const auto& s : stages) a.push_back(s.to_json());
    return a;
  }

  std::string task_log() const {
    std::string out;
    for (const auto& s : stages) {
      out += json::dump(Json{{"stage", s.stage}, {"input_hash", s.input_hash}, {"output_hash", s.output_hash},
                             {"stats", s.stats}}) +
             "\n";
    }
    return out;
  }

  std::vector<std::string> resolve_seeds(const Corpus& corpus, const Plan& plan) {
    std::vector<std::string> raw = cfg.seed_terms;
    if (raw.empty()) {
      std::vector<fs::path> cands;
      if (!plan.repo_path.empty()) cands.push_back(fs::path(plan.repo_path) / "project_manifest.json");
      if (!cfg.out_dir.empty()) cands.push_back(fsutil::expand_user(cfg.out_dir) / "project_manifest.json");
      for (const auto& p : cands) {
        auto txt = fsutil::read_file(p);
        if (!txt) continue;
        Json m = json::parse_or(*txt, Json::object());
        for (const auto& t : json_strings(m["vocabulary"])) {
          raw.push_back(t);
          if (raw.size() >= static_cast<std::size_t>(runner.policy().integer("/pipeline/manifest_seed_limit"))) break;
        }
        if (!raw.empty()) break;
      }
    }
    if (raw.empty()) raw = corpus.default_seeds;
    std::vector<std::string> out;
    for (const auto& s : raw) {
      auto toks = tokenize(s);
      std::string t;
      for (const auto& x : toks) t += (t.empty() ? "" : " ") + x;
      if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    return out;
  }

  Result<Json> run() {
    st.notify("plan", 0, 1, "scanning sources");
    LOOM_TRY_ASSIGN(Plan plan, make_plan(rt, cfg, &runner.policy()));
    Json ing = Json::object();
    {
      Json hp{{"plan", plan.fingerprint()}};
      LOOM_TRY_ASSIGN(ing, stage("ingest", "archive.ingest", Json{{"plan", plan.to_json()}}, hp));
    }
    std::string corpus_h = json::get_string(ing, "output");
    std::string bindings_h = json::get_string(ing, "bindings");
    LOOM_TRY_ASSIGN(auto corpus, runner.corpus(corpus_h));
    auto seeds = resolve_seeds(*corpus, plan);
    if (cfg.max_hits_per_term <= 0) {
      cfg.max_hits_per_term = static_cast<int>(std::clamp<std::size_t>(corpus->docs.size() / static_cast<std::size_t>(runner.policy().integer("/pipeline/auto_hits_divisor")), static_cast<std::size_t>(runner.policy().integer("/pipeline/auto_hits_min")), static_cast<std::size_t>(runner.policy().integer("/pipeline/auto_hits_max"))));
    }

    std::vector<TermRecord> vocab;
    for (const auto& s : seeds) vocab.push_back(TermRecord{s, 0, "seed", runner.policy().number("/pipeline/initial_seed_weight"), {"seed term"}, {}});
    Json passes = Json::array();
    Json syn_out = Json::object();
    Json cluster_r, relate_r, synth_r;
    std::vector<std::string> discovered;
    int pass = 0;
    std::set<std::string> prev_hits;
    std::string final_hits_h;
    for (int round = 0; round <= cfg.max_synthesis_rounds; ++round) {
      // retrieve / expand to a fixpoint
      for (int k = 0; k < cfg.max_passes; ++k) {
        ++pass;
        std::vector<std::string> terms;
        for (const auto& v : vocab) terms.push_back(v.term);
        std::sort(terms.begin(), terms.end());
        Json rp{{"corpus", corpus_h}, {"bindings", bindings_h}, {"terms", str_array(terms)}, {"pass", pass},
                {"max_hits", cfg.max_hits_per_term}};
        Json rh{{"corpus", corpus_h}, {"terms", str_array(terms)}, {"max_hits", cfg.max_hits_per_term}};
        LOOM_TRY_ASSIGN(Json rr, stage("retrieve#" + std::to_string(pass), "archive.retrieve", rp, rh));
        final_hits_h = json::get_string(rr, "output");
        LOOM_TRY_ASSIGN(Json hits, get_json(rt, final_hits_h));
        std::set<std::string> hk;
        for (const auto& h : hits["hits"]) hk.insert(json::get_string(h, "key"));
        std::size_t new_hits = 0;
        for (const auto& k2 : hk) new_hits += !prev_hits.count(k2);
        prev_hits = hk;
        Json pj{{"pass", pass}, {"terms", terms.size()}, {"hits", hk.size()}, {"new_hits", new_hits},
                {"added", Json::array()}};
        if (k + 1 == cfg.max_passes) {
          passes.push_back(pj);
          break;
        }
        Json ep{{"corpus", corpus_h}, {"bindings", bindings_h}, {"hits", final_hits_h}, {"vocab", str_array(terms)},
                {"pass", pass}, {"max_new", cfg.max_new_terms}};
        Json eh{{"corpus", corpus_h}, {"hits", final_hits_h}, {"vocab", str_array(terms)}, {"pass", pass},
                {"max_new", cfg.max_new_terms}};
        LOOM_TRY_ASSIGN(Json er, stage("expand#" + std::to_string(pass), "archive.expand", ep, eh));
        LOOM_TRY_ASSIGN(Json added, get_json(rt, json::get_string(er, "output")));
        for (const auto& t : added["added"]) {
          vocab.push_back(TermRecord::from_json(t));
          pj["added"].push_back(t["term"]);
        }
        passes.push_back(pj);
        if (added["added"].empty()) break;
      }
      std::vector<std::string> terms;
      for (const auto& v : vocab) terms.push_back(v.term);
      Json vocab_j = Json::array();
      for (const auto& v : vocab) vocab_j.push_back(v.to_json(&runner.policy()));
      LOOM_TRY_ASSIGN(std::string vocab_h, put_json(rt, Json{{"terms", vocab_j}, {"passes", passes}}));

      Json gp{{"corpus", corpus_h}, {"bindings", bindings_h}, {"hits", final_hits_h}, {"vocab", str_array(terms)},
              {"llm", cfg.llm}};
      Json gh{{"corpus", corpus_h}, {"hits", final_hits_h}, {"vocab", str_array(terms)}, {"llm", cfg.llm}};
      std::string sfx = round ? "@" + std::to_string(round + 1) : "";
      LOOM_TRY_ASSIGN(Json gr, stage("graph" + sfx, "archive.graph", gp, gh));
      Json cp{{"corpus", corpus_h}, {"graph", gr["output"]}, {"hits", final_hits_h}};
      LOOM_TRY_ASSIGN(cluster_r, stage("cluster" + sfx, "archive.cluster", cp, cp));
      Json tp{{"corpus", corpus_h}, {"cluster", cluster_r["output"]}};
      LOOM_TRY_ASSIGN(Json tr, stage("timeline" + sfx, "archive.timeline", tp, tp));
      Json ip{{"corpus", corpus_h}, {"cluster", cluster_r["output"]}, {"llm", cfg.llm}};
      LOOM_TRY_ASSIGN(Json ir, stage("items" + sfx, "archive.items", ip, ip));
      Json lp{{"corpus", corpus_h}, {"items", ir["output"]}};
      LOOM_TRY_ASSIGN(relate_r, stage("relate" + sfx, "archive.relate", lp, lp));
      Json settings{{"seed_terms", str_array(seeds)},
                    {"max_passes", cfg.max_passes},
                    {"max_new_terms", cfg.max_new_terms},
                    {"max_hits_per_term", cfg.max_hits_per_term},
                    {"max_synthesis_rounds", cfg.max_synthesis_rounds},
                    {"llm", cfg.llm}};
      Json sp{{"corpus", corpus_h},          {"vocab", vocab_h},        {"hits", final_hits_h},
              {"cluster", cluster_r["output"]}, {"timeline", tr["output"]}, {"relate", relate_r["output"]},
              {"round", round + 1},          {"project", plan.project}, {"settings", settings}};
      LOOM_TRY_ASSIGN(synth_r, stage("synthesize" + sfx, "archive.synthesize", sp, sp));
      LOOM_TRY_ASSIGN(syn_out, get_json(rt, json::get_string(synth_r, "output")));
      discovered.clear();
      std::set<std::string> have(terms.begin(), terms.end());
      for (const auto& t : json_strings(syn_out["discovered_terms"])) {
        if (!have.count(t)) discovered.push_back(t);
      }
      if (discovered.empty() || round == cfg.max_synthesis_rounds) break;
      for (const auto& t : discovered) {
        vocab.push_back(TermRecord{t, pass, "synthesis", runner.policy().number("/pipeline/synthesis_seed_weight"), {"named in decisions/requirements during synthesis"}, {}});
      }
      st.notify("synthesize", 1, 1, "new terms from synthesis: " + std::to_string(discovered.size()));
    }

    // materialize (always runs: writes files, artifact rows and graph nodes)
    std::string log_text = task_log();
    LOOM_TRY_ASSIGN(BlobRef log_ref, rt.blobs().put(log_text, "application/x-ndjson"));
    Json mp{{"synthesis", synth_r["output"]}, {"relate", relate_r["output"]}, {"cluster", cluster_r["output"]},
            {"bindings", bindings_h},        {"task_log", log_ref.hash},      {"out_dir", cfg.out_dir},
            {"project", plan.project},       {"run_id", run_id}};
    bool force = cfg.force;
    cfg.force = true;  // never a cache hit
    LOOM_TRY_ASSIGN(Json mr, stage("materialize", "archive.materialize", mp, mp));
    cfg.force = force;

    Json themes = Json::array();
    LOOM_TRY_ASSIGN(Json cl, get_json(rt, json::get_string(cluster_r, "output")));
    for (const auto& t : cl["themes"]) themes.push_back(Json{{"id", t["id"]}, {"label", t["label"]}, {"size", t["size"]}});
    Json summary{{"project", plan.project},
                 {"docs", corpus->docs.size()},
                 {"sources", corpus->sources.size()},
                 {"hits", prev_hits.size()},
                 {"vocabulary", vocab.size()},
                 {"passes", passes},
                 {"themes", themes},
                 {"items", relate_r["log"]},
                 {"gap", syn_out["gap"]},
                 {"discovered_terms", str_array(discovered)},
                 {"artifacts", mr["artifacts"]},
                 {"out_dir", mr["out_dir"]},
                 {"warnings", plan.warnings}};
    return summary;
  }
};

}  // namespace

ArchiveIntelligence::ArchiveIntelligence(Runtime& rt) : rt_(rt), st_(std::make_unique<State>()) {
  auto profile = ArchiveProfile::load(rt.paths().root);
  if (profile) st_->profile = std::move(*profile);
  else st_->profile_error = profile.error();
  auto reg = [this](const std::string& kind,
                    std::function<Result<Json>(StageRunner&, const Json&, StageControl&, TaskContext&)> body) {
    rt_.tasks().register_handler(kind, [this, kind, body](TaskContext& ctx) -> Status {
      if (st_->profile_error) return *st_->profile_error;
      StageRunner runner(rt_, *st_);
      StageControl ctl;
      std::string stage = kind.substr(kind.find('.') + 1);
      ctl.progress = [&](std::int64_t cur, std::int64_t total, std::string_view msg) {
        ctx.progress(cur, total, msg);
        st_->notify(stage, cur, total, msg);
      };
      ctl.should_stop = [&] { return ctx.should_stop() || st_->stop_requested(); };
      ctl.checkpoint = [&](const Json& j) { return ctx.save_checkpoint(j); };
      ctl.resume_from = ctx.checkpoint();
      if (ctx.cancelled()) return Error(Errc::Cancelled, "cancelled");
      if (ctl.should_stop()) return Error(Errc::Paused, "stopped before start");
      auto r = body(runner, ctx.params(), ctl, ctx);
      if (!r) {
        if (ctx.cancelled()) return Error(Errc::Cancelled, r.error().message);
        return r.error();
      }
      ctx.set_result(std::move(*r));
      return {};
    });
  };
  reg("archive.ingest", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.ingest(p, c); });
  reg("archive.retrieve", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.retrieve(p, c); });
  reg("archive.expand", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.expand(p, c); });
  reg("archive.graph", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.graph(p, c); });
  reg("archive.cluster", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.cluster(p, c); });
  reg("archive.timeline", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.timeline(p, c); });
  reg("archive.items", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.items(p, c); });
  reg("archive.relate", [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.relate(p, c); });
  reg("archive.synthesize",
      [](StageRunner& r, const Json& p, StageControl& c, TaskContext&) { return r.synthesize_stage(p, c); });
  reg("archive.materialize", [](StageRunner& r, const Json& p, StageControl& c, TaskContext& ctx) {
    return r.materialize(p, c, ctx.id());
  });

  // The run itself: orchestrates the stages above (resumable as a whole).
  rt_.tasks().register_handler("archive.run", [this](TaskContext& ctx) -> Status {
    if (st_->profile_error) return *st_->profile_error;
    auto cfg = ArchiveConfig::from_json_with_profile(ctx.params()["config"], *st_->profile);
    if (!cfg) return cfg.error();
    StageRunner runner(rt_, *st_);
    Orchestrator orch{rt_, *st_, runner, *cfg, ctx.id(), &ctx, {}};
    {
      std::lock_guard lk(st_->mu);
      st_->run_ctx = &ctx;
    }
    auto r = orch.run();
    {
      std::lock_guard lk(st_->mu);
      st_->run_ctx = nullptr;
    }
    Json result{{"stages", orch.stages_json()}};
    if (!runner.policy().is_builtin()) {
      LOOM_TRY_ASSIGN(auto profile_provenance, runner.policy().provenance());
      LOOM_TRY_ASSIGN(auto snapshot, put_json(rt_, profile_provenance));
      result["profile_provenance"] = snapshot;
    }
    if (!r) {
      (void)ctx.save_checkpoint(result);
      if (r.error().code == Errc::Paused && ctx.cancelled()) return Error(Errc::Cancelled, r.error().message);
      return r.error();
    }
    result["summary"] = *r;
    ctx.set_result(result);
    return {};
  });
}

ArchiveIntelligence::~ArchiveIntelligence() = default;

Result<ArchiveRunResult> ArchiveIntelligence::run(const ArchiveConfig& cfg, const ArchiveProgressFn& progress,
                                                  const CancelToken* cancel) {
  if (st_->profile_error) return *st_->profile_error;
  std::unique_lock run_lock(st_->run_mu, std::try_to_lock);
  if (!run_lock.owns_lock()) return Error(Errc::Busy, "an archive run is already in progress");
  {
    std::lock_guard lk(st_->mu);
    st_->progress = progress;
    st_->cancel = cancel;
  }
  struct Reset {
    ArchiveIntelligence::State& s;
    ~Reset() {
      std::lock_guard lk(s.mu);
      s.progress = nullptr;
      s.cancel = nullptr;
    }
  } reset{*st_};

  (void)rt_.tasks().recover_interrupted();
  SubmitOptions so;
  so.max_attempts = st_->profile->integer("/pipeline/run_attempts");
  Json params{{"config", cfg.to_json()}};
  if (!st_->profile->is_builtin()) params["archive_profile_hash"] = st_->profile->hash();
  LOOM_TRY_ASSIGN(std::string id, rt_.tasks().submit("archive.run", params, so));
  TaskRecord rec;
  while (true) {
    LOOM_TRY_ASSIGN(auto r, rt_.tasks().get(id));
    if (!r) return Error(Errc::NotFound, "archive run task vanished");
    if (r->status == task_status::kPending) {
      auto x = rt_.tasks().run_sync(id);
      if (!x && x.error().code != Errc::Busy) return x.error();
      if (!x) std::this_thread::sleep_for(std::chrono::milliseconds(st_->profile->integer("/pipeline/run_poll_interval_ms")));
      continue;
    }
    if (r->status == task_status::kRunning) {
      std::this_thread::sleep_for(std::chrono::milliseconds(st_->profile->integer("/pipeline/run_poll_interval_ms")));
      continue;
    }
    rec = *r;
    break;
  }
  ArchiveRunResult out;
  out.run_id = id;
  out.status = rec.status == task_status::kDone ? "done" : rec.status == task_status::kPaused ? "paused" : rec.status;
  const Json src = rec.result ? *rec.result : rec.checkpoint ? *rec.checkpoint : Json::object();
  if (const Json* s = json::find(src, "stages"); s && s->is_array()) {
    for (const auto& x : *s) {
      StageRun sr;
      sr.stage = json::get_string(x, "stage");
      sr.task_id = json::get_string(x, "task_id");
      sr.input_hash = json::get_string(x, "input_hash");
      sr.output_hash = json::get_string(x, "output_hash");
      sr.cache_hit = json::get_bool(x, "cache_hit");
      sr.resumed = json::get_bool(x, "resumed");
      sr.stats = x.contains("stats") ? x["stats"] : Json::object();
      out.stages.push_back(std::move(sr));
    }
  }
  if (const Json* s = json::find(src, "summary")) out.summary = *s;
  if (rec.status == task_status::kFailed) return Error(Errc::Internal, "archive run failed: " + rec.error);
  if (rec.status == task_status::kCancelled) out.status = "cancelled";
  if (out.status == "paused") out.summary["message"] = "paused; run again with the same inputs to resume";
  return out;
}

Result<Json> ArchiveIntelligence::status(std::string_view run_id) {
  if (st_->profile_error) return *st_->profile_error;
  std::optional<TaskRecord> run;
  if (run_id.empty()) {
    TaskFilter f;
    f.kind = "archive.run";
    f.limit = 1;
    LOOM_TRY_ASSIGN(auto v, rt_.tasks().list(f));
    if (v.empty()) return Json{{"run", nullptr}, {"stages", Json::array()}, {"artifacts", Json::array()}};
    run = v.front();
  } else {
    LOOM_TRY_ASSIGN(run, rt_.tasks().get(run_id));
    if (!run || run->kind != "archive.run") return Error(Errc::NotFound, "no archive run " + std::string(run_id));
  }
  Json stages = Json::array();
  const Json src = run->result ? *run->result : run->checkpoint ? *run->checkpoint : Json::object();
  if (const Json* s = json::find(src, "stages"); s && s->is_array()) {
    for (const auto& x : *s) {
      Json e = x;
      if (auto t = rt_.tasks().get(json::get_string(x, "task_id")); t && *t) e["status"] = (*t)->status;
      stages.push_back(e);
    }
  }
  Json arts = Json::array();
  LOOM_TRY_ASSIGN(auto all, rt_.provenance().list_artifacts(st_->profile->integer("/pipeline/status_artifact_limit")));
  for (const auto& a : all) {
    if (a.task_id == run->id) arts.push_back(a.to_json());
  }
  Json rj{{"id", run->id},           {"status", run->status}, {"created", run->created}, {"updated", run->updated},
          {"error", run->error},     {"config", run->params["config"]}};
  if (const Json* s = json::find(src, "summary")) rj["summary"] = *s;
  Json out{{"run", rj}, {"stages", stages}, {"artifacts", arts}};
  const auto snapshot_hash = json::get_string(src, "profile_provenance");
  if (!snapshot_hash.empty()) {
    LOOM_TRY_ASSIGN(auto snapshot, get_json(rt_, snapshot_hash));
    LOOM_TRY_ASSIGN(auto recorded, ArchiveProfile::from_provenance(snapshot));
    out["profile"] = recorded.inspection();
    out["profile_provenance_hash"] = snapshot_hash;
  } else if (const auto hash = json::get_string(run->params, "archive_profile_hash"); !hash.empty()) {
    out["profile"] = Json{{"hash", hash}, {"available", false}};
  }
  if (!st_->profile->is_builtin()) out["active_profile_hash"] = st_->profile->hash();
  return out;
}

Result<Json> ArchiveIntelligence::profile() const {
  if (st_->profile_error) return *st_->profile_error;
  return st_->profile->inspection();
}

}  // namespace loom::archive
