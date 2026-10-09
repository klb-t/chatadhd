// OWNER: wave 2 semantic/graph. Exact port of engine/graph_engine.py.
#include "loom/graph_engine.h"

#include "loom/db.h"
#include "loom/log.h"
#include "loom/relations.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/runtime_profile.h"
#include "loom/util/utf8.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.graph";

// Shared by on_message() and ingest_analysis(): materialises entities,
// topics and relations from a unified analysis dict, plus the msg->conv
// "part_of" edge. Returns whether anything (other than the conv edge) was
// written.
Result<bool> ingest_common(Database& db, std::string_view msg_id, std::string_view conv_id, const Json& analysis,
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
      LOOM_TRY_ASSIGN(auto nid, db.get_or_create_node(name, kind));
      LOOM_TRY(db.create_link(msg_id, nid, ep.at("predicate").get<std::string>(), relevance));
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
      LOOM_TRY_ASSIGN(auto nid, db.get_or_create_node(tp.at("lowercase").get<bool>() ? utf8::to_lower(label) : label, tp.at("kind").get<std::string>()));
      LOOM_TRY(db.create_link(msg_id, nid, tp.at("predicate").get<std::string>(), conf));
      changed = true;
    }
  }

  if (const Json* rels = json::find(analysis, rp.at("array").get<std::string>()); rels && rels->is_array()) {
    for (const auto& r : *rels) {
      std::string subj = json::get_string(r, rp.at("subject").get<std::string>());
      std::string obj = json::get_string(r, rp.at("object").get<std::string>());
      std::string pred = json::get_string(r, rp.at("predicate").get<std::string>(), rp.at("default_predicate").get<std::string>());
      if (subj.empty() || obj.empty()) continue;
      LOOM_TRY_ASSIGN(auto sn, db.find_node(subj));
      LOOM_TRY_ASSIGN(auto dn, db.find_node(obj));
      if (sn && dn) {
        if (relations) LOOM_TRY(relations->ensure(pred));
        LOOM_TRY(db.create_link(sn->id, dn->id, pred, rp.at("weight").get<double>()));
        changed = true;
      }
    }
  }

  if (!conv_id.empty()) {
    LOOM_TRY(db.create_link(msg_id, conv_id, membership.at("predicate").get<std::string>(), membership.at("weight").get<double>()));
  }

  return changed;
}

Json source_identity(const Message& message) {
  return Json{{"message_id", message.id}, {"conv_id", message.conv_id}, {"role", message.role},
              {"text_sha256", Sha256::hex(message.text)}, {"status", message.status}, {"version_num", message.version_num},
              {"version_group_id", message.version_group_id ? Json(*message.version_group_id) : Json(nullptr)}};
}

Result<Json> attempt_metadata(const Message& message) {
  Json metadata = message.metadata;
  if (!metadata.is_object()) metadata = Json{{"loom_preserved_metadata", metadata}};
  if (metadata.contains("loom_semantic_attempts") && !metadata.at("loom_semantic_attempts").is_array())
    return Error(Errc::Conflict, "semantic attempt history is not an array; existing data retained");
  if (!metadata.contains("loom_semantic_attempts")) metadata["loom_semantic_attempts"] = Json::array();
  return metadata;
}

Status matches_attempt(const Message& message, const SemanticAttempt& attempt) {
  const Json* attempts = json::find(message.metadata, "loom_semantic_attempts");
  if (message.semantic_status != "executing" || message.conv_id != attempt.conv_id ||
      json::canonical(source_identity(message)) != json::canonical(attempt.source) ||
      !attempts || !attempts->is_array() || attempts->empty() ||
      json::get_string(attempts->back(), "id") != attempt.id ||
      json::get_string(attempts->back(), "state") != "executing" ||
      json::get_string(attempts->back(), "schema") != "loom.semantic_attempt/1" ||
      !attempts->back().contains("source") ||
      json::canonical(attempts->back().at("source")) != json::canonical(attempt.source) ||
      !attempts->back().contains("graph_profile") ||
      json::get_string(attempts->back().at("graph_profile"), "hash") != attempt.graph_profile.hash())
    return Error(Errc::Conflict, "semantic attempt superseded or source changed; response not applied");
  return {};
}

Status validate_graph_profile(const RuntimeProfile& profile) {
  if (profile.domain() != "graph_ingest") return Error(Errc::InvalidArgument, "expected graph_ingest profile");
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("graph_ingest"));
  LOOM_TRY(builtin.with_values(profile.values()));
  return {};
}

}  // namespace

GraphEngine::GraphEngine(Database& db, EventBus& bus, const SemanticAnalyzer& regex, SemanticLLM* llm,
                         RelationRegistry* relations, AnalyzerBinding analyzer_binding)
    : db_(db), bus_(bus), regex_(regex), analyzer_binding_(analyzer_binding), llm_(llm), relations_(relations) {}

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
  const auto mid = json::get_string(data, "id");
  const auto text = json::get_string(data, "text");
  const auto conv_id = json::get_string(data, "conv_id");
  if (mid.empty() || utf8::length(text) < profile.values().at("min_input_codepoints").get<std::size_t>()) return {};

  std::unique_ptr<SemanticAnalyzer> effective_analyzer;
  if (analyzer_binding_ == AnalyzerBinding::RuntimeProfile || !semantic_profile.is_builtin()) {
    LOOM_TRY_ASSIGN(effective_analyzer, SemanticAnalyzer::create_with_profile(semantic_profile));
  }
  const auto& analyzer = effective_analyzer ? *effective_analyzer : regex_;
  const bool use_llm = llm_ && llm_->enabled();
  Json provenance{{"consumer", "graph.on_message"}, {"mode", use_llm ? "llm" : "regex"},
                  {"analyzer_profile_hash", analyzer.profile_hash()}};
  LOOM_TRY_ASSIGN(auto attempt, begin_analysis(mid, text, conv_id, profile, provenance, true));
  Json analysis = nullptr;
  Status outcome;
  try {
    analysis = use_llm ? llm_->analyse(attempt.text, analyzer) : analyzer.to_unified_profile(analyzer.analyse(attempt.text));
    if (!profile.is_builtin()) analysis["runtime_profile_hash"] = profile.hash();
    if (!semantic_profile.is_builtin()) analysis["analyzer_profile_hash"] = semantic_profile.hash();
    auto completed = complete_analysis(attempt, analysis, true);
    if (completed) return {};
    outcome = completed.error();
  } catch (const std::exception& e) {
    outcome = Error(Errc::Internal, "graph message analysis: " + std::string(e.what()));
  }
  auto failed = fail_analysis(attempt, outcome.error(), analysis);
  if (!failed) log::error(kLog, "semantic failure state not written for {}: {}", mid, failed.error().message);
  return outcome;
}

Result<SemanticAttempt> GraphEngine::begin_analysis(std::string_view msg_id, std::string_view expected_text,
    std::string_view expected_conv_id, const RuntimeProfile& profile, const Json& provenance,
    bool reanalyse_completed, std::optional<std::string_view> expected_status) {
  LOOM_TRY(validate_graph_profile(profile));
  if (!provenance.is_object()) return Error(Errc::InvalidArgument, "semantic provenance must be an object");
  auto lock = db_.lock();
  // A claim must be durable before dispatch; accepting an outer transaction
  // could roll it back after a provider has already received the source.
  if (db_.conn().in_transaction()) return Error(Errc::Conflict, "semantic claim requires a durable transaction boundary");
  sql::Txn txn(db_.conn());
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto message, db_.get_msg(msg_id));
  if (!message) return Error(Errc::NotFound, "semantic message no longer exists");
  if (message->text != expected_text || message->conv_id != expected_conv_id ||
      (expected_status && message->status != *expected_status))
    return Error(Errc::Conflict, "semantic source changed before dispatch");
  if (message->semantic_status != "pending" && !(reanalyse_completed && message->semantic_status == "done"))
    return Error(Errc::Conflict, "semantic source is not queued; explicit requeue required");
  LOOM_TRY_ASSIGN(auto metadata, attempt_metadata(*message));
  SemanticAttempt attempt{gen_id(id_prefix::kTask), message->id, message->conv_id, message->text,
                          source_identity(*message), profile};
  metadata["loom_semantic_attempts"].push_back(Json{
    {"schema", "loom.semantic_attempt/1"}, {"id", attempt.id}, {"state", "executing"},
    {"started", timeutil::utc_now_iso()}, {"source", attempt.source},
    {"graph_profile", {{"hash", profile.hash()}, {"revision", profile.definition().at("revision")},
                       {"values", profile.values()}, {"sources", profile.source_provenance()}}},
    {"provenance", provenance}});
  MsgPatch patch; patch.metadata = std::move(metadata); patch.semantic_status = "executing";
  LOOM_TRY(db_.update_msg(msg_id, patch));
  LOOM_TRY(txn.commit());
  return attempt;
}

Result<bool> GraphEngine::complete_analysis(const SemanticAttempt& attempt, const Json& analysis, bool live_summary) {
  LOOM_TRY(validate_graph_profile(attempt.graph_profile));
  if (!analysis.is_object()) return Error(Errc::InvalidArgument, "semantic analysis must be an object");
  bool changed = false;
  {
    auto lock = db_.lock();
    if (db_.conn().in_transaction()) return Error(Errc::Conflict, "semantic completion requires a durable transaction boundary");
    sql::Txn txn(db_.conn());
    LOOM_TRY(txn.begin_status());
    LOOM_TRY_ASSIGN(auto message, db_.get_msg(attempt.message_id));
    if (!message) return Error(Errc::NotFound, "semantic message no longer exists");
    LOOM_TRY(matches_attempt(*message, attempt));
    LOOM_TRY_ASSIGN(auto metadata, attempt_metadata(*message));
    LOOM_TRY_ASSIGN(changed, ingest_common(db_, attempt.message_id, attempt.conv_id, analysis,
                                         relations_, attempt.graph_profile.values()));
    auto& record = metadata["loom_semantic_attempts"].back();
    record["state"] = "done";
    record["finished"] = timeutil::utc_now_iso();
    record["result"] = analysis;
    if (live_summary) {
      const auto& policy = attempt.graph_profile.values();
      const Json* entities = json::find(analysis, policy.at("entities").at("array").get<std::string>());
      const Json* topics = json::find(analysis, policy.at("topics").at("array").get<std::string>());
      metadata["semantic"] = Json{{"source", json::get_string(analysis, "source", "unknown")},
        {"summary", json::get_string(analysis, "summary")}, {"sentiment", json::get_string(analysis, "sentiment")},
        {"entity_count", entities ? json::py_len(*entities) : 0}, {"topic_count", topics ? json::py_len(*topics) : 0}};
      if (!attempt.graph_profile.is_builtin()) metadata["semantic"]["runtime_profile_hash"] = attempt.graph_profile.hash();
      if (const Json* hash = json::find(analysis, "analyzer_profile_hash")) metadata["semantic"]["analyzer_profile_hash"] = *hash;
    }
    MsgPatch patch; patch.metadata = std::move(metadata);
    LOOM_TRY(db_.update_msg(attempt.message_id, patch));
    LOOM_TRY(db_.mark_analysed(attempt.message_id, analysis));
    LOOM_TRY(txn.commit());
  }
  // Subscribers may use other threads/DB connections. No callback under locks.
  if (changed) bus_.emit(events::kGraphChanged, Json{{"conv_id", attempt.conv_id}, {"trigger", attempt.message_id}});
  return changed;
}

Status GraphEngine::fail_analysis(const SemanticAttempt& attempt, const Error& error, const Json& analysis) {
  auto lock = db_.lock();
  if (db_.conn().in_transaction()) return Error(Errc::Conflict, "semantic failure requires a durable transaction boundary");
  sql::Txn txn(db_.conn());
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto message, db_.get_msg(attempt.message_id));
  if (!message) return Error(Errc::NotFound, "semantic message no longer exists");
  LOOM_TRY(matches_attempt(*message, attempt));
  LOOM_TRY_ASSIGN(auto metadata, attempt_metadata(*message));
  auto& record = metadata["loom_semantic_attempts"].back();
  record["state"] = "failed";
  record["finished"] = timeutil::utc_now_iso();
  record["error"] = Json{{"code", errc_name(error.code)}, {"message", error.message}};
  if (!analysis.is_null()) record["result"] = analysis;
  MsgPatch patch; patch.metadata = std::move(metadata); patch.semantic_status = "failed";
  LOOM_TRY(db_.update_msg(attempt.message_id, patch));
  return txn.commit();
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
    bool changed = false;
    {
      auto lock = db_.lock();
      sql::Txn txn(db_.conn());
      LOOM_TRY(txn.begin_status());
      LOOM_TRY_ASSIGN(changed, ingest_common(db_, msg_id, conv_id, analysis, relations_, profile.values()));
      LOOM_TRY(txn.commit());
    }
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
