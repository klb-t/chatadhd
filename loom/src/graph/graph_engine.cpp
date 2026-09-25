// OWNER: wave 2 semantic/graph. Exact port of engine/graph_engine.py.
#include "loom/graph_engine.h"

#include "loom/db.h"
#include "loom/log.h"
#include "loom/relations.h"
#include "loom/semantic_analyzer.h"
#include "loom/semantic_llm.h"
#include "loom/util/utf8.h"

namespace loom {

namespace {
constexpr std::string_view kLog = "loom.graph";

// Shared by on_message() and ingest_analysis(): materialises entities,
// topics and relations from a unified analysis dict, plus the msg->conv
// "part_of" edge. Returns whether anything (other than the conv edge) was
// written.
bool ingest_common(Database& db, std::string_view msg_id, std::string_view conv_id, const Json& analysis,
                   RelationRegistry* relations) {
  bool changed = false;

  if (const Json* ents = json::find(analysis, "entities"); ents && ents->is_array()) {
    for (const auto& ent : *ents) {
      std::string name = json::get_string(ent, "name", "");
      std::string kind = json::get_string(ent, "kind", "entity");
      double relevance = json::get_number(ent, "relevance", 0.5);
      if (name.empty() || utf8::length(name) < 2 || relevance < 0.3) continue;
      auto nid = db.get_or_create_node(name, kind);
      if (!nid) {
        log::debug(kLog, "get_or_create_node failed for entity '{}': {}", name, nid.error().message);
        continue;
      }
      auto lr = db.create_link(msg_id, *nid, "mentions", relevance);
      if (!lr) log::debug(kLog, "create_link (mentions) failed: {}", lr.error().message);
      changed = true;
    }
  }

  if (const Json* tops = json::find(analysis, "topics"); tops && tops->is_array()) {
    for (const auto& t : *tops) {
      std::string label;
      double conf = 0.5;
      if (t.is_object()) {
        label = json::get_string(t, "label", "");
        conf = json::get_number(t, "confidence", 0.5);
      } else if (t.is_string()) {
        label = t.get<std::string>();
      }
      if (label.empty() || conf < 0.3) continue;
      auto nid = db.get_or_create_node(utf8::to_lower(label), "topic");
      if (!nid) {
        log::debug(kLog, "get_or_create_node failed for topic '{}': {}", label, nid.error().message);
        continue;
      }
      auto lr = db.create_link(msg_id, *nid, "tagged_with", conf);
      if (!lr) log::debug(kLog, "create_link (tagged_with) failed: {}", lr.error().message);
      changed = true;
    }
  }

  if (const Json* rels = json::find(analysis, "relations"); rels && rels->is_array()) {
    for (const auto& r : *rels) {
      std::string subj = json::get_string(r, "subject", "");
      std::string obj = json::get_string(r, "object", "");
      std::string pred = json::get_string(r, "predicate", "related");
      if (subj.empty() || obj.empty()) continue;
      auto sn = db.find_node(subj);
      auto dn = db.find_node(obj);
      if (sn && *sn && dn && *dn) {
        if (relations) (void)relations->ensure(pred);
        auto lr = db.create_link((*sn)->id, (*dn)->id, pred, 0.7);
        if (!lr) log::debug(kLog, "create_link (relation) failed: {}", lr.error().message);
        changed = true;
      }
    }
  }

  if (!conv_id.empty()) {
    auto lr = db.create_link(msg_id, conv_id, "part_of", 0.3);
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
  if (!active_.load()) return;
  try {
    std::string mid = json::get_string(data, "id", "");
    std::string text = json::get_string(data, "text", "");
    std::string conv_id = json::get_string(data, "conv_id", "");
    if (mid.empty()) return;
    if (utf8::length(text) < 10) return;

    Json analysis;
    if (llm_ && llm_->enabled()) {
      analysis = llm_->analyse(text);
    } else {
      Analysis raw = regex_.analyse(text);
      analysis = SemanticAnalyzer::to_unified(raw);
    }

    bool changed = ingest_common(db_, mid, conv_id, analysis, relations_);

    std::size_t entity_count = 0, topic_count = 0;
    if (const Json* e = json::find(analysis, "entities"); e) entity_count = json::py_len(*e);
    if (const Json* t = json::find(analysis, "topics"); t) topic_count = json::py_len(*t);

    MsgPatch patch;
    patch.metadata = Json{{"semantic", Json{{"source", json::get_string(analysis, "source", "unknown")},
                                            {"summary", json::get_string(analysis, "summary", "")},
                                            {"sentiment", json::get_string(analysis, "sentiment", "")},
                                            {"entity_count", entity_count},
                                            {"topic_count", topic_count}}}};
    if (auto st = db_.update_msg(mid, patch); !st) {
      log::debug(kLog, "update_msg failed for {}: {}", mid, st.error().message);
    } else if (auto st2 = db_.mark_analysed(mid, analysis); !st2) {
      log::debug(kLog, "mark_analysed failed for {}: {}", mid, st2.error().message);
    }

    if (changed) bus_.emit(events::kGraphChanged, Json{{"conv_id", conv_id}, {"trigger", mid}});
  } catch (const std::exception& e) {
    log::error(kLog, "GraphEngine failed processing message {}: {}", json::get_string(data, "id", "?"), e.what());
  }
}

bool GraphEngine::ingest_analysis(std::string_view msg_id, std::string_view conv_id, const Json& analysis) {
  try {
    bool changed = ingest_common(db_, msg_id, conv_id, analysis, relations_);
    if (changed) bus_.emit(events::kGraphChanged, Json{{"conv_id", std::string(conv_id)}, {"trigger", std::string(msg_id)}});
    return changed;
  } catch (const std::exception& e) {
    log::debug(kLog, "ingest_analysis failed for {}: {}", std::string(msg_id), e.what());
    return false;
  }
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
  auto convs = db_.list_convs(9999);
  if (!convs) return 0;
  for (const auto& c : *convs) total += reindex_conversation(c.id);
  return total;
}

}  // namespace loom
