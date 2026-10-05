// OWNER: wave 2 net/chat/worker. Port of engine/chat_engine.py.
#include "loom/chat_engine.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>

#include "loom/config.h"
#include "loom/event_bus.h"
#include "loom/graph_memory.h"
#include "loom/knowledge_store.h"
#include "loom/log.h"
#include "loom/memory_engine.h"
#include "loom/providers.h"
#include "loom/provenance.h"
#include "loom/net/http.h"
#include "loom/net/sse.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "stub.h"
#include "active_task_spec.h"
#include "active_task_acceptance.h"
#include "context/context_execution.h"
#include "context/unified_context.h"
#include "context/method_registry.h"
#include "graph_reply.h"
#include "reasoning_profile.h"

namespace loom {

namespace {

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

std::string cfg_string(const Config& cfg, std::string_view key, std::string fallback = "") {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::move(fallback);
}

double cfg_number(const Config& cfg, std::string_view key, double fallback) {
  Json v = cfg.get(key, fallback);
  return v.is_number() ? v.get<double>() : fallback;
}

bool cfg_bool(const Config& cfg, std::string_view key, bool fallback) {
  return json::truthy(cfg.get(key, fallback));
}

const std::vector<std::string>& image_exts() {
  static const std::vector<std::string> kExts = {".png", ".jpg", ".jpeg", ".gif", ".webp"};
  return kExts;
}
const std::vector<std::string>& text_exts() {
  static const std::vector<std::string> kExts = {".txt", ".py", ".md", ".json", ".csv", ".xml", ".html",
                                                  ".css", ".js", ".ts", ".yaml", ".yml", ".toml", ".ini",
                                                  ".cfg", ".sh", ".bat", ".rs", ".go", ".java", ".c",
                                                  ".cpp", ".h", ".sql"};
  return kExts;
}
constexpr std::size_t kMaxFileChars = 15000;

std::string lower_ext(const std::filesystem::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// Only an explicitly selected graph mode creates these borrowed services.
// Definition/prompt/default data is supplied by the caller's native graph;
// this adapter advertises executable protocols, never a method catalogue.
struct ChatGraphExecution {
  ChatGraphExecution(const Config& config, const Secrets& secrets, Database& db, net::HttpTransport& http,
                     Json graph_options)
      : paths(DataPaths::for_root(config.path().parent_path())), blobs(paths.blobs, db), provenance(db),
        providers(config, secrets), registry(db), services{paths.root, db, config, secrets, http, &providers, blobs, provenance},
        options(std::move(graph_options)) {}
  DataPaths paths;
  BlobStore blobs;
  ProvenanceStore provenance;
  ProviderRegistry providers;
  context::MethodRegistry registry;
  chat::GraphReplyServices services;
  Json options;
  std::optional<chat::GraphReplyPrepared> prepared;
  std::unique_ptr<chat::ModelUsageGuard> guard;
  Json result = Json::object();
  Json first_response = Json::object();
  Json chunk_sources = Json::array();
  std::string wire_bytes;
  bool attempted = false;
  bool settled = false;
  bool capture_failed = false;
  std::string transport_issue;
  ~ChatGraphExecution() {
    // Callback exceptions after dispatch keep attempted usage unknown rather
    // than cancelling a request that may already have incurred a charge.
    if (guard && !settled) { try { settle(Json::object()); } catch (...) {} }
  }

  void issue(std::string_view status, std::string_view code, std::string_view text = "") {
    const auto resolution = result.value("method_resolution", Json(nullptr));
    result = Json{{"schema", "loom.chat_graph_reply/1"}, {"mode", options.value("mode", Json("off"))},
        {"status", status}, {"error", Json{{"code", code}}}, {"text", text}, {"retained_text", text},
        {"model_content_origin", "model"}, {"canonical_store_written", false}};
    if (!resolution.is_null()) result["method_resolution"] = resolution;
  }
  Result<Json> accept(const Json& supplied) {
    const auto* admission = json::find(options, "admission");
    const auto* scope = admission ? json::find(*admission, "selection_scope") : nullptr;
    // An absent dynamic policy preserves the caller's exact selection and CAS.
    if (!scope) return registry.accept(supplied);
    if (!scope->is_string() || *scope != "all_native_rows")
      return Error(Errc::Unsupported, "graph admission selection_scope unavailable");
    if (!supplied.is_object() || !supplied.contains("packet") ||
        !supplied["packet"].is_object() || json::get_string(supplied, "target").empty() ||
        !supplied.contains("explicitly_accepted") || supplied["explicitly_accepted"] != true)
      return Error(Errc::InvalidArgument, "dynamic graph acceptance needs packet, target and explicit acceptance");

    auto lock = services.db.lock();
    sql::Txn snapshot(services.db.conn());
    LOOM_TRY(snapshot.begin_status());
    kb::KnowledgeStore store(services.db);
    LOOM_TRY(store.ensure_schema());
    const std::string run = kb::KnowledgeRun::make_id("loom.graph_packet_store/1",
        Json{{"graph_packet_target", supplied["target"]}});
    Json request = supplied;
    request["selection"] = Json::object();
    request["expected_rows"] = Json::object();
    for (const char* collection : {"entities", "claims", "sources"}) {
      const auto* rows = json::find(supplied["packet"], collection);
      if (!rows || !rows->is_array())
        return Error(Errc::InvalidArgument, "dynamic graph acceptance needs native packet rows");
      request["selection"][collection] = Json::array();
      request["expected_rows"][collection] = Json::object();
      for (const auto& row : *rows) {
        const Json* native = &row;
        if (std::string_view(collection) == "sources") native = json::find(row, "observation");
        const std::string id = native ? json::get_string(*native, "id") : "";
        if (id.empty() || request["expected_rows"][collection].contains(id))
          return Error(Errc::InvalidArgument, "dynamic graph acceptance has invalid or duplicate row ID");
        Json expected = nullptr;
        if (std::string_view(collection) == "entities") {
          LOOM_TRY_ASSIGN(auto current, store.get_entity(run, id));
          if (current) expected = Sha256::hex(json::canonical(current->to_json()));
        } else if (std::string_view(collection) == "claims") {
          LOOM_TRY_ASSIGN(auto current, store.get_claim(run, id));
          if (current) expected = Sha256::hex(json::canonical(current->to_json()));
        } else {
          LOOM_TRY_ASSIGN(auto current, store.get_observation(run, id));
          if (current) expected = Sha256::hex(json::canonical(current->to_json()));
        }
        request["selection"][collection].push_back(id);
        request["expected_rows"][collection][id] = std::move(expected);
      }
    }
    // The store validates closure and CAS in its nested transaction while this
    // same-database snapshot remains locked. A conflict is returned unchanged.
    LOOM_TRY_ASSIGN(auto receipt, registry.accept(request));
    LOOM_TRY(snapshot.commit());
    return receipt;
  }
  Status prepare(std::string_view model, const Json& original_request, std::string_view primary_text,
                 std::string_view user_message_id) {
    LOOM_TRY(providers.load_builtin());
    if (const auto* manifests = json::find(options, "provider_manifests")) LOOM_TRY(providers.load(*manifests));
    const auto* profile = json::find(options, "profile");
    if (!profile) return Error(Errc::Unavailable, "graph reply method profile unavailable");
    std::vector<std::string> receipts;
    if (const auto* value = json::find(options, "receipt_ids")) {
      if (!value->is_array()) return Error(Errc::InvalidArgument, "graph reply receipt_ids must be an array");
      for (const auto& id : *value) {
        if (!id.is_string()) return Error(Errc::InvalidArgument, "graph reply receipt ID must be text");
        receipts.push_back(id.get<std::string>());
      }
    }
    LOOM_TRY_ASSIGN(auto snapshot, registry.load(*profile, receipts));
    Json capabilities{{"execution", Json{{"llm.chat.completions", Json{{"available", true},
        {"protocol", "openai_chat_completions"}}}}}, {"fusion", Json::object()}};
    LOOM_TRY_ASSIGN(auto resolved, registry.resolve(snapshot, options.value("selection", Json::object()), capabilities));
    result["method_resolution"] = resolved;
    // Reply composition needs one fully bound final provider request. The
    // registry still exposes every member; an unsupported composition is never
    // reduced silently to the first member of a combination.
    if (resolved["leaves"].size() != 1)
      return Error(Errc::Unsupported, "graph reply composition requires one final request method");
    const auto& leaf = resolved["leaves"][0];
    Json run = options.value("run_context", Json::object());
    if (!run.is_object()) return Error(Errc::InvalidArgument, "graph reply run_context must be an object");
    run["run_id"] = "e_chat_method_run_" + random_hex(32);
    run["known_at"] = timeutil::utc_now_iso();
    run["input_sha256"] = Sha256::hex(json::canonical(Json{{"original_request", original_request},
        {"primary_text", primary_text}, {"first_response_ref", first_response},
        {"user_message_id", user_message_id}, {"selected_leaf", leaf},
        {"base_packet", options.value("base_packet", Json(nullptr))}}));
    run["request"] = original_request;
    run["messages"] = original_request.at("messages");
    run["model"] = model;
    run["primary_text"] = primary_text;
    run["user_message_id"] = user_message_id;
    run["first_response_ref"] = first_response;
    if (options.contains("base_packet")) run["base_packet"] = options["base_packet"];
    auto operation = chat::graph_reply_packet_operation;
    LOOM_TRY_ASSIGN(auto registered, registry.prepare(snapshot, leaf, run, operation));
    Json host = options.value("host", Json::object());
    if (!host.is_object()) return Error(Errc::InvalidArgument, "graph reply host must be an object");
    host["request_id"] = "chat_request_" + random_hex(32);
    host["turn_id"] = "chat_turn_" + random_hex(32);
    const auto& recipe = registered["effective_recipe"];
    const auto* actual_request = json::find(recipe, "request");
    if (!actual_request || !actual_request->is_object() ||
        json::get_string(*actual_request, "model").empty() || !actual_request->contains("messages") ||
        !(*actual_request)["messages"].is_array() || !actual_request->contains("stream") ||
        !(*actual_request)["stream"].is_boolean())
      return Error(Errc::InvalidArgument, "graph recipe must bind a complete provider request with model/messages/stream");
    host["model"] = (*actual_request)["model"];
    host["recipe_sha256"] = recipe.at("definition_sha256");
    host["known_at"] = run["known_at"];
    chat::GraphReplyCallbacks callbacks;
    callbacks.operation = operation;
    callbacks.bind_results = [this, origin = run.value("origin", Json(nullptr))](
        const Json& candidate, const Json& manifest, const Json& bindings) -> Result<Json> {
      Json actual = bindings;
      actual["origin"] = origin;
      actual["known_at"] = timeutil::utc_now_iso();
      actual["response_provenance"] = "first_provider_response_captured";
      return registry.bind_results(candidate, manifest, actual, chat::graph_reply_packet_operation);
    };
    callbacks.accept = [this](const Json& request) { return accept(request); };
    LOOM_TRY_ASSIGN(auto projected, chat::prepare_graph_reply(options, registered["packet"], host,
        recipe, registered["manifest"], std::move(callbacks)));
    if (!projected.append_messages.empty())
      return Error(Errc::Unsupported, "graph reply append_messages must be incorporated in the complete recipe request");
    prepared = std::move(projected);
    return {};
  }
  void capture_chunk(std::string_view bytes, std::string_view model) {
    wire_bytes.append(bytes);
    auto source = chat::retain_graph_reply_bytes(services, bytes, Json{{"channel", "transport_chunk"},
        {"ordinal", chunk_sources.size()}, {"model_origin", Json{{"kind", "model"}, {"model", nullptr}}},
        {"requested_model", model}, {"model_identity_basis", "requested_unverified"}});
    if (source) chunk_sources.push_back(*source);
    else capture_failed = true;
  }
  void capture_final(std::string_view model, bool cancelled, int status) {
    auto source = chat::retain_graph_reply_bytes(services, wire_bytes, Json{{"channel", "first_transport_response"},
        {"chunk_sources", chunk_sources}, {"cancelled", cancelled}, {"status", status},
        {"model_origin", Json{{"kind", "model"}, {"model", nullptr}}},
        {"requested_model", model}, {"model_identity_basis", "requested_unverified"}});
    if (source) first_response = *source;
    else { capture_failed = true; first_response = Json{{"source_status", "unavailable"},
        {"blob_hash", Sha256::hex(wire_bytes)}, {"bytes", wire_bytes.size()}}; }
  }
  void settle(const Json& usage) {
    if (!guard || settled) return;
    settled = true;
    auto accounting = guard->settle(attempted, wire_bytes.size(), usage);
    if (!accounting) result["usage_settlement_error"] = std::string(errc_name(accounting.error().code));
    else result["usage_settlement"] = *accounting;
    result["execution_usage"] = guard->decisions();
  }
};

Result<context::ContextRequest> chat_context_request(const Json& value) {
  if (!value.is_object()) return Error(Errc::InvalidArgument, "knowledge_context must be an object");
  for (auto it = value.begin(); it != value.end(); ++it) {
    const auto& k = it.key();
    const auto& v = it.value();
    bool valid = false;
    if (k == "text" || k == "project" || k == "run" || k == "lang") valid = v.is_string();
    else if (k == "goal_type") valid = v.is_string() || v.is_null();
    else if (k == "targets") {
      valid = v.is_array() && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_string(); });
    } else if (k == "budget_tokens") {
      valid = v.is_number_integer() && v > 0 && v <= std::numeric_limits<int>::max();
    } else if (k == "relation_hops") {
      valid = v.is_number_integer() && v >= 0 && v <= std::numeric_limits<int>::max();
    } else if (k == "detail_resolution") {
      valid = v.is_null() || v == "label" || v == "summary" || v == "full" || v == "raw";
    } else if (k == "plan" || k == "claim_targets" || k == "include_counter_evidence" ||
               k == "candidate_channels" || k == "candidate_scan_limit" || k == "lexical_shadow") {
      // These extensions have strict nested validators in ContextRequest.
      // Keep one schema: permit only their names here, then delegate below.
      // In particular plan.source_ref is caller-declared provenance, not an
      // ActiveTaskSpec binding or permission to compile/infer task statements.
      valid = true;
    }
    if (!valid) return Error(Errc::InvalidArgument, "invalid knowledge_context option: " + k);
  }
  return context::ContextRequest::from_json(value);
}

Result<std::size_t> active_memory_cap(const Json& execution) {
  const auto* unified = json::find(execution, "unified");
  if (!unified || !unified->value("enabled", false)) return std::size_t(16000);
  const auto* value = json::find(*unified, "memory_max_chars");
  if (!value) return std::size_t(16000);
  if (!value->is_number_integer() || (!value->is_number_unsigned() && value->get<std::int64_t>() < 0))
    return Error(Errc::InvalidArgument, "unified memory_max_chars must be a nonnegative supported integer");
  const auto number = value->get<std::uint64_t>();
  if (number > std::numeric_limits<std::size_t>::max())
    return Error(Errc::InvalidArgument, "unified memory_max_chars must be a nonnegative supported integer");
  const auto cap = static_cast<std::size_t>(number);
  return cap == 0 ? std::numeric_limits<std::size_t>::max() : cap;
}

Json native_history_row(const Message& message) {
  return Json{{"id", message.id}, {"conv_id", message.conv_id}, {"status", message.status},
              {"role", message.role}, {"text", message.text}, {"weight", message.weight}};
}

Result<Json> native_history_projection(Database& db, std::string_view conversation, bool include_history) {
  Json projection = Json::array();
  if (!include_history || conversation.empty()) return projection;
  auto messages = db.get_msgs(conversation);
  if (!messages) return messages.error();
  for (const auto& message : *messages) {
    if (message.status == msg_status::kActive) projection.push_back(native_history_row(message));
  }
  return projection;
}

Result<Json> native_request_inputs(Database& db, std::string_view conversation) {
  auto messages = db.get_msgs(conversation, true);
  if (!messages) return messages.error();
  Json snapshot = Json::array();
  for (const auto& message : *messages) snapshot.push_back(message.to_json());
  return snapshot;
}

Result<Json> prepare_active_task(Database& db, std::string_view conv_id, const ChatOptions& opts,
                                const std::map<std::string, Json>& in_flight = {}) {
  auto invalid = [](std::string_view reason) -> Result<Json> {
    return Error(Errc::InvalidArgument, "active_task: " + std::string(reason));
  };
  if (!opts.active_task_spec) {
    if (!opts.active_task_bindings.is_object() || !opts.active_task_bindings.empty() ||
        opts.active_task_history != "replace_refinement") {
      return invalid("bindings and history mode require a specification");
    }
    return Json(nullptr);
  }
  if (opts.active_task_history != "replace_refinement" && opts.active_task_history != "append") {
    return invalid("unknown history mode");
  }
  auto compiled = chat::compile_active_task_spec(*opts.active_task_spec);
  if (!compiled) return compiled.error();
  const auto& spec = *compiled;
  if (conv_id.empty() || spec["scope"]["conversation_id"] != conv_id) {
    return invalid("specification must name the current existing conversation");
  }
  if (spec["scope"]["branch_id"] != "native:active") {
    return invalid("only the native:active source selection is supported");
  }
  const auto& bindings = opts.active_task_bindings;
  if (!bindings.is_object() || bindings.size() != spec["history_event_ids"].size()) {
    return invalid("bindings must cover exactly the declared history events");
  }
  Json sources = Json::array();
  std::set<std::string> message_ids;
  std::map<std::string, std::string> source_texts;
  for (const auto& event : spec["history_event_ids"]) {
    const auto id = event.get<std::string>();
    auto bound = bindings.find(id);
    if (bound == bindings.end() || !bound->is_object() || bound->size() != 2 ||
        !bound->contains("message_id") || !(*bound)["message_id"].is_string() ||
        !bound->contains("text_sha256") || !(*bound)["text_sha256"].is_string()) {
      return invalid("each event needs a message_id and text_sha256 binding");
    }
    const auto mid = (*bound)["message_id"].get<std::string>();
    if (mid.empty() || !message_ids.insert(mid).second) return invalid("message bindings must be nonempty and unique");
    auto message = db.get_msg(mid);
    if (!message) return message.error();
    if (!*message || (**message).conv_id != conv_id || (**message).status != msg_status::kActive) {
      return invalid("bound message is absent or outside the active conversation selection");
    }
    const auto& m = **message;
    if (!utf8::is_valid(m.text)) return invalid("bound source text must be valid UTF-8");
    const auto digest = Sha256::hex(m.text);
    if ((*bound)["text_sha256"] != digest) return invalid("bound source text changed");
    source_texts.emplace(id, m.text);
    sources.push_back(Json{{"event_id", id}, {"message_id", mid}, {"role", m.role}, {"text", m.text},
                           {"text_sha256", digest}, {"status", m.status},
                           {"created", m.created}, {"attachments", m.attachments},
                           {"version_group_id", m.version_group_id ? Json(*m.version_group_id) : Json(nullptr)},
                           {"version_num", m.version_num}});
  }
  for (const auto& source : spec["source_refs"]) {
    if (source.contains("quote") &&
        source_texts.at(source["event_id"].get<std::string>()).find(source["quote"].get<std::string>()) == std::string::npos) {
      return invalid("source quote does not occur in the bound native message");
    }
  }

  auto history = chat::read_active_task_history(db, conv_id, in_flight);
  if (!history) return history.error();
  const auto& retained_tasks = history->snapshots;
  const Json* latest = nullptr;
  std::map<std::string, const Json*> products;
  std::map<std::uint64_t, std::string> versions;
  for (auto retained = retained_tasks.begin(); retained != retained_tasks.end(); ++retained) {
    if (!chat::valid_active_task_snapshot(*retained)) {
      return invalid("retained revision metadata is malformed");
    }
    auto old = retained->find("supplied_spec");
    if ((*old)["product_ref"] == spec["product_ref"] &&
        (*old != *opts.active_task_spec || retained->value("bindings", Json()) != bindings ||
         (*retained)["source_messages"] != sources)) {
      return invalid("a product identifier cannot be reused for different content or bindings");
    }
    if (chat::active_task_scope_key((*old)["scope"]) != chat::active_task_scope_key(spec["scope"])) continue;
    const auto product_id = (*old)["product_ref"]["id"].get<std::string>();
    auto [product, added] = products.emplace(product_id, &*retained);
    if (!added && !chat::same_active_task_identity(*product->second, *retained)) {
      return invalid("retained product identity has conflicting content");
    }
    auto [version, first] = versions.emplace((*old)["version"].get<std::uint64_t>(), product_id);
    if (!first && version->second != product_id) return invalid("retained task has competing version identities");
    if (!latest || (*old)["version"] > (*latest)["supplied_spec"]["version"]) latest = &*retained;
  }
  // Recover the exact ancestry, not every object with a coincidentally equal
  // goal or version. This keeps source coverage when a new spec binds only its
  // new refinement messages; it never merges or invents statement content.
  std::vector<const Json*> ancestry;
  std::set<std::string> seen_products;
  for (const Json* ancestor = latest; ancestor;) {
    const auto& old = (*ancestor)["supplied_spec"];
    const auto id = old["product_ref"]["id"].get<std::string>();
    if (!seen_products.insert(id).second || old["goal_id"] != spec["goal_id"]) {
      return invalid("retained task ancestry is cyclic or changes goal");
    }
    ancestry.push_back(ancestor);
    const auto number = old["version"].get<std::uint64_t>();
    if (old["previous_product_ref"].is_null()) {
      if (number != 1) return invalid("retained ancestry has no version-one root");
      break;
    }
    auto previous = products.find(old["previous_product_ref"]["id"].get<std::string>());
    if (number <= 1 || previous == products.end() ||
        (*previous->second)["supplied_spec"]["version"].get<std::uint64_t>() != number - 1) {
      return invalid("retained ancestry has a missing or inconsistent predecessor");
    }
    ancestor = previous->second;
  }
  if (latest) {
    const auto& old = (*latest)["supplied_spec"];
    const bool replay = old["product_ref"] == spec["product_ref"] && old == *opts.active_task_spec &&
                        (*latest)["bindings"] == bindings;
    const auto previous_version = old["version"].get<std::uint64_t>();
    if (!replay && (spec["previous_product_ref"] != old["product_ref"] ||
                   previous_version == std::numeric_limits<std::uint64_t>::max() ||
                   spec["version"].get<std::uint64_t>() != previous_version + 1 ||
                   spec["goal_id"] != old["goal_id"])) {
      return invalid("revision must extend the latest retained product of this task and goal");
    }
  } else if (spec["version"] != 1 || !spec["previous_product_ref"].is_null()) {
    return invalid("a new task must start with version 1 and no predecessor");
  }
  Json covered = Json::array();
  Json inherited = Json::array();
  for (const auto& source : sources) covered.push_back(source["message_id"]);
  std::set<std::string> covered_ids = message_ids;
  for (const auto* ancestor : ancestry) {
    for (const auto& source : (*ancestor)["source_messages"]) {
      const auto mid = source["message_id"].get<std::string>();
      // Current explicit bindings, then the most recent ancestor, take priority.
      if (!covered_ids.insert(mid).second) continue;
      if (opts.include_history && opts.active_task_history == "replace_refinement") {
        auto live = db.get_msg(mid);
        if (!live) return live.error();
        if (*live && (**live).status == msg_status::kActive && (**live).conv_id == conv_id &&
            source["text_sha256"] != Sha256::hex((**live).text)) {
          return invalid("inherited active source changed; explicitly rebind it before replacing history");
        }
        // Native edits create a new active row in the same version group.
        // Its bytes are not covered by the accepted ancestor's snapshot.
        if (source.contains("version_group_id") && source["version_group_id"].is_string()) {
          auto siblings = db.get_versions(source["version_group_id"].get<std::string>());
          if (!siblings) return siblings.error();
          for (const auto& sibling : *siblings) {
            if (sibling.status == msg_status::kActive && sibling.conv_id == conv_id &&
                sibling.id != mid && !covered_ids.contains(sibling.id)) {
              return invalid("inherited source has an active edited version; explicitly bind it before replacing history");
            }
          }
        }
      }
      covered.push_back(mid);
      inherited.push_back(Json{{"product_ref", (*ancestor)["supplied_spec"]["product_ref"]},
                               {"source_message", source}});
    }
  }
  return Json{{"schema", "loom.chat_active_task/1"}, {"supplied_spec", *opts.active_task_spec},
              {"compiled_spec", *compiled}, {"bindings", bindings}, {"source_messages", sources},
              {"history_coverage_message_ids", covered}, {"inherited_source_messages", inherited},
              {"history_mode", opts.active_task_history}, {"acceptance", "explicit_caller_supplied"},
              {"compiler", {{"id", "loom.active_task_renderer"}, {"version", "1"}}},
              {"binding_verification", "native_message_text_sha256_and_optional_quote"},
              {"source_selection", "native:active"}};
}

}  // namespace

Result<ChatOptions> ChatOptions::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "chat request must be a JSON object");
  ChatOptions o;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    const Json& v = it.value();
    if (v.is_null() || k == "message" || k == "request_id") continue;
    auto bad = [&] { return Error(Errc::InvalidArgument, "invalid type for chat option: " + k); };
    if (k == "conv_id" || k == "model" || k == "reasoning_effort" || k == "system_prompt") {
      if (!v.is_string()) return bad();
      std::string s = v.get<std::string>();
      if (k == "conv_id") o.conv_id = s;
      if (k == "model") o.model = s;
      if (k == "reasoning_effort") o.reasoning_effort = s;
      if (k == "system_prompt") o.system_prompt = s;
    } else if (k == "attachments") {
      if (!v.is_array()) return bad();
      for (const auto& a : v) {
        if (!a.is_string()) return bad();
        o.attachments.push_back(a.get<std::string>());
      }
    } else if (k == "web_search" || k == "deep_research" || k == "stream" ||
               k == "include_memory" || k == "include_graph_memory" || k == "include_history" || k == "trace_context") {
      if (!v.is_boolean()) return bad();
      if (k == "web_search") o.web_search = v.get<bool>();
      if (k == "deep_research") o.deep_research = v.get<bool>();
      if (k == "stream") o.stream = v.get<bool>();
      if (k == "include_memory") o.include_memory = v.get<bool>();
      if (k == "include_graph_memory") o.include_graph_memory = v.get<bool>();
      if (k == "include_history") o.include_history = v.get<bool>();
      if (k == "trace_context") o.trace_context = v.get<bool>();
    } else if (k == "knowledge_context") {
      auto context = chat_context_request(v);
      if (!context) return context.error();
      o.knowledge_context = *context;
    } else if (k == "active_task_spec") {
      auto compiled = chat::compile_active_task_spec(v);
      if (!compiled) return compiled.error();
      o.active_task_spec = v;
    } else if (k == "active_task_bindings") {
      if (!v.is_object()) return bad();
      o.active_task_bindings = v;
    } else if (k == "active_task_history") {
      if (!v.is_string() || (v != "replace_refinement" && v != "append")) return bad();
      o.active_task_history = v.get<std::string>();
    } else if (k == "temperature") {
      if (!v.is_number()) return bad();
      o.temperature = v.get<double>();
    } else if (k == "max_tokens" || k == "context_depth") {
      if (!v.is_number_integer()) return bad();
      if (k == "max_tokens") o.max_tokens = v.get<int>();
      if (k == "context_depth") o.context_depth = v.get<int>();
    } else {
      return Error(Errc::InvalidArgument, "unknown chat option: " + k);
    }
  }
  if (!o.active_task_spec && (j.contains("active_task_bindings") || j.contains("active_task_history"))) {
    return Error(Errc::InvalidArgument, "active_task options require active_task_spec");
  }
  return o;
}

Json ChatResult::to_json() const {
  Json out{{"conv_id", conv_id},
              {"user_message_id", user_message_id},
              {"assistant_message_id", assistant_message_id},
              {"text", text},
              {"reasoning", reasoning ? Json(*reasoning) : Json(nullptr)},
              {"model", model},
              {"usage", usage},
              {"title", new_title ? Json(*new_title) : Json(nullptr)},
              {"cancelled", cancelled}};
  if (!context_trace.is_null()) out["context_trace"] = context_trace;
  return out;
}

ChatEngine::ChatEngine(const Config& cfg, const Secrets& secrets, Database& db, EventBus& bus,
                       net::HttpTransport& http, const SemanticAnalyzer& analyzer, MemoryEngine* memory,
                       GraphMemorySelector* graph_memory)
    : cfg_(cfg),
      secrets_(secrets),
      db_(db),
      bus_(bus),
      http_(http),
      analyzer_(analyzer),
      memory_(memory),
      graph_memory_(graph_memory) {}

void ChatEngine::set_knowledge_context_builder(KnowledgeContextBuilder builder) {
  std::lock_guard lk(mu_);
  knowledge_context_builder_ = std::move(builder);
}

Status ChatEngine::resume_last() {
  auto r = db_.list_convs(1);
  if (!r) return r.error();
  std::lock_guard lk(mu_);
  conv_ = r->empty() ? std::nullopt : std::optional<Conversation>(r->front());
  return ok_status();
}

Result<Conversation> ChatEngine::new_conv(std::string_view title) {
  auto r = db_.create_conv(title.empty() ? std::string_view("New Chat") : title);
  if (!r) return r.error();
  std::lock_guard lk(mu_);
  conv_ = *r;
  return *r;
}

Result<std::optional<Conversation>> ChatEngine::load_conv(std::string_view conv_id) {
  auto r = db_.get_conv(conv_id);
  if (!r) return r.error();
  std::lock_guard lk(mu_);
  conv_ = *r;
  return *r;
}

std::optional<Conversation> ChatEngine::current_conv() const {
  std::lock_guard lk(mu_);
  return conv_;
}

Json ChatEngine::build_content(std::string_view text, const std::vector<std::string>& attachments) const {
  if (attachments.empty()) return Json(std::string(text));

  Json parts = Json::array();
  parts.push_back(Json{{"type", "text"}, {"text", std::string(text)}});

  for (const auto& path_str : attachments) {
    std::filesystem::path p(path_str);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
      log::warn("loom.chat_engine", "Attachment not found: {}", path_str);
      continue;
    }
    std::string ext = lower_ext(p);
    if (contains(image_exts(), ext)) {
      auto data = fsutil::read_file(p);
      if (!data) {
        log::error("loom.chat_engine", "Failed to encode image: {}", path_str);
        continue;
      }
      std::string mime = "image/" + ext.substr(1);
      if (mime == "image/jpg") mime = "image/jpeg";
      parts.push_back(Json{{"type", "image_url"},
                           {"image_url", Json{{"url", "data:" + mime + ";base64," + base64::encode(*data)}}}});
    } else if (contains(text_exts(), ext)) {
      auto data = fsutil::read_file(p);
      if (!data) {
        log::error("loom.chat_engine", "Failed to read text file: {}", path_str);
        continue;
      }
      std::string repaired = utf8::repair(*data);
      std::string file_text(utf8::prefix(repaired, kMaxFileChars));
      std::string name = p.filename().string();
      parts.push_back(
          Json{{"type", "text"}, {"text", "\n--- FILE: " + name + " ---\n" + file_text + "\n--- END FILE ---"}});
    }
  }
  return parts;
}

Result<Json> ChatEngine::build_messages(std::string_view conv_id, std::string_view current_text,
                                        const std::vector<std::string>& attachments, const ChatOptions& opts,
                                        std::string_view exclude_message_id, Json* context_trace) {
  const bool capture_native_history = context_trace && context_trace->is_object() &&
      context_trace->value("_capture_native_history", Json()) == true;
  std::unique_lock<std::recursive_mutex> source_lock;
  if (opts.active_task_spec) source_lock = db_.lock();
  auto active_task = prepare_active_task(db_, conv_id, opts, active_task_in_flight_);
  if (!active_task) return active_task.error();
  Json messages = Json::array();
  Json compiled_context;
  Json resolved_context_request;
  Json history_ids = Json::array();
  Json replaced_ids = Json::array();
  Json captured_native_history = Json::array();
  std::set<std::string> refinement_ids;
  if (opts.active_task_spec && opts.active_task_history == "replace_refinement") {
    for (const auto& mid : (*active_task)["history_coverage_message_ids"]) {
      refinement_ids.insert(mid.get<std::string>());
    }
  }

  std::string sys_prompt = opts.system_prompt ? *opts.system_prompt : cfg_string(cfg_, "system_prompt");
  if (!sys_prompt.empty()) messages.push_back(Json{{"role", "system"}, {"content", sys_prompt}});

  // The configured modern recipe is explicit; historical callers retain their
  // compatible message order. Preview compilation never creates an execution
  // scope, so configured provider credentials cannot turn it into a paid call.
  const Json execution = context::current_context_execution_scope()
      ? context::current_context_execution_scope()->options() : cfg_.get("context_execution", Json::object());
  LOOM_TRY(context::validate_context_execution_options(execution));
  const Json unified_options = execution.value("unified", Json::object());
  if (!unified_options.is_object() || (unified_options.contains("enabled") && !unified_options["enabled"].is_boolean()))
    return Error(Errc::InvalidArgument, "context_execution.unified must be an object with boolean enabled");
  const bool unified = unified_options.value("enabled", false);
  Json unified_context;

  if (!unified && memory_ && opts.include_memory) {
    std::string mem_ctx = memory_->get_active_context();
    if (!mem_ctx.empty()) {
      messages.push_back(Json{{"role", "system"}, {"content", "User's memory/context:\n" + mem_ctx}});
    }
  }

  bool graph_disabled = opts.context_depth.has_value() && *opts.context_depth == 0;
  if (!unified && graph_memory_ && opts.include_graph_memory && !graph_disabled) {
    try {
      GraphSelectOptions gopts;
      if (opts.context_depth && *opts.context_depth > 0) gopts.depth = *opts.context_depth;
      if (!conv_id.empty()) gopts.current_conv_id = std::string(conv_id);
      std::string graph_ctx = graph_memory_->select_context(current_text, gopts);
      if (!graph_ctx.empty()) messages.push_back(Json{{"role", "system"}, {"content", graph_ctx}});
    } catch (const std::exception& e) {
      log::debug("loom.chat_engine", "Graph memory selection failed: {}", e.what());
    }
  }

  if (opts.knowledge_context || unified) {
    context::ContextRequest request;
    if (opts.knowledge_context) request = *opts.knowledge_context;
    else if (const auto* configured = json::find(execution, "request")) {
      auto parsed = chat_context_request(*configured);
      if (!parsed) return parsed.error();
      request = *parsed;
    }
    if (request.budget_tokens <= 0) return Error(Errc::InvalidArgument, "knowledge_context budget_tokens must be positive");
    if (request.text.empty()) request.text = current_text;
    KnowledgeContextBuilder builder;
    {
      std::lock_guard lk(mu_);
      builder = knowledge_context_builder_;
    }
    const bool include_knowledge = !unified || unified_options.value("include_knowledge", true);
    if (include_knowledge) {
      if (!builder && (!unified || opts.knowledge_context))
        return Error(Errc::NotImplemented, "knowledge context is unavailable on this ChatEngine");
      if (builder) {
        auto compiled = builder(request);
        if (!compiled) {
          // A modern legacy-only recipe can run before its first KB snapshot.
          // Explicit run/goal requests still fail rather than hiding a typo.
          if (!unified || opts.knowledge_context || !request.run.empty() ||
              (request.goal_type && !request.goal_type->empty()) || compiled.error().code != Errc::NotFound)
            return compiled.error();
        } else {
          if (!compiled->is_object() || !compiled->contains("prompt") || !(*compiled)["prompt"].is_string() ||
              !compiled->contains("context_set") || !(*compiled)["context_set"].is_object()) {
            return Error(Errc::Internal, "knowledge context builder returned an invalid result");
          }
          compiled_context = *compiled;
        }
      }
    }
    resolved_context_request = compiled_context.is_object() ? compiled_context.value("request", request.to_json()) : request.to_json();
    if (unified) {
      Json settings = unified_options;
      if (opts.context_depth) settings["legacy_relation_hops"] = *opts.context_depth;
      else if (!settings.contains("legacy_relation_hops")) settings["legacy_relation_hops"] = cfg_.get("graph_memory_depth", 2);
      auto selected = context::compile_unified_context(db_, cfg_, graph_memory_, memory_, request,
          compiled_context, conv_id, opts.include_memory, opts.include_graph_memory && !graph_disabled, settings);
      if (!selected) return selected.error();
      unified_context = *selected;
      const auto prompt = json::get_string(unified_context, "prompt");
      if (!prompt.empty()) messages.push_back(Json{{"role", "system"}, {"content", prompt}});
    } else {
      const auto prompt = json::get_string(compiled_context, "prompt");
      if (!prompt.empty()) messages.push_back(Json{{"role", "system"}, {"content", prompt}});
    }
  }

  if (!conv_id.empty() && opts.include_history) {
    auto msgs = db_.get_msgs(conv_id);
    if (!msgs) return msgs.error();
    for (const auto& m : *msgs) {
      if (m.status != msg_status::kActive) continue;
      if (capture_native_history) captured_native_history.push_back(native_history_row(m));
      if (!exclude_message_id.empty() && m.id == exclude_message_id) continue;
      if (refinement_ids.count(m.id)) {
        replaced_ids.push_back(m.id);
        continue;
      }
      std::string content = m.text;
      if (m.weight > 1.5) {
        content = "[IMPORTANT] " + content;
      } else if (m.weight < 0.5) {
        content = "[low priority] " + content;
      }
      messages.push_back(Json{{"role", m.role}, {"content", content}});
      history_ids.push_back(m.id);
    }
  }

  if (opts.active_task_spec) {
    messages.push_back(Json{{"role", "user"},
      {"content", "[Active task specification; derived from the selected source messages]\n" +
        (*active_task)["compiled_spec"]["compiled_instruction"]["text"].get<std::string>()}});
  }
  messages.push_back(Json{{"role", "user"}, {"content", build_content(current_text, attachments)}});
  if (context_trace) {
    *context_trace = Json{{"kind", "compiled_messages"}, {"version", 1},
                         {"messages", messages}, {"messages_sha256", Sha256::hex(json::dump(messages))},
                         {"history_message_ids", history_ids},
                         {"selection", {{"include_memory", opts.include_memory},
                                        {"include_graph_memory", opts.include_graph_memory && !graph_disabled},
                                        {"include_history", opts.include_history},
                                        {"context_depth", opts.context_depth ? Json(*opts.context_depth) : Json(nullptr)}}},
                         {"knowledge_context_request", resolved_context_request},
                         {"knowledge_context", compiled_context}};
    if (unified) (*context_trace)["unified_context"] = unified_context;
    if (opts.active_task_spec) {
      (*context_trace)["active_task"] = *active_task;
      (*context_trace)["replaced_history_message_ids"] = replaced_ids;
    }
    if (capture_native_history) (*context_trace)["_native_history_snapshot"] = std::move(captured_native_history);
  }
  return messages;
}

void ChatEngine::configure_reasoning(Json& payload, std::string_view model, const std::optional<std::string>& effort) {
  // Compatibility API has no configuration argument. Its preset is generated
  // from the same canonical data used by send(), never a second C++ recipe.
  static const auto recipe = chat::reasoning_recipe();
  if (!recipe) throw std::runtime_error(recipe.error().message);
  chat::apply_reasoning_recipe(payload, model, effort, *recipe);
}

std::optional<std::string> ChatEngine::last_reasoning() const {
  std::lock_guard lk(mu_);
  return last_reasoning_;
}

Result<ChatResult> ChatEngine::send(std::string_view text, const ChatOptions& opts, const ChatCallbacks& cb,
                                    const CancelToken* cancel) {
  const Json execution = cfg_.get("context_execution", Json::object());
  LOOM_TRY(context::validate_context_execution_options(execution));
  const Json reasoning_options = cfg_.get("chat_reasoning", Json::object());
  LOOM_TRY_ASSIGN(auto reasoning_recipe, chat::reasoning_recipe(reasoning_options));
  const bool reasoning_profile_explicit = !reasoning_options.empty();
  const Json graph_options = execution.value("graph_reply", Json::object());
  if (!graph_options.is_object() || (graph_options.contains("mode") && !graph_options["mode"].is_string()))
    return Error(Errc::InvalidArgument, "context_execution.graph_reply must be an object with text mode");
  const auto graph_mode = json::get_string(graph_options, "mode", "off");
  const bool graph_enabled = graph_mode != "off";
  LOOM_TRY_ASSIGN(auto memory_cap, active_memory_cap(execution));
  context::ContextExecutionScope context_execution(execution);
  // 1. Resolve the conversation. Keep one lock order (database -> mu_)
  // shared with task compilation, and serialize implicit first-conversation
  // creation so concurrent sends do not unexpectedly create separate chats.
  Conversation conv;
  {
    auto resolution_lock = db_.lock();
    if (opts.active_task_spec && db_.conn().in_transaction()) {
      return Error(Errc::InvalidArgument,
        "active_task: acceptance cannot run inside an externally owned transaction");
    }
    std::lock_guard lk(mu_);
    if (opts.conv_id) {
      auto r = db_.get_conv(*opts.conv_id);
      if (!r) return r.error();
      if (!*r) return Error(Errc::NotFound, "conversation not found: " + *opts.conv_id);
      conv = **r;
    } else if (conv_) {
      conv = *conv_;
    } else {
      if (opts.active_task_spec) {
        return Error(Errc::InvalidArgument, "active_task requires an existing conversation");
      }
      auto r = db_.create_conv("New Chat");
      if (!r) return r.error();
      conv = *r;
    }
    conv_ = conv;
  }

  // Compile a task against a stable native source selection before accepting
  // the current turn. The same snapshot becomes both the request and its
  // retained provenance; callbacks cannot change it between those operations.
  const bool record_context = opts.trace_context.value_or(opts.active_task_spec.has_value() ||
      opts.knowledge_context.has_value() || !opts.include_memory ||
      !opts.include_graph_memory || !opts.include_history ||
      execution.value("unified", Json::object()).value("enabled", false) ||
      execution.value("goal_typing", Json::object()).value("enabled", false) ||
      execution.value("embedding", Json::object()).value("enabled", false) || graph_enabled);
  Json context_trace;
  Json task_messages;
  Json accepted_active_task;
  struct InFlightTaskGuard {
    Database& db;
    std::map<std::string, Json>& tasks;
    std::string message_id;
    ~InFlightTaskGuard() {
      if (!message_id.empty()) {
        auto lock = db.lock();
        tasks.erase(message_id);
      }
    }
  } in_flight_guard{db_, active_task_in_flight_, {}};
  std::unique_lock<std::recursive_mutex> source_lock;
  std::unique_ptr<sql::Txn> acceptance_tx;
  chat::ActiveTaskHistory acceptance_history;
  if (opts.active_task_spec) {
    source_lock = db_.lock();
    if (db_.conn().in_transaction()) {
      return Error(Errc::InvalidArgument,
        "active_task: acceptance cannot run inside an externally owned transaction");
    }
    auto before = native_request_inputs(db_, conv.id);
    if (!before) return before.error();
    const auto prepared_system = opts.system_prompt.value_or(cfg_string(cfg_, "system_prompt"));
    const auto prepared_memory = memory_ && opts.include_memory ? memory_->get_active_context(memory_cap) : std::string{};
    // Arbitrary context builders run outside SQL transactions. Their returned
    // graph/knowledge/file context is frozen derived content, not a promise
    // that independent stores or external files remain globally current.
    context_trace = Json{{"_capture_native_history", true}};
    auto built = build_messages(conv.id, text, opts.attachments, opts, "", &context_trace);
    if (!built) return built.error();
    auto captured = context_trace.find("_native_history_snapshot");
    if (captured == context_trace.end() || !captured->is_array())
      return Error(Errc::Internal, "active_task: actual native history was not captured");
    Json prepared_native_history = std::move(*captured);
    context_trace.erase("_native_history_snapshot"); // internal validation, not a public trace field
    if (memory_ && opts.include_memory && execution.value("unified", Json::object()).value("enabled", false)) {
      const auto& consumed = context_trace["unified_context"];
      if (consumed.value("memory_input_sha256", "") != Sha256::hex(prepared_memory) ||
          !consumed.value("memory_input_mapping_verified", false))
        return Error(Errc::InvalidArgument, "active_task: memory consumed during context preparation changed or could not be verified");
    }
    if (db_.conn().in_transaction()) {
      return Error(Errc::InvalidArgument,
        "active_task: context preparation left an externally owned transaction open");
    }
    acceptance_tx = std::make_unique<sql::Txn>(db_.conn(), true);
    LOOM_TRY(acceptance_tx->begin_status());
    // BEGIN IMMEDIATE serializes the final authority/source read across
    // independent Database objects, including distinct processes.
    auto after = native_request_inputs(db_, conv.id);
    if (!after) return after.error();
    auto rechecked = prepare_active_task(db_, conv.id, opts, active_task_in_flight_);
    if (!rechecked) return rechecked.error();
    auto current_history = native_history_projection(db_, conv.id, opts.include_history);
    if (!current_history) return current_history.error();
    // Equal before/after endpoints alone miss A -> B (used by the builder) -> A.
    // Validate the native rows actually consumed when composing this array.
    if (*before != *after || *current_history != prepared_native_history ||
        *rechecked != context_trace["active_task"] ||
        prepared_system != opts.system_prompt.value_or(cfg_string(cfg_, "system_prompt")) ||
        prepared_memory != (memory_ && opts.include_memory ? memory_->get_active_context(memory_cap) : std::string{})) {
      return Error(Errc::InvalidArgument,
        "active_task: request inputs changed during context preparation; request was not accepted");
    }
    auto history = chat::read_active_task_history(db_, conv.id, active_task_in_flight_);
    if (!history) return history.error();
    acceptance_history = std::move(*history);
    task_messages = std::move(*built);
    accepted_active_task = context_trace["active_task"];
  } else {
    // Direct C++ callers can bypass from_json; do not accept orphan controls.
    auto checked = prepare_active_task(db_, conv.id, opts);
    if (!checked) return checked.error();
  }

  // 2. Persist the user row and acceptance atomically. create_msg nests a
  // savepoint; only the outer commit makes acceptance durable.
  NewMessage nm;
  nm.conv_id = conv.id;
  nm.text = std::string(text);
  nm.role = "user";
  if (opts.active_task_spec) {
    nm.metadata = Json{{"active_task", accepted_active_task}};
    if (record_context) nm.metadata["context_trace"] = context_trace;
    else context_trace = nullptr;
  }
  if (!opts.attachments.empty()) {
    Json atts = Json::array();
    for (const auto& a : opts.attachments) atts.push_back(a);
    nm.attachments = atts;
  }
  auto umid = db_.create_msg(nm);
  if (!umid) return umid.error();
  std::string user_mid = *umid;
  if (opts.active_task_spec) {
    LOOM_TRY(chat::append_active_task_acceptance(db_, conv.id, user_mid, accepted_active_task, acceptance_history));
    LOOM_TRY(acceptance_tx->commit());
    acceptance_tx.reset();
    // Only masks temporary metadata projection divergence during callbacks;
    // the committed EventLog is the durable ancestry authority.
    in_flight_guard.message_id = user_mid;
    active_task_in_flight_.emplace(user_mid, accepted_active_task);
  }
  if (source_lock.owns_lock()) source_lock.unlock();

  auto retain_request_metadata = [&]() -> Status {
    if (!record_context && !opts.active_task_spec && !reasoning_profile_explicit) return ok_status();
    // Retain compilation even if the provider later fails. This is deliberately
    // not a RequestSnapshot or a claim that a provider received these messages.
    auto db_lock = db_.lock();
    auto user_message = db_.get_msg(user_mid);
    if (!user_message) return user_message.error();
    if (!*user_message) return Error(Errc::NotFound, "user message removed before accepted request metadata was retained");
    const bool current_message_changed = opts.active_task_spec && ((**user_message).conv_id != conv.id ||
        (**user_message).role != "user" || (**user_message).text != text ||
        (**user_message).attachments != nm.attachments);
    Json metadata = (**user_message).metadata;
    if (!metadata.is_object()) metadata = Json{{"loom_preserved_metadata", metadata}};
    if (reasoning_profile_explicit) metadata["reasoning_profile"] = reasoning_recipe.snapshot.inspection();
    if (opts.active_task_spec && (!metadata.contains("active_task") || metadata["active_task"] != accepted_active_task)) {
      // Callbacks may add arbitrary metadata or replace the entire column.
      // Retain their conflicting value as evidence, while keeping the exact
      // accepted task inspectable independently of trace opt-out.
      Json recovery{{"stage", "after_message_created_callbacks_before_transport"},
                    {"previous_active_task_present", metadata.contains("active_task")}};
      if (metadata.contains("active_task")) recovery["previous_active_task"] = metadata["active_task"];
      if (metadata.contains("loom_active_task_metadata_recovery")) {
        recovery["previous_recovery"] = metadata["loom_active_task_metadata_recovery"];
      }
      metadata["loom_active_task_metadata_recovery"] = std::move(recovery);
      metadata["active_task"] = accepted_active_task;
    }
    if (record_context) metadata["context_trace"] = context_trace;
    MsgPatch patch;
    patch.metadata = std::move(metadata);
    auto saved = db_.update_msg(user_mid, patch);
    if (!saved) return saved.error();
    if (current_message_changed) {
      if ((**user_message).conv_id != conv.id) {
        return Error(Errc::InvalidArgument,
          "active_task: current message moved after acceptance; metadata retained on moved row; request was not sent");
      }
      return Error(Errc::InvalidArgument, "active_task: current message changed after acceptance; request was not sent");
    }
    return ok_status();
  };

  try {
    bus_.emit(events::kMsgCreated,
             Json{{"id", user_mid}, {"text", std::string(text)}, {"conv_id", conv.id}, {"role", "user"}});
    if (cb.on_start) cb.on_start(conv.id, user_mid);
  } catch (...) {
    // Keep the original callback exception. Retention is best effort only if
    // the callback also removed/moved the row or the database itself failed.
    if (opts.active_task_spec) {
      try {
        auto retained = retain_request_metadata();
        if (!retained) log::warn("loom.chat_engine", "Failed to retain accepted task after callback exception: {}",
                                 retained.error().to_string());
      } catch (...) {
        log::warn("loom.chat_engine", "Accepted task metadata retention threw after callback exception");
      }
    }
    throw;
  }

  // 3. Build the message array (history excludes the message just persisted).
  auto msgs_json = opts.active_task_spec ? Result<Json>(std::move(task_messages)) :
      build_messages(conv.id, text, opts.attachments, opts, user_mid,
                     record_context ? &context_trace : nullptr);
  if (!msgs_json) return msgs_json.error();
  LOOM_TRY(retain_request_metadata());

  std::string model = opts.model.value_or(cfg_string(cfg_, "default_model"));

  // 5. Call the API.
  std::string key = secrets_.get_string("api_key");
  if (key.empty()) return Error(Errc::Auth, "No API key configured - open Settings");

  std::string base = rstrip_slash(cfg_string(cfg_, "base_url"));
  bool stream = opts.stream.value_or(cfg_bool(cfg_, "stream", true)) && static_cast<bool>(cb.on_chunk);

  double temperature = opts.temperature.value_or(cfg_number(cfg_, "temperature", 0.7));
  int max_tokens = opts.max_tokens.value_or(static_cast<int>(cfg_number(cfg_, "max_tokens", 4096)));

  Json payload{{"model", model}, {"messages", *msgs_json}, {"temperature", temperature},
              {"max_tokens", max_tokens}, {"stream", stream}};

  if (opts.web_search || opts.deep_research) {
    Json plugin{{"id", "web"}};
    if (opts.deep_research) {
      plugin["max_results"] = 10;
      payload["web_search_options"] = Json{{"search_context_size", "high"}};
    }
    payload["plugins"] = Json::array({plugin});
  }

  chat::apply_reasoning_recipe(payload, model, opts.reasoning_effort, reasoning_recipe);
  payload["include_reasoning"] = true;

  std::unique_ptr<ChatGraphExecution> graph;
  const bool graph_postprocess = graph_mode == "separate_model_afterwards" || graph_mode == "separate-model-postprocess";
  if (graph_enabled) {
    graph = std::make_unique<ChatGraphExecution>(cfg_, secrets_, db_, http_, graph_options);
    if (!graph_postprocess) {
      auto prepared = graph->prepare(model, payload, "", user_mid);
      if (!prepared) graph->issue("preparation_error", errc_name(prepared.error().code));
      else if (graph->prepared->capability.value("available", false)) {
        // Exact complete caller recipe, instantiated by the registry. No
        // late protocol/default patch can make its request hash misleading.
        payload = graph->prepared->request_patch;
        model = json::get_string(payload, "model");
        stream = payload["stream"].get<bool>();
        if (opts.active_task_spec) {
          const Json task_message{{"role", "user"}, {"content", "[Active task specification; derived from the selected source messages]\n" +
              context_trace["active_task"]["compiled_spec"]["compiled_instruction"]["text"].get<std::string>()}};
          if (std::find(payload["messages"].begin(), payload["messages"].end(), task_message) == payload["messages"].end())
            return Error(Errc::InvalidArgument, "graph recipe omitted the explicitly accepted active task instruction");
        }
        if (record_context) {
          context_trace["pre_graph_messages"] = context_trace["messages"];
          context_trace["pre_graph_messages_sha256"] = context_trace["messages_sha256"];
          context_trace["messages"] = payload["messages"];
          context_trace["messages_sha256"] = Sha256::hex(json::dump(payload["messages"]));
          LOOM_TRY(retain_request_metadata());
        }
      }
    }
  }

  net::HttpRequest req;
  req.method = "POST";
  req.url = base + "/chat/completions";
  req.headers = {
      {"Authorization", "Bearer " + key},
      {"Content-Type", "application/json"},
      {"HTTP-Referer", "https://github.com/chatadhd"},
      {"X-Title", "ChatADHD"},
  };
  req.body = json::dump(payload);
  req.timeout_ms = 180000;
  req.stream = stream;
  if (graph && graph->prepared && graph->prepared->capability.value("available", false) && !graph_postprocess) {
    const Json transport = graph_options.value("transport", Json::object());
    if (!transport.is_object() || transport.value("calls_authorized", Json(false)) != Json(true)) {
      graph->issue("authorization_required", "graph_reply_calls_not_authorized");
      if (record_context) { context_trace["graph_reply"] = graph->result; LOOM_TRY(retain_request_metadata()); }
      return Error(Errc::Paused, "graph reply calls require explicit authorization");
    }
    if (const auto* timeout = json::find(transport, "timeout_ms")) {
      if (!timeout->is_number_integer() || *timeout <= 0 || *timeout > std::numeric_limits<int>::max())
        return Error(Errc::InvalidArgument, "graph reply transport timeout must be a positive native integer");
      req.timeout_ms = timeout->get<int>();
    }
    graph->prepared->request_bytes = req.body;
    graph->prepared->binding_origin = graph_options.value("run_context", Json::object()).value("origin", Json(nullptr));
    graph->guard = std::make_unique<chat::ModelUsageGuard>(graph->services);
    auto admitted = graph->guard->admit(req, transport.value("usage_estimate", Json::object()),
        transport.value("confirmation", Json::object()));
    if (!admitted || !admitted->value("dispatch_authorized", false)) {
      graph->issue("usage_policy_pending", admitted ? json::get_string(*admitted, "status", "unavailable") :
          std::string(errc_name(admitted.error().code)));
      graph->result["execution_usage"] = graph->guard->decisions();
      if (admitted) graph->result["usage_decision"] = *admitted;
      if (record_context) { context_trace["graph_reply"] = graph->result; LOOM_TRY(retain_request_metadata()); }
      return Error(Errc::Paused, "graph reply model usage is not admitted; inspect retained context trace");
    }
  }

  std::string full_text, reasoning_text;
  std::string reported_model;
  Json usage = Json::object();
  bool cancelled = false;
  int response_status = 0;

  auto handle_event = [&](const net::SseEvent& evt) {
    if (cancel && cancel->cancelled()) return;
    if (evt.is_done()) return;
    auto parsed = json::parse(evt.data);
    if (!parsed) {
      if (graph) graph->transport_issue = "parse";
      log::debug("loom.chat_engine", "Skipped malformed SSE chunk: {}", std::string(utf8::prefix(evt.data, 100)));
      return;
    }
    if (graph) {
      if (const auto* name = json::find(*parsed, "model"); name && !name->is_null()) {
        if (!name->is_string()) graph->transport_issue = "provider_model_invalid";
        else if (!reported_model.empty() && reported_model != name->get<std::string>())
          graph->transport_issue = "provider_model_changed";
        else reported_model = name->get<std::string>();
      }
    }
    const Json* choices = json::find(*parsed, "choices");
    if (choices && choices->is_array() && !choices->empty()) {
      const Json* delta = json::find((*choices)[0], "delta");
      if (delta) {
        std::string chunk_text = json::get_string(*delta, "content");
        if (!chunk_text.empty()) {
          full_text += chunk_text;
          if (cb.on_chunk) cb.on_chunk(chunk_text);
          if (cancel && cancel->cancelled()) return;
        }
        std::string r = json::get_string(*delta, "reasoning");
        if (!r.empty()) {
          reasoning_text += r;
          if (cb.on_reasoning) cb.on_reasoning(r);
          if (cancel && cancel->cancelled()) return;
        }
      }
    }
    if (const Json* u = json::find(*parsed, "usage"); u && u->is_object() && !u->empty()) usage = *u;
  };

  if (stream) {
    net::SseParser parser;
    net::StreamSink sink;
    int status = 0;
    bool aborted = false;
    std::string err_body;

    sink.on_headers = [&](int s, const net::Headers&) {
      status = s;
      response_status = s;
      return true;
    };
    sink.on_data = [&](std::string_view chunk) -> bool {
      // Preserve each exact source chunk before parsing or a cancellation
      // callback. The final full wire source is independent of model content.
      if (graph) graph->capture_chunk(chunk, model);
      if (cancel && cancel->cancelled()) {
        aborted = true;
        return false;
      }
      if (status != 200) {
        err_body.append(chunk);
        return true;
      }
      parser.feed(chunk, handle_event);
      if (cancel && cancel->cancelled()) {
        aborted = true;
        return false;
      }
      return true;
    };

    Result<net::HttpResponse> resp = Error(Errc::Internal, "transport not attempted");
    if (graph) {
      graph->attempted = true;
      try { resp = http_.send(req, &sink, cancel); }
      catch (...) { resp = Error(Errc::Internal, "graph reply transport callback failed"); }
    } else resp = http_.send(req, &sink, cancel);
    bool was_cancelled = aborted || (cancel && cancel->cancelled()) || (!resp && resp.error().code == Errc::Cancelled);
    if (was_cancelled) {
      cancelled = true;
    } else if (!resp) {
      if (!graph) return resp.error();
      graph->transport_issue = std::string(errc_name(resp.error().code));
    } else if (status != 200) {
      if (!graph) return Error(Errc::Http, "API error " + std::to_string(status) + ": " + std::string(utf8::prefix(err_body, 500)));
      graph->transport_issue = "http";
      if (full_text.empty()) full_text = err_body;
    } else {
      if (graph) {
        try { parser.finish(handle_event); }
        catch (...) { graph->transport_issue = "callback_exception"; }
      } else parser.finish(handle_event);
      if (cancel && cancel->cancelled()) cancelled = true;
    }
  } else {
    Result<net::HttpResponse> resp = Error(Errc::Internal, "transport not attempted");
    if (graph) {
      graph->attempted = true;
      try { resp = http_.send(req, nullptr, cancel); }
      catch (...) { resp = Error(Errc::Internal, "graph reply transport callback failed"); }
      if (resp) {
        response_status = resp->status;
        graph->capture_chunk(resp->body, model);
        graph->capture_final(model, false, response_status);
      }
    } else resp = http_.send(req, nullptr, cancel);
    if (!resp) {
      if (resp.error().code == Errc::Cancelled) {
        cancelled = true;
      } else {
        if (!graph) return resp.error();
        graph->transport_issue = std::string(errc_name(resp.error().code));
      }
    } else if (resp->status != 200) {
      if (!graph) return Error(Errc::Http, "API error " + std::to_string(resp->status) + ": " + std::string(utf8::prefix(resp->body, 500)));
      graph->transport_issue = "http";
      full_text = resp->body;
    } else {
      auto j = resp->json();
      const Json* choices = j ? json::find(*j, "choices") : nullptr;
      const Json* message = choices && choices->is_array() && !choices->empty() ? json::find((*choices)[0], "message") : nullptr;
      if (!j || !message) {
        if (!graph) return Error(Errc::Parse, !j ? "invalid JSON response from chat completions" :
            (!choices || !choices->is_array() || choices->empty()) ? "response missing choices" : "response missing message");
        graph->transport_issue = "parse";
        full_text = resp->body;
      } else {
        if (graph) {
          if (const auto* name = json::find(*j, "model"); name && !name->is_null()) {
            if (!name->is_string()) graph->transport_issue = "provider_model_invalid";
            else reported_model = name->get<std::string>();
          }
        }
        full_text = json::get_string(*message, "content");
        reasoning_text = json::get_string(*message, "reasoning");
        if (const Json* u = json::find(*j, "usage"); u && u->is_object()) usage = *u;
      }
    }
  }

  if (graph) {
    if (stream || graph->first_response.empty()) graph->capture_final(model, cancelled, response_status);
    graph->settle(usage);
    const auto settlement = graph->result;
    if (graph_postprocess && !cancelled && graph->transport_issue.empty() && !graph->capture_failed) {
      auto prepared = graph->prepare(model, payload, full_text, user_mid);
      if (!prepared) graph->issue("preparation_error", errc_name(prepared.error().code), full_text);
      else {
        graph->prepared->binding_origin = graph_options.value("run_context", Json::object()).value("origin", Json(nullptr));
        graph->result = chat::run_graph_reply_postprocess(graph->services, *graph->prepared,
            graph->prepared->request_patch, graph_options.value("postprocess_transport", Json::object()),
            full_text, graph->first_response, cancel);
      }
    } else if (graph->prepared && graph->transport_issue.empty() && !cancelled && !graph->capture_failed) {
      if (!reported_model.empty()) graph->prepared->host["model"] = reported_model;
      graph->result = chat::finish_graph_reply(graph->services, *graph->prepared, full_text, full_text,
          graph->first_response, Json{{"requests", graph->attempted ? 1 : 0}, {"response_bytes", graph->wire_bytes.size()},
              {"provider_usage", usage}, {"requested_model", model},
              {"reported_model", reported_model.empty() ? Json(nullptr) : Json(reported_model)},
              {"model_identity_basis", reported_model.empty() ? "requested_unverified" : "provider_reported"}});
      const auto graph_status = json::get_string(graph->result, "status");
      const bool bound_first_text = json::get_string(graph->result, "display_text_source") == "first_response_output_binding";
      if ((graph_status == "candidate" || graph_status == "accepted" || bound_first_text) &&
          graph->result.contains("text") && graph->result["text"].is_string())
        full_text = graph->result["text"].get<std::string>();
    } else {
      if (graph->capture_failed) graph->issue("storage_error", "first_response_storage_failed", full_text);
      else if (cancelled) graph->issue("cancelled", "incomplete_response", full_text);
      else if (!graph->transport_issue.empty()) graph->issue("transport_error", graph->transport_issue, full_text);
      else { graph->result["text"] = full_text; graph->result["retained_text"] = full_text; }
    }
    if (graph_postprocess) graph->result["primary_response_ref"] = graph->first_response;
    if (!graph_postprocess || !graph->result.contains("first_response_ref")) graph->result["first_response_ref"] = graph->first_response;
    graph->result[graph_postprocess ? "primary_wire_response_sha256" : "wire_response_sha256"] = Sha256::hex(graph->wire_bytes);
    graph->result[graph_postprocess ? "primary_wire_response_bytes" : "wire_response_bytes"] = graph->wire_bytes.size();
    graph->result["transport_chunk_sources"] = graph->chunk_sources;
    for (const auto* usage_field : {"usage_settlement", "usage_settlement_error", "execution_usage"})
      if (settlement.contains(usage_field)) graph->result[usage_field] = settlement[usage_field];
    if (record_context) {
      context_trace["graph_reply"] = graph->result;
      LOOM_TRY(retain_request_metadata());
    }
  }

  // 7. Persist the assistant message.
  Json metadata = Json::object();
  if (reasoning_profile_explicit)
    metadata["reasoning_profile"] = reasoning_recipe.snapshot.inspection();
  if (graph) metadata["graph_reply"] = graph->result;
  if (!reasoning_text.empty()) metadata["reasoning"] = std::string(utf8::prefix(reasoning_text, 2000));
  {
    auto topics = analyzer_.extract_topics(full_text, 2);
    if (!topics.empty()) metadata["topics"] = topics;
  }
  if (cancelled) metadata["cancelled"] = true;

  NewMessage anm;
  anm.conv_id = conv.id;
  anm.text = full_text;
  anm.role = "assistant";
  anm.model = model;
  anm.metadata = metadata;
  auto amid = db_.create_msg(anm);
  if (!amid) return amid.error();
  std::string asst_mid = *amid;

  bus_.emit(events::kMsgCreated,
           Json{{"id", asst_mid}, {"text", full_text}, {"conv_id", conv.id}, {"role", "assistant"}});

  {
    std::lock_guard lk(mu_);
    last_reasoning_ = reasoning_text.empty() ? std::nullopt : std::optional<std::string>(reasoning_text);
  }

  ChatResult result;
  result.conv_id = conv.id;
  result.user_message_id = user_mid;
  result.assistant_message_id = asst_mid;
  result.text = full_text;
  result.reasoning = reasoning_text.empty() ? std::nullopt : std::optional<std::string>(reasoning_text);
  result.model = model;
  result.usage = usage;
  result.cancelled = cancelled;
  result.context_trace = std::move(context_trace);

  // 8. Auto-title from the first exchange.
  auto all_msgs = db_.get_msgs(conv.id);
  if (all_msgs && all_msgs->size() <= 2 && cfg_bool(cfg_, "auto_title", true)) {
    std::string title = std::string(utf8::prefix(text, 30)) + (utf8::length(text) > 30 ? "..." : "");
    ConvPatch patch;
    patch.title = title;
    if (db_.update_conv(conv.id, patch)) {
      result.new_title = title;
      std::lock_guard lk(mu_);
      if (conv_ && conv_->id == conv.id) conv_->title = title;
    }
  }

  return result;
}

}  // namespace loom
