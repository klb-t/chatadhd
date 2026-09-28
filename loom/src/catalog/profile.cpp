// catalog.h: Catalog::build_profile — turns profiles/self.json (project
// aliases with context gates) and the pack's principle phrasings (R2: the
// coding-philosophy probe fires even in chats that never name a project)
// into the flat, typed SelfProfile.terms list that AliasIndex and score()
// read. A light best-effort repo scan adds "path" class terms when
// ProfileConfig.repo is given (declared symbols/full parsing is a gap, noted
// in the final report; paths alone already help precision on pasted code).
#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>

#include "catalog_internal.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog::internal {

namespace fs = std::filesystem;

namespace {

void add_term(Json& terms, std::string_view surface, const kb::Normalizer& norm, std::string_view cls,
              double weight, std::string_view project, std::string_view provenance, bool ambiguous,
              const Json& requires_context, const Json& negative_context, bool prefix = false) {
  if (surface.empty()) return;
  Json t{{"term", std::string(surface)},
        {"key", norm.fold(surface)},
        {"class", std::string(cls)},
        {"weight", weight},
        {"project", std::string(project)},
        {"provenance", std::string(provenance)},
        {"ambiguous", ambiguous},
        {"prefix", prefix || cls == "principle"}};
  if (requires_context.is_object()) {
    Json rc{{"min", json::get_int(requires_context, "min", 1)}};
    Json any = Json::array();
    if (const Json* a = json::find(requires_context, "any"); a && a->is_array()) {
      for (const auto& c : *a) any.push_back(norm.fold(c.get<std::string>()));
    }
    rc["any"] = any;
    t["requires_context"] = rc;
  }
  if (negative_context.is_array()) {
    Json neg = Json::array();
    for (const auto& c : negative_context) neg.push_back(norm.fold(c.get<std::string>()));
    t["negative_context"] = neg;
  }
  terms.push_back(std::move(t));
}

// Best-effort: file stems under `repo` become "path" class terms, capped so
// a huge repository cannot blow up profile size or scan time.
void add_repo_terms(Json& terms, const kb::Normalizer& norm, const fs::path& repo) {
  constexpr std::size_t kMaxFiles = 4000;
  static const std::set<std::string> kExts = {".py", ".cpp", ".cc", ".h", ".hpp", ".md", ".json", ".ts", ".tsx"};
  std::error_code ec;
  if (!fs::exists(repo, ec) || ec) return;
  std::size_t n = 0;
  std::set<std::string> seen;
  for (auto it = fs::recursive_directory_iterator(repo, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (n >= kMaxFiles) break;
    const auto& p = *it;
    if (p.path().filename() == ".loom-archive" || p.path().string().find("/.git/") != std::string::npos) continue;
    if (p.is_directory(ec)) continue;
    std::string ext = p.path().extension().string();
    if (!kExts.count(ext)) continue;
    std::string stem = p.path().stem().string();
    if (stem.size() < 4 || !seen.insert(stem).second) continue;
    ++n;
    add_term(terms, stem, norm, "path", 2.0, "", "repo:" + p.path().string(), false, Json(), Json());
  }
}

}  // namespace

Json flatten_self_profile(const kb::Pack& pack, const kb::Normalizer& norm, const ProfileConfig& cfg) {
  Json terms = Json::array();
  Json projects = Json::array();
  const Json& self = pack.profile("self");
  if (self.is_object()) {
    if (const Json* projs = json::find(self, "projects"); projs && projs->is_array()) {
      for (const auto& p : *projs) {
        std::string pid = json::get_string(p, "id");
        projects.push_back(Json{{"id", pid},
                                {"name", json::get_string(p, "name")},
                                {"project_kind", json::get_string(p, "project_kind")},
                                {"facets", json::find(p, "facets") ? *json::find(p, "facets") : Json::array()}});
        if (const Json* aliases = json::find(p, "aliases"); aliases && aliases->is_array()) {
          for (const auto& a : *aliases) {
            std::string surface = json::get_string(a, "t");
            const Json* rc = json::find(a, "requires_context");
            const Json* neg = json::find(a, "negative_context");
            add_term(terms, surface, norm, "alias", 3.0, pid, "profiles/self.json", json::get_bool(a, "ambiguous"),
                     rc ? *rc : Json(), neg ? *neg : Json(), json::get_bool(a, "prefix"));
          }
        }
      }
    }
    if (const Json* probe = json::find(self, "philosophy_probe"); probe && probe->is_object()) {
      if (const Json* anchors = json::find(*probe, "anchors"); anchors && anchors->is_array()) {
        for (const auto& a : *anchors) {
          add_term(terms, a.get<std::string>(), norm, "principle", 1.5, "", "profiles/self.json#philosophy_probe",
                   false, Json(), Json());
        }
      }
    }
  }
  if (auto principles = model::principles(pack, cfg.priors)) {
    for (auto& p : *principles) {
      for (auto& ph : p.phrasings) {
        add_term(terms, ph, norm, "principle", 1.5, "", "philosophy/principles.json#" + p.id, false, Json(), Json());
      }
    }
  }
  for (auto& t : cfg.extra_terms) {
    add_term(terms, t, norm, "alias", 3.0, "", "user", false, Json(), Json());
  }
  if (cfg.repo) add_repo_terms(terms, norm, *cfg.repo);
  return Json{{"terms", terms}, {"projects", projects}};
}

int alias_context_window_tokens(const kb::Pack& pack) {
  const Json* cat = json::find(pack.policy("thresholds"), "catalog");
  auto value = cat ? json::get_int(*cat, "context_window_tokens", 30) : 30;
  if (value < 0) return 0;
  return static_cast<int>(std::min<std::int64_t>(value, std::numeric_limits<int>::max()));
}

AliasIndex AliasIndex::from_pack(const kb::Pack& pack) {
  kb::Normalizer norm(pack);
  Json flat = flatten_self_profile(pack, norm, ProfileConfig{});
  SelfProfile p;
  p.terms = flat["terms"];
  p.projects = flat["projects"];
  return AliasIndex::from_profile(p, alias_context_window_tokens(pack));
}

}  // namespace loom::catalog::internal

namespace loom::catalog {

Result<SelfProfile> Catalog::build_profile(const ProfileConfig& cfg) {
  LOOM_TRY(ensure_schema(rt_.db()));
  kb::Normalizer norm(*pack_);
  Json flat = internal::flatten_self_profile(*pack_, norm, cfg);

  SelfProfile p;
  p.terms = flat["terms"];
  p.projects = flat["projects"];
  Json fp{{"pack_hash", pack_->hash()}, {"cfg", cfg.to_json()}, {"terms", p.terms}, {"projects", p.projects}};
  p.input_hash = Sha256::hex(json::canonical(fp));
  p.id = "cp_" + p.input_hash.substr(0, 16);

  auto lk = rt_.db().lock();
  sql::Connection& c = rt_.db().conn();
  LOOM_TRY(c.run("INSERT OR REPLACE INTO loom_cat_profiles (id, input_hash, body, created) VALUES (?, ?, ?, ?)",
                p.id, p.input_hash, json::dump(p.to_json()), timeutil::utc_now_iso()));
  return p;
}

}  // namespace loom::catalog
