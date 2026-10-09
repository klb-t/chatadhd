// OWNER: wave 2 semantic/graph. Exact port of engine/graph_engine.py.
#include "loom/graph_engine.h"

#include "loom/db.h"
#include "loom/log.h"
#include "loom/relations.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/runtime_profile.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.graph";

// Shared by on_message() and ingest_analysis(): materialises entities,
// topics and relations from a unified analysis dict, plus the msg->conv
// "part_of" edge. Returns whether anything (other than the conv edge) was
// written.
bool ingest_common(Database& db, std::string_view msg_id, std::string_view conv_id, const Json& analysis,
                   RelationRegistry* relations, const Json& policy) {
  const Json& ep = policy.at("entities");
  const Json& tp = policy.at("topics");
  const Json& rp = policy.at("relations");
  const Json& membership = policy.at("membership");
  bool changed = false;

  if (const Json* ents = json::find(analysis, ep.at("array").get<std::string>()); ents && ents->is_array()) {
    for (const auto& ent : *ents) {
      std::string name = json::get_string(ent, ep.at("name").get<std::string>());
      std::string kind = json::get_string(ent, ep.at("kind").get<std::string>(), ep.at("default_kind").get<std::string>());
      double relevance = json::get_number(ent, ep.at("relevance").get<std::string>(), ep.at("default_relevance").get<double>());
      if (name.empty() || utf8::length(name) < ep.at("min_label_codepoints").get<std::size_t>() || relevance < ep.at("min_relevance").get<double>()) continue;
      auto nid = db.get_or_create_node(name, kind);
      if (!nid) {
        log::debug(kLog, "get_or_create_node failed for entity '{}': {}", name, nid.error().message);
        continue;
      }
      auto lr = db.create_link(msg_id, *nid, ep.at("predicate").get<std::string>(), relevance);
      if (!lr) log::debug(kLog, "create_link (mentions) failed: {}", lr.error().message);
      changed = true;
    }
  }

  if (const Json* tops = json::find(analysis, tp.at("array").get<std::string>()); tops && tops->is_array()) {
    for (const auto& t : *tops) {
      std::string label;
      double conf = tp.at("default_confidence").get<double>();
      if (t.is_object()) {
        label = json::get_string(t, tp.at("label").get<std::string>());
        conf = json::get_number(t, tp.at("confidence").get<std::string>(), tp.at("default_confidence").get<double>());
      } else if (t.is_string()) {
        label = t.get<std::string>();
      }
      if (label.empty() || conf < tp.at("min_confidence").get<double>()) continue;
      auto nid = db.get_or_create_node(tp.at("lowercase").get<bool>() ? utf8::to_lower(label) : label, tp.at("kind").get<std::string>());
      if (!nid) {
        log::debug(kLog, "get_or_create_node failed for topic '{}': {}", label, nid.error().message);
        continue;
      }
      auto lr = db.create_link(msg_id, *nid, tp.at("predicate").get<std::string>(), conf);
      if (!lr) log::debug(kLog, "create_link (tagged_with) failed: {}", lr.error().message);
      changed = true;
    }
  }

  if (const Json* rels = json::find(analysis, rp.at("array").get<std::string>()); rels && rels->is_array()) {
    for (const auto& r : *rels) {
      std::string subj = json::get_string(r, rp.at("subject").get<std::string>());
      std::string obj = json::get_string(r, rp.at("object").get<std::string>());
      std::string pred = json::get_string(r, rp.at("predicate").get<std::string>(), rp.at("default_predicate").get<std::string>());
      if (subj.empty() || obj.empty()) continue;
      auto sn = db.find_node(subj);
      auto dn = db.find_node(obj);
      if (sn && *sn && dn && *dn) {
        if (relations) (void)relations->ensure(pred);
        auto lr = db.create_link((*sn)->id, (*dn)->id, pred, rp.at("weight").get<double>());
        if (!lr) log::debug(kLog, "create_link (relation) failed: {}", lr.error().message);
        changed = true;
      }
    }
  }

  if (!conv_id.empty()) {
    auto lr = db.create_link(msg_id, conv_id, membership.at("predicate").get<std::string>(), membership.at("weight").get<double>());
    if (!lr) log::debug(kLog, "create_link (part_of) failed: {}", lr.error().message);
  }

  return changed;
}

}  // namespace

GraphEngine::GraphEngine(Database& db, EventBus& bus, const SemanticAnalyzer& regex, SemanticLLM* llm,
                         RelationRegistry* relations)
    : db_(db), bus_(bus), regex_(regex), llm_(llm), relations_(relations) {}

GraphEngine::~GraphEngine() { stop(); }

void GraphEngine::start() {
  if (active_.exchange(true)) return;
  sub_ = ScopedSubscription(bus_, bus_.on(events::kMsgCreated,
                                          [this](std::string_view, const Json& data) { on_message(data); }));
  log::info(kLog, "GraphEngine started (LLM={})", (llm_ ? "yes" : "regex-only"));
}

void GraphEngine::stop() {
  active_.store(false);
  sub_.reset();
}

void GraphEngine::on_message(const Json& data) {
  auto result = on_message_checked(data);
  if (!result) log::error(kLog, "GraphEngine failed processing message {}: {}", json::get_string(data, "id", "?"), result.error().message);
}

Status GraphEngine::on_message_checked(const Json& data, const Json& overrides) {
  if (!active_.load()) return {};
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_ingest", db_.path().parent_path(), overrides));
  LOOM_TRY_ASSIGN(auto semantic_profile, RuntimeProfile::load("semantic_analyzer", db_.path().parent_path()));
  const Json& policy = profile.values();
  try {
    std::string mid = json::get_string(data, "id");
    std::string text = json::get_string(data, "text");
    std::string conv_id = json::get_string(data, "conv_id");
    if (mid.empty() || utf8::length(text) < policy.at("min_input_codepoints").get<std::size_t>()) return {};

    Json analysis;
    bool used_analyzer_overlay = false;
    std::unique_ptr<SemanticAnalyzer> effective_analyzer;
    if (!semantic_profile.is_builtin()) {
      LOOM_TRY_ASSIGN(effective_analyzer, SemanticAnalyzer::create_with_profile(semantic_profile));
      used_analyzer_overlay = true;
    }
    const auto& analyzer = effective_analyzer ? *effective_analyzer : regex_;
    if (llm_ && llm_->enabled()) {
      analysis = llm_->analyse(text, analyzer);
    } else {
      analysis = analyzer.to_unified_profile(analyzer.analyse(text));
    }
    if (!profile.is_builtin()) analysis["runtime_profile_hash"] = profile.hash();
    if (used_analyzer_overlay) analysis["analyzer_profile_hash"] = semantic_profile.hash();

    bool changed = ingest_common(db_, mid, conv_id, analysis, relations_, policy);
    std::size_t entity_count = 0, topic_count = 0;
    if (const Json* e = json::find(analysis, policy.at("entities").at("array").get<std::string>())) entity_count = json::py_len(*e);
    if (const Json* t = json::find(analysis, policy.at("topics").at("array").get<std::string>())) topic_count = json::py_len(*t);

    {
      auto lock = db_.lock();
      LOOM_TRY_ASSIGN(auto message, db_.get_msg(mid));
      if (!message) return {};
      Json metadata = message->metadata;
      if (!metadata.is_object()) metadata = Json{{"loom_preserved_metadata", metadata}};
      metadata["semantic"] = Json{{"source", json::get_string(analysis, "source", "unknown")},
                                  {"summary", json::get_string(analysis, "summary")},
                                  {"sentiment", json::get_string(analysis, "sentiment")},
                                  {"entity_count", entity_count}, {"topic_count", topic_count}};
      if (!profile.is_builtin()) metadata["semantic"]["runtime_profile_hash"] = profile.hash();
      if (used_analyzer_overlay) metadata["semantic"]["analyzer_profile_hash"] = semantic_profile.hash();
      MsgPatch patch;
      patch.metadata = std::move(metadata);
      LOOM_TRY(db_.update_msg(mid, patch));
      LOOM_TRY(db_.mark_analysed(mid, analysis));
    }
    if (changed) bus_.emit(events::kGraphChanged, Json{{"conv_id", conv_id}, {"trigger", mid}});
    return {};
  } catch (const std::exception& e) {
    return Error(Errc::Internal, "graph message analysis: " + std::string(e.what()));
  }
}

bool GraphEngine::ingest_analysis(std::string_view msg_id, std::string_view conv_id, const Json& analysis) {
  auto result = ingest_analysis_checked(msg_id, conv_id, analysis);
  if (!result) {
    log::error(kLog, "ingest_analysis failed for {}: {}", std::string(msg_id), result.error().message);
    return false;
  }
  return *result;
}

Result<bool> GraphEngine::ingest_analysis_checked(std::string_view msg_id, std::string_view conv_id,
                                                const Json& analysis, const Json& overrides) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_ingest", db_.path().parent_path(), overrides));
  try {
    bool changed = ingest_common(db_, msg_id, conv_id, analysis, relations_, profile.values());
    if (changed) bus_.emit(events::kGraphChanged, Json{{"conv_id", std::string(conv_id)}, {"trigger", std::string(msg_id)}});
    return changed;
  } catch (const std::exception& e) {
    return Error(Errc::Internal, "graph analysis ingestion: " + std::string(e.what()));
  }
}

Result<Json> GraphEngine::profile_inspection(const Json& overrides) const {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_ingest", db_.path().parent_path(), overrides));
  return profile.inspection();
}

int GraphEngine::reindex_conversation(std::string_view conv_id) {
  auto msgs = db_.get_msgs(conv_id, /*include_all=*/true);
  if (!msgs) {
    log::debug(kLog, "reindex_conversation: get_msgs failed for {}: {}", std::string(conv_id), msgs.error().message);
    return 0;
  }
  int count = 0;
  for (const auto& m : *msgs) {
    on_message(Json{{"id", m.id}, {"text", m.text}, {"conv_id", std::string(conv_id)}, {"role", m.role}});
    count++;
  }
  log::info(kLog, "Reindexed {} messages for conv {}", count, std::string(conv_id));
  return count;
}

int GraphEngine::reindex_all() {
  int total = 0;
  auto profile = RuntimeProfile::load("graph_ingest", db_.path().parent_path());
  if (!profile) { log::error(kLog, "reindex_all profile rejected: {}", profile.error().message); return 0; }
  int limit = profile->values().at("reindex_conversation_limit").get<int>();
  auto convs = db_.list_convs(limit == 0 ? -1 : limit);
  if (!convs) return 0;
  for (const auto& c : *convs) total += reindex_conversation(c.id);
  return total;
}

}  // namespace loom
