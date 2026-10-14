// The result of an action run lands in the graph (see loom/action_result.h). Data: runtime profile action_results.
#include "loom/action_result.h"

#include <algorithm>
#include <map>
#include <set>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::actions {

namespace {

// One result part on its way into the graph.
struct Item {
  std::string step;        // id of the step that produced it
  std::size_t index = 0;   // position among that step's outputs
  std::string kind;        // typed part kind
  std::string cls;         // form class
  std::string text;        // kind text: the full text
  std::string mime;
  std::string blob_hash;   // media bytes, or text that was moved to the blob store
  std::string blob_node;   // graph node of the blob (a media output has one from the output store)
  std::string url;
  std::int64_t size = 0;
  std::string preview;
  std::string form;        // node id (or message id) the item became
  std::string message_id;
  bool kept = false;       // the output of a step whose action keeps it (keeps_output: a recording, a picture) though a later step goes on from it
};

Json ref_of(const Item& it) {
  Json p{{"kind", it.kind}, {"text", ""}, {"blob_hash", it.blob_hash}, {"mime", it.mime}, {"size", it.size}, {"preview", it.preview}, {"stored", "blob"}};
  return p;
}

bool is_ref(const Json& part) { return json::get_string(part, "stored") == "blob" && json::get_string(part, "kind") == "text"; }

// Model calls of a step record: [{model, reported_model, provider, adapter, endpoint, request_sha256}].
Json calls_of(const Json& rec) {
  Json calls = Json::array();
  for (const auto& v : rec.value("variants", Json::array()))
    for (const auto& p : v.value("provenance", Json::array())) {
      if (!p.is_object() || json::get_string(p, "schema") != "loom.model_use/1") continue;
      Json c = Json::object();
      for (const char* k : {"requested_model", "reported_model", "provider", "adapter", "endpoint", "request_sha256", "status", "operation_id", "usage"})
        if (const Json* x = json::find(p, k); x && !x->is_null()) c[k] = *x;
      c["variant"] = v.value("label", "");
      calls.push_back(std::move(c));
    }
  return calls;
}

Json run_node_metadata(const Json& run, const std::string& result_step) {
  Json m = Json::object();
  m["schema"] = std::string(kRunNodeSchema);
  m["run_id"] = run.value("id", "");
  const Json plan = run.value("plan", Json::object());
  Json actions = Json::array();
  for (const auto& item : plan.value("items", Json::array())) actions.push_back(item.value("action", ""));
  const Json request = run.value("request", Json::object());
  m["action"] = json::get_string(request, "action", actions.empty() ? std::string() : (actions.size() == 1 ? actions[0].get<std::string>() : std::string("chain")));
  m["actions"] = actions;
  m["status"] = run.value("status", "completed");
  m["created"] = run.value("created", "");
  m["step"] = result_step;
  const Json element = run.value("element", Json::object());
  m["element"] = Json{{"id", json::get_string(element, "id")}, {"class", json::get_string(element, "class")}};
  Json steps = Json::array();
  Json first_call = nullptr;
  for (const auto& rec : run.value("steps", Json::array())) {
    Json s{{"id", rec.value("id", "")}, {"action", rec.value("action", "")}, {"status", rec.value("status", "")}};
    const Json calls = calls_of(rec);
    if (!calls.empty()) s["calls"] = calls;
    if (rec.value("id", "") == result_step && !calls.empty()) first_call = calls[0];
    steps.push_back(std::move(s));
  }
  m["steps"] = steps;
  if (first_call.is_object()) {
    m["model"] = first_call.value("requested_model", Json(nullptr));
    m["adapter"] = first_call.value("adapter", Json(nullptr));
    m["provider"] = first_call.value("provider", Json(nullptr));
  }
  Json receipts = Json::array();
  for (const auto& c : run.value("confirmations", Json::array()))
    if (json::get_string(c, "kind") == "usage" && json::get_bool(c, "approved", false) && !json::get_string(c, "receipt_id").empty())
      receipts.push_back(c.at("receipt_id"));
  m["usage_receipt_ids"] = receipts;
  // Every admitted model call carries the number of its usage receipt (operation_id of its provenance record), a call that the guard
  // held and the user confirmed included. The receipt of the run is the user's confirmation when there was one, else the last call's.
  Json operations = Json::array();
  for (const auto& rec : run.value("steps", Json::array()))
    for (const auto& call : calls_of(rec)) {
      const Json id = call.value("operation_id", Json(nullptr));
      if (id.is_string() && !id.get<std::string>().empty() && std::find(operations.begin(), operations.end(), id) == operations.end()) operations.push_back(id);
    }
  m["usage_operation_ids"] = operations;
  m["usage_receipt_id"] = !receipts.empty() ? receipts[receipts.size() - 1] : operations.empty() ? Json(nullptr) : operations[operations.size() - 1];
  if (const Json* ctx = json::find(request, "context"); ctx && ctx->is_object()) {
    for (const char* k : {"origin", "conv_id", "message_id"})
      if (const Json* x = json::find(*ctx, k); x && x->is_string()) m["context"][k] = *x;
  }
  return m;
}

// content_op of the action the result comes from: the last item of the plan.
std::string content_op_of(const Engine* engine, const Json& run) {
  if (!engine) return std::string();
  const Json items = run.value("plan", Json::object()).value("items", Json::array());
  if (items.empty()) return std::string();
  const Json* entry = json::find(engine->catalogue(), items[items.size() - 1].value("action", ""));
  return entry ? json::get_string(*entry, "content_op", "none") : std::string();
}

// An answer to a question (the list of conversations, a spend report, what can be done here) is said in the conversation and is
// not content: an action that declares `stores_result: false` leaves no form or content node behind.
bool stores_result(const Engine* engine, const Json& run) {
  if (!engine) return true;
  const Json items = run.value("plan", Json::object()).value("items", Json::array());
  if (items.empty()) return true;
  const Json* entry = json::find(engine->catalogue(), items[items.size() - 1].value("action", ""));
  return !entry || json::get_bool(*entry, "stores_result", true);
}

Json fidelity_of(const Json& profile, const std::string& content_op) {
  const Json* table = json::find(profile, "fidelity");
  if (table && !content_op.empty())
    if (const Json* row = json::find(*table, content_op)) return *row;
  Json unknown = profile.value("unknown_fidelity", Json{{"fidelity", 0.5}});
  unknown["known"] = false;
  return unknown;
}

Result<bool> link_exists(Database& db, const std::string& src, const std::string& dst, const std::string& type) {
  LOOM_TRY_ASSIGN(auto links, db.get_links(src, type));
  for (const auto& l : links)
    if (l.src == src && l.dst == dst) return true;
  return false;
}

}  // namespace

Json scope_of_run(const Json& run) {
  const Json request = run.value("request", Json::object());
  if (const Json* s = json::find(request, "scope"); s && !s->is_null()) return *s;
  const Json ctx = request.value("context", Json::object());
  const std::string conv = json::get_string(ctx, "conv_id");
  if (!conv.empty()) return Json{{"conversation", conv}, {"turn", ""}};
  return Json(nullptr);
}

Result<ScopedValue> result_target(const ResultServices& s, const Json& run) {
  const Json* target = json::find(s.profile, "target");
  if (!target) return Error(Errc::InvalidArgument, "the action_results profile has no target");
  std::vector<Baseline> baselines;
  const std::string active = json::get_string(*target, "active");
  if (!active.empty()) baselines.emplace_back("installation", active, "profile:action_results.target.active");
  const std::string setting = json::get_string(*target, "setting");
  if (s.config && !setting.empty()) {
    const Json v = s.config->get(setting);
    if (v.is_string() && !v.get<std::string>().empty()) baselines.emplace_back("person", v, "setting:" + setting);
  }
  if (!s.engine) {
    ScopedValue out;
    if (!baselines.empty()) {
      const Baseline& b = baselines.back();
      out.value = b.value;
      out.defined = true;
      out.scope = b.level;
      out.source = b.source;
      out.rule = "baseline:" + b.level;
    }
    return out;
  }
  LOOM_TRY_ASSIGN(Scope scope, s.engine->parse_scope(scope_of_run(run)));
  return s.engine->resolve_value(json::get_string(*target, "key", "action_results.target"), scope, baselines);
}

Result<std::string> part_text(BlobStore* blobs, const Json& part) {
  if (!part.is_object()) return std::string();
  if (is_ref(part)) {
    if (!blobs) return Error(Errc::Unavailable, "the text of this result is in the blob store, which is not available here");
    return blobs->read(json::get_string(part, "blob_hash"));
  }
  return json::get_string(part, "text");
}

Status store_run_results(const ResultServices& s, Json& run) {
  if (!s.profile.is_object() || json::get_bool(run, "sensitive", false)) return {};
  if (!run.contains("result") || !run["result"].is_object()) return {};
  if (!stores_result(s.engine, run)) return {};
  const Json& profile = s.profile;
  const Json by_part = profile.value("classes", Json::object()).value("by_part", Json::object());
  const std::int64_t inline_max = [&] {
    const Json* v = json::find(profile, "inline_max_bytes");
    return v && v->is_number_integer() ? v->get<std::int64_t>() : static_cast<std::int64_t>(-1);
  }();
  const std::size_t preview_len = static_cast<std::size_t>(json::get_int(profile, "preview_codepoints", 160));
  const std::size_t label_len = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(profile, "label_codepoints", 80)));
  const std::string blob_mime = json::get_string(profile, "blob_mime", "text/plain");
  const bool all_steps = json::get_string(profile, "steps", "result") == "all";
  Json issues = Json::array();
  auto issue = [&](const std::string& code, const std::string& message) { issues.push_back(Json{{"code", code}, {"message", message}}); };

  // 1. The parts of the result (or of every step) that are worth storing.
  std::vector<Item> items;
  auto gather = [&](const std::string& step, const Json& parts, bool kept = false) {
    std::size_t index = 0;
    for (const auto& p : parts) {
      const std::size_t at = index++;
      const std::string kind = json::get_string(p, "kind");
      const std::string cls = json::get_string(by_part, kind);
      if (cls.empty()) continue;
      Item it;
      it.kept = kept;
      it.step = step;
      it.index = at;
      it.kind = kind;
      it.cls = cls;
      it.mime = json::get_string(p, "mime");
      it.blob_hash = json::get_string(p, "blob_hash");
      it.blob_node = json::get_string(p, "node_id");
      it.url = json::get_string(p, "url");
      if (kind == "text") {
        if (is_ref(p)) continue;  // already stored
        it.text = json::get_string(p, "text");
        if (it.text.empty()) continue;
        it.size = static_cast<std::int64_t>(it.text.size());
        it.preview = std::string(utf8::prefix(it.text, preview_len));
      } else if (it.blob_hash.empty() && it.url.empty()) {
        continue;  // nothing to refer to
      }
      items.push_back(std::move(it));
    }
  };
  const Json result = run["result"];
  if (all_steps) {
    for (const auto& rec : run.value("steps", Json::array()))
      if (rec.value("status", "") == "ok") gather(rec.value("id", ""), rec.value("outputs", Json::array()));
  } else {
    // What a device brought (a recording, a picture, a picked file) is original material: it is kept as a form of its own though
    // a later step (transcribe, ocr) goes on from it and the result of the run is something else. Its forms come first.
    if (s.engine)
      for (const auto& rec : run.value("steps", Json::array())) {
        const std::string step = rec.value("id", "");
        if (rec.value("status", "") != "ok" || step == json::get_string(result, "from")) continue;
        const Json* entry = json::find(s.engine->catalogue(), rec.value("action", ""));
        if (entry && json::get_bool(*entry, "keeps_output", false)) gather(step, rec.value("outputs", Json::array()), true);
      }
    gather(json::get_string(result, "from"), result.value("parts", Json::array()));
  }

  // 2. Large text goes to the blob store; the run document keeps a reference. Whatever the target is.
  std::map<std::string, Json> moved;  // text -> reference, so the same text is written once
  if (inline_max >= 0 && s.blobs) {
    auto offload = [&](Json& part) {
      if (json::get_string(part, "kind") != "text" || is_ref(part)) return;
      const std::string text = json::get_string(part, "text");
      if (static_cast<std::int64_t>(text.size()) <= inline_max) return;
      auto found = moved.find(text);
      if (found == moved.end()) {
        auto ref = s.blobs->put(text, blob_mime);
        if (!ref) {
          issue("blob_write_failed", ref.error().message);
          moved.emplace(text, Json(nullptr));
          return;
        }
        Item shown;
        shown.kind = "text";
        shown.blob_hash = ref->hash;
        shown.mime = blob_mime;
        shown.size = ref->size;
        shown.preview = std::string(utf8::prefix(text, preview_len));
        found = moved.emplace(text, ref_of(shown)).first;
      }
      if (!found->second.is_null()) part = found->second;
    };
    auto walk = [&](Json& parts) {
      if (!parts.is_array()) return;
      for (auto& p : parts) offload(p);
    };
    if (run["result"].contains("parts")) walk(run["result"]["parts"]);
    if (run.contains("steps") && run["steps"].is_array())
      for (auto& rec : run["steps"]) {
        if (rec.contains("outputs")) walk(rec["outputs"]);
        if (rec.contains("variants") && rec["variants"].is_array())
          for (auto& v : rec["variants"])
            if (v.contains("parts")) walk(v["parts"]);
      }
    for (auto& it : items) {
      if (it.kind != "text") continue;
      auto found = moved.find(it.text);
      if (found != moved.end() && found->second.is_object()) {
        it.blob_hash = json::get_string(found->second, "blob_hash");
        it.mime = blob_mime;
      }
    }
  }

  // 3. Where the results go.
  auto target = result_target(s, run);
  const Json target_cfg = profile.value("target", Json::object());
  const Json* presets = json::find(target_cfg, "presets");
  std::string target_id;
  bool to_graph = false, to_message = false;
  Json target_json = Json::object();
  if (!target) {
    issue("target_unresolved", target.error().message);
  } else {
    target_json = target->to_json();
    target_id = target->value.is_string() ? target->value.get<std::string>() : std::string();
    const Json* preset = presets && !target_id.empty() ? json::find(*presets, target_id) : nullptr;
    if (!preset) {
      issue("target_unknown", "no preset '" + target_id + "' in the action_results profile; the result stays in the run document");
    } else {
      to_graph = json::get_bool(*preset, "graph", false);
      to_message = json::get_bool(*preset, "message", false);
    }
  }

  Json produced = Json::object();
  produced["schema"] = std::string(kProducedSchema);
  produced["target"] = target_id;
  produced["target_scope"] = target_json;
  produced["graph"] = to_graph;
  produced["message"] = to_message;
  produced["items"] = Json::array();
  produced["content"] = nullptr;
  produced["run_node"] = nullptr;

  if ((to_graph || to_message) && !items.empty() && s.db) {
    Database& db = *s.db;
    const Json links = profile.value("links", Json::object());
    auto link_type = [&](const char* key) { return json::get_string(links, key, key); };
    const Json classes = profile.value("classes", Json::object());
    const Json element = run.value("element", Json::object());
    const std::string run_id = run.value("id", "");
    const Json request = run.value("request", Json::object());
    const Json context = request.value("context", Json::object());

    // The source element: a node id the links can point to. A group of elements (combine) has one source per member and no
    // single source: its content is the group's own, and every member is something the result is derived from.
    auto source_node = [&](const Json& el) {
      std::string id = json::get_string(el, "id");
      if (json::get_string(el, "source") == "device" && !id.empty()) {
        // A device (device:microphone ...) is a node of its class: what it captured is derived from it.
        NodeOptions o;
        o.tags = Json::array({"device"});
        o.metadata = Json{{"device", id}, {"origin", "action_result"}};
        auto made = db.get_or_create_node(id, json::get_string(el, "class", "device"), o);
        if (made) return *made;
        issue("source_device_node", made.error().message);
        return id;
      }
      if (json::get_string(el, "source") == "blob" && !id.empty()) {
        const Json attrs = el.value("attrs", Json::object());
        std::string node = json::get_string(attrs, "node_id");
        if (node.empty()) {
          NodeOptions o;
          o.metadata = Json{{"blob_hash", json::get_string(attrs, "hash", id)}, {"mime", json::get_string(attrs, "mime")}};
          auto made = db.get_or_create_node("blob:" + json::get_string(attrs, "hash", id), "blob", o);
          if (made) node = *made;
          else issue("source_blob_node", made.error().message);
        }
        if (!node.empty()) id = node;
      }
      return id;
    };
    const bool grouped = json::get_string(element, "source") == "group";
    // What a device captured is a content of its own for each run (every recording differs); the device is only where it came from.
    const bool from_device = json::get_string(element, "source") == "device";
    std::vector<std::string> sources;
    if (grouped) {
      for (const auto& m : element.value("members", Json::array()))
        if (std::string one = source_node(m); !one.empty()) sources.push_back(std::move(one));
    } else if (std::string one = source_node(element); !one.empty()) {
      sources.push_back(std::move(one));
    }
    auto joined = [&](const char* sep) {
      std::string out;
      for (const auto& x : sources) out += (out.empty() ? "" : sep) + x;
      return out;
    };
    const std::string source = grouped ? std::string() : (sources.empty() ? std::string() : sources[0]);

    // The content node: one per source element (per set of them, for a group), reused by every later run, never rewritten.
    const std::string content_kind = json::get_string(classes, "content", "content");
    const std::string content_label = "content:" + (grouped ? (sources.empty() ? run_id : joined("+")) : (source.empty() || from_device ? run_id : source));
    std::string content_id;
    bool content_reused = false;
    {
      auto existing = db.find_node(content_label, content_kind);
      if (!existing) {
        issue("content_node", existing.error().message);
      } else if (*existing) {
        content_id = (*existing)->id;
        content_reused = true;
      } else {
        NodeOptions o;
        o.tags = Json::array({"content", "action_result"});
        o.metadata = Json{{"schema", "loom.action_content/1"}, {"source", grouped ? joined(",") : source}, {"source_class", json::get_string(element, "class")},
                          {"origin", "action_result"}, {"first_run", run_id}};
        auto made = db.create_node(content_label, content_kind, o);
        if (made) content_id = *made;
        else issue("content_node", made.error().message);
      }
    }
    produced["content"] = content_id.empty() ? Json(nullptr) : Json(content_id);
    produced["content_reused"] = content_reused;
    produced["source"] = grouped ? joined(",") : source;
    if (grouped) produced["sources"] = sources;

    // The source is a form of that content as well (fidelity 1): a search for the content finds it in every form.
    if (!content_id.empty() && !source.empty() && !from_device) {
      auto have = link_exists(db, source, content_id, link_type("expresses"));
      if (have && !*have) {
        auto made = db.create_link(source, content_id, link_type("expresses"), 1.0,
                                   Json{{"origin", "source"}, {"first_run", run_id}});
        if (!made) issue("source_link", made.error().message);
      }
    }

    // The run node.
    const std::string result_step = json::get_string(result, "from");
    const std::string run_kind = json::get_string(classes, "run", "action_run");
    std::string run_node;
    const Json run_meta = run_node_metadata(run, result_step);
    {
      NodeOptions o;
      o.tags = Json::array({"action_run"});
      o.metadata = run_meta;
      auto made = db.get_or_create_node("run:" + run_id, run_kind, o);
      if (made) run_node = *made;
      else issue("run_node", made.error().message);
    }
    produced["run_node"] = run_node.empty() ? Json(nullptr) : Json(run_node);
    // The port runs the steps made were written before this node existed: they name the run, and are linked to it now (port_graph.h). Only
    // this run's own calls are looked up, by the label their operation id gives them ("port_run:<operation id>"). port_graph::
    // reconcile_action_runs would read every port run of the data directory and look up an action run for each of them, on every run that
    // made a call; the nodes table has an index on `kind` only, so that cost grows with the history (see the P14 report).
    if (!run_node.empty())
      for (const auto& operation : run_meta.value("usage_operation_ids", Json::array())) {
        const std::string label = "port_run:" + operation.get<std::string>();
        auto found = db.find_node(label, "port_run");
        if (!found) {
          issue("port_run_links", found.error().message);
          continue;
        }
        if (!*found || json::get_string((*found)->metadata, "action_run_id") != run_id) continue;  // another run's call is not linked here
        if (auto linked = db.create_link((*found)->id, run_node, "part_of", 1.0, Json{{"run_id", run_id}}); !linked) issue("port_run_links", linked.error().message);
      }

    // The conversation a message goes to.
    const std::string conv_id = [&] {
      std::string c = json::get_string(context, "conv_id");
      if (c.empty() && json::get_string(element, "source") == "conversation") c = json::get_string(element, "id");
      if (c.empty())
        if (const Json* a = json::find(element, "attrs")) c = json::get_string(*a, "conv_id");
      return c;
    }();
    const std::string action_id = run_meta.value("action", "");
    const std::string content_op = content_op_of(s.engine, run);
    const Json fidelity = fidelity_of(profile, content_op);

    for (auto& it : items) {
      // Text that went to the blob store is a blob node; a media output already has one from the output store.
      std::string blob_node = it.blob_node;
      if (blob_node.empty() && !it.blob_hash.empty()) {
        NodeOptions o;
        o.tags = Json::array({"blob", "action_result"});
        o.metadata = Json{{"blob_hash", it.blob_hash}, {"size", it.size}, {"mime", it.mime}};
        if (auto made = db.get_or_create_node("blob:" + it.blob_hash, "blob", o)) blob_node = *made;
        else issue("blob_node", made.error().message);
      }
      const bool post = to_message && it.kind == "text" && !conv_id.empty();
      if (to_message && it.kind == "text" && conv_id.empty())
        issue("no_conversation", "the target posts a message but the run belongs to no conversation; the result is a node");
      std::string form;
      if (post) {
        NewMessage nm;
        nm.conv_id = conv_id;
        nm.role = json::get_string(profile.value("message", Json::object()), "role", "assistant");
        nm.text = it.text;
        if (const std::string model = json::get_string(profile.value("message", Json::object()), "model"); !model.empty()) nm.model = model;
        nm.metadata = Json{{"action_result", Json{{"run_id", run_id}, {"action", action_id}, {"step", it.step}, {"content", content_id}, {"run_node", run_node}}}};
        auto made = db.create_msg(nm);
        if (made) {
          form = *made;
          it.message_id = *made;
          if (s.bus) s.bus->emit(events::kMsgCreated, Json{{"id", *made}, {"text", it.text}, {"conv_id", conv_id}, {"role", nm.role}});
        } else {
          issue("message_failed", made.error().message);
        }
      }
      if (form.empty() && to_graph) {
        NodeOptions o;
        o.content = it.kind == "text" && it.blob_hash.empty() ? it.text : std::string();
        o.tags = Json::array({"action_result", action_id});
        Json meta = Json::object();
        meta["schema"] = std::string(kFormNodeSchema);
        meta["run_id"] = run_id;
        meta["action"] = action_id;
        meta["step"] = it.step;
        meta["part"] = it.index;
        meta["part_kind"] = it.kind;
        meta["source"] = grouped ? joined(",") : source;
        meta["content"] = content_id;
        if (!it.mime.empty()) meta["mime"] = it.mime;
        if (!it.blob_hash.empty()) meta["blob_hash"] = it.blob_hash;
        if (!it.url.empty()) meta["url"] = it.url;
        if (it.kind == "text") {
          meta["size"] = it.size;
          meta["sha256"] = Sha256::hex(it.text);
          if (!it.blob_hash.empty()) meta["preview"] = it.preview;
        }
        o.metadata = std::move(meta);
        std::string label = it.kind == "text" ? std::string(utf8::prefix(it.text, label_len)) : action_id + " (" + it.kind + ")";
        for (char& c : label)
          if (c == '\n' || c == '\r') c = ' ';
        if (label.empty()) label = action_id;
        auto made = db.create_node(label, it.cls, o);
        if (made) form = *made;
        else issue("form_node", made.error().message);
      }
      if (form.empty()) continue;
      it.form = form;
      // The links of the form.
      if (!content_id.empty()) {
        Json meta{{"origin", "action_result"}, {"run_id", run_id}, {"action", action_id}, {"content_op", content_op},
                  {"kept", fidelity.value("kept", Json::array())}, {"lost", fidelity.value("lost", Json::array())},
                  {"added", fidelity.value("added", Json::array())}, {"fidelity_known", fidelity.value("known", true)}};
        auto made = db.create_link(form, content_id, link_type("expresses"), json::get_number(fidelity, "fidelity", 0.5), meta);
        if (!made) issue("expresses_link", made.error().message);
      }
      for (const auto& from : sources) {
        auto made = db.create_link(form, from, link_type("derived_from"), 1.0, Json{{"run_id", run_id}, {"action", action_id}});
        if (!made) issue("derived_link", made.error().message);
      }
      if (!run_node.empty()) {
        Json meta{{"run_id", run_id}, {"step", it.step}, {"action", action_id}};
        for (const char* k : {"model", "adapter", "provider", "usage_receipt_id"})
          if (const Json* x = json::find(run_meta, k)) meta[k] = *x;
        auto made = db.create_link(form, run_node, link_type("produced_by"), 1.0, meta);
        if (!made) issue("produced_link", made.error().message);
      }
      if (!blob_node.empty()) {
        auto made = db.create_link(form, blob_node, link_type("stored_as"), 1.0, Json{{"blob_hash", it.blob_hash}, {"mime", it.mime}});
        if (!made) issue("stored_link", made.error().message);
      }
      // The result goes on from what a device brought (the transcript of the recording): derived from that form as well.
      if (!it.kept)
        for (const auto& earlier : items) {
          if (!earlier.kept || earlier.form.empty()) continue;
          auto made = db.create_link(form, earlier.form, link_type("derived_from"), 1.0, Json{{"run_id", run_id}, {"action", action_id}, {"from_step", earlier.step}});
          if (!made) issue("derived_link", made.error().message);
        }
      Json row{{"part", it.index}, {"step", it.step}, {"kind", it.kind}, {"class", it.cls}, {"form", form}};
      if (it.kept) row["kept"] = true;
      if (!it.message_id.empty()) row["message_id"] = it.message_id;
      if (!it.blob_hash.empty()) row["blob_hash"] = it.blob_hash;
      if (!blob_node.empty()) row["blob_node"] = blob_node;
      if (it.size > 0) row["size"] = it.size;
      produced["items"].push_back(std::move(row));
    }
  }
  produced["issues"] = issues;
  run["result"]["produced"] = produced;
  return {};
}

}  // namespace loom::actions
