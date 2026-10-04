#include "method_execution.h"

#include <cmath>
#include <limits>
#include <set>

#include "loom/model.h"
#include "loom/runtime.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "../chat/graph_reply.h"
#include "method_channels.h"

namespace loom::context {
struct MethodExecutionState {
  explicit MethodExecutionState(Database& db) : registry(db) {}
  MethodRegistry registry;
  Json snapshot, plan, options, packet = nullptr;
  Json records = Json::object();
  Json stages = Json::object();
  Json prepared_stages = Json::object();
  MethodPacketOperation operation = chat::graph_reply_packet_operation;
};
namespace {
Error invalid(std::string message) {
  return Error(Errc::InvalidArgument, "method_execution: " + std::move(message));
}
Result<double> finite_value(const Json& value, std::string_view name) {
  if (!value.is_number() || !std::isfinite(value.get<double>()))
    return invalid(std::string(name) + " must be finite");
  return value.get<double>();
}
Result<CandidateChannelRequest> actual_request(const Json& params, const std::string& id) {
  // These controls must be in resolved data before any instrument runs; the
  // public request object's old preset is not an implicit method definition.
  if (!params.is_object() || !params.contains("limit") || !params.contains("min_score"))
    return invalid("method parameters require limit and min_score");
  return CandidateChannelRequest::from_json(Json{{"id", id}, {"limit", params["limit"]},
                                                {"min_score", params["min_score"]}});
}

using Execution = MethodExecutionState;

Json composed_parameters(const Execution& state) {
  Json composition = Json::array();
  std::size_t index = 0;
  for (const auto& leaf : state.plan["leaves"]) {
    Json actual = nullptr;
    for (const auto& record : state.records)
      if (record.value("leaf_index", std::numeric_limits<std::size_t>::max()) == index)
        actual = record.value("parameters", Json(nullptr));
    composition.push_back(Json{{"leaf_index", index++}, {"method_version_id", leaf["method_version_id"]},
        {"method_version_sha256", leaf["method_version_sha256"]}, {"weight", leaf["weight"]},
        {"path", leaf["path"]}, {"fusions", leaf["fusions"]},
        {"effective_parameters", leaf["effective_parameters"]}, {"actual_execution_parameters", actual},
        {"available", leaf["available"]}});
  }
  return Json{{"ordered_composition", composition}, {"fusion", state.plan["selection"].value("fusion", Json(nullptr))}};
}

Json execution_graph(const Execution& state) {
  return Json{{"status", state.packet.is_null() ? "unavailable" : "candidate"},
      {"packet", state.packet}, {"canonical_store_written", false},
      {"records", state.records}, {"stages", state.stages}};
}

Result<bool> prepare_stage(Execution& state, const std::string& name,
    const std::string& capability, const Json& input, const Json& parameters, bool complete_input) {
  Json record{{"status", "unavailable"}, {"reason", "result_method_graph_version_not_configured"}};
  state.stages[name] = record;
  const Json methods = state.options.value("result_methods", Json::object());
  if (!methods.is_object()) return invalid("result_methods must be an object");
  if (!methods.contains(name)) return false;
  if (!methods[name].is_object()) return invalid("result method selection overlay must be an object");
  Json overlay = methods[name];
  if (!overlay.contains("members")) return false;
  if (!overlay["members"].is_array()) return invalid("result method graph members must be an array");
  if (overlay["members"].empty()) return false;
  if (overlay.contains("fusion") && !overlay["fusion"].is_null())
    return invalid("result stage dispatch does not execute an additional outer fusion");
  // A stage is one actual executor. Explicit null removes the retrieval
  // profile's outer fusion: that upstream arithmetic is captured as consumed
  // stage parameters, not represented as another executed stage method.
  overlay["fusion"] = nullptr;
  const Json capabilities{{"execution", Json{{capability, Json{{"available", true},
      {"authorization", "native_caller_owned"}, {"verification", "not_performed"}}}}},
      {"fusion", method_fusion_capabilities()}};
  auto resolution = state.registry.resolve(state.snapshot, overlay, capabilities);
  if (!resolution) {
    if (resolution.error().code != Errc::NotImplemented) return resolution.error();
    state.stages[name]["reason"] = resolution.error().message;
    return false;
  }
  state.stages[name]["resolution"] = *resolution;
  if ((*resolution)["leaves"].size() != 1)
    return invalid("result stage dispatch requires one graph executor version");
  const auto& leaf = (*resolution)["leaves"][0];
  if (!leaf.value("available", false)) {
    state.stages[name]["reason"] = "resolved_result_executor_unavailable";
    return false;
  }
  if (leaf["execution_capability"] != capability || leaf["weight"] != 1 || leaf["path"].size() != 1)
    return invalid("result stage executor must directly select its actual unweighted native capability");
  if (!leaf["recipe"].is_null() || !leaf["prompt"].is_null())
    return invalid("native result arithmetic does not execute a prompt or recipe");
  if (!leaf["effective_parameters"].is_object()) return invalid("result stage parameters must be an object");
  for (const auto& [key, value] : leaf["effective_parameters"].items())
    if (!parameters.contains(key) || parameters[key] != value)
      return invalid("declared result stage parameter differs from its actual consumed value: " + key);
  Json context = state.options.value("run_context", Json::object());
  context["run_id"] = gen_id("method_run_");
  context["input_sha256"] = complete_input ? Json(Sha256::hex(json::canonical(input))) : Json(nullptr);
  context["effective_parameters"] = parameters;
  context["user_overrides"] = leaf["user_overrides"];
  if (!state.packet.is_null()) context["base_packet"] = state.packet;
  LOOM_TRY_ASSIGN(auto prepared, state.registry.prepare(state.snapshot, leaf, context, state.operation));
  state.packet = prepared["packet"];
  state.prepared_stages[name] = Json{{"prepared", prepared}, {"context", context}, {"input", input}};
  state.stages[name] = Json{{"status", "prepared"}, {"resolution", *resolution},
      {"prepared_manifest", prepared["manifest"]}, {"parameters", parameters},
      {"captured_input_sha256", Sha256::hex(json::canonical(input))},
      {"input_identity_scope", complete_input ? "complete_fusion_input" : "captured_preselection_input"},
      {"complete_input_sha256", context["input_sha256"]}};
  return true;
}

Status bind_stage(Execution& state, const std::string& name, const Json& output,
                  const Json& measurements, const Json& target_results) {
  if (!state.prepared_stages.contains(name)) return {};
  const auto& captured = state.prepared_stages[name];
  const auto& context = captured["context"];
  const auto& prepared = captured["prepared"];
  const auto& vocabulary = state.snapshot["vocabulary"];
  if (!vocabulary["kinds"].contains("result")) return invalid("vocabulary.kinds.result required for result stage artifacts");
  Json entities = Json::array(), ids = Json::array(), refs = Json::object();
  const auto add = [&](Json attrs) {
    model::Entity entity;
    entity.id = gen_id("method_result_");
    entity.canonical_key = entity.id;
    entity.kind = vocabulary["kinds"]["result"].get<std::string>();
    entity.label = name + " result";
    entity.evidence = model::EvidenceClass::Derived;
    entity.origin = model::Origin::System;
    entity.confidence = 1;
    attrs["confidence_scope"] = "instrument_measurement";
    attrs["content_truth"] = "not_established";
    entity.attrs = std::move(attrs);
    ids.push_back(entity.id);
    entities.push_back(entity.to_json());
    return entity.id;
  };
  add(Json{{"result_type", name + "_batch"}, {"output", output}});
  for (const auto& [ref, result] : target_results.items())
    refs[ref] = add(Json{{"result_type", name + "_measurement"}, {"target_ref", ref}, {"measurement", result}});
  LOOM_TRY_ASSIGN(auto diff, state.operation(Json{{"operation", "empty_diff"}, {"packet", state.packet},
      {"proposal_id", gen_id("method_results_")}, {"origin", context["origin"]}, {"known_at", context["known_at"]}}));
  diff["entities"]["add"] = entities;
  LOOM_TRY_ASSIGN(auto preview, state.operation(Json{{"operation", "preview"}, {"packet", state.packet}, {"diff", diff}}));
  const auto response_bytes = json::canonical(output);
  LOOM_TRY_ASSIGN(auto completed, state.registry.bind_results(preview["candidate_packet"], prepared["manifest"],
      Json{{"result_entity_ids", ids}, {"origin", context["origin"]}, {"known_at", context["known_at"]},
           {"input_sha256", context["input_sha256"]}, {"request_bytes", json::canonical(captured["input"])},
           {"response_sha256", Sha256::hex(response_bytes)}, {"measurements", measurements}, {"availability", "ok"}},
      state.operation));
  state.packet = completed["packet"];
  auto& record = state.stages[name];
  record["status"] = "candidate";
  record["manifest"] = completed["manifest"];
  record["result_entity_ids"] = ids;
  record["measurement_nodes"] = refs;
  record["response_sha256"] = Sha256::hex(response_bytes);
  return {};
}

// The wrapper receives the actual query and full searchable corpus. It cannot
// relabel a pre-existing claim as newly produced by a retrieval instrument.
class RecordedChannel final : public CandidateChannel {
 public:
  RecordedChannel(std::shared_ptr<Execution> execution, std::shared_ptr<CandidateChannel> channel,
                  Json leaf, Json consumed_parameters, std::size_t index)
      : execution_(std::move(execution)), channel_(std::move(channel)), leaf_(std::move(leaf)),
        parameters_(std::move(consumed_parameters)), index_(index) {}

  RetrievalBatch retrieve(std::string_view query, const std::vector<RetrievalDocument>& corpus,
                           const CandidateChannelRequest& request) override {
    RetrievalBatch batch;
    batch.method = leaf_.value("execution_capability", "");
    batch.corpus_count = corpus.size();
    Json record{{"leaf_index", index_}, {"method_version_id", leaf_["method_version_id"]},
                {"parameters", parameters_}, {"declared_parameters", leaf_["effective_parameters"]}};
    auto fail = [&](const Error& error) {
      batch.status = "error";
      batch.reason = error.to_string();
      record["status"] = "error";
      record["error"] = Json{{"code", errc_name(error.code)}, {"message", error.message}};
      execution_->records[request.id] = record;
      return batch;
    };
    Json input{{"query", std::string(query)}, {"corpus", Json::array()}, {"request", request.to_json()},
               {"corpus_scope", execution_->options["corpus_scope"]},
               {"execution_parameters", parameters_}, {"method_version_id", leaf_["method_version_id"]}};
    for (const auto& document : corpus)
      input["corpus"].push_back(Json{{"ref", document.ref}, {"text", document.text}});
    Json context = execution_->options.value("run_context", Json::object());
    context["run_id"] = gen_id("method_run_");
    context["input_sha256"] = Sha256::hex(json::canonical(input));
    context["effective_parameters"] = parameters_;
    if (!execution_->packet.is_null()) context["base_packet"] = execution_->packet;
    auto prepared = execution_->registry.prepare(execution_->snapshot, leaf_, context, execution_->operation);
    if (!prepared) return fail(prepared.error());
    record["prepared_manifest"] = (*prepared)["manifest"];
    // Registering the method graph must succeed BEFORE a provider-backed
    // instrument may consume resources. An unavailable compiler is explicit.
    execution_->packet = (*prepared)["packet"];
    try {
      batch = channel_->retrieve(query, corpus, request);
    } catch (const std::exception&) {
      return fail(invalid("instrument exception"));
    } catch (...) {
      return fail(invalid("instrument exception"));
    }
    record["status"] = batch.status;
    auto completed = bind(*prepared, context, input, batch);
    if (!completed) {
      // No ranked result may claim recorded method provenance if that record
      // failed. Raw measurements remain in the diagnostic record.
      record["unbound_measurements"] = batch.to_json();
      return fail(completed.error());
    }
    execution_->packet = (*completed)["packet"];
    record["manifest"] = (*completed)["manifest"];
    record["result_entity_ids"] = result_ids_;
    record["measurement_nodes"] = result_refs_;
    execution_->records[request.id] = std::move(record);
    return batch;
  }

 private:
  Result<Json> bind(const Json& prepared, const Json& context, const Json& input, const RetrievalBatch& batch) {
    const auto& vocabulary = execution_->snapshot["vocabulary"];
    if (!vocabulary["kinds"].contains("result"))
      return invalid("vocabulary.kinds.result is required for measured retrieval artifacts");
    Json results = Json::array();
    result_ids_ = Json::array();
    const auto add = [&](std::string label, Json attrs) {
      model::Entity entity;
      entity.id = gen_id("method_result_");
      entity.canonical_key = entity.id;
      entity.kind = vocabulary["kinds"]["result"].get<std::string>();
      entity.label = std::move(label);
      entity.evidence = model::EvidenceClass::Derived;
      entity.origin = model::Origin::System;
      entity.confidence = 1.0;
      attrs["confidence_scope"] = "instrument_measurement";
      attrs["content_truth"] = "not_established";
      entity.attrs = std::move(attrs);
      result_ids_.push_back(entity.id);
      results.push_back(entity.to_json());
      return entity.id;
    };
    add("retrieval batch", Json{{"result_type", "retrieval_batch"}, {"batch", batch.to_json()},
                                 {"input_sha256", context["input_sha256"]}});
    std::set<std::string> refs;
    for (const auto& score : batch.scores) {
      if (!std::isfinite(score.score) || !refs.insert(score.ref).second)
        return invalid("invalid or duplicate measured result");
      bool found = false;
      for (const auto& document : input["corpus"]) if (document["ref"] == score.ref) { found = true; break; }
      if (!found) return invalid("measured result references a different corpus");
      result_refs_[score.ref] = add("retrieval measurement", Json{{"result_type", "retrieval_measurement"},
          {"target_claim_id", score.ref}, {"score", score.score}, {"instrument_status", batch.status}});
    }
    const Json& base = prepared["packet"];
    LOOM_TRY_ASSIGN(auto diff, execution_->operation(Json{{"operation", "empty_diff"}, {"packet", base},
        {"proposal_id", gen_id("method_results_")}, {"origin", context["origin"]}, {"known_at", context["known_at"]}}));
    diff["entities"]["add"] = results;
    LOOM_TRY_ASSIGN(auto preview, execution_->operation(Json{{"operation", "preview"}, {"packet", base}, {"diff", diff}}));
    Json bindings{{"result_entity_ids", result_ids_}, {"origin", context["origin"]},
        {"known_at", context["known_at"]}, {"input_sha256", context["input_sha256"]},
        {"request_bytes", json::canonical(input)}, {"response_sha256", Sha256::hex(json::canonical(batch.to_json()))},
        {"measurements", Json{{"scored_count", batch.scored_count}, {"eligible_count", batch.eligible_count},
                             {"accepted_count", batch.hits.size()}}}, {"availability", batch.status}};
    return execution_->registry.bind_results(preview["candidate_packet"], prepared["manifest"], bindings,
                                             execution_->operation);
  }
  std::shared_ptr<Execution> execution_;
  std::shared_ptr<CandidateChannel> channel_;
  Json leaf_;
  Json parameters_;
  std::size_t index_;
  Json result_ids_ = Json::array();
  Json result_refs_ = Json::object();
};
}  // namespace

Result<MethodCandidates> gather_method_candidates(Runtime& runtime, kb::KnowledgeStore& store,
    const std::string& run, std::shared_ptr<const kb::Pack> pack, const ContextRequest& request,
    const std::vector<model::EvidenceClass>& evidence, const std::vector<std::string>& graph_claims,
    const std::map<std::string, std::shared_ptr<CandidateChannel>>& installed, const Json& execution) {
  MethodCandidates result;
  const Json settings = execution.value("method_registry", Json::object());
  if (!settings.is_object()) return invalid("method_registry must be an object");
  if (!settings.value("enabled", false)) return result;
  result.enabled = true;
  if (!settings.contains("profile"))
    return Error(Errc::NotImplemented, "method_execution: profile graph data are unavailable; no built-in method preset is fabricated");
  auto state = std::make_shared<Execution>(runtime.db());
  state->options = settings;
  state->options["corpus_scope"] = Json{{"knowledge_run", run}, {"pack_sha256", pack->hash()}, {"graph_claim_ids", graph_claims}};
  auto receipts = settings.value("receipt_ids", std::vector<std::string>{});
  LOOM_TRY_ASSIGN(state->snapshot, state->registry.load(settings["profile"], receipts));
  Json capabilities{{"execution", method_channel_capabilities()}, {"fusion", method_fusion_capabilities()}};
  const Json native = capabilities["execution"];
  const Json bindings = settings.value("capability_bindings", Json::object());
  if (!bindings.is_object()) return invalid("capability_bindings must be an object");
  Json embedding = execution.value("_native_embedding_binding", Json::object());
  const auto embedding_id = json::get_string(embedding, "id");
  for (const auto& [id, channel] : installed) {
    if (id == embedding_id) {
      capabilities["execution"][id] = embedding;
      if (!embedding.value("actual_consumed_parameters", Json(nullptr)).is_object()) {
        capabilities["execution"][id]["available"] = false;
        capabilities["execution"][id]["reason"] = "actual_parameter_binding_unavailable";
      }
    } else if (!native.contains(id) || bindings.contains(id)) {
      const bool bound = bindings.contains(id) && bindings[id].is_object() &&
          bindings[id].contains("actual_parameters") && bindings[id]["actual_parameters"].is_object();
      capabilities["execution"][id] = Json{{"available", static_cast<bool>(channel) && bound},
          {"injected", true}, {"verification", "not_performed"}, {"authorization", "native_caller_owned"},
          {"reason", bound ? "explicit_native_parameter_binding" : "actual_parameter_binding_unavailable"}};
    }
  }
  LOOM_TRY_ASSIGN(state->plan, state->registry.resolve(state->snapshot,
      settings.value("selection", Json::object()), capabilities));
  // Policy is explicit data and validated even when no results are measured.
  LOOM_TRY(blend_method_score(0.0, std::nullopt, settings));
  LOOM_TRY(method_diversity_score(0.0, 0, settings));
  ContextRequest actual = request;
  actual.candidate_channels.clear();
  std::map<std::string, std::shared_ptr<CandidateChannel>> channels;
  std::size_t index = 0;
  for (const auto& leaf : state->plan["leaves"]) {
    const std::string id = "method_occurrence_" + std::to_string(index);
    LOOM_TRY_ASSIGN(auto spec, actual_request(leaf["effective_parameters"], id));
    actual.candidate_channels.push_back(spec);
    if (leaf.value("available", false)) {
      const auto capability = leaf["execution_capability"].get<std::string>();
      std::shared_ptr<CandidateChannel> channel;
      Json consumed = leaf["effective_parameters"];
      const auto injected = installed.find(capability);
      if (injected != installed.end() && (capability == embedding_id || bindings.contains(capability) || !native.contains(capability))) {
        const Json actual_parameters = capability == embedding_id ? embedding.value("actual_consumed_parameters", Json(nullptr)) :
            bindings[capability].value("actual_parameters", Json(nullptr));
        if (!actual_parameters.is_object()) return invalid("actual injected parameters unavailable");
        for (auto parameter = consumed.begin(); parameter != consumed.end(); ++parameter) {
          if (parameter.key() == "limit" || parameter.key() == "min_score") continue;
          if (!actual_parameters.contains(parameter.key()) || actual_parameters[parameter.key()] != parameter.value())
            return invalid("declared injected parameter differs from the actual instrument: " + parameter.key());
        }
        consumed = actual_parameters;
        consumed["limit"] = spec.limit;
        consumed["min_score"] = spec.min_score;
        channel = injected->second;
      }
      else {
        std::set<std::string> consumed_keys;
        for (const auto& key : native[capability]["parameter_keys"]) consumed_keys.insert(key.get<std::string>());
        for (auto parameter = consumed.begin(); parameter != consumed.end(); ++parameter)
          if (!consumed_keys.contains(parameter.key())) return invalid("unused native instrument parameter: " + parameter.key());
        LOOM_TRY_ASSIGN(channel, method_channel(capability, leaf["effective_parameters"], pack, graph_claims));
      }
      if (channel) channels[id] = std::make_shared<RecordedChannel>(state, channel, leaf, consumed, index);
    }
    ++index;
  }
  result.candidates = gather_context_candidates(store, run, pack, actual, evidence, graph_claims, channels);
  index = 0;
  for (auto& batch : result.candidates.trace["channels"]) {
    batch["leaf_index"] = index++;
    const auto id = batch["id"].get<std::string>();
    if (state->records.contains(id)) {
      batch["method_graph"] = state->records[id];
      const auto& record = state->records[id];
      if (record.contains("measurement_nodes")) {
        for (auto ref = record["measurement_nodes"].begin(); ref != record["measurement_nodes"].end(); ++ref)
          result.candidates.factors[ref.key()]["method_results"][id] = Json{{"entity_id", ref.value()},
              {"leaf_index", batch["leaf_index"]}, {"manifest", record["manifest"]}};
      }
    }
    else batch["method_graph"] = Json{{"status", "unavailable"}, {"reason", "resolved_execution_unavailable"}};
  }
  const auto fusion_parameters = composed_parameters(*state);
  const Json fusion_input{{"parameters", fusion_parameters}, {"executed_batches", result.candidates.trace},
      {"corpus_scope", state->options["corpus_scope"]}};
  LOOM_TRY(prepare_stage(*state, "fusion", "context_fusion", fusion_input, fusion_parameters, true));
  LOOM_TRY_ASSIGN(result.scores, fuse_method_results(state->plan, result.candidates.trace));
  Json fused = Json::object();
  for (const auto& [ref, score] : result.scores) fused[ref] = score;
  LOOM_TRY(bind_stage(*state, "fusion", Json{{"scores", fused}},
      Json{{"measured_ref_count", result.scores.size()}}, fused));
  if (state->stages["fusion"].contains("measurement_nodes")) {
    const auto& fusion = state->stages["fusion"];
    for (const auto& [ref, id] : fusion["measurement_nodes"].items())
      result.candidates.factors[ref]["method_fusion_result"] =
          Json{{"entity_id", id}, {"manifest", fusion["manifest"]}};
  }
  result.plan = state->plan;
  result.settings = settings;
  result.graph = execution_graph(*state);
  result.execution_state = state;
  return result;
}

Status prepare_method_selection(MethodCandidates& methods, const Json& input, const Json& parameters) {
  if (!methods.enabled) return {};
  if (!methods.execution_state) return invalid("method selection execution state unavailable");
  auto& state = *methods.execution_state;
  Json consumed = parameters;
  consumed["composition"] = composed_parameters(state);
  consumed["graph_blend_operation"] = methods.settings["graph_blend_operation"];
  consumed["diversity"] = methods.settings["diversity"];
  // The dependency closure performs database reads after initial budgeting.
  // This exact snapshot is useful evidence, but is not the complete input of
  // the whole selector. Do not fabricate an all-input attestation from it.
  LOOM_TRY(prepare_stage(state, "selection", "context_selection",
      Json{{"captured_candidates", input}, {"consumed_parameters", consumed},
           {"input_scope", "preselection_snapshot; dynamic_dependency_inputs_recorded_in_final_diagnostics"}},
      consumed, false));
  methods.graph = execution_graph(state);
  return {};
}

Status finalize_method_selection(MethodCandidates& methods, model::ContextSet& selected) {
  if (!methods.enabled) return {};
  if (!methods.execution_state) return invalid("method selection execution state unavailable");
  auto& state = *methods.execution_state;
  Json items = Json::array(), dropped = Json::array(), decisions = Json::object();
  for (const auto& item : selected.items) {
    items.push_back(item.to_json());
    decisions[item.ref] = Json{{"decision", "included"}, {"item", item.to_json()}};
  }
  for (const auto& item : selected.dropped) {
    dropped.push_back(item.to_json());
    decisions[item.ref] = Json{{"decision", "budget_dropped"}, {"item", item.to_json()}};
  }
  const Json output{{"items", items}, {"dropped", dropped}, {"budget_tokens", selected.budget_tokens},
      {"used_tokens", selected.used_tokens}, {"pack_hash", selected.pack_hash},
      {"retrieval_diagnostics", selected.goal.params.value("retrieval_diagnostics", Json::object())}};
  LOOM_TRY(bind_stage(state, "selection", output,
      Json{{"included_count", items.size()}, {"budget_dropped_count", dropped.size()},
           {"used_tokens", selected.used_tokens}}, decisions));
  methods.graph = execution_graph(state);
  selected.goal.params["method_registry"]["result_graph"] = methods.graph;
  return {};
}

Result<double> blend_method_score(double legacy, const std::optional<double>& measured, const Json& settings) {
  const auto op = settings.value("graph_blend_operation", "");
  double score;
  if (op == "method_only") score = measured.value_or(legacy); // caller excludes unmeasured roots
  else if (op == "replace_measured") score = measured.value_or(legacy);
  else if (op == "sum") score = legacy + measured.value_or(0.0);
  else if (op == "max") score = measured ? std::max(legacy, *measured) : legacy;
  else if (op == "min") score = measured ? std::min(legacy, *measured) : legacy;
  else return invalid("graph_blend_operation data missing or unsupported");
  if (!std::isfinite(score)) return invalid("nonfinite blended score");
  return score;
}

Result<double> method_diversity_score(double score, std::size_t count, const Json& settings) {
  const Json policy = settings.value("diversity", Json::object());
  const auto op = policy.value("operation", "");
  if (op == "identity") return score;
  if (op != "signed_discount" || !policy.contains("factor"))
    return invalid("diversity operation/factor data missing or unsupported");
  LOOM_TRY_ASSIGN(auto factor, finite_value(policy["factor"], "diversity.factor"));
  if (factor <= 0) return invalid("diversity factor must be positive for signed division");
  const double discount = std::pow(factor, count);
  const double value = score >= 0 ? score * discount : score / discount;
  if (!std::isfinite(value)) return invalid("diversity arithmetic overflow");
  return value;
}
}  // namespace loom::context
