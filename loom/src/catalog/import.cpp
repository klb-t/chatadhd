// catalog.h: Catalog::import_selected — targeted import of the units
// `select()` marked selected, by locator (never a full re-scan): re-reads
// exactly those units' bytes (read_unit(), verified against content_hash),
// parses conversations with the lossless provider-export parsers, and
// writes conversations/messages plus one loom_provenance row per message
// (transform "catalog.import@1"). Idempotent by unit id (loom_cat_imports).
#include "loom/catalog.h"

#include <chrono>
#include <set>

#include "catalog_internal.h"
#include "source_index.h"
#include "import/export_internal.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/time.h"

namespace loom::catalog {

using namespace loom::catalog::internal;

namespace {
Result<std::string> copy_conversation(Runtime& runtime, const CatalogUnit& unit, const Json& document,
                                      const std::string& source_id, const CancelToken* cancel) {
  xport::ConvModel model;
  xport::Counts counts;
  loom::ImportOptions options;
  options.cancel = cancel;
  xport::Env env{runtime.db(), nullptr, options, {}, ""};
  LOOM_TRY_ASSIGN(auto observed_index, internal::source_index(unit));
  const int mapping_index = observed_index.value_or(0);
  if (unit.platform == "chatgpt" && document.is_object() && document.contains("mapping") &&
      document["mapping"].is_object()) {
    xport::OpenAiCtx context;
    context.env = &env;
    xport::parse_openai_conversation(document, mapping_index, unit.unit.locator.member, context, model, counts);
  } else if (unit.platform == "claude" && document.is_object() && document.contains("chat_messages") &&
             document["chat_messages"].is_array()) {
    xport::parse_anthropic_conversation(document, mapping_index, unit.unit.locator.member, env, model, counts);
  } else {
    return Error(Errc::NotImplemented, "catalog conversation has no supported lossless provider mapping");
  }
  // Older catalog rows have no observed ordinal. Their local parser index
  // must not masquerade as an original source array index.
  model.export_meta["mapping_index_scope"] = observed_index ? "source_array" : "unit_relative";
  if (observed_index) model.export_meta["source_index"] = mapping_index;
  if (!unit.unit.locator.json_pointer.empty())
    model.export_meta["json_pointer"] = unit.unit.locator.json_pointer;
  model.export_meta["catalog_unit"] = unit.unit.id;
  model.export_meta["selector"] = unit.unit.locator.to_json();
  model.export_meta["mapping_version"] = std::string(kExportParserVersion);
  if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "catalog import cancelled");
  LOOM_TRY_ASSIGN(auto conversation, xport::write_conversation(env, model));
  const std::string transform = "catalog.import.export@" + std::string(kExportParserVersion);
  std::vector<ProvenanceRecord> records;
  ProvenanceRecord conversation_record;
  conversation_record.subject_id = conversation.id;
  conversation_record.subject_kind = "conversation";
  conversation_record.source_id = source_id;
  conversation_record.locator = unit.unit.locator.to_json();
  conversation_record.locator["unit_id"] = unit.unit.id;
  if (observed_index) conversation_record.locator["source_conversation_index"] = mapping_index;
  conversation_record.transform = transform;
  records.push_back(conversation_record);
  LOOM_TRY_ASSIGN(auto messages, runtime.db().get_msgs(conversation.id, true));
  for (const auto& message : messages) {
    ProvenanceRecord record = conversation_record;
    record.subject_id = message.id;
    record.subject_kind = "message";
    const auto& metadata = message.metadata.at("export");
    record.locator["source_key"] = metadata.at("key");
    if (const auto* index = json::find(metadata, "source_index")) record.locator["message_index"] = *index;
    records.push_back(std::move(record));
  }
  LOOM_TRY(runtime.provenance().add_many(std::move(records)));
  return conversation.id;
}

Result<std::string> latest_decisions_run(sql::Connection& c) {
  auto r = c.query_text("SELECT run_id FROM loom_cat_decisions ORDER BY rowid DESC LIMIT 1");
  if (!r) return r.error();
  return r->value_or(std::string());
}

// mode == "full": every catalogued unit, in id order (deterministic,
// independent of selection -- R1's lossless import path).
Result<std::vector<std::string>> all_unit_ids(sql::Connection& c) {
  std::vector<std::string> out;
  LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare("SELECT id FROM loom_cat_units ORDER BY id"));
  while (true) {
    LOOM_TRY_ASSIGN(bool has, st.step());
    if (!has) break;
    out.push_back(st.get_text(0));
  }
  return out;
}

// mode == "selective": select()'s decisions for `run_id`, in unit id order.
Result<std::vector<std::string>> selected_unit_ids(sql::Connection& c, std::string_view run_id) {
  std::vector<std::string> out;
  LOOM_TRY_ASSIGN(sql::Stmt st,
                  c.prepare("SELECT unit_id FROM loom_cat_decisions WHERE run_id = ? AND selected = 1 ORDER BY unit_id"));
  st.bind(1, std::string(run_id));
  while (true) {
    LOOM_TRY_ASSIGN(bool has, st.step());
    if (!has) break;
    out.push_back(st.get_text(0));
  }
  return out;
}

// Every other unit sharing a base unit's non-empty project_ext_id ("everything
// inside a matching Claude project"): project_ext_id is not populated by
// scan() yet (disclosed gap), so this is a no-op today and activates for
// free once it is (the query itself needs no change).
Status add_project_siblings(sql::Connection& c, std::set<std::string>& ids) {
  std::vector<std::string> base(ids.begin(), ids.end());
  for (const auto& uid : base) {
    auto body = c.query_text("SELECT body FROM loom_cat_units WHERE id = ?", uid);
    if (!body || !*body) continue;
    auto j = json::parse(**body);
    if (!j) continue;
    std::string project_ext_id = json::get_string(*j, "project_ext_id");
    if (project_ext_id.empty()) continue;
    // Sibling lookup by project_ext_id needs a body scan (not indexed);
    // acceptable for the bounded "expand a small selection" use case this
    // serves, not a corpus-wide query.
    LOOM_TRY_ASSIGN(sql::Stmt all, c.prepare("SELECT id, body FROM loom_cat_units"));
    while (true) {
      LOOM_TRY_ASSIGN(bool has, all.step());
      if (!has) break;
      std::string other_id = all.get_text(0);
      if (ids.count(other_id)) continue;
      auto oj = json::parse(all.get_text(1));
      if (!oj) continue;
      if (json::get_string(*oj, "project_ext_id") == project_ext_id) ids.insert(other_id);
    }
  }
  return {};
}

// Same-platform units whose date is within `hours` of a base unit's date
// ("related data": a lightweight same-session heuristic).
Status add_time_window(sql::Connection& c, std::set<std::string>& ids, int hours) {
  if (hours <= 0) return {};
  std::vector<std::pair<std::string, std::string>> base_platform_date;  // (platform, date)
  {
    std::vector<std::string> base(ids.begin(), ids.end());
    for (const auto& uid : base) {
      LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare("SELECT platform, date FROM loom_cat_units WHERE id = ?"));
      st.bind(1, uid);
      LOOM_TRY_ASSIGN(bool has, st.step());
      if (has && !st.get_text(1).empty()) base_platform_date.emplace_back(st.get_text(0), st.get_text(1));
    }
  }
  if (base_platform_date.empty()) return {};
  LOOM_TRY_ASSIGN(sql::Stmt all, c.prepare("SELECT id, platform, date FROM loom_cat_units WHERE date != ''"));
  while (true) {
    LOOM_TRY_ASSIGN(bool has, all.step());
    if (!has) break;
    std::string id = all.get_text(0), platform = all.get_text(1), date = all.get_text(2);
    if (ids.count(id)) continue;
    auto tp = timeutil::parse_iso_utc(date);
    if (!tp) continue;
    for (auto& [bp, bd] : base_platform_date) {
      if (bp != platform) continue;
      auto btp = timeutil::parse_iso_utc(bd);
      if (!btp) continue;
      double diff_hours = std::abs(std::chrono::duration<double>(*tp - *btp).count()) / 3600.0;
      if (diff_hours <= hours) {
        ids.insert(id);
        break;
      }
    }
  }
  return {};
}
}  // namespace

Result<Json> Catalog::import_selected(const ImportOptions& opts, const ProgressFn& progress,
                                      const CancelToken* cancel) {
  LOOM_TRY(ensure_schema(rt_.db()));
  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();

  std::string run_id(opts.run_id);
  std::set<std::string> id_set;
  if (opts.mode == "full") {
    LOOM_TRY_ASSIGN(auto all, all_unit_ids(c));
    id_set.insert(all.begin(), all.end());
  } else {
    if (run_id.empty()) {
      LOOM_TRY_ASSIGN(run_id, latest_decisions_run(c));
      if (run_id.empty()) return Error(Errc::NotFound, "no selection run yet; call select() first");
    }
    LOOM_TRY_ASSIGN(auto sel, selected_unit_ids(c, run_id));
    id_set.insert(sel.begin(), sel.end());
    if (opts.include_project_siblings) LOOM_TRY(add_project_siblings(c, id_set));
    if (opts.related_time_window_hours > 0) LOOM_TRY(add_time_window(c, id_set, opts.related_time_window_hours));
  }
  std::vector<std::string> unit_ids(id_set.begin(), id_set.end());
  lk.unlock();

  // A full copy retains the original containers as well as their indexed
  // units. Unknown fields, binary members and unrecognised formats must not
  // depend on what today's scanner knows how to normalise.
  Json retained_sources = Json::array();
  if (opts.mode == "full" && opts.store_mode == "copy" && !opts.dry_run) {
    struct SourceFile { std::string id, path; std::int64_t size; };
    std::vector<SourceFile> sources;
    {
      auto guard = rt_.db().lock();
      LOOM_TRY_ASSIGN(auto st, c.prepare("SELECT id, path, bytes FROM loom_cat_sources ORDER BY id"));
      while (true) {
        LOOM_TRY_ASSIGN(bool row, st.step());
        if (!row) break;
        sources.push_back({st.get_text(0), st.get_text(1), st.get_int(2)});
      }
    }
    for (const auto& [sid, path, original_size] : sources) {
      if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "catalog import cancelled");
      const std::string hash = sid.starts_with("sha256:") ? sid.substr(7) : std::string();
      if (hash.size() != 64) return Error(Errc::Conflict, "invalid catalog source hash: " + sid);
      std::int64_t size = original_size;
      if (rt_.blobs().has(hash)) {
        LOOM_TRY(rt_.blobs().verify(hash));
      } else {
        LOOM_TRY_ASSIGN(auto blob, rt_.blobs().put_file(path));
        if (blob.hash != hash) return Error(Errc::Conflict, "source changed since scan: " + path);
        size = blob.size;
      }
      const std::string record_id = "catalog_raw_" + hash;
      LOOM_TRY_ASSIGN(auto existing, rt_.provenance().get_source(record_id));
      if (!existing) {
        SourceRecord source;
        source.id = record_id;
        source.kind = "catalog_source";
        source.uri = path;
        source.blob_hash = hash;
        source.size = size;
        source.parser = "loom.catalog.import";
        source.parser_version = std::string(kScannerVersion);
        source.metadata = Json{{"catalog_source", sid}, {"store_mode", "copy"}, {"scope", "full"}};
        LOOM_TRY(rt_.provenance().add_source(std::move(source)));
      }
      retained_sources.push_back(Json{{"source_id", sid}, {"blob_hash", hash}});
    }
  }

  std::int64_t imported = 0, skipped = 0, bytes = 0;
  Json conversations = Json::array();
  std::int64_t idx_i = 0;
  for (const auto& uid : unit_ids) {
    ++idx_i;
    if (cancel && cancel->cancelled()) return Error(Errc::Cancelled, "catalog import cancelled");
    if (progress) progress("import", idx_i, static_cast<std::int64_t>(unit_ids.size()), uid);

    std::string previous_conv, previous_raw, previous_source;
    bool previously_imported = false;
    {
      auto lk2 = rt_.db().lock();
      LOOM_TRY_ASSIGN(auto st, c.prepare("SELECT conv_id, raw_blob, source_id FROM loom_cat_imports WHERE unit_id = ?"));
      st.bind(1, uid);
      LOOM_TRY_ASSIGN(bool row, st.step());
      previously_imported = row;
      if (row) {
        previous_conv = st.get_text(0);
        previous_raw = st.get_text(1);
        previous_source = st.get_text(2);
      }
    }
    if (previously_imported && (opts.store_mode == "link" || !previous_raw.empty())) {
      if (opts.store_mode == "copy") LOOM_TRY(rt_.blobs().verify(previous_raw));
      if (!opts.import_messages || !previous_conv.empty()) {
        ++skipped;
        continue;
      }
    }

    auto body_r = [&]() -> Result<std::string> {
      auto lk3 = rt_.db().lock();
      auto b = rt_.db().conn().query_text("SELECT body FROM loom_cat_units WHERE id = ?", uid);
      if (!b) return b.error();
      if (!*b) return Error(Errc::NotFound, "unit vanished: " + uid);
      return **b;
    }();
    if (!body_r) return body_r.error();
    LOOM_TRY_ASSIGN(auto uj, json::parse(*body_r));
    LOOM_TRY_ASSIGN(CatalogUnit cu, CatalogUnit::from_json(uj));
    if (previously_imported && !previous_raw.empty() && cu.unit.kind != "conversation" &&
        cu.unit.kind != "project" && cu.unit.kind != "memory") {
      ++skipped;
      continue;
    }

    if (opts.dry_run) {
      ++imported;
      bytes += cu.unit.bytes;
      continue;
    }

    Json src_meta{{"unit_id", uid}, {"platform", cu.platform}, {"ext_id", cu.ext_id}};
    // Choosing link for a later message projection must not discard bytes
    // already retained by an earlier copy-only import.
    std::string raw, raw_blob = previous_raw;
    if (opts.store_mode == "copy") {
      LOOM_TRY_ASSIGN(raw, read_unit(uid));
      LOOM_TRY_ASSIGN(auto blob, rt_.blobs().put(raw));
      LOOM_TRY(rt_.blobs().verify(blob.hash));
      raw_blob = blob.hash;
    }
    // Keep copied projection, per-record provenance and resume journal atomic.
    // The lossless writer uses a nested SAVEPOINT. Immutable blobs already
    // retained above may be reused after any interrupted database transaction.
    auto unit_lock = rt_.db().lock();
    std::unique_ptr<sql::Txn> copy_transaction;
    if (opts.store_mode == "copy") {
      copy_transaction = std::make_unique<sql::Txn>(c);
      LOOM_TRY(copy_transaction->begin_status());
    }
    SourceRecord src;
    src.kind = "catalog_unit";
    src.uri = cu.unit.source + (cu.unit.locator.member.empty() ? "" : ("!" + cu.unit.locator.member));
    src.format = cu.platform;
    src.title = cu.unit.title;
    src.parser = "loom.catalog.import";
    src.parser_version = std::string(kScannerVersion);
    if (opts.store_mode == "copy" && opts.import_messages && cu.unit.kind == "conversation") {
      src.parser = "loom.catalog.import.export";
      src.parser_version = std::string(kExportParserVersion);
    }
    src.blob_hash = raw_blob;
    src.size = static_cast<std::int64_t>(raw.size());
    src.metadata = src_meta;
    std::string source_id = previous_source;
    if (source_id.empty()) {
      LOOM_TRY_ASSIGN(source_id, rt_.provenance().add_source(src));
    } else if (opts.store_mode == "copy" && !raw_blob.empty()) {
      auto guard = rt_.db().lock();
      LOOM_TRY(c.run("UPDATE loom_sources SET blob_hash = ?, size = ? WHERE id = ?", raw_blob, src.size, source_id));
    }

    std::string conv_id = previous_conv;
    std::string title = cu.unit.title.empty() ? "[Import] " + cu.ext_id : cu.unit.title;
    if (opts.import_messages && conv_id.empty() && opts.store_mode == "link") {
      // "link": one placeholder message carrying the locator + content_hash
      // in its metadata, no raw bytes re-read, nothing duplicated into the
      // database. The source file stays the copy of record.
      auto conv = rt_.db().create_conv(title);
      if (!conv) return conv.error();
      conv_id = conv->id;
      NewMessage nm;
      nm.conv_id = conv_id;
      nm.role = "document";
      nm.text = "[linked to catalog unit " + uid + ", " + std::to_string(cu.n_msgs) + " message(s)] " + cu.head;
      nm.metadata = Json{{"catalog_unit", uid},
                         {"locator", cu.unit.locator.to_json()},
                         {"content_hash", cu.content_hash},
                         {"store_mode", "link"}};
      auto mid = rt_.db().create_msg(nm);
      if (mid) {
        ProvenanceRecord pr;
        pr.subject_id = *mid;
        pr.subject_kind = "message";
        pr.source_id = source_id;
        pr.locator = cu.unit.locator.to_json();
        pr.transform = "catalog.import.link@1";
        LOOM_TRY(rt_.provenance().add(pr));
      }
      ProvenanceRecord conv_pr;
      conv_pr.subject_id = conv_id;
      conv_pr.subject_kind = "conversation";
      if (!mid) return mid.error();
      conv_pr.source_id = source_id;
      conv_pr.locator = cu.unit.locator.to_json();
      conv_pr.transform = "catalog.import.link@1";
      LOOM_TRY(rt_.provenance().add(conv_pr));
      conversations.push_back(conv_id);
    } else if (opts.import_messages && conv_id.empty() && cu.unit.kind == "conversation") {
      LOOM_TRY_ASSIGN(auto parsed, json::parse(raw));
      LOOM_TRY_ASSIGN(conv_id, copy_conversation(rt_, cu, parsed, source_id, cancel));
      conversations.push_back(conv_id);
    } else if (opts.import_messages && conv_id.empty() && (cu.unit.kind == "project" || cu.unit.kind == "memory")) {
      auto parsed = json::parse(raw);
      Json messages_j = Json::array();
      if (parsed) {
        ExtractedText et = extract_text(*parsed, cu.unit.kind == "project" ? "claude_projects" : "claude_memories");
        if (!et.prose.empty()) messages_j.push_back(Json{{"role", "document"}, {"text", et.prose}});
      }
      if (!messages_j.empty()) {
        auto conv = rt_.db().create_conv(title);
        if (!conv) return conv.error();
        conv_id = conv->id;
        int mi = 0;
        for (const auto& m : messages_j) {
          std::string text = json::get_string(m, "text");
          if (text.empty()) {
            ++mi;
            continue;
          }
          NewMessage nm;
          nm.conv_id = conv_id;
          nm.text = text;
          nm.role = json::get_string(m, "role", "user");
          nm.metadata = Json{{"catalog_unit", uid}, {"message_index", mi}};
          auto mid = rt_.db().create_msg(nm);
          if (mid) {
            ProvenanceRecord pr;
            pr.subject_id = *mid;
            pr.subject_kind = "message";
            pr.source_id = source_id;
            pr.locator = Json{{"unit_id", uid}, {"message_index", mi}};
            pr.transform = "catalog.import@1";
            LOOM_TRY(rt_.provenance().add(pr));
          }
          if (!mid) return mid.error();
          ++mi;
        }
        ProvenanceRecord conv_pr;
        conv_pr.subject_id = conv_id;
        conv_pr.subject_kind = "conversation";
        conv_pr.source_id = source_id;
        conv_pr.locator = cu.unit.locator.to_json();
        conv_pr.transform = "catalog.import@1";
        LOOM_TRY(rt_.provenance().add(conv_pr));
        conversations.push_back(conv_id);
      }
    }

    {
      auto lk4 = rt_.db().lock();
      LOOM_TRY(rt_.db().conn().run(
          "INSERT OR REPLACE INTO loom_cat_imports (unit_id, conv_id, raw_blob, source_id, task_id, created) "
          "VALUES (?,?,?,?,?,?)",
          uid, conv_id, raw_blob, source_id, std::string(), timeutil::utc_now_iso()));
    }
    if (copy_transaction) LOOM_TRY(copy_transaction->commit());
    ++imported;
    bytes += cu.unit.bytes;
  }

  return Json{{"imported", imported}, {"skipped", skipped}, {"bytes", bytes}, {"conversations", conversations},
              {"units", unit_ids}, {"retained_sources", retained_sources}};
}

}  // namespace loom::catalog
