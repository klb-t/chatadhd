#include "unified_context.h"
#include "context_diagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <vector>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::context {
namespace {

struct Candidate {
  Json row;
  std::string identity;
  std::string channel;
  std::string band;
  std::string text;
};

Result<std::size_t> count_option(const Json& options, std::string_view key, std::size_t fallback) {
  const auto* value = json::find(options, key);
  if (!value) return fallback;
  if (!value->is_number_integer()) return Error(Errc::InvalidArgument, "unified context: " + std::string(key) + " must be a non-negative integer");
  if (!value->is_number_unsigned() && value->get<std::int64_t>() < 0) {
    return Error(Errc::InvalidArgument, "unified context: " + std::string(key) + " must be non-negative");
  }
  const auto number = value->get<std::uint64_t>();
  if (number > std::numeric_limits<std::size_t>::max()) return Error(Errc::InvalidArgument, "unified context: integer does not fit this platform");
  return static_cast<std::size_t>(number);
}

Status boolean_option(const Json& options, std::string_view key) {
  if (const auto* value = json::find(options, key); value && !value->is_boolean()) {
    return Error(Errc::InvalidArgument, "unified context: " + std::string(key) + " must be boolean");
  }
  return {};
}

Result<std::size_t> tokens(std::string_view text, double codepoints_per_token) {
  const auto n = utf8::length(text);
  if (codepoints_per_token == 4.0) return n / 4 + (n % 4 != 0);
  const auto estimate = std::ceil(static_cast<long double>(n) / static_cast<long double>(codepoints_per_token));
  const auto exclusive_bound = std::ldexp(1.0L, std::numeric_limits<std::size_t>::digits);
  if (!std::isfinite(estimate) || estimate >= exclusive_bound) {
    return Error(Errc::InvalidArgument, "unified context: token estimate unavailable (representation overflow)");
  }
  return static_cast<std::size_t>(estimate);
}

int band_rank(std::string_view band) {
  return band == "stable" ? 0 : band == "project" ? 1 : 2;
}

void append_unique(Json& into, const Json& values) {
  if (!into.is_array()) into = Json::array();
  if (!values.is_array()) return;
  for (const auto& value : values) {
    if (std::find(into.begin(), into.end(), value) == into.end()) into.push_back(value);
  }
}

Json source_span(std::string_view source_ref, std::string_view raw, std::string_view represented) {
  return Json{{"origin", "recorded"}, {"source_ref", source_ref},
      {"text_sha256", Sha256::hex(raw)}, {"byte_span", Json::array({0, represented.size()})},
      {"codepoint_span", Json::array({0, utf8::length(represented)})},
      {"span_coordinate", "current_stored_text"}, {"truth_status", "not_assessed"}};
}

std::string knowledge_identity(const Json& row) {
  // Same representation semantics as context_plan.cpp: equal record IDs do
  // not erase alternative resolutions, bands or dependency projections.
  return "knowledge:" + Sha256::hex(json::dump(Json{{"ref_kind", json::get_string(row, "ref_kind")},
      {"ref", json::get_string(row, "ref")}, {"band", json::get_string(row, "band", "goal")},
      {"resolution", json::get_string(row, "resolution")}, {"text", json::get_string(row, "text")},
      {"tokens", row.value("tokens", Json(0))}, {"missing_premises", row.value("missing_premises", Json::array())}}));
}

std::string memory_line(const MemoryNode& node, std::size_t indent) {
  std::string prefix(indent * 2, ' ');
  const auto weight = node.weight == 1.0 ? "" : std::format(" [w:{:.1f}]", node.weight);
  std::string tags;
  for (const auto& tag : node.tags) tags += " #" + tag;
  if (node.node_type == "folder") return prefix + "[" + node.content + "]" + weight + tags;
  if (node.node_type == "file" || node.node_type == "dir") {
    return prefix + (node.node_type == "file" ? "FILE: " : "DIR: ") + node.content +
        " (" + json::get_string(node.metadata, "path") + ")" + weight;
  }
  return prefix + node.content + weight + tags;
}

// The API's formatted text and this raw-node snapshot are separate reads. Only
// a byte-identical reconstruction is given source mapping; drift is explicit.
Json memory_sources(MemoryEngine& memory, const std::string& rendered, std::size_t max_chars) {
  const auto nodes = memory.get_all();
  std::map<std::string, std::vector<std::size_t>> children;
  for (std::size_t i = 0; i < nodes.size(); ++i) children[nodes[i].parent_id.value_or("")].push_back(i);
  for (auto& [parent, indices] : children) {
    (void)parent;
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
      const bool af = nodes[a].node_type != "folder", bf = nodes[b].node_type != "folder";
      return af != bf ? af < bf : nodes[a].created < nodes[b].created;
    });
  }
  std::vector<std::pair<std::size_t, std::size_t>> stack;
  const auto push_children = [&](const std::string& parent, std::size_t depth) {
    const auto found = children.find(parent);
    if (found == children.end()) return;
    for (auto it = found->second.rbegin(); it != found->second.rend(); ++it) stack.emplace_back(*it, depth);
  };
  push_children("", 0);
  std::size_t used_chars = 0;
  std::set<std::size_t> visited;
  std::string reconstructed;
  bool have_line = false;
  Json refs = Json::array();
  while (!stack.empty()) {
    const auto [index, depth] = stack.back();
    stack.pop_back();
    const auto& node = nodes[index];
    if (!visited.insert(index).second || !node.active || used_chars > max_chars) continue;
    const auto line = memory_line(node, depth);
    if (have_line) reconstructed += "\n";
    have_line = true;
    const auto start = reconstructed.size();
    reconstructed += line;
    used_chars += utf8::length(line);
    Json source = source_span(node.id, node.content, node.content);
    source["source_kind"] = "memory_node";
    source["formatted_byte_span"] = Json::array({start, reconstructed.size()});
    source["source_text"] = node.content;
    source["metadata"] = node.metadata;
    source["parent_ref"] = node.parent_id ? Json(*node.parent_id) : Json(nullptr);
    source["node_type"] = node.node_type;
    refs.push_back(std::move(source));
    push_children(node.id, depth + 1);
  }
  const bool matches = reconstructed == rendered;
  return Json{{"origin", "derived"}, {"instrument", "MemoryEngine.get_active_context"},
      {"source_kind", "memory_rendering"}, {"truth_status", "not_assessed"},
      {"rendered_sha256", Sha256::hex(rendered)}, {"raw_node_mapping_verified", matches},
      {"mapping_status", matches ? "byte_identical_reconstruction" : "snapshot_drift_or_unmapped_rendering"},
      {"sources", matches ? refs : Json::array()}};
}

std::string render(const std::vector<Json>& selected, const Json& labels) {
  std::string out, band;
  std::string knowledge_diagnostics;
  for (const auto& row : selected) {
    const auto item_band = json::get_string(row, "band", "goal");
    if (item_band != band) {
      if (!out.empty()) out += "\n\n";
      band = item_band;
      out += json::get_string(labels, band, "# " + band + " context") + "\n";
    }
    const auto channel = json::get_string(row, "channel");
    if (channel == "knowledge" && knowledge_diagnostics.empty()) {
      knowledge_diagnostics = json::get_string(row, "knowledge_diagnostics");
    }
    if (channel == "memory") out += "[MEMORY " + json::get_string(row, "ref") + "; derived formatting; truth not assessed]\n";
    else if (channel == "legacy_graph") {
      out += "[RECORDED MESSAGE " + json::get_string(row, "ref") + " | role=" + json::get_string(row, "role") +
          "; observation of message content, not a truth verdict]\n";
    } else {
      out += "[KNOWLEDGE " + json::get_string(row, "ref_kind") + ":" + json::get_string(row, "ref") +
          "; representation=" + json::get_string(row, "resolution", "unspecified") + "]\n";
    }
    out += json::get_string(row, "text");
    if (const auto* factors = json::find(row, "factors")) {
      if (const auto* theses = json::find(*factors, "thesis_ids")) out += " [plan theses: " + json::dump(*theses) + "]";
    }
    if (const auto* missing = json::find(row, "missing_premises"); missing && missing->is_array() && !missing->empty()) {
      out += "\n[INCOMPLETE: premises not included:";
      for (const auto& ref : *missing) if (ref.is_string()) out += " " + ref.get<std::string>();
      out += "]";
    }
    out += "\n";
  }
  // One tail for all selected knowledge, including every trial used by the
  // shared selector. If no knowledge survives the budget, no tail is emitted.
  out += knowledge_diagnostics;
  return out;
}

}  // namespace

Result<Json> compile_unified_context(Database& db, const Config& cfg,
    GraphMemorySelector* graph, MemoryEngine* memory, const ContextRequest& req,
    Json knowledge_result, std::string_view conv_id, bool include_memory,
    bool include_graph, const Json& options) {
  if (!options.is_object()) return Error(Errc::InvalidArgument, "unified context options must be an object");
  for (const auto key : {"include_knowledge", "legacy_include_inactive", "legacy_include_current_conversation"}) LOOM_TRY(boolean_option(options, key));
  double codepoints_per_token = 4.0;
  if (const auto* configured = json::find(options, "token_codepoints_per_token")) {
    if (!configured->is_number()) return Error(Errc::InvalidArgument, "unified context: token_codepoints_per_token must be a finite positive number");
    codepoints_per_token = configured->get<double>();
    if (!std::isfinite(codepoints_per_token) || codepoints_per_token <= 0.0) {
      return Error(Errc::InvalidArgument, "unified context: token_codepoints_per_token must be a finite positive number");
    }
  }
  LOOM_TRY_ASSIGN(auto budget, count_option(options, "budget_tokens", req.budget_tokens > 0 ? static_cast<std::size_t>(req.budget_tokens) : 4000));
  LOOM_TRY_ASSIGN(auto memory_chars, count_option(options, "memory_max_chars", 16000));
  LOOM_TRY_ASSIGN(auto depth, count_option(options, "legacy_relation_hops", req.relation_hops < 0 ? 0 : static_cast<std::size_t>(req.relation_hops)));
  const auto configured_messages = cfg.get("graph_memory_max_nodes", 20);
  LOOM_TRY_ASSIGN(auto default_messages, count_option(Json{{"value", configured_messages}}, "value", 20));
  LOOM_TRY_ASSIGN(auto max_messages, count_option(options, "legacy_max_messages", default_messages));
  LOOM_TRY_ASSIGN(auto max_visited, count_option(options, "legacy_max_visited", 0));
  LOOM_TRY_ASSIGN(auto detail_chars, count_option(options, "legacy_detail_chars", 0));
  const bool include_knowledge = json::get_bool(options, "include_knowledge", true);
  const bool include_inactive = json::get_bool(options, "legacy_include_inactive", false);
  const bool include_current = json::get_bool(options, "legacy_include_current_conversation", false);
  Json labels = options.value("section_labels", Json::object());
  if (!labels.is_object()) return Error(Errc::InvalidArgument, "unified context section_labels must be an object");
  for (const auto& key : {"stable", "project", "goal"}) {
    if (const auto* value = json::find(labels, key); value && !value->is_string()) {
      return Error(Errc::InvalidArgument, "unified context section labels must be strings");
    }
  }
  const auto priority = options.value("source_priority", Json::array({"memory", "knowledge", "legacy_graph"}));
  if (!priority.is_array()) return Error(Errc::InvalidArgument, "unified context source_priority must be an array");
  std::map<std::string, std::size_t> rank;
  for (const auto& channel : priority) {
    if (!channel.is_string()) return Error(Errc::InvalidArgument, "unified context source_priority values must be strings");
    const auto id = channel.get<std::string>();
    if (id != "memory" && id != "knowledge" && id != "legacy_graph") return Error(Errc::InvalidArgument, "unified context: unknown source priority " + id);
    if (!rank.emplace(id, rank.size()).second) return Error(Errc::InvalidArgument, "unified context: duplicate source priority " + id);
  }
  for (const auto& channel : {"memory", "knowledge", "legacy_graph"}) if (!rank.count(channel)) rank[channel] = rank.size();

  std::vector<Candidate> candidates;
  Json dropped = Json::array(), channels = Json::array();
  std::optional<std::string> memory_input_sha256;
  bool memory_input_mapping_verified = false;
  const auto add = [&](Json row, const std::string& channel, std::string identity) {
    row["channel"] = channel;
    row["identity"] = identity;
    const auto band = json::get_string(row, "band", "goal");
    const auto text = json::get_string(row, "text");
    candidates.push_back(Candidate{std::move(row), std::move(identity), channel, band, text});
  };

  channels.push_back(Json{{"id", "memory"}, {"available", memory != nullptr}, {"enabled", include_memory},
      {"instrument", "MemoryEngine.get_active_context"}, {"truth_assessment", "not_performed"},
      {"max_chars", memory_chars}, {"cap_semantics", "legacy_stop_when_prior_line_sum_exceeds_cap; zero_is_unlimited"}});
  if (include_memory && memory) {
    const auto cap = memory_chars == 0 ? std::numeric_limits<std::size_t>::max() : memory_chars;
    const auto text = memory->get_active_context(cap);
    memory_input_sha256 = Sha256::hex(text);
    const auto mapping = memory_sources(*memory, text, cap);
    memory_input_mapping_verified = mapping["raw_node_mapping_verified"] == true;
    if (!text.empty()) {
      if (memory_input_mapping_verified) {
        for (const auto& source : mapping["sources"]) {
          const auto start = source["formatted_byte_span"][0].get<std::size_t>();
          const auto end = source["formatted_byte_span"][1].get<std::size_t>();
          const auto formatted = text.substr(start, end - start);
          const auto id = source["source_ref"].get<std::string>();
          Json provenance{{"origin", "derived"}, {"instrument", "MemoryEngine.get_active_context"},
              {"source_kind", "memory_node_rendering"}, {"truth_status", "not_assessed"},
              {"rendered_context_sha256", mapping["rendered_sha256"]},
              {"raw_node_mapping_verified", true}, {"source", source}};
          add(Json{{"ref_kind", "memory_node"}, {"ref", id}, {"band", "stable"},
              {"text", formatted}, {"source_text", source["source_text"]}, {"resolution", "derived"},
              {"parent_ref", source["parent_ref"]}, {"node_type", source["node_type"]},
              {"provenance", provenance}, {"why", "active hierarchical memory node; derived formatting of stored content"}},
              "memory", "memory:" + id + ":" + Sha256::hex(formatted));
        }
      } else {
        dropped.push_back(Json{{"ref_kind", "memory_rendering"}, {"ref", "active_memory"}, {"channel", "memory"},
            {"drop_reason", "source_mapping_unverified"}, {"mapping_status", mapping["mapping_status"]}});
      }
    }
  }

  const auto* set = json::find(knowledge_result, "context_set");
  const bool knowledge_available = set && set->is_object();
  std::string knowledge_diagnostics;
  std::string diagnostics_mapping_status = knowledge_available ? "context_set_model_parse_unsupported" : "knowledge_unavailable";
  if (knowledge_available) {
    auto parsed = model::ContextSet::from_json(*set);
    if (parsed) {
      knowledge_diagnostics = render_context_diagnostics(*parsed);
      if (const auto* diagnostics = json::find(parsed->goal.params, "retrieval_diagnostics");
          diagnostics && json::get_string(*diagnostics, "status") == "incomplete") {
        knowledge_diagnostics = "\n[INCOMPLETE: retrieval encountered a store query error or reached a query cap; see retrieval_diagnostics.]\n" +
            knowledge_diagnostics;
      }
      diagnostics_mapping_status = "model_context_set_rendered";
    }
  }
  channels.push_back(Json{{"id", "knowledge"}, {"available", knowledge_available}, {"enabled", include_knowledge},
      {"instrument", "ContextEngine"}, {"scope", "already_selected_items"},
      {"diagnostics_mapping_status", diagnostics_mapping_status}});
  if (knowledge_available) {
    if (const auto* items = json::find(*set, "items"); items && items->is_array()) {
      for (const auto& item : *items) {
        if (!item.is_object()) return Error(Errc::InvalidArgument, "unified context: knowledge item must be an object");
        const auto ref = json::get_string(item, "ref");
        const auto kind = json::get_string(item, "ref_kind");
        const auto band = json::get_string(item, "band", "goal");
        if (ref.empty() || kind.empty() || (band != "stable" && band != "project" && band != "goal")) {
          return Error(Errc::InvalidArgument, "unified context: knowledge item requires ref, ref_kind and a supported band");
        }
        if (include_knowledge) {
          auto row = item;
          row["knowledge_diagnostics"] = knowledge_diagnostics;
          add(std::move(row), "knowledge", knowledge_identity(item));
        }
        else {
          auto row = item;
          row["channel"] = "knowledge";
          row["drop_reason"] = "channel_disabled";
          dropped.push_back(std::move(row));
        }
      }
    }
    if (const auto* prior = json::find(*set, "dropped"); prior && prior->is_array()) {
      for (const auto& item : *prior) {
        auto row = item;
        if (!row.is_object()) continue;
        row["channel"] = "knowledge";
        row["drop_reason"] = "upstream_selector";
        dropped.push_back(std::move(row));
      }
    }
  }

  Json traversal{{"relation_hops", depth}, {"detail_chars", detail_chars}, {"max_messages", max_messages},
      {"max_visited", max_visited}, {"visited", 0}, {"message_candidates", 0}, {"truncated", false},
      {"seed_refs", Json::array()}, {"source_scope", "current_stored_message_content"}};
  if (include_graph && graph) {
    std::deque<std::pair<std::string, std::size_t>> queue;
    std::set<std::string> enqueued;
    const auto seed = [&](const std::string& id) {
      if (enqueued.insert(id).second) {
        queue.emplace_back(id, 0);
        traversal["seed_refs"].push_back(id);
      }
    };
    if (const auto* seeds = json::find(options, "legacy_seed_ids")) {
      if (!seeds->is_array()) return Error(Errc::InvalidArgument, "unified context legacy_seed_ids must be an array");
      for (const auto& id : *seeds) {
        if (!id.is_string()) return Error(Errc::InvalidArgument, "unified context legacy_seed_ids values must be strings");
        seed(id.get<std::string>());
      }
    }
    std::optional<Json> analysis;
    if (const auto* supplied = json::find(options, "analysis")) analysis = *supplied;
    for (const auto& label : graph->extract_seed_labels(req.text, analysis)) {
      LOOM_TRY_ASSIGN(auto direct, db.find_node(label));
      if (direct) seed(direct->id);
      LOOM_TRY_ASSIGN(auto topic, db.find_node(utf8::to_lower(label), "topic"));
      if (topic) seed(topic->id);
    }
    std::size_t visited = 0, messages = 0;
    std::string stop_reason;
    while (!queue.empty()) {
      if (max_visited && visited >= max_visited) { stop_reason = "visited_limit"; break; }
      if (max_messages && messages >= max_messages) { stop_reason = "message_limit"; break; }
      const auto [id, hops] = queue.front();
      queue.pop_front();
      ++visited;
      if (id.rfind("m_", 0) == 0) {
        LOOM_TRY_ASSIGN(auto message, db.get_msg(id));
        if (message) {
          const bool exclude_current = message->conv_id == conv_id && !include_current;
          if (exclude_current || (!include_inactive && message->status != msg_status::kActive)) {
            dropped.push_back(Json{{"ref", id}, {"channel", "legacy_graph"}, {"ref_kind", "message"},
                {"drop_reason", exclude_current ? "current_conversation" : "inactive_message"},
                {"status", message->status}, {"relation_hops", hops}});
          } else {
            const auto represented = detail_chars == 0 ? message->text : std::string(utf8::prefix(message->text, detail_chars));
            auto provenance = source_span(id, message->text, represented);
            provenance["source_kind"] = "message";
            provenance["observation_scope"] = "message_content";
            provenance["conversation_ref"] = message->conv_id;
            provenance["role"] = message->role;
            provenance["stored_source_metadata"] = message->metadata;
            add(Json{{"ref_kind", "message"}, {"ref", id}, {"band", "goal"}, {"text", represented},
                {"source_text", message->text}, {"resolution", represented.size() == message->text.size() ? "raw" : "raw_excerpt"},
                {"role", message->role}, {"status", message->status}, {"version_num", message->version_num},
                {"version_group_id", message->version_group_id ? Json(*message->version_group_id) : Json(nullptr)},
                {"model", message->model ? Json(*message->model) : Json(nullptr)},
                {"relation_hops", hops}, {"provenance", provenance},
                {"why", "recorded message reached through existing legacy graph links"}},
                "legacy_graph", "message:" + id + ":" + std::to_string(message->version_num));
            ++messages;
          }
        } else dropped.push_back(Json{{"ref", id}, {"channel", "legacy_graph"}, {"drop_reason", "missing_message"}});
      }
      if (hops < depth) {
        LOOM_TRY_ASSIGN(auto links, db.get_links(id));
        for (const auto& link : links) {
          const auto neighbor = link.src == id ? link.dst : link.src;
          if (enqueued.insert(neighbor).second) queue.emplace_back(neighbor, hops + 1);
        }
      }
    }
    traversal["visited"] = visited;
    traversal["message_candidates"] = messages;
    traversal["pending_refs"] = queue.size();
    if (!stop_reason.empty()) {
      traversal["truncated"] = true;
      traversal["stop_reason"] = stop_reason;
      dropped.push_back(Json{{"ref_kind", "retrieval_scope"}, {"ref", "legacy_graph_traversal"},
          {"channel", "legacy_graph"}, {"drop_reason", stop_reason}, {"pending_refs", queue.size()}});
    }
  }
  channels.push_back(Json{{"id", "legacy_graph"}, {"available", graph != nullptr}, {"enabled", include_graph},
      {"instrument", "GraphMemorySelector.seed_labels + Database.get_links"}, {"traversal", traversal}});

  std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
    if (band_rank(a.band) != band_rank(b.band)) return band_rank(a.band) < band_rank(b.band);
    return rank.at(a.channel) < rank.at(b.channel);
  });
  std::vector<Json> unique;
  std::map<std::string, std::size_t> identities;
  for (auto& candidate : candidates) {
    const auto found = identities.find(candidate.identity);
    if (found != identities.end()) {
      auto& row = unique[found->second];
      for (const auto key : {"required_by", "missing_premises"}) {
        if (const auto* refs = json::find(candidate.row, key)) append_unique(row[key], *refs);
      }
      if (const auto* factors = json::find(candidate.row, "factors"); factors && factors->is_object()) {
        if (!row.contains("factors") || !row["factors"].is_object()) row["factors"] = Json::object();
        for (const auto key : {"thesis_ids", "counter_for"}) {
          if (const auto* values = json::find(*factors, key)) append_unique(row["factors"][key], *values);
        }
        // Native channel factors are maps keyed by instrument ID, not arrays.
        // Keep each scored/ranked signal intact; differing same-key projections
        // are still preserved verbatim below in duplicate_paths.
        for (const auto key : {"candidate_channels", "thesis_selection"}) {
          if (const auto* values = json::find(*factors, key); values && values->is_object()) {
            auto& target = row["factors"][key];
            if (target.is_null()) target = Json::object();
            if (!target.is_object()) continue;
            for (auto it = values->begin(); it != values->end(); ++it) {
              if (!target.contains(it.key())) target[it.key()] = it.value();
            }
          }
        }
      }
      if (!row.contains("duplicate_paths")) row["duplicate_paths"] = Json::array();
      row["duplicate_paths"].push_back(candidate.row);
      auto omission = candidate.row;
      omission["drop_reason"] = "duplicate_identity";
      dropped.push_back(std::move(omission));
      continue;
    }
    identities.emplace(candidate.identity, unique.size());
    unique.push_back(std::move(candidate.row));
  }

  // Required_by is the recorded reverse dependency relation. Preserve it from
  // both selected and upstream-dropped records before applying the shared budget.
  std::map<std::string, std::set<std::string>> premises;
  const auto dependencies = [&](const Json& row) {
    if (json::get_string(row, "channel") != "knowledge") return;
    if (const auto* by = json::find(row, "required_by"); by && by->is_array()) {
      for (const auto& puller : *by) if (puller.is_string()) premises[puller.get<std::string>()].insert(json::get_string(row, "ref"));
    }
  };
  for (const auto& row : unique) dependencies(row);
  for (const auto& row : dropped) dependencies(row);
  const auto annotate = [&](std::vector<Json>& rows) {
    std::set<std::string> selected_knowledge;
    for (const auto& row : rows) if (json::get_string(row, "channel") == "knowledge") selected_knowledge.insert(json::get_string(row, "ref"));
    for (auto& row : rows) {
      if (json::get_string(row, "channel") != "knowledge") continue;
      for (const auto& ref : premises[json::get_string(row, "ref")]) {
        if (!selected_knowledge.count(ref)) append_unique(row["missing_premises"], Json::array({ref}));
      }
    }
  };
  std::vector<Json> selected;
  for (const auto& row : unique) {
    auto trial = selected;
    trial.push_back(row);
    LOOM_TRY_ASSIGN(auto trial_tokens, tokens(render(trial, labels), codepoints_per_token));
    if (trial_tokens <= budget) selected.push_back(row);
    else {
      auto omission = row;
      omission["drop_reason"] = "rendered_prompt_budget";
      dropped.push_back(std::move(omission));
    }
  }
  auto annotated = selected;
  annotate(annotated);
  LOOM_TRY_ASSIGN(auto annotated_tokens, tokens(render(annotated, labels), codepoints_per_token));
  while (!annotated.empty() && annotated_tokens > budget) {
    auto omission = selected.back();
    omission["drop_reason"] = "rendered_dependency_annotations_budget";
    dropped.push_back(std::move(omission));
    selected.pop_back();
    annotated = selected;
    annotate(annotated);
    LOOM_TRY_ASSIGN(annotated_tokens, tokens(render(annotated, labels), codepoints_per_token));
  }
  const auto prompt = render(annotated, labels);
  std::vector<Json> prefix;
  std::size_t prefix_tokens = 0;
  for (auto& row : annotated) {
    prefix.push_back(row);
    LOOM_TRY_ASSIGN(auto full_tokens, tokens(render(prefix, labels), codepoints_per_token));
    // Marginal estimates include each heading/separator exactly once, and sum
    // to used_tokens even when ceil() would make per-item sums overestimate.
    row["prompt_tokens"] = full_tokens - prefix_tokens;
    prefix_tokens = full_tokens;
  }
  LOOM_TRY_ASSIGN(auto used_tokens, tokens(prompt, codepoints_per_token));
  Json result{{"schema", "loom.unified_context/1"}, {"prompt", prompt}, {"used_tokens", used_tokens},
      {"budget_tokens", budget}, {"token_metric", codepoints_per_token == 4.0 ? "ceil_rendered_prompt_codepoints_div_4" : "ceil_rendered_prompt_codepoints_div_configured"},
      {"token_codepoints_per_token", codepoints_per_token},
      {"includes_prompt_headers", true}, {"selected", annotated}, {"dropped", dropped},
      {"selector_channels", channels}, {"knowledge_result", std::move(knowledge_result)},
      {"limitations", Json::array({"token_estimate_is_not_provider_tokenization", "knowledge_candidates_are_already_selected_upstream",
          "stored_message_content_is_not_an_immutable_import_source", "legacy_graph_links_are_reachability_not_semantic_support"})}};
  if (memory_input_sha256) {
    result["memory_input_sha256"] = *memory_input_sha256;
    result["memory_input_mapping_verified"] = memory_input_mapping_verified;
  }
  return result;
}

}  // namespace loom::context
