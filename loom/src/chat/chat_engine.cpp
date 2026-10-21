// OWNER: wave 2 net/chat/worker. Port of engine/chat_engine.py.
#include "loom/chat_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
#include "loom/usage_policy.h"
#include "loom/usage_spend.h"
#include "loom/use_model.h"
#include "llm/shared_gate.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"
#include "stub.h"
#include "active_task_spec.h"
#include "loom/guardian.h"
#include "active_task_acceptance.h"
#include "context/context_execution.h"
#include "context/unified_context.h"
#include "context/method_registry.h"
#include "graph_reply.h"
#include "reasoning_profile.h"
#include "context_format.h"
#include "profile_prefix.h"
#include "loom/runtime_profile.h"
#include "loom/scope_resolver.h"

namespace loom {

namespace {


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

// Estimate is exact input bytes only; output/cost are unknown (null), never a
// configured cap such as max_tokens.
Json chat_usage_estimate(const Config& cfg, std::string_view endpoint, std::string_view model,
                         std::string_view operation_id) {
  Json estimate{{"baseline_key", usage_baseline_key(cfg, "chat.send", endpoint, model)},
      {"resources", Json{{"output_tokens", nullptr}, {"cost_usd", nullptr}}},
      {"instrument", "loom.chat.send"}, {"model", model},
      {"estimate_basis", "exact_request_input_bytes_output_and_cost_unknown"}};
  if (!operation_id.empty()) estimate["operation_id"] = operation_id;
  return estimate;
}

Json compose_chat_payload(const Config& cfg, const ChatOptions& opts, const std::string& model, const Json& messages,
                          bool stream, const chat::ReasoningRecipe& reasoning_recipe) {
  const double temperature = opts.temperature.value_or(cfg_number(cfg, "temperature", 0.7));
  const int max_tokens = opts.max_tokens.value_or(static_cast<int>(cfg_number(cfg, "max_tokens", 4096)));
  Json payload{{"model", model}, {"messages", messages}, {"temperature", temperature},
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
  return payload;
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
    } else if (k == "context_order") {
      if (!v.is_string()) return bad();
      o.context_order = v.get<std::string>();
    } else if (k == "attachments") {
      if (!v.is_array()) return bad();
      for (const auto& a : v) {
        if (!a.is_string()) return bad();
        o.attachments.push_back(a.get<std::string>());
      }
    } else if (k == "web_search" || k == "deep_research" || k == "stream" ||
               k == "include_memory" || k == "include_graph_memory" || k == "include_history" || k == "trace_context" ||
               k == "new_conversation") {
      if (!v.is_boolean()) return bad();
      if (k == "web_search") o.web_search = v.get<bool>();
      if (k == "deep_research") o.deep_research = v.get<bool>();
      if (k == "stream") o.stream = v.get<bool>();
      if (k == "include_memory") o.include_memory = v.get<bool>();
      if (k == "include_graph_memory") o.include_graph_memory = v.get<bool>();
      if (k == "include_history") o.include_history = v.get<bool>();
      if (k == "trace_context") o.trace_context = v.get<bool>();
      if (k == "new_conversation") o.new_conversation = v.get<bool>();
    } else if (k == "knowledge_context") {
      auto context = chat_context_request(v);
      if (!context) return context.error();
      o.knowledge_context = *context;
    } else if (k == "active_task_spec") {
      auto compiled = chat::compile_active_task_spec(v);
      if (!compiled) return compiled.error();
      o.active_task_spec = v;
    } else if (k == "flow") {
      if (!v.is_string()) return bad();
      o.flow = v.get<std::string>();
    } else if (k == "selector_profile") {
      if (!v.is_string()) return bad();
      o.selector_profile = v.get<std::string>();
    } else if (k == "send_as_message") {
      if (!v.is_string()) return bad();
      o.send_as_message = v.get<std::string>();
    } else if (k == "action") {
      // Read by the conversation window (chat_route.h): the action the composer named for this message (P33); not a chat option.
      if (!v.is_string()) return bad();
    } else if (k == "intent_routing_method" || k == "lang" || k == "route_usage_confirmation" || k == "client_id") {
      // Read by the conversation window (chat_route.h) from the request itself; not a chat option.
      // client_id: the client that declared its devices (client_devices.h).
    } else if (k == "usage_confirmation") {
      if (!v.is_object() || !v.contains("approved") || !v["approved"].is_boolean()) return bad();
      o.usage_confirmation = v;
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
  if (usage_pending) out["usage_pending"] = *usage_pending;
  if (usage_notices.is_array() && !usage_notices.empty()) out["usage_notices"] = usage_notices;
  return out;
}

namespace {

bool guardian_traffic(const Message& m) {
  // The guardian's own exchange (a proposal, the answer to it, a notice) is traffic of the application, not conversation content.
  return m.metadata.is_object() && m.metadata.contains("guardian") && json::get_bool(m.metadata["guardian"], "traffic", false);
}
// The exchange of a chat command (what the user typed as a command, the proposal, the answer to it; metadata.routed) is traffic of
// the application. Setting chat_history_command_traffic: skip_exchange (preset: the exchange is left out, a result stays as context),
// skip_all (results too), include (everything is sent). Without it the model reads earlier proposals and imitates them.
bool command_traffic_skipped(const ChatOptions& opts, const Message& m) {
  if (!m.metadata.is_object() || opts.history_command_traffic == "include") return false;
  const bool result = m.metadata.contains("action_run") || json::get_string(m.metadata, "routed") == "result";
  const bool traffic = m.metadata.contains("routed") || m.metadata.contains("action_proposal") || m.metadata.contains("action_run");
  if (!traffic) return false;
  return !result || opts.history_command_traffic == "skip_all";
}
bool history_skipped(const ChatOptions& opts, const std::string& id) {
  return !opts.history_skip.empty() && std::find(opts.history_skip.begin(), opts.history_skip.end(), id) != opts.history_skip.end();
}

// The model of a send that names none: the setting default_model through the one scope chain (loom/scope_resolver.h): conversation
// (conversations.metadata.settings.default_model, what the + menu and the model picker write) -> units -> user -> installation. A chain that
// cannot be read is logged by value_or and the user's setting decides, as before.
std::string scoped_default_model(Database& db, const Config& cfg, const std::string& conv_id) {
  const std::string mine = cfg_string(cfg, "default_model");
  scopes::Binding bind;
  bind.key = "default_model";
  bind.conv_id = conv_id;
  const Json v = scopes::value_or(cfg.path().parent_path(), cfg, &db, bind, Json(mine));
  if (v.is_string() && !v.get<std::string>().empty()) return v.get<std::string>();
  if (!v.is_null()) log::warn("loom.chat_engine", "default_model from the scope chain is not a model name ({}); the setting decides", v.dump());
  return mine;
}

// The flow of a request. A profile that cannot be read never fails a send: it is logged and the request stays plain.
std::optional<chat::formats::Axes> resolve_flow(Database& db, const Config& cfg, const ChatOptions& opts, const std::string& conv_id,
                                                const std::string& model, bool reply_allowed) {
  auto axes = chat::formats::resolve(cfg.path().parent_path(), cfg, &db, conv_id, model, opts.flow, reply_allowed);
  if (!axes) {
    log::warn("loom.chat_engine", "flow not resolved, the request stays plain: {}", axes.error().message);
    return std::nullopt;
  }
  return std::move(*axes);
}

// The messages of the selection (the graph read, then the selector's choices) in order, with the text the selector chose.
Result<std::vector<chat::formats::Entry>> selection_entries(Database& db, const std::string& conv_id, const ChatOptions& opts,
                                                            std::string_view exclude, const std::set<std::string>& refinement) {
  std::vector<chat::formats::Entry> entries;
  if (conv_id.empty() || !opts.include_history) return entries;
  LOOM_TRY_ASSIGN(auto msgs, db.get_msgs(conv_id));
  for (const auto& m : msgs) {
    if (m.status != msg_status::kActive) continue;
    if (!exclude.empty() && m.id == exclude) continue;
    if (guardian_traffic(m) || command_traffic_skipped(opts, m) || history_skipped(opts, m.id) || refinement.count(m.id)) continue;
    chat::formats::Entry e;
    e.msg = m;
    e.text = m.text;
    if (auto level = opts.history_text.find(m.id); level != opts.history_text.end()) {
      e.text = level->second;
      e.selected_level = true;
    }
    entries.push_back(std::move(e));
  }
  return entries;
}

Result<Json> selection_packet(Database& db, const Json& section, const std::string& conv_id, const ChatOptions& opts, std::string_view exclude,
                              const std::set<std::string>& refinement) {
  LOOM_TRY_ASSIGN(auto entries, selection_entries(db, conv_id, opts, exclude, refinement));
  std::set<std::string> ids;
  for (const auto& e : entries) ids.insert(e.msg.id);
  std::vector<Link> links;
  std::set<std::string> seen;
  for (const auto& e : entries) {
    LOOM_TRY_ASSIGN(auto at, db.get_links(e.msg.id));
    for (const auto& l : at)
      if (ids.count(l.src) && ids.count(l.dst) && seen.insert(l.id).second) links.push_back(l);
  }
  std::sort(links.begin(), links.end(), [](const Link& a, const Link& b) { return a.id < b.id; });
  std::size_t substituted = 0;
  for (const auto& e : entries) substituted += e.selected_level ? 1 : 0;
  return chat::formats::packet(section, conv_id, entries, links, Json{{"substituted", substituted}, {"skipped", opts.history_skip.size()}});
}

// ---- M1-K1a: the selector profile of a request and the modifiers on it ----
struct SelectorChoice {
  bool chosen = false;
  std::string id;
  std::string source;   // request | turn | scope_chain
  Json row = nullptr;   // the entry of chat_context.pack selector_profiles
  std::string problem;  // an id of the turn or the chain that names no profile: ignored, and the receipt says so
  // A source set to false is not read; a class the profile does not mention is read. No profile: everything is read.
  bool source_on(const char* cls) const { return !chosen || json::get_bool(row.value("sources", Json::object()), cls, true); }
  // null = what the setting says now.
  Json knob(const char* group, const char* name) const {
    if (!chosen) return nullptr;
    const Json g = row.value(group, Json::object());
    return g.is_object() && g.contains(name) ? g.at(name) : Json(nullptr);
  }
};

// Order of choice: the request (ChatOptions.selector_profile), the value of this turn (turn_values context.profile), the scope chain (rules on
// context.profile at conversation / unit / user level, then the setting selector_profile). No selector_profiles in the effective view, or none
// chosen: no profile, the settings as they are (today's request bytes).
Result<SelectorChoice> choose_selector_profile(Database& db, const Config& cfg, const ChatOptions& opts, const std::string& conv_id, const Json& values) {
  SelectorChoice c;
  const Json table = values.value("selector_profiles", Json::array());
  if (!table.is_array() || table.empty()) {
    // Nothing to choose from: the settings as they are. A profile named by the request itself is refused rather than ignored.
    if (opts.selector_profile && !opts.selector_profile->empty())
      return Error(Errc::InvalidArgument, "unknown selector_profile: " + *opts.selector_profile + " (the effective view has no selector_profiles)");
    return c;
  }
  std::string wanted;
  if (opts.selector_profile && !opts.selector_profile->empty()) {
    wanted = *opts.selector_profile;
    c.source = "request";
  } else if (opts.turn_values.is_object() && opts.turn_values.contains("context.profile") && opts.turn_values.at("context.profile").is_string() &&
             !opts.turn_values.at("context.profile").get<std::string>().empty()) {
    wanted = opts.turn_values.at("context.profile").get<std::string>();
    c.source = "turn";
  } else {
    scopes::Binding bind;
    bind.key = "context.profile";
    bind.person_setting = "selector_profile";
    bind.conv_id = conv_id;
    const Json v = scopes::value_or(cfg.path().parent_path(), cfg, &db, bind, Json(nullptr));
    if (v.is_string() && !v.get<std::string>().empty()) {
      wanted = v.get<std::string>();
      c.source = "scope_chain";
    }
  }
  if (wanted.empty()) return c;
  for (const auto& row : table)
    if (row.is_object() && json::get_string(row, "id") == wanted) {
      c.chosen = true;
      c.id = wanted;
      c.row = row;
      return c;
    }
  if (c.source == "request") return Error(Errc::InvalidArgument, "unknown selector_profile: " + wanted);
  c.problem = "no selector profile '" + wanted + "' (from " + c.source + "); the settings are used";
  log::warn("loom.chat_engine", "{}", c.problem);
  c.source.clear();
  return c;
}

// A modifier (szerzej / węziej / głębiej / płycej): a step id, or a list of them, from this turn or from the scope chain.
struct Delta {
  Json steps = Json::array();
  std::string scope;  // turn | scope_chain | "" (none)
  Json applied = Json::array();
};

Delta read_delta(Database& db, const Config& cfg, const ChatOptions& opts, const std::string& conv_id, const char* key, const char* person_setting) {
  Delta d;
  Json v = opts.turn_values.is_object() && opts.turn_values.contains(key) ? opts.turn_values.at(key) : Json(nullptr);
  if (!v.is_null()) {
    d.scope = "turn";
  } else {
    scopes::Binding bind;
    bind.key = key;
    bind.person_setting = person_setting;
    bind.conv_id = conv_id;
    v = scopes::value_or(cfg.path().parent_path(), cfg, &db, bind, Json(nullptr));
    if (!v.is_null()) d.scope = "scope_chain";
  }
  if (v.is_string() && !v.get<std::string>().empty()) d.steps.push_back(v);
  else if (v.is_array())
    for (const auto& e : v)
      if (e.is_string()) d.steps.push_back(e);
  return d;
}

// The steps are data (action_rules.pack scoped_values.keys.<key>.steps: op multiply | divide | add, by). An unknown step changes nothing.
long long apply_steps(long long base, Delta& d, const Json& defs) {
  for (const auto& id : d.steps) {
    const Json def = defs.is_object() ? defs.value(id.get<std::string>(), Json::object()) : Json::object();
    const std::string op = json::get_string(def, "op");
    const double by = json::get_number(def, "by", 0.0);
    if (op == "multiply") base = std::llround(static_cast<double>(base) * by);
    else if (op == "divide" && by != 0.0) base = base == 0 ? 0 : std::max<long long>(1, std::llround(static_cast<double>(base) / by));
    else if (op == "add") base = std::max<long long>(0, base + std::llround(by));
    else continue;
    d.applied.push_back(id);
  }
  return base;
}

long long cfg_integer(const Config& cfg, const char* key, long long fallback) {
  const Json v = cfg.get(key, Json(fallback));
  return v.is_number() ? static_cast<long long>(v.get<double>()) : fallback;
}

long long json_tokens(const Json& content) {
  return chat::formats::estimate_tokens(content.is_string() ? content.get<std::string>() : json::dump(content));
}

}  // namespace

ChatOptions ChatEngine::select_representation(const ChatOptions& in_raw, const std::string& conv_id) {
  ChatOptions in = in_raw;
  if (in.history_command_traffic.empty()) in.history_command_traffic = cfg_string(cfg_, "chat_history_command_traffic", "skip_exchange");
  // The selector profile (M1-K1a) names its own flow and representation; a flow named by the request itself stays.
  std::string profile_representation;
  if (auto cc = RuntimeProfile::load("chat_context", cfg_.path().parent_path()); cc) {
    if (auto choice = choose_selector_profile(db_, cfg_, in, conv_id, cc->values()); choice && choice->chosen) {
      profile_representation = json::get_string(choice->row, "representation");
      if (!in.flow) if (const auto f = json::get_string(choice->row, "flow"); !f.empty()) in.flow = f;
    }
  }
  if (conv_id.empty() || !in.include_history || in.active_task_spec) return in;
  auto sel = guardian::select(db_, cfg_, conv_id, profile_representation);
  if (!sel) {
    log::debug("loom.chat_engine", "guardian selection ignored: {}", sel.error().message);
    return in;
  }
  if (sel->empty()) return in;
  ChatOptions out = in;
  out.history_text = sel->substitutions;
  out.history_level = sel->levels;
  out.guardian_rules = sel->rules;
  for (const auto& id : sel->skip) {
    out.history_skip.push_back(id);
    out.history_skip_reason[id] = "thread_filter";
  }
  if (sel->spec) {
    out.active_task_spec = *sel->spec;
    out.active_task_bindings = sel->bindings;
    out.active_task_history = sel->history;
    // The compiled instruction must be acceptable as it stands; if it is not (a message changed, an ancestor moved), the raw messages go.
    std::map<std::string, Json> in_flight;
    {
      auto lock = db_.lock();
      in_flight = active_task_in_flight_;
    }
    auto lock = db_.lock();
    if (auto check = prepare_active_task(db_, conv_id, out, in_flight); !check) {
      log::debug("loom.chat_engine", "guardian representation left raw: {}", check.error().message);
      out.active_task_spec.reset();
      out.active_task_bindings = Json::object();
      out.active_task_history = "replace_refinement";
    }
  }
  return out;
}

Result<Json> ChatEngine::context_selection(std::string_view conv_id_in, const ChatOptions& opts_in, std::string_view exclude_message_id) {
  const std::string conv_id(conv_id_in);
  const ChatOptions opts = select_representation(opts_in, conv_id);
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("guardian", cfg_.path().parent_path()));
  if (!profile.values().contains("context_io")) return Error(Errc::NotFound, "profile guardian has no context_io section");
  return selection_packet(db_, profile.values().at("context_io"), conv_id, opts, exclude_message_id, {});
}

Result<Json> ChatEngine::embed_selection(std::string_view conv_id, const Json& selection) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("guardian", cfg_.path().parent_path()));
  if (!profile.values().contains("context_io")) return Error(Errc::NotFound, "profile guardian has no context_io section");
  LOOM_TRY_ASSIGN(auto done, chat::formats::embed_packet(db_, profile.values().at("context_io"), std::string(conv_id), selection));
  // The embedder is structure only. What is new has entered the graph like any message: analyses run as their own step, on this event.
  for (const auto& id : done.value("created_message_ids", Json::array())) {
    LOOM_TRY_ASSIGN(auto row, db_.get_msg(id.get<std::string>()));
    if (row) bus_.emit(events::kMsgCreated, Json{{"id", row->id}, {"text", row->text}, {"conv_id", row->conv_id}, {"role", row->role}});
  }
  return done;
}

Result<Json> ChatEngine::preview_usage(std::string_view text, const ChatOptions& opts_in) {
  LOOM_TRY(check_attachments(opts_in.attachments));  // a file that cannot go is said before anything else (P33)
  LOOM_TRY_ASSIGN(auto reasoning_recipe, chat::reasoning_recipe(cfg_.get("chat_reasoning", Json::object())));
  std::string conv_id;
  {
    std::lock_guard lk(mu_);
    if (opts_in.conv_id) conv_id = *opts_in.conv_id;
    else if (conv_ && !opts_in.new_conversation) conv_id = conv_->id;  // a new conversation (R35) has no history yet: nothing of the remembered one
  }
  const ChatOptions opts = select_representation(opts_in, conv_id);
  LOOM_TRY_ASSIGN(auto messages, build_messages(conv_id, text, opts.attachments, opts));
  const std::string model = opts.model ? *opts.model : scoped_default_model(db_, cfg_, conv_id);
  const bool stream = opts.stream.value_or(cfg_bool(cfg_, "stream", true));
  llm::RouteSelector preview_route;
  preview_route.require_credential = false;  // a preview spends nothing and needs no key
  LOOM_TRY_ASSIGN(auto route, llm::resolve_route(llm::Services(cfg_, secrets_, http_), preview_route));
  const std::string base = route.base_url;
  const std::string body = json::dump(compose_chat_payload(cfg_, opts, model, messages, stream, reasoning_recipe));
  Json estimate = chat_usage_estimate(cfg_, base, model, "usage_preview");
  estimate["resources"]["requests"] = 1;
  estimate["resources"]["input_bytes"] = body.size();
  estimate["estimate_basis"] = "compiled_request_body_bytes_without_persisting_output_and_cost_unknown";
  LOOM_TRY_ASSIGN(auto settings, effective_usage_policy_options(cfg_));
  LOOM_TRY_ASSIGN(auto policy, UsagePolicy::open(cfg_.path().parent_path() / "usage-policy.sqlite", settings));
  return policy->preview(estimate);
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

void ChatEngine::set_attachment_resolver(AttachmentResolver resolver) { attachment_resolver_ = std::move(resolver); }

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

std::optional<std::pair<std::filesystem::path, std::filesystem::path>> ChatEngine::locate_attachment(const std::string& ref) const {
  std::filesystem::path file(ref);
  std::filesystem::path shown = file;  // the name that decides the kind of file and is shown to the model
  if (ref.rfind("blob:", 0) == 0) {
    const auto slash = ref.find('/');
    const std::string hash = ref.substr(5, slash == std::string::npos ? std::string::npos : slash - 5);
    std::optional<std::filesystem::path> stored;
    if (attachment_resolver_) stored = attachment_resolver_(hash);
    if (!stored) return std::nullopt;
    shown = std::filesystem::path(slash == std::string::npos ? std::string() : ref.substr(slash + 1)).filename();
    file = *stored;
  }
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return std::nullopt;
  return std::make_pair(file, shown);
}

Status ChatEngine::check_attachments(const std::vector<std::string>& attachments) const {
  for (const auto& ref : attachments) {
    const auto located = locate_attachment(ref);
    if (!located) return Error(Errc::NotFound, "attachment not found: " + ref);
    const std::string ext = lower_ext(located->second);
    if (!contains(image_exts(), ext) && !contains(text_exts(), ext))
      return Error(Errc::Unsupported, "this kind of file cannot be sent (images and plain-text files can): " + ref);
  }
  return ok_status();
}

Json ChatEngine::build_content(std::string_view text, const std::vector<std::string>& attachments) const {
  if (attachments.empty()) return Json(std::string(text));

  Json parts = Json::array();
  // A message of files alone (P33) has no words: no empty text part, which some providers refuse.
  if (!text.empty()) parts.push_back(Json{{"type", "text"}, {"text", std::string(text)}});

  for (const auto& path_str : attachments) {
    const auto located = locate_attachment(path_str);
    if (!located) {
      log::warn("loom.chat_engine", "Attachment not found: {}", path_str);
      continue;
    }
    const std::filesystem::path& p = located->first;
    const std::filesystem::path& shown = located->second;
    std::string ext = lower_ext(shown);
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
      std::string name = shown.filename().string();
      parts.push_back(
          Json{{"type", "text"}, {"text", "\n--- FILE: " + name + " ---\n" + file_text + "\n--- END FILE ---"}});
    } else {
      log::warn("loom.chat_engine", "Attachment of a kind that is not read: {}", path_str);
    }
  }
  if (parts.empty()) parts.push_back(Json{{"type", "text"}, {"text", std::string(text)}});
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
  // Context classes are collected, then ordered by the chat_context preset
  // (S0 system, S1 memory, S2 project, S3 history, S4 question-specific graph).
  Json part_system = Json::array(), part_memory = Json::array(), part_project = Json::array(),
       part_history = Json::array(), part_graph = Json::array(), part_profile = Json::array(), part_rules = Json::array();
  LOOM_TRY_ASSIGN(auto order_profile, RuntimeProfile::load("chat_context", cfg_.path().parent_path()));
  const Json& chat_context_values = order_profile.values();
  const std::string conv_s(conv_id);
  // The selector profile (M1-K1a): order of precedence for the order of the parts is the request, the chosen profile, the setting, the pack.
  LOOM_TRY_ASSIGN(auto choice, choose_selector_profile(db_, cfg_, opts, conv_s, chat_context_values));
  std::string order_name = opts.context_order ? *opts.context_order : choice.chosen ? json::get_string(choice.row, "order") : std::string();
  if (order_name.empty()) order_name = cfg_string(cfg_, "chat_context_order");
  if (order_name.empty()) order_name = chat_context_values.at("context_order").get<std::string>();
  const Json& known_orders = chat_context_values.at("orders");
  if (!known_orders.contains(order_name)) return Error(Errc::InvalidArgument, "unknown context_order: " + order_name);
  const auto order_has = [&](const char* cls) {
    for (const auto& c : known_orders.at(order_name)) if (c == cls) return true;
    return false;
  };
  // The receipt (loom.context_receipt/1) is compiled from the same parts as the request: items per class, and what was left out.
  const std::string receipt_store = [&] { const auto m = cfg_string(cfg_, "context_receipt_store", "auto"); return m == "always" || m == "never" ? m : std::string("auto"); }();
  const bool want_receipt = context_trace != nullptr && receipt_store != "never";
  chat::formats::Receipt rc;
  std::map<std::string, Json> ritems;
  const auto note = [&](const std::string& cls, Json item) {
    if (!want_receipt) return;
    auto& list = ritems[cls];
    if (list.is_null()) list = Json::array();
    list.push_back(std::move(item));
  };
  // The numbers: a null knob of the profile is what the setting says; a modifier acts on the number the request would have had.
  std::optional<long long> nodes_eff, window_eff, hops_eff;
  if (opts.context_depth) hops_eff = *opts.context_depth;
  if (const Json k = choice.knob("breadth", "nodes"); k.is_number_integer()) nodes_eff = k.get<long long>();
  if (const Json k = choice.knob("breadth", "history_window"); k.is_number_integer()) window_eff = k.get<long long>();
  if (const Json k = choice.knob("depth", "hops"); !hops_eff && k.is_number_integer()) hops_eff = k.get<long long>();
  Delta breadth_delta = read_delta(db_, cfg_, opts, conv_s, "context.breadth_delta", "context_breadth_delta");
  Delta depth_delta = read_delta(db_, cfg_, opts, conv_s, "context.depth_delta", "context_depth_delta");
  if (!breadth_delta.steps.empty() || !depth_delta.steps.empty()) {
    Json keys = Json::object();
    if (auto ar = RuntimeProfile::load("action_rules", cfg_.path().parent_path()); ar)
      keys = ar->values().value("scoped_values", Json::object()).value("keys", Json::object());
    const auto steps_of = [&](const char* key) { return keys.is_object() && keys.contains(key) ? keys.at(key).value("steps", Json::object()) : Json::object(); };
    if (!breadth_delta.steps.empty()) {
      const Json defs = steps_of("context.breadth_delta");
      nodes_eff = apply_steps(nodes_eff ? *nodes_eff : cfg_integer(cfg_, "graph_memory_max_nodes", 20), breadth_delta, defs);
      if (window_eff) window_eff = apply_steps(*window_eff, breadth_delta, defs);
    }
    if (!depth_delta.steps.empty())
      hops_eff = apply_steps(hops_eff ? *hops_eff : cfg_integer(cfg_, "graph_memory_depth", 2), depth_delta, steps_of("context.depth_delta"));
  }
  const bool has_rules_class = order_has("rules"), has_profile_class = order_has("profile");
  if (!sys_prompt.empty()) {
    part_system.push_back(Json{{"role", "system"}, {"content", sys_prompt}});
    note("system", chat::formats::receipt_item("system_prompt", "instruction", "instruction", "the instruction of the application or of the conversation", sys_prompt));
  }
  // The rules about the person: with an order that has the class they are their own part; with an order that does not (legacy, stable_first) they
  // stay in the part with the instruction, exactly where they always were.
  if (!opts.guardian_rules.empty()) {
    if (!choice.source_on("rules")) {
      rc.drop("rules", "source_off", {"guardian_rules"}, 1);
    } else {
      (has_rules_class ? part_rules : part_system).push_back(Json{{"role", "system"}, {"content", opts.guardian_rules}});
      note(has_rules_class ? "rules" : "system", chat::formats::receipt_item("guardian_rules", "rules about the person", "rule", "standing rules the guardian was allowed to send", opts.guardian_rules));
    }
  }
  // The profile (M1-K1a): what the person told the app about themselves, one deterministic block, only with an order that has the class.
  Json profile_prefix_info = nullptr;
  if (has_profile_class && choice.source_on("profile")) {
    const Json recipe = chat_context_values.value("profile_prefix", Json::object());
    if (recipe.is_object() && !recipe.empty()) {
      const auto gathered = chat::prefix::gather(db_, recipe);
      const auto rendered = chat::prefix::render(recipe, gathered.subgraphs, cfg_string(cfg_, "profile_detail_level", json::get_string(recipe, "default_level", "normal")), gathered.facts);
      profile_prefix_info = Json{{"level", rendered.level}, {"slots", rendered.items.size()}};
      if (!gathered.unavailable.empty()) profile_prefix_info["unavailable"] = gathered.unavailable;
      if (!rendered.text.empty()) {
        part_profile.push_back(Json{{"role", "system"}, {"content", rendered.text}});
        for (const auto& it : rendered.items)
          note("profile", Json{{"ref", it.at("ref")}, {"label", it.at("label")}, {"source_kind", "profile_slot"},
                               {"why", "said by the person; part " + it.value("subgraph", "") + ", detail level " + rendered.level}, {"tokens_est", it.at("tokens_est")}});
      }
    }
  } else if (has_profile_class) {
    rc.drop("profile", "source_off", {}, 1);
  }

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
  // The flow: how the selection (read from the graph) is rendered for this model and what form of answer is asked for. Preset plain = today's bytes.
  const std::string flow_model = opts.model ? *opts.model : scoped_default_model(db_, cfg_, std::string(conv_id));
  const bool graph_off = json::get_string(execution.value("graph_reply", Json::object()), "mode", "off") == "off";
  ChatOptions flow_opts;
  const ChatOptions* flow_source = &opts;
  if (!opts.flow && choice.chosen)
    if (const auto f = json::get_string(choice.row, "flow"); !f.empty()) { flow_opts = opts; flow_opts.flow = f; flow_source = &flow_opts; }
  const auto flow = resolve_flow(db_, cfg_, *flow_source, std::string(conv_id), flow_model, graph_off);
  Json selection = nullptr;
  if (flow && (flow->packet || flow->reply_graph)) {
    auto sp = selection_packet(db_, flow->section, std::string(conv_id), opts, exclude_message_id, refinement_ids);
    if (!sp) return sp.error();
    selection = *sp;
  }

  const bool memory_on = opts.include_memory && choice.source_on("memory");
  if (!unified && memory_ && opts.include_memory && !memory_on) rc.drop("memory", "source_off", {}, 1);
  if (!unified && memory_ && memory_on) {
    std::string mem_ctx = memory_->get_active_context();
    if (!mem_ctx.empty()) {
      part_memory.push_back(Json{{"role", "system"}, {"content", "User's memory/context:\n" + mem_ctx}});
      note("memory", chat::formats::receipt_item("memory_tree", "active memory", "memory_node", "the active nodes of the memory tree", mem_ctx));
    }
  }

  // A depth or a width of 0 (a profile, a modifier or the request) switches the graph memory off.
  bool graph_disabled = (hops_eff && *hops_eff == 0) || (nodes_eff && *nodes_eff == 0);
  const bool graph_on = opts.include_graph_memory && !graph_disabled && choice.source_on("graph");
  if (!unified && graph_memory_ && opts.include_graph_memory && !graph_disabled && !graph_on) rc.drop("graph", "source_off", {}, 1);
  if (!unified && graph_memory_ && graph_on) {
    try {
      GraphSelectOptions gopts;
      if (hops_eff && *hops_eff > 0) gopts.depth = static_cast<int>(*hops_eff);
      if (nodes_eff && *nodes_eff > 0) gopts.max_nodes = static_cast<int>(*nodes_eff);
      if (!conv_id.empty()) gopts.current_conv_id = std::string(conv_id);
      std::string graph_ctx = graph_memory_->select_context(current_text, gopts);
      if (!graph_ctx.empty()) {
        part_graph.push_back(Json{{"role", "system"}, {"content", graph_ctx}});
        note("graph", chat::formats::receipt_item("graph_memory", "graph context", "graph_node", "nodes of the graph related to the question", graph_ctx));
      }
    } catch (const std::exception& e) {
      log::debug("loom.chat_engine", "Graph memory selection failed: {}", e.what());
    }
  }

  const bool project_on = choice.source_on("project");
  if ((opts.knowledge_context || unified) && !project_on && !unified) rc.drop("project", "source_off", {}, 1);
  if ((opts.knowledge_context || unified) && (project_on || unified)) {
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
    const bool include_knowledge = (!unified || unified_options.value("include_knowledge", true)) && project_on;
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
      if (hops_eff) settings["legacy_relation_hops"] = *hops_eff;
      else if (!settings.contains("legacy_relation_hops")) settings["legacy_relation_hops"] = cfg_.get("graph_memory_depth", 2);
      auto selected = context::compile_unified_context(db_, cfg_, graph_memory_, memory_, request,
          compiled_context, conv_id, memory_on, graph_on, settings);
      if (!selected) return selected.error();
      unified_context = *selected;
      const auto prompt = json::get_string(unified_context, "prompt");
      if (!prompt.empty()) {
        part_project.push_back(Json{{"role", "system"}, {"content", prompt}});
        note("project", chat::formats::receipt_item("unified_context", "project context", "kb_claim", "memory, graph and knowledge compiled together (context_execution.unified)", prompt));
      }
    } else {
      const auto prompt = json::get_string(compiled_context, "prompt");
      if (!prompt.empty()) {
        part_project.push_back(Json{{"role", "system"}, {"content", prompt}});
        note("project", chat::formats::receipt_item("knowledge_context", "project context", "kb_claim", "selected by the knowledge context request", prompt));
      }
    }
  }

  // The conversation: rows are collected first, so that the profile's window and budget can cut the oldest before the parts are filled.
  struct HistRow { std::string id, role, content, level; };
  std::vector<HistRow> hist;
  std::map<std::string, std::vector<std::string>> hist_dropped;  // reason -> message ids (the receipt)
  const bool history_on = opts.include_history && choice.source_on("history");
  if (!conv_id.empty() && opts.include_history && !history_on) rc.drop("history", "source_off", {}, 1);
  if (!conv_id.empty() && history_on) {
    auto msgs = db_.get_msgs(conv_id, true);  // all rows: the excluded ones are counted for the receipt, only the active ones are sent
    if (!msgs) return msgs.error();
    for (const auto& m : *msgs) {
      if (m.status == msg_status::kExcluded) hist_dropped["excluded"].push_back(m.id);
      if (m.status != msg_status::kActive) continue;
      if (capture_native_history) captured_native_history.push_back(native_history_row(m));
      if (!exclude_message_id.empty() && m.id == exclude_message_id) continue;
      // The guardian's own exchange (a proposal, the answer to it, a notice) is traffic of the application, not conversation content.
      if (m.metadata.is_object() && m.metadata.contains("guardian") && json::get_bool(m.metadata["guardian"], "traffic", false)) { hist_dropped["command_traffic"].push_back(m.id); continue; }
      if (command_traffic_skipped(opts, m)) { hist_dropped["command_traffic"].push_back(m.id); continue; }
      if (!opts.history_skip.empty() && std::find(opts.history_skip.begin(), opts.history_skip.end(), m.id) != opts.history_skip.end()) {
        const auto why = opts.history_skip_reason.find(m.id);
        hist_dropped[why != opts.history_skip_reason.end() ? why->second : std::string("history_skip")].push_back(m.id);
        continue;
      }
      if (refinement_ids.count(m.id)) {
        replaced_ids.push_back(m.id);
        hist_dropped["replaced_by_task"].push_back(m.id);
        continue;
      }
      std::string content = m.text;
      std::string level_name = "L0";
      if (!opts.history_text.empty())
        if (auto level = opts.history_text.find(m.id); level != opts.history_text.end()) {
          content = level->second;
          const auto named = opts.history_level.find(m.id);
          level_name = named != opts.history_level.end() ? named->second : std::string("L1");
          hist_dropped["representation_level"].push_back(m.id);
        }
      if (m.weight > 1.5) {
        content = "[IMPORTANT] " + content;
      } else if (m.weight < 0.5) {
        content = "[low priority] " + content;
      }
      hist.push_back({m.id, m.role, std::move(content), std::move(level_name)});
    }
  }
  // The profile's window (the last N messages; a modifier "szerzej" widens it) and its own budget.
  if (window_eff && *window_eff >= 0 && hist.size() > static_cast<std::size_t>(*window_eff)) {
    const std::size_t cut = hist.size() - static_cast<std::size_t>(*window_eff);
    for (std::size_t i = 0; i < cut; ++i) hist_dropped["history_window"].push_back(hist[i].id);
    hist.erase(hist.begin(), hist.begin() + static_cast<std::ptrdiff_t>(cut));
  }
  Json budget_info = nullptr;
  if (const Json b = choice.chosen ? choice.row.value("budget_tokens", Json()) : Json(); b.is_number_integer() && b.get<long long>() > 0) {
    // Over the budget the graph goes first, then the oldest messages of the conversation (the last one always stays). The instruction, the profile,
    // the rules and the question are never cut.
    const long long limit = b.get<long long>();
    const auto tokens_of = [](const Json& part) { long long n = 0; for (const auto& m : part) n += json_tokens(m.value("content", Json(""))); return n; };
    long long fixed = tokens_of(part_system) + tokens_of(part_profile) + tokens_of(part_rules) + tokens_of(part_memory) + tokens_of(part_project) +
                      chat::formats::estimate_tokens(current_text);
    long long graph = tokens_of(part_graph), hist_tokens = 0;
    for (const auto& h : hist) hist_tokens += chat::formats::estimate_tokens(h.content);
    if (fixed + graph + hist_tokens > limit && graph > 0) {
      rc.drop("graph", "budget", {"graph_memory"}, 1);
      part_graph = Json::array();
      ritems.erase("graph");
      graph = 0;
    }
    while (fixed + graph + hist_tokens > limit && hist.size() > 1) {
      hist_tokens -= chat::formats::estimate_tokens(hist.front().content);
      hist_dropped["budget"].push_back(hist.front().id);
      hist.erase(hist.begin());
    }
    budget_info = Json{{"limit", limit}, {"tokens_est", fixed + graph + hist_tokens}, {"met", fixed + graph + hist_tokens <= limit}};
  }
  for (const auto& [reason, ids] : hist_dropped) {
    // Substituted messages are still sent (at a lossy level); the receipt counts them under their reason so that "what was left out" is whole.
    rc.drop("history", reason, ids, ids.size());
  }
  for (const auto& h : hist) {
    if (!flow || flow->turns) part_history.push_back(Json{{"role", h.role}, {"content", h.content}});
    history_ids.push_back(h.id);
    if (want_receipt)
      note("history", chat::formats::receipt_item(h.id, h.role, "message:" + h.level,
           h.level == "L0" ? "a message of the conversation, as written" : "a message of the conversation at the lossy level " + h.level + " chosen by the secretary", h.content));
  }

  if (flow && flow->packet && !selection.is_null()) {
    const Json rendered = chat::formats::render_packet(flow->section, flow->renderer, selection);
    const std::string block = chat::formats::packet_block(flow->section, rendered);
    part_history.insert(part_history.begin(), Json{{"role", "system"}, {"content", block}});
    if (want_receipt) {
      // selection_packet -> receipt: the packet stands for the messages it carries; it is one item at the head of the class history.
      auto& list = ritems["history"];
      if (list.is_null()) list = Json::array();
      Json item = chat::formats::receipt_item(json::get_string(selection, "packet_id"), "graph packet", "graph_packet", "the selection as a graph packet (flow " + flow->flow + ")", block);
      item["packet"] = chat::formats::packet_summary(selection);
      list.insert(list.begin(), std::move(item));
    }
  }

  // A class this kernel does not know sends nothing: it must not fall through to the graph part, or the graph would be sent once per class name.
  const Json no_part = Json::array();
  for (const auto& cls : known_orders.at(order_name)) {
    const auto name = cls.get<std::string>();
    const Json& part = name == "system" ? part_system : name == "profile" ? part_profile : name == "rules" ? part_rules
                     : name == "memory" ? part_memory : name == "project" ? part_project
                     : name == "history" ? part_history : name == "graph" ? part_graph : no_part;
    for (const auto& item : part) messages.push_back(item);
  }

  Json tail_items = Json::object();  // task | reply_instruction | question -> item (the receipt)
  if (opts.active_task_spec) {
    const std::string task_text = "[Active task specification; derived from the selected source messages]\n" +
        (*active_task)["compiled_spec"]["compiled_instruction"]["text"].get<std::string>();
    messages.push_back(Json{{"role", "user"}, {"content", task_text}});
    tail_items["task"] = chat::formats::receipt_item("active_task", "active task", "task", "the instruction compiled from the selected source messages", task_text);
  }
  if (flow && flow->reply_graph && !selection.is_null()) {
    const std::string reply_text = chat::formats::reply_instruction(flow->section, flow->mode, json::get_string(selection, "packet_id"));
    messages.push_back(Json{{"role", "system"}, {"content", reply_text}});
    tail_items["reply_instruction"] = chat::formats::receipt_item("graph_reply_instruction", "graph reply", "instruction", "asks for the answer as a graph (flow " + flow->flow + ")", reply_text);
  }
  messages.push_back(Json{{"role", "user"}, {"content", build_content(current_text, attachments)}});
  tail_items["question"] = chat::formats::receipt_item("question", "the question", "question", "what the person wrote in this turn", current_text);
  if (context_trace) {
    *context_trace = Json{{"kind", "compiled_messages"}, {"version", 1},
                         {"context_order", {{"name", order_name}, {"classes", known_orders.at(order_name)}}},
                         {"messages", messages}, {"messages_sha256", Sha256::hex(json::dump(messages))},
                         {"history_message_ids", history_ids},
                         {"selection", {{"include_memory", opts.include_memory},
                                        {"include_graph_memory", opts.include_graph_memory && !graph_disabled},
                                        {"include_history", opts.include_history},
                                        {"context_depth", opts.context_depth ? Json(*opts.context_depth) : Json(nullptr)}}},
                         {"knowledge_context_request", resolved_context_request},
                         {"knowledge_context", compiled_context}};
    if (flow && !flow->plain())
      (*context_trace)["flow"] = Json{{"flow", flow->flow}, {"renderer", flow->renderer}, {"output", flow->output}, {"mode", flow->mode},
                                      {"model_class", flow->model_class}, {"why", flow->reason},
                                      {"selection_packet_id", selection.is_null() ? Json(nullptr) : selection.value("packet_id", Json(nullptr))}};
    if (unified) (*context_trace)["unified_context"] = unified_context;
    if (opts.active_task_spec) {
      (*context_trace)["active_task"] = *active_task;
      (*context_trace)["replaced_history_message_ids"] = replaced_ids;
    }
    if (want_receipt) {
      for (const auto& cls : known_orders.at(order_name)) {
        const auto name = cls.get<std::string>();
        rc.section(name, ritems.count(name) && ritems[name].is_array() ? ritems[name] : Json::array());
      }
      for (const char* tail : {"task", "reply_instruction", "question"})
        if (tail_items.contains(tail)) rc.section(tail, Json::array({tail_items.at(tail)}));
      Json unapplied = Json::array();
      if (choice.chosen) {
        if (json::get_bool(choice.row.value("sources", Json::object()), "search", false)) unapplied.push_back("search");
        if (json::get_bool(choice.row.value("sources", Json::object()), "kb", false)) unapplied.push_back("kb");
      }
      Json profile_info = choice.chosen
          ? Json{{"id", choice.id}, {"label", choice.row.value("label", Json(nullptr))}, {"source", choice.source}, {"status", json::get_string(choice.row, "status")},
                 {"applied", Json{{"order", order_name}, {"nodes", nodes_eff ? Json(*nodes_eff) : Json(nullptr)},
                                  {"history_window", window_eff ? Json(*window_eff) : Json(nullptr)}, {"hops", hops_eff ? Json(*hops_eff) : Json(nullptr)},
                                  {"representation", choice.row.value("representation", Json(nullptr))},
                                  {"flow", flow ? Json(flow->flow) : Json("plain")}, {"budget", budget_info}}},
                 {"unapplied", unapplied}}
          : Json{{"id", nullptr}, {"source", "settings"}};
      if (!choice.problem.empty()) profile_info["ignored"] = choice.problem;
      Json header = Json::object();
      header["order"] = Json{{"name", order_name}, {"classes", known_orders.at(order_name)}};
      header["profile"] = std::move(profile_info);
      header["profile_prefix"] = profile_prefix_info.is_null() && !has_profile_class ? Json{{"state", "order_without_profile_class"}} : profile_prefix_info;
      header["modifiers"] = Json{{"profile", choice.chosen ? Json(choice.id) : Json(nullptr)},
                                 {"breadth_delta", breadth_delta.applied.empty() ? Json(nullptr) : breadth_delta.applied},
                                 {"depth_delta", depth_delta.applied.empty() ? Json(nullptr) : depth_delta.applied},
                                 {"scope", !breadth_delta.applied.empty() ? Json(breadth_delta.scope) : !depth_delta.applied.empty() ? Json(depth_delta.scope) : Json(nullptr)}};
      header["guard"] = Json{{"baseline", nullptr}, {"ratio", nullptr}, {"asked", false}, {"unknown_reason", "not_computed_before_m1_k1b"}};
      header["flow"] = flow ? Json{{"flow", flow->flow}, {"renderer", flow->renderer}, {"output", flow->output}, {"mode", flow->mode}, {"model_class", flow->model_class}, {"why", flow->reason}}
                            : Json{{"flow", "plain"}, {"renderer", "chat_turns"}, {"output", "text"}};
      header["renderer"] = flow ? flow->renderer : std::string("chat_turns");
      long long max_bytes = cfg_integer(cfg_, "context_receipt_max_bytes", 65536);
      (*context_trace)["receipt"] = chat::formats::receipt_bound(chat::formats::receipt_finish(rc, std::move(header), messages), receipt_store, max_bytes);
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

Result<ChatResult> ChatEngine::send(std::string_view text, const ChatOptions& opts_in, const ChatCallbacks& cb,
                                    const CancelToken* cancel) {
  // A file that cannot go to the model (not found, a kind that is not read) is said before anything is stored or sent (P33).
  LOOM_TRY(check_attachments(opts_in.attachments));
  std::string selector_conv;
  {
    std::lock_guard lk(mu_);
    selector_conv = opts_in.conv_id ? *opts_in.conv_id : (conv_ && !opts_in.new_conversation ? conv_->id : std::string());
  }
  const ChatOptions opts = select_representation(opts_in, selector_conv);
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
    // The conversation remembered from the last send may have been deleted since (by the person, or by a command in the chat): a send
    // that names none then starts a new one, not a write into a conversation that is gone (a foreign-key failure with no answer).
    std::optional<Conversation> remembered;
    if (!opts.conv_id && !opts.new_conversation && conv_) {
      auto r = db_.get_conv(conv_->id);
      if (!r) return r.error();
      if (*r) remembered = **r;
    }
    if (opts.conv_id) {
      auto r = db_.get_conv(*opts.conv_id);
      if (!r) return r.error();
      if (!*r) return Error(Errc::NotFound, "conversation not found: " + *opts.conv_id);
      conv = **r;
    } else if (remembered) {
      conv = *remembered;
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
  // The whole trace (compiled messages and everything the flags ask for) is kept as it always was. M1-K1a: besides it, every chat request leaves the
  // receipt (loom.context_receipt/1: references, not contents) with the user message, unless context_receipt_store is "never" or the request opted out
  // with trace_context = false. That is the preset "always in chat mode"; the slim trace holds nothing but the receipt and what identifies the request.
  const bool trace_full = opts.trace_context.value_or(opts.active_task_spec.has_value() ||
      opts.knowledge_context.has_value() || !opts.include_memory ||
      !opts.include_graph_memory || !opts.include_history ||
      execution.value("unified", Json::object()).value("enabled", false) ||
      execution.value("goal_typing", Json::object()).value("enabled", false) ||
      execution.value("embedding", Json::object()).value("enabled", false) || graph_enabled);
  const bool opted_out_of_trace = opts.trace_context.has_value() && !*opts.trace_context;
  const bool record_context = trace_full || (!opted_out_of_trace && cfg_string(cfg_, "context_receipt_store", "auto") != "never");
  // What is written to the message: the whole trace, or the slim one.
  const auto stored_trace = [&](const Json& t) -> Json {
    if (trace_full || !t.is_object()) return t;
    Json slim = Json::object();
    for (const char* k : {"kind", "version", "context_order", "messages_sha256", "pre_graph_messages_sha256", "selection", "flow", "receipt"})
      if (t.contains(k)) slim[k] = t.at(k);
    return slim;
  };
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
    if (record_context) nm.metadata["context_trace"] = stored_trace(context_trace);
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
    if (record_context) metadata["context_trace"] = stored_trace(context_trace);
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

  std::string model = opts.model ? *opts.model : scoped_default_model(db_, cfg_, conv.id);

  // 5. Call the API (through the one use-model action).
  const llm::Services services(cfg_, secrets_, http_);
  auto route = llm::resolve_route(services, llm::RouteSelector{});
  if (!route) {
    if (route.error().code == Errc::Auth) return Error(Errc::Auth, "No API key configured - open Settings");
    return route.error();
  }
  const std::string base = route->base_url;
  bool stream = opts.stream.value_or(cfg_bool(cfg_, "stream", true)) && static_cast<bool>(cb.on_chunk);

  // The output node of the flow: a graph answer is FORCED through a structured response format (prompt-only and validated for a model that
  // cannot). It is not streamed: partial JSON is not readable; the rendered text is delivered once, below.
  const auto out_flow = resolve_flow(db_, cfg_, opts, conv.id, model, !graph_enabled);
  const bool reply_graph = out_flow && out_flow->reply_graph;
  std::string reply_packet_id;
  std::set<std::string> reply_prior_ids;
  if (reply_graph) {
    stream = false;
    const std::string line = json::get_string(out_flow->section.value("outputs", Json::object()).value("graph_reply", Json::object()), "base_packet_line");
    const std::string marker = line.substr(0, line.find("{{packet_id}}"));
    for (const auto& m : *msgs_json) {
      if (!m.value("content", Json()).is_string()) continue;
      const std::string c = m["content"].get<std::string>();
      const auto at = marker.empty() ? std::string::npos : c.find(marker);
      if (at != std::string::npos) reply_packet_id = c.substr(at + marker.size(), 64);
    }
    // Ids that were in the selection actually sent are real references; any other id becomes an emergent node (P29), never a real id.
    LOOM_TRY_ASSIGN(auto sent_selection, selection_packet(db_, out_flow->section, conv.id, opts, user_mid, {}));
    for (const auto& n : sent_selection["nodes"]) reply_prior_ids.insert(n["id"].get<std::string>());
  }

  Json payload = compose_chat_payload(cfg_, opts, model, *msgs_json, stream, reasoning_recipe);
  if (reply_graph) chat::formats::apply_reply_request(payload, out_flow->section, out_flow->mode);

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
          // The receipt names the request that went out: the sha256 follows the messages; the compiled ones before the graph recipe stay named.
          if (context_trace.contains("receipt") && context_trace["receipt"].is_object()) {
            context_trace["receipt"]["pre_graph_sha256"] = context_trace["pre_graph_messages_sha256"];
            context_trace["receipt"]["sha256"] = context_trace["messages_sha256"];
          }
          LOOM_TRY(retain_request_metadata());
        }
      }
    }
  }

  llm::ModelRequest mreq;
  mreq.kind = "chat.send";
  mreq.action = "chat.send";
  mreq.models = {llm::ModelSpec{model, "", Json::object()}};
  mreq.body = payload;  // the complete recipe payload, sent byte for byte
  mreq.stream = stream;
  mreq.title = "ChatADHD";
  mreq.timeout_ms = 180000;
  mreq.cancel = cancel;
  if (graph) mreq.contain_exceptions = Error(Errc::Internal, "graph reply transport callback failed");
  auto call = llm::prepare_call(services, mreq);
  if (!call) return call.error();
  net::HttpRequest& req = call->request;

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
    // The graph reply keeps its own guard and settles it after its own
    // post-processing; the action only admits through it.
    mreq.usage.gate = std::make_shared<llm::SharedUsageGate>(*graph->guard, transport.value("usage_estimate", Json::object()),
        transport.value("confirmation", Json::object()), false);
  }

  // Every ordinary transport (including the primary call of a postprocess
  // graph run, or a graph run whose capability is unavailable) is admitted by
  // the same shared guard/ledger as the graph reply and settled afterwards.
  if (!(graph && graph->guard)) {
    const Json confirmation = opts.usage_confirmation.value_or(Json::object());
    if (!confirmation.is_object()) return Error(Errc::InvalidArgument, "usage_confirmation must be an object");
    mreq.usage.estimate = chat_usage_estimate(cfg_, base, model, json::get_string(confirmation, "operation_id"));
    mreq.usage.confirmation = confirmation;
    mreq.usage.require_scope = true;
  }

  mreq.hooks.on_dispatch = [&] { if (graph) graph->attempted = true; };
  mreq.hooks.on_raw = [&](std::string_view chunk) { if (graph) graph->capture_chunk(chunk, model); };
  mreq.hooks.on_text = [&](std::string_view part) { if (cb.on_chunk) cb.on_chunk(part); };
  mreq.hooks.on_reasoning = [&](std::string_view part) { if (cb.on_reasoning) cb.on_reasoning(part); };
  llm::VariantResult sent = llm::execute_call(services, mreq, *call);

  if (graph && graph->guard && (sent.state == llm::State::GateError || sent.state == llm::State::Held)) {
    graph->issue("usage_policy_pending", sent.state == llm::State::Held ? json::get_string(sent.hold, "status", "unavailable") :
        std::string(errc_name(sent.error->code)));
    graph->result["execution_usage"] = graph->guard->decisions();
    if (sent.state == llm::State::Held) graph->result["usage_decision"] = sent.hold["decision"];
    if (record_context) { context_trace["graph_reply"] = graph->result; LOOM_TRY(retain_request_metadata()); }
    return Error(Errc::Paused, "graph reply model usage is not admitted; inspect retained context trace");
  }
  if (sent.state == llm::State::GateError) return *sent.error;
  if (sent.state == llm::State::Held) {
    const auto status = json::get_string(sent.hold, "status", "unavailable");
    if (status != "requires_confirmation" && status != "denied" && status != "cancelled")
      return Error(Errc::Paused, "model usage is not admitted: " + status);
    // Nothing was sent. The user's turn is withdrawn from the conversation so
    // the identical confirmed re-send reproduces the identical request.
    bool retained = true;
    if (!opts.active_task_spec && db_.set_msg_status(user_mid, "deleted")) retained = false;
    const Json& admitted = sent.hold["decision"];
    Json resources = Json::object();
    if (const auto* r = json::find(admitted, "resources")) resources = *r;
    ChatResult paused;
    paused.conv_id = conv.id;
    paused.user_message_id = user_mid;
    paused.model = model;
    paused.usage_pending = Json{{"schema", "loom.chat_usage_pending/1"}, {"status", status},
        {"operation_id", admitted.value("operation_id", Json(nullptr))},
        {"receipt_id", admitted.value("receipt_id", Json(nullptr))},
        {"baseline_key", admitted.value("baseline_key", Json(nullptr))},
        {"projected", resources}, {"decisions", sent.hold["decisions"]},
        {"sent", false}, {"user_message_retained", retained},
        {"resend", "repeat the same request with usage_confirmation {approved, receipt_id, ref, operation_id}"}};
    // P18: a spending limit that holds or refuses has its own reason code and texts (limits, reason_codes); a refusal (denied) cannot be confirmed.
    const Json limit = usage_limit_reasons(admitted);
    for (auto it = limit.begin(); it != limit.end(); ++it)
      if (it.key() != "status") (*paused.usage_pending)[it.key()] = it.value();
    return paused;
  }

  std::string full_text = sent.text, reasoning_text = sent.reasoning;
  std::string reported_model = sent.reported_model;
  Json usage = sent.usage;
  bool cancelled = false;
  const int response_status = sent.http_status;

  // Issues the decoders noted while reading, in order (graph runs only act on them).
  if (graph)
    for (const auto& issue : sent.issues) {
      if (issue == "malformed_chunk") graph->transport_issue = "parse";
      else if (issue == "model_invalid") graph->transport_issue = "provider_model_invalid";
      else if (issue == "model_changed") graph->transport_issue = "provider_model_changed";
      else graph->transport_issue = issue;
    }
  const bool got_response = sent.state == llm::State::Ok || sent.state == llm::State::HttpError ||
                            sent.state == llm::State::ParseError;  // the transport itself succeeded
  if (graph && !stream && got_response) {
    graph->capture_chunk(sent.body, model);
    graph->capture_final(model, false, response_status);
  }

  switch (sent.state) {
    case llm::State::Ok:
      break;
    case llm::State::Cancelled:
      cancelled = true;
      break;
    case llm::State::TransportError:
      // R37: a model on this device that does not answer is a server that is not running, said in words the person can act on.
      if (!graph && json::get_bool(route->metadata, "local", false))
        return Error(Errc::Unavailable, "The local model at " + base + " does not answer (" + sent.error->message +
                                            "). Start a llama.cpp or Ollama server there, set its address in the setting "
                                            "local_model_base_url, or choose another provider in llm_provider.");
      if (!graph) return *sent.error;
      graph->transport_issue = std::string(errc_name(sent.error->code));
      break;
    case llm::State::HttpError:
      if (!graph) return Error(Errc::Http, "API error " + std::to_string(sent.http_status) + ": " + std::string(utf8::prefix(sent.body, 500)));
      graph->transport_issue = "http";
      full_text = sent.body;
      break;
    case llm::State::ParseError:
      if (!graph) return *sent.error;
      graph->transport_issue = "parse";
      full_text = sent.body;
      break;
    case llm::State::Unavailable:
    case llm::State::GateError:
    case llm::State::Held:
      return sent.error ? *sent.error : Error(Errc::Unavailable, "model call unavailable");
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

  // The output node: parse the graph reply, render it to the text the person reads. An unusable reply is shown raw with a visible note.
  Json reply_meta = nullptr;
  chat::formats::Reply parsed_reply;
  if (reply_graph && !graph) {
    const std::string raw_text = full_text;
    const Json reply_base{{"format", "graph"}, {"flow", out_flow->flow}, {"mode", out_flow->mode}, {"model_class", out_flow->model_class},
                    {"raw_sha256", Sha256::hex(raw_text)}};
    reply_meta = reply_base;
    if (cancelled) {
      reply_meta["status"] = "not_parsed";
      reply_meta["reason"] = "cancelled";
    } else {
      parsed_reply = chat::formats::parse_reply(out_flow->section, raw_text, reply_packet_id, reply_prior_ids);
      if (parsed_reply.ok) {
        full_text = parsed_reply.text;
        reply_meta["status"] = "ok";
        reply_meta["fence_stripped"] = parsed_reply.fence_stripped;
        reply_meta["graph"] = parsed_reply.graph;
        if (!parsed_reply.emergent.empty()) {
          Json notes = Json::array();
          for (const auto& e : parsed_reply.emergent)
            notes.push_back(chat::formats::emergent_note(out_flow->section, cfg_string(cfg_, "language", "en"), e["claimed_id"].get<std::string>()));
          reply_meta["notes"] = notes;
          reply_meta["emergent_refs"] = parsed_reply.emergent;
        }
      } else {
        const std::string note = chat::formats::invalid_note(out_flow->section, cfg_string(cfg_, "language", "en"), parsed_reply.error);
        full_text = raw_text + "\n\n" + note;
        reply_meta["status"] = "invalid";
        reply_meta["reason"] = parsed_reply.error;
        reply_meta["note"] = note;
        log::warn("loom.chat_engine", "graph reply not usable ({}); raw text shown with a note", parsed_reply.error);
      }
      if (cb.on_chunk) cb.on_chunk(full_text);
    }
  }

  // 7. Persist the assistant message.
  Json metadata = Json::object();
  if (!reply_meta.is_null()) metadata["reply_format"] = reply_meta;
  if (reasoning_profile_explicit)
    metadata["reasoning_profile"] = reasoning_recipe.snapshot.inspection();
  if (graph) metadata["graph_reply"] = graph->result;
  if (!reasoning_text.empty()) metadata["reasoning"] = std::string(utf8::prefix(reasoning_text, 2000));
  {
    auto topics = analyzer_.extract_topics(full_text, 2);
    if (!topics.empty()) metadata["topics"] = topics;
  }
  if (cancelled) metadata["cancelled"] = true;
  if (sent.notices.is_array() && !sent.notices.empty()) metadata["usage_notices"] = sent.notices;

  NewMessage anm;
  anm.conv_id = conv.id;
  anm.text = full_text;
  anm.role = "assistant";
  anm.model = model;
  anm.metadata = metadata;
  auto amid = db_.create_msg(anm);
  if (!amid) return amid.error();
  std::string asst_mid = *amid;
  if (parsed_reply.ok) {
    // The embedder is structure only (no model, no analysis): the parts and the proposed links enter the graph with provenance to the message.
    const Json plan = chat::formats::plan_placement(db_, out_flow->section, asst_mid, out_flow->flow, parsed_reply, reply_prior_ids);
    auto embedded = chat::formats::embed(db_, out_flow->section, asst_mid, conv.id, model, parsed_reply, plan);
    Json summary = embedded ? *embedded : Json{{"embedded", false}, {"error", embedded.error().message}};
    if (!embedded) log::warn("loom.chat_engine", "graph reply not embedded: {}", embedded.error().message);
    metadata["reply_format"]["embedded"] = summary;
    MsgPatch patch;
    patch.metadata = metadata;
    if (auto saved = db_.update_msg(asst_mid, patch); !saved) log::warn("loom.chat_engine", "graph reply summary not saved: {}", saved.error().message);
  }

  bus_.emit(events::kMsgCreated,
           Json{{"id", asst_mid}, {"text", full_text}, {"conv_id", conv.id}, {"role", "assistant"}});
  if (sent.notices.is_array() && !sent.notices.empty())
    bus_.emit(events::kUsageLimitNotice, Json{{"conv_id", conv.id}, {"message_id", asst_mid}, {"notices", sent.notices}});

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
  result.context_trace = trace_full ? std::move(context_trace) : Json(nullptr);
  result.usage_notices = sent.notices;

  // 8. Auto-title from the first exchange.
  auto all_msgs = db_.count_msgs(conv.id);  // Perf (P19): a count, not the conversation
  if (all_msgs && *all_msgs <= 2 && cfg_bool(cfg_, "auto_title", true)) {
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
