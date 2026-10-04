// catalog.h: Catalog::query / preview / read_unit / status / add_profile_terms.
#include "loom/catalog.h"

#include <algorithm>
#include <cctype>

#include "catalog_internal.h"
#include "relevance_recipe.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
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

  return Json{{"unit", cu.to_json()},
              {"score", score},
              {"verified", verified_ok},
              {"verified_snippets", verified},
              {"neighbours", Json::array()},
              {"cost_estimate", Json{{"bytes", cu.unit.bytes}, {"messages", cu.n_msgs}}}};
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
