// catalog.h: Catalog::query / preview / read_unit / status / add_profile_terms.
#include "loom/catalog.h"

#include <algorithm>
#include <cctype>

#include "catalog_internal.h"
#include "relevance_recipe.h"
#include "source_index.h"
#include "loom/export_mapping.h"
#include "loom/importer.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog {

using namespace loom::catalog::internal;

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

Result<std::string> latest_run(sql::Connection& c) {
  auto r = c.query_text("SELECT run_id FROM loom_cat_scores ORDER BY rowid DESC LIMIT 1");
  if (!r) return r.error();
  return r->value_or(std::string());
}

}  // namespace

Result<std::vector<CatalogUnit>> Catalog::query(const UnitQuery& q) {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();
  std::string run_id(q.run_id);
  if (run_id.empty()) {
    LOOM_TRY_ASSIGN(run_id, latest_run(c));
  }

  struct Row {
    CatalogUnit unit;
    double score = 0.0;
    std::string label;
    bool has_score = false;
    bool selected = false;
    bool has_decision = false;
  };
  std::vector<Row> rows;
  LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare("SELECT id, body FROM loom_cat_units"));
  while (true) {
    LOOM_TRY_ASSIGN(bool has, st.step());
    if (!has) break;
    auto j = json::parse(st.get_text(1));
    if (!j) continue;
    auto cu = CatalogUnit::from_json(*j);
    if (!cu) continue;
    Row row;
    row.unit = std::move(*cu);
    rows.push_back(std::move(row));
  }
  if (!run_id.empty()) {
    for (auto& row : rows) {
      LOOM_TRY_ASSIGN(sql::Stmt sst, c.prepare("SELECT score, label FROM loom_cat_scores WHERE run_id = ? AND unit_id = ?"));
      sst.bind(1, run_id);
      sst.bind(2, row.unit.unit.id);
      LOOM_TRY_ASSIGN(bool has, sst.step());
      if (has) {
        row.score = sst.get_double(0);
        row.label = sst.get_text(1);
        row.has_score = true;
      }
      LOOM_TRY_ASSIGN(sql::Stmt dst, c.prepare("SELECT selected FROM loom_cat_decisions WHERE run_id = ? AND unit_id = ?"));
      dst.bind(1, run_id);
      dst.bind(2, row.unit.unit.id);
      LOOM_TRY_ASSIGN(bool dhas, dst.step());
      if (dhas) {
        row.selected = dst.get_int(0) != 0;
        row.has_decision = true;
      }
    }
  }

  std::vector<Row> filtered;
  std::string text_needle = q.text ? lower(*q.text) : std::string();
  for (auto& row : rows) {
    if (q.label && row.label != *q.label) continue;
    if (q.project) {
      bool found = false;
      for (const auto& m : row.unit.mentions) {
        if (json::get_string(m, "key") == *q.project) {
          found = true;
          break;
        }
      }
      if (!found) continue;
    }
    if (q.selected && (!row.has_decision || row.selected != *q.selected)) continue;
    if (!text_needle.empty()) {
      std::string hay = lower(row.unit.unit.title + " " + row.unit.head);
      if (hay.find(text_needle) == std::string::npos) continue;
    }
    filtered.push_back(std::move(row));
  }

  if (q.sort == "date") {
    std::sort(filtered.begin(), filtered.end(),
             [](const Row& a, const Row& b) { return a.unit.unit.date > b.unit.unit.date; });
  } else if (q.sort == "id") {
    std::sort(filtered.begin(), filtered.end(), [](const Row& a, const Row& b) { return a.unit.unit.id < b.unit.unit.id; });
  } else {
    std::sort(filtered.begin(), filtered.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
  }

  std::vector<CatalogUnit> out;
  for (std::size_t i = static_cast<std::size_t>(std::max(0, q.offset));
       i < filtered.size() && static_cast<int>(out.size()) < q.limit; ++i) {
    out.push_back(std::move(filtered[i].unit));
  }
  return out;
}

Result<Json> Catalog::preview(std::string_view unit_id) {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();
  auto body = c.query_text("SELECT body FROM loom_cat_units WHERE id = ?", std::string(unit_id));
  if (!body) return body.error();
  if (!*body) return Error(Errc::NotFound, "no such unit: " + std::string(unit_id));
  LOOM_TRY_ASSIGN(Json uj, json::parse(**body));
  LOOM_TRY_ASSIGN(CatalogUnit cu, CatalogUnit::from_json(uj));


  Json score = Json(nullptr);
  {
    LOOM_TRY_ASSIGN(std::string run_id, latest_run(c));
    if (!run_id.empty()) {
      LOOM_TRY_ASSIGN(sql::Stmt st,
                      c.prepare("SELECT score, label, features, reasons, trap FROM loom_cat_scores WHERE run_id = ? "
                                "AND unit_id = ?"));
      st.bind(1, run_id);
      st.bind(2, std::string(unit_id));
      LOOM_TRY_ASSIGN(bool has, st.step());
      if (has) {
        score = Json{{"run_id", run_id},
                    {"score", st.get_double(0)},
                    {"label", st.get_text(1)},
                    {"features", json::parse_or(st.get_text(2), Json::object())},
                    {"reasons", json::parse_or(st.get_text(3), Json::array())},
                    {"trap", st.get_text(4)}};
      }
    }
  }

  Json verified = Json::array();
  auto raw = read_unit(unit_id);
  bool verified_ok = raw.has_value();
  if (raw) {
    for (const auto& m : cu.mentions) {
      if (static_cast<int>(verified.size()) >= 8) break;
      std::string snippet = json::get_string(m, "snippet");
      if (!snippet.empty()) verified.push_back(Json{{"kind", json::get_string(m, "kind")}, {"key", json::get_string(m, "key")}, {"snippet", snippet}});
    }
  }

  lk.unlock();
  LOOM_TRY_ASSIGN(Json resource, read_resource(unit_id));
  return Json{{"unit", cu.to_json()},
              {"resource", resource},
              {"score", score},
              {"verified", verified_ok},
              {"verified_snippets", verified},
              {"neighbours", Json::array()},
              {"cost_estimate", Json{{"bytes", cu.unit.bytes}, {"messages", cu.n_msgs}}}};
}

Result<Json> Catalog::read_resource(std::string_view unit_id, const Json& read_options) {
  LOOM_TRY_ASSIGN(auto read_profile, RuntimeProfile::load("resource_read", rt_.paths().root, read_options));
  const bool persist = read_profile.values().at("projection_storage") == "snapshot";
  LOOM_TRY(ensure_schema(rt_.db()));
  Json unit;
  {
    auto lk = rt_.db().lock();
    LOOM_TRY_ASSIGN(auto body, rt_.db().conn().query_text(
        "SELECT body FROM loom_cat_units WHERE id = ?", std::string(unit_id)));
    if (!body) return Error(Errc::NotFound, "no such unit: " + std::string(unit_id));
    LOOM_TRY_ASSIGN(unit, json::parse(*body));
  }
  LOOM_TRY_ASSIGN(CatalogUnit cu, CatalogUnit::from_json(unit));
  const std::string root_id = "resource:" + std::string(unit_id);
  LOOM_TRY_ASSIGN(auto previous, rt_.db().get_node(root_id));
  auto unavailable = [&](const Error& error) -> Result<Json> {
    Json result{{"status", error.code == Errc::Conflict ? "source_changed" : "unavailable"},
                {"current", false}, {"error", {{"code", errc_name(error.code)}, {"message", error.message}}},
                {"last_successful", nullptr}};
    if (previous) {
      Json retained = previous->metadata;
      retained.erase("availability"); // observation is separate from the last successful projection
      result["last_successful"] = retained;
      NodePatch patch;
      retained["availability"] = Json{{"status", result["status"]}, {"current", false}, {"error", result["error"]}};
      patch.metadata = std::move(retained);
      if (persist) LOOM_TRY(rt_.db().update_node(root_id, patch));
    }
    return result;
  };
  auto raw = read_unit(unit_id);
  if (!raw) return unavailable(raw.error());
  auto parsed = json::parse(*raw);
  if (!parsed) return unavailable(parsed.error());

  // Domain interpretation is shared with lossless export import. No writes,
  // attachment fetches, HTTP requests or inferred adapter activation here.
  LOOM_TRY_ASSIGN(auto index, source_index(cu));
  Json model;
  bool recognized = false;
  std::optional<RuntimeProfile> profile;
  std::optional<Error> mapping_error;
  if (parsed->is_object() && parsed->contains("mapping") && (*parsed)["mapping"].is_object()) {
    LOOM_TRY_ASSIGN(model, map_openai_export_conversation(*parsed, cu.unit.locator.member, index.value_or(0)));
    recognized = true;
  } else if (parsed->is_object() && parsed->contains("chat_messages") && (*parsed)["chat_messages"].is_array()) {
    LOOM_TRY_ASSIGN(model, map_anthropic_export_conversation(*parsed, cu.unit.locator.member, index.value_or(0)));
    recognized = true;
  }
  if (recognized) model["export"]["mapping_index_scope"] = index ? "source_array" : "unit_relative";
  const bool profile_declared = json::get_string(*parsed, "schema") == "loom.runtime_profile_overlay/1";
  if (profile_declared) {
    auto base = RuntimeProfile::builtin(json::get_string(*parsed, "domain"));
    if (!base) mapping_error = base.error();
    else {
      auto effective = base->with_overlay(*parsed);
      if (!effective) mapping_error = effective.error();
      else profile = std::move(*effective);
    }
  }
  LOOM_TRY_ASSIGN(auto projection, RuntimeProfile::load("resource_projection", rt_.paths().root));
  const auto& kinds = projection.values().at("kinds");
  const auto& predicates = projection.values().at("predicates");
  auto graph_lock = rt_.db().lock();
  std::unique_ptr<sql::Txn> transaction;
  if (persist) {
    transaction = std::make_unique<sql::Txn>(rt_.db().conn());
    LOOM_TRY(transaction->begin_status());
  }
  Json nodes = Json::array(), edges = Json::array();
  auto add_node = [&](const std::string& id, std::string_view kind, const std::string& label,
                      const std::string& content, Json metadata) -> Status {
    NodeOptions node;
    node.node_id = id;
    node.content = content;
    node.metadata = metadata;
    if (persist) LOOM_TRY(rt_.db().create_node(label, kind, node));
    nodes.push_back(Json{{"id", id}, {"kind", kind}, {"label", label},
                         {"content", content}, {"metadata", std::move(metadata)}});
    return {};
  };
  auto add_edge = [&](const std::string& src, const std::string& dst, std::string_view kind) -> Status {
    if (persist) LOOM_TRY(rt_.db().create_link(src, dst, kind));
    edges.push_back(Json{{"src", src}, {"dst", dst}, {"link_type", kind}});
    return {};
  };
  // Stable content-versioned IDs keep the same logical graph for copy and
  // reference. Provider parent/status/raw fields retain alternative branches.
  const std::string conversation_id = "resource-conversation:" + std::string(kExportParserVersion) + ":" + projection.hash() + ":" + cu.content_hash;
  if (recognized) {
    LOOM_TRY(add_node(conversation_id, kinds.at("conversation").get<std::string>(), model.at("title").get<std::string>(), "", model.at("export")));
    for (std::size_t i = 0; i < model.at("messages").size(); ++i) {
      const auto& message = model.at("messages")[i];
      const std::string id = conversation_id + ":" + std::to_string(i);
      Json metadata{{"export", message.at("export")}, {"role", message.at("role")}, {"status", message.at("status")},
                    {"source_key", message.at("key")}, {"version_group", message.at("group")}, {"version_num", message.at("version_num")}};
      LOOM_TRY(add_node(id, kinds.at("message").get<std::string>(), message.at("role").get<std::string>(), message.at("text").get<std::string>(), std::move(metadata)));
      LOOM_TRY(add_edge(conversation_id, id, predicates.at("contains").get<std::string>()));
      if (message.at("parent_index").get<int>() >= 0)
        LOOM_TRY(add_edge(conversation_id + ":" + std::to_string(message.at("parent_index").get<int>()), id, predicates.at("parent").get<std::string>()));
    }
  }
  std::string profile_id;
  if (profile_declared) {
    profile_id = profile ? "resource-profile:" + profile->hash() + ":" + projection.hash() + ":" + cu.content_hash
                         : "resource-json:" + projection.hash() + ":" + cu.content_hash;
    if (profile) LOOM_TRY(add_node(profile_id, kinds.at("profile").get<std::string>(), profile->domain(), "", profile->inspection()));
    else LOOM_TRY(add_node(profile_id, kinds.at("json").get<std::string>(), std::string(unit_id), "",
                           Json{{"declared_schema", "loom.runtime_profile_overlay/1"}, {"mapping_status", "uncertain"}}));
    // Validated fields are real graph nodes, not a renderer-only JSON blob.
    // Domain validation precedes projection; unknown executable fields never
    // get silently dropped or activated. No changes to RuntimeProfile rules.
    std::function<Status(const Json&, const std::string&, const std::string&)> fields;
    fields = [&](const Json& value, const std::string& pointer, const std::string& parent) -> Status {
      const std::string id = profile_id + ":field:" + pointer;
      Json metadata{{"pointer", pointer}, {"value_type", value.type_name()}};
      if (profile) metadata["profile_hash"] = profile->hash();
      else metadata["value_ref"] = Json{{"unit_id", unit_id}, {"source", cu.unit.source},
                                         {"content_hash", cu.content_hash}, {"pointer", pointer}};
      LOOM_TRY(add_node(id, profile ? kinds.at("profile_field").get<std::string>() : kinds.at("json_field").get<std::string>(), pointer,
                        profile && value.is_primitive() ? value.dump() : "", std::move(metadata)));
      LOOM_TRY(add_edge(parent, id, predicates.at("contains").get<std::string>()));
      if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
          std::string key;
          for (char ch : it.key()) key += ch == '~' ? "~0" : ch == '/' ? "~1" : std::string(1, ch);
          LOOM_TRY(fields(it.value(), pointer + "/" + key, id));
        }
      } else if (value.is_array()) {
        for (std::size_t i = 0; i < value.size(); ++i) LOOM_TRY(fields(value[i], pointer + "/" + std::to_string(i), id));
      }
      return {};
    };
    LOOM_TRY(fields(profile ? profile->values() : *parsed, "", profile_id));
  }
  Json snapshot{{"source", cu.unit.source}, {"selector", cu.unit.locator.to_json()},
                {"content_hash", cu.content_hash}, {"projection_profile", projection.inspection()},
                {"read_configuration", read_profile.inspection()},
                {"read_scope", {{"unit_bytes", raw->size()}, {"projection", "complete_unit"},
                                {"container_io", "not_instrumented"}}},
                {"source_index", index ? Json(*index) : Json(nullptr)},
                {"mapping_index_scope", index ? "source_array" : "unit_relative"},
                {"mapping_version", profile_declared ? "loom.runtime_profile_overlay/1" : std::string(kExportParserVersion)},
                {"mapping_status", recognized || profile ? "recognized" : "uncertain"},
                {"coverage", recognized ? "provider_conversation" : profile ? "runtime_profile" : profile_declared ? "syntax_only" : "not_implemented"},
                {"nodes", nodes}, {"edges", edges},
                {"permission", "readonly"}, {"activation", "not_requested"}};
  // Unknown domains may contain credentials. Keep their verified source
  // reference, not unclassified values in an exportable graph snapshot.
  if (recognized) snapshot["raw"] = *parsed;
  if (mapping_error) snapshot["mapping_error"] = Json{{"code", errc_name(mapping_error->code)}, {"message", mapping_error->message}};
  NodeOptions root;
  root.node_id = root_id;
  root.metadata = snapshot;
  root.metadata["availability"] = Json{{"status", "available"}, {"current", true}};
  if (persist) LOOM_TRY(rt_.db().create_node(std::string(unit_id), kinds.at("resource").get<std::string>(), root));
  NodePatch patch;
  patch.metadata = root.metadata;
  if (persist) LOOM_TRY(rt_.db().update_node(root_id, patch));
  if (recognized) LOOM_TRY(add_edge(root_id, conversation_id, predicates.at("projects").get<std::string>()));
  if (profile_declared) LOOM_TRY(add_edge(root_id, profile_id, predicates.at("projects").get<std::string>()));
  if (transaction) LOOM_TRY(transaction->commit());
  return Json{{"status", "available"}, {"current", true}, {"last_successful", snapshot}};
}

Result<std::string> Catalog::read_unit(std::string_view unit_id) {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();
  auto body = c.query_text("SELECT body FROM loom_cat_units WHERE id = ?", std::string(unit_id));
  if (!body) return body.error();
  if (!*body) return Error(Errc::NotFound, "no such unit: " + std::string(unit_id));
  LOOM_TRY_ASSIGN(Json uj, json::parse(**body));
  LOOM_TRY_ASSIGN(CatalogUnit cu, CatalogUnit::from_json(uj));

  LOOM_TRY_ASSIGN(auto raw_blob, c.query_text("SELECT raw_blob FROM loom_cat_imports WHERE unit_id = ?", std::string(unit_id)));
  if (raw_blob && !raw_blob->empty()) {
    lk.unlock();
    LOOM_TRY_ASSIGN(auto retained, rt_.blobs().read(*raw_blob));
    if (Sha256::hex(retained) != cu.content_hash || *raw_blob != cu.content_hash)
      return Error(Errc::Conflict, "retained unit hash mismatch: " + std::string(unit_id));
    return retained;
  }


  auto path_r = c.query_text("SELECT path FROM loom_cat_sources WHERE id = ?", cu.unit.source);
  if (!path_r) return path_r.error();
  if (!*path_r) return Error(Errc::NotFound, "source not registered: " + cu.unit.source);
  std::filesystem::path path(**path_r);
  lk.unlock();
  const std::string source_hash = cu.unit.source.starts_with("sha256:") ? cu.unit.source.substr(7) : std::string();
  if (rt_.blobs().has(source_hash)) {
    LOOM_TRY(rt_.blobs().verify(source_hash));
    path = rt_.blobs().path_for(source_hash);
  }

  std::string bytes;
  if (cu.unit.locator.member.empty()) {
    LOOM_TRY_ASSIGN(bytes, fsutil::read_file(path));
    if (cu.unit.locator.byte_start && cu.unit.locator.byte_len) {
      std::size_t start = static_cast<std::size_t>(std::min<std::int64_t>(*cu.unit.locator.byte_start, static_cast<std::int64_t>(bytes.size())));
      std::size_t len = static_cast<std::size_t>(std::min<std::int64_t>(*cu.unit.locator.byte_len, static_cast<std::int64_t>(bytes.size() - start)));
      bytes = bytes.substr(start, len);
    }
  } else {
    LOOM_TRY_ASSIGN(auto entries, list_zip_entries(path));
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const ZipEntry& e) { return e.name == cu.unit.locator.member; });
    if (it == entries.end()) return Error(Errc::NotFound, "zip member not found: " + cu.unit.locator.member);
    std::string whole;
    LOOM_TRY(stream_zip_member(path, it->index, [&](std::string_view chunk) { whole.append(chunk); }));
    if (cu.unit.locator.byte_start && cu.unit.locator.byte_len) {
      std::size_t start = static_cast<std::size_t>(std::min<std::int64_t>(*cu.unit.locator.byte_start, static_cast<std::int64_t>(whole.size())));
      std::size_t len = static_cast<std::size_t>(std::min<std::int64_t>(*cu.unit.locator.byte_len, static_cast<std::int64_t>(whole.size() - start)));
      bytes = whole.substr(start, len);
    } else {
      bytes = std::move(whole);
    }
  }
  if (!cu.unit.locator.json_pointer.empty() && !cu.unit.locator.byte_start && !cu.unit.locator.byte_len) {
    LOOM_TRY_ASSIGN(auto document, json::parse(bytes));
    const auto& pointer = cu.unit.locator.json_pointer;
    std::vector<std::string> candidates;
    try {
      candidates.push_back(json::dump(document.at(Json::json_pointer(pointer))));
    } catch (const Json::exception&) {
      // Scanner v1 wrote /N for object-wrapped exports (and /0 for a
      // standalone object). Recover those existing rows only when the
      // stored hash verifies the candidate; never silently guess a value.
    }
    if (document.is_object() && pointer.size() > 1 && pointer[0] == '/' &&
        std::all_of(pointer.begin() + 1, pointer.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; })) {
      if (pointer == "/0") candidates.push_back(json::dump(document));
      for (const auto* wrapper : {"conversations", "projects", "memories"}) {
        const auto* inner = json::find(document, wrapper);
        if (!inner || !inner->is_array()) continue;
        try { candidates.push_back(json::dump(inner->at(Json::json_pointer(pointer)))); }
        catch (const Json::exception&) {}
      }
    }
    auto match = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
      return Sha256::hex(candidate) == cu.content_hash;
    });
    if (match == candidates.end()) return Error(Errc::Conflict, "JSON locator/hash mismatch for " + std::string(unit_id));
    bytes = std::move(*match);
  }
  if (Sha256::hex(bytes) != cu.content_hash) {
    return Error(Errc::Conflict, "content hash mismatch for " + std::string(unit_id) + ": source bytes changed");
  }
  return bytes;
}

Result<Json> Catalog::status() {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();
  auto count = [&](const char* sql) -> std::int64_t {
    auto v = c.query_int(sql);
    return v && v->has_value() ? **v : 0;
  };
  std::int64_t selected = count("SELECT COUNT(*) FROM loom_cat_decisions WHERE selected = 1");
  std::int64_t imported = count("SELECT COUNT(*) FROM loom_cat_imports");
  return Json{{"units", count("SELECT COUNT(*) FROM loom_cat_units")},
              {"sources", count("SELECT COUNT(*) FROM loom_cat_sources")},
              {"profiles", count("SELECT COUNT(*) FROM loom_cat_profiles")},
              {"score_runs", count("SELECT COUNT(DISTINCT run_id) FROM loom_cat_scores")},
              {"selected", selected},
              {"imported", imported}};
}

Status Catalog::add_profile_terms(const Json& terms) {
  if (!terms.is_array()) return Error(Errc::InvalidArgument, "add_profile_terms: expected a JSON array");
  LOOM_TRY_ASSIGN(auto recipe, load_relevance_recipe(pack_->policy("relevance")));
  const auto expansion = recipe.class_weights.find("expansion");
  if (expansion == recipe.class_weights.end())
    return Error(Errc::InvalidArgument, "catalog relevance recipe: /term_class_weights/expansion is required");
  LOOM_TRY(ensure_schema(rt_.db()));
  kb::Normalizer norm(*pack_);

  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();
  auto latest = c.query_text("SELECT body FROM loom_cat_profiles ORDER BY created DESC LIMIT 1");
  if (!latest) return latest.error();
  SelfProfile profile;
  if (*latest) {
    LOOM_TRY_ASSIGN(Json j, json::parse(**latest));
    LOOM_TRY_ASSIGN(profile, SelfProfile::from_json(j));
  }
  for (const auto& t : terms) {
    std::string surface = t.is_string() ? t.get<std::string>() : json::get_string(t, "term");
    if (surface.empty()) continue;
    profile.terms.push_back(Json{{"term", surface},
                                 {"key", norm.fold(surface)},
                                 {"class", "expansion"},
                                 {"weight", expansion->second},
                                 {"project", json::get_string(t, "project")},
                                 {"provenance", "expansion"},
                                 {"ambiguous", false}});
  }
  Json fp{{"pack_hash", pack_->hash()}, {"terms", profile.terms}, {"projects", profile.projects}};
  profile.input_hash = Sha256::hex(json::canonical(fp));
  profile.id = "cp_" + profile.input_hash.substr(0, 16);
  return c.run("INSERT OR REPLACE INTO loom_cat_profiles (id, input_hash, body, created) VALUES (?, ?, ?, ?)",
              profile.id, profile.input_hash, json::dump(profile.to_json()), timeutil::utc_now_iso());
}

}  // namespace loom::catalog
