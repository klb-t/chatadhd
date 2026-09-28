// catalog.h: Catalog::import_selected — targeted import of the units
// `select()` marked selected, by locator (never a full re-scan): re-reads
// exactly those units' bytes (read_unit(), verified against content_hash),
// parses conversations with the same walkers the archive area uses, and
// writes conversations/messages plus one loom_provenance row per message
// (transform "catalog.import@1"). Idempotent by unit id (loom_cat_imports).
#include "loom/catalog.h"

#include <chrono>
#include <set>

#include "archive/archive_internal.h"
#include "catalog_internal.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/time.h"

namespace loom::catalog {

using namespace loom::catalog::internal;

namespace {
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

  std::int64_t imported = 0, skipped = 0, bytes = 0;
  Json conversations = Json::array();
  std::int64_t idx_i = 0;
  for (const auto& uid : unit_ids) {
    ++idx_i;
    if (cancel && cancel->cancelled()) break;
    if (progress) progress("import", idx_i, static_cast<std::int64_t>(unit_ids.size()), uid);

    {
      auto lk2 = rt_.db().lock();
      auto already = rt_.db().conn().query_text("SELECT conv_id FROM loom_cat_imports WHERE unit_id = ?", uid);
      if (already && *already && !(**already).empty()) {
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
    if (!body_r) {
      ++skipped;
      continue;
    }
    auto uj = json::parse(*body_r);
    if (!uj) {
      ++skipped;
      continue;
    }
    auto cu_r = CatalogUnit::from_json(*uj);
    if (!cu_r) {
      ++skipped;
      continue;
    }
    CatalogUnit cu = std::move(*cu_r);

    if (opts.dry_run) {
      ++imported;
      bytes += cu.unit.bytes;
      continue;
    }

    Json src_meta{{"unit_id", uid}, {"platform", cu.platform}, {"ext_id", cu.ext_id}};
    SourceRecord src;
    src.kind = "catalog_unit";
    src.uri = cu.unit.source + (cu.unit.locator.member.empty() ? "" : ("!" + cu.unit.locator.member));
    src.format = cu.platform;
    src.title = cu.unit.title;
    src.parser = "loom.catalog.import";
    src.parser_version = std::string(kScannerVersion);
    src.metadata = src_meta;
    auto source_id = rt_.provenance().add_source(src);
    if (!source_id) {
      ++skipped;
      continue;
    }

    std::string conv_id;
    std::string title = cu.unit.title.empty() ? "[Import] " + cu.ext_id : cu.unit.title;
    if (opts.import_messages && opts.store_mode == "link") {
      // "link": one placeholder message carrying the locator + content_hash
      // in its metadata, no raw bytes re-read, nothing duplicated into the
      // database. The source file stays the copy of record.
      auto conv = rt_.db().create_conv(title);
      if (!conv) {
        ++skipped;
        continue;
      }
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
        pr.source_id = *source_id;
        pr.locator = cu.unit.locator.to_json();
        pr.transform = "catalog.import.link@1";
        (void)rt_.provenance().add(pr);
      }
      ProvenanceRecord conv_pr;
      conv_pr.subject_id = conv_id;
      conv_pr.subject_kind = "conversation";
      conv_pr.source_id = *source_id;
      conv_pr.locator = cu.unit.locator.to_json();
      conv_pr.transform = "catalog.import.link@1";
      (void)rt_.provenance().add(conv_pr);
      conversations.push_back(conv_id);
    } else if (opts.import_messages && (cu.unit.kind == "conversation" || cu.unit.kind == "project" || cu.unit.kind == "memory")) {
      auto raw = read_unit(uid);
      if (!raw) {
        ++skipped;
        continue;
      }
      auto parsed = json::parse(*raw);
      Json messages_j = Json::array();
      if (parsed && cu.unit.kind == "conversation") {
        std::string kind = cu.platform == "chatgpt" ? "chatgpt" : "claude";
        archive::ChatWalk w = kind == "chatgpt" ? archive::walk_chatgpt(*parsed) : archive::walk_claude(*parsed);
        messages_j = w.messages;
      } else if (parsed) {
        ExtractedText et = extract_text(*parsed, cu.unit.kind == "project" ? "claude_projects" : "claude_memories");
        if (!et.prose.empty()) messages_j.push_back(Json{{"role", "document"}, {"text", et.prose}});
      }
      if (!messages_j.empty()) {
        auto conv = rt_.db().create_conv(title);
        if (!conv) {
          ++skipped;
          continue;
        }
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
            pr.source_id = *source_id;
            pr.locator = Json{{"unit_id", uid}, {"message_index", mi}};
            pr.transform = "catalog.import@1";
            (void)rt_.provenance().add(pr);
          }
          ++mi;
        }
        ProvenanceRecord conv_pr;
        conv_pr.subject_id = conv_id;
        conv_pr.subject_kind = "conversation";
        conv_pr.source_id = *source_id;
        conv_pr.locator = cu.unit.locator.to_json();
        conv_pr.transform = "catalog.import@1";
        (void)rt_.provenance().add(conv_pr);
        conversations.push_back(conv_id);
      }
    }

    {
      auto lk4 = rt_.db().lock();
      LOOM_TRY(rt_.db().conn().run(
          "INSERT OR REPLACE INTO loom_cat_imports (unit_id, conv_id, raw_blob, source_id, task_id, created) "
          "VALUES (?,?,?,?,?,?)",
          uid, conv_id, std::string(), *source_id, std::string(), timeutil::utc_now_iso()));
    }
    ++imported;
    bytes += cu.unit.bytes;
  }

  return Json{{"imported", imported}, {"skipped", skipped}, {"bytes", bytes}, {"conversations", conversations},
              {"units", unit_ids}};
}

}  // namespace loom::catalog
