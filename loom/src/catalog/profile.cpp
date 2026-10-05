// catalog.h: Catalog::build_profile — turns profiles/self.json (project
// aliases with context gates) and the pack's principle phrasings (R2: the
// coding-philosophy probe fires even in chats that never name a project)
// into the flat, typed SelfProfile.terms list that AliasIndex and score()
// read. A light best-effort repo scan adds "path" class terms when
// ProfileConfig.repo is given (declared symbols/full parsing is a gap, noted
// in the final report; paths alone already help precision on pasted code).
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "catalog_internal.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::catalog::internal {

namespace fs = std::filesystem;

namespace {

using TermClassWeights = std::map<std::string, double, std::less<>>;

std::string class_weight_pointer(std::string_view cls) {
  std::string pointer = "/term_class_weights/";
  for (char c : cls) {
    if (c == '~') pointer += "~0";
    else if (c == '/') pointer += "~1";
    else pointer += c;
  }
  return pointer;
}

Result<TermClassWeights> term_class_weights(const kb::Pack& pack) {
  const Json* configured = json::find(pack.policy("relevance"), "term_class_weights");
  if (!configured || !configured->is_object()) {
    return Error(Errc::InvalidArgument,
                 "catalog.profile.term_class_weights.invalid pointer=/term_class_weights");
  }
  TermClassWeights weights;
  for (auto it = configured->begin(); it != configured->end(); ++it) {
    if (!it.value().is_number() || !std::isfinite(it.value().get<double>())) {
      return Error(Errc::InvalidArgument,
                   "catalog.profile.term_class_weights.invalid pointer=" + class_weight_pointer(it.key()));
    }
    weights.emplace(it.key(), it.value().get<double>());
  }
  return weights;
}

Status add_term(Json& terms, std::string_view surface, const kb::Normalizer& norm, std::string_view cls,
              const TermClassWeights& weights, std::string_view project, std::string_view provenance, bool ambiguous,
              const Json& requires_context, const Json& negative_context, bool prefix = false) {
  if (surface.empty()) return {};
  auto weight = weights.find(cls);
  if (weight == weights.end()) {
    return Error(Errc::InvalidArgument,
                 "catalog.profile.term_class_weights.missing pointer=" + class_weight_pointer(cls));
  }
  Json t{{"term", std::string(surface)},
        {"key", norm.fold(surface)},
        {"class", std::string(cls)},
        {"weight", weight->second},
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
  return {};
}

// Best-effort: file stems under `repo` become "path" class terms, capped so
// a huge repository cannot blow up profile size or scan time.
Status add_repo_terms(Json& terms, const kb::Normalizer& norm, const fs::path& repo,
                      const TermClassWeights& weights) {
  constexpr std::size_t kMaxFiles = 4000;
  static const std::set<std::string> kExts = {".py", ".cpp", ".cc", ".h", ".hpp", ".md", ".json", ".ts", ".tsx"};
  std::error_code ec;
  if (!fs::exists(repo, ec) || ec) return {};
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
    LOOM_TRY(add_term(terms, stem, norm, "path", weights, "", "repo:" + p.path().string(), false, Json(), Json()));
  }
  return {};
}

}  // namespace

Result<Json> flatten_self_profile_checked(const kb::Pack& pack, const kb::Normalizer& norm, const ProfileConfig& cfg) {
  LOOM_TRY_ASSIGN(auto weights, term_class_weights(pack));
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
            LOOM_TRY(add_term(terms, surface, norm, "alias", weights, pid, "profiles/self.json", json::get_bool(a, "ambiguous"),
                     rc ? *rc : Json(), neg ? *neg : Json(), json::get_bool(a, "prefix")));
          }
        }
      }
    }
    if (const Json* probe = json::find(self, "philosophy_probe"); probe && probe->is_object()) {
      if (const Json* anchors = json::find(*probe, "anchors"); anchors && anchors->is_array()) {
        for (const auto& a : *anchors) {
          LOOM_TRY(add_term(terms, a.get<std::string>(), norm, "principle", weights, "", "profiles/self.json#philosophy_probe",
                   false, Json(), Json()));
        }
      }
    }
  }
  if (auto principles = model::principles(pack, cfg.priors)) {
    for (auto& p : *principles) {
      for (auto& ph : p.phrasings) {
        LOOM_TRY(add_term(terms, ph, norm, "principle", weights, "", "philosophy/principles.json#" + p.id, false, Json(), Json()));
      }
    }
  }
  for (auto& t : cfg.extra_terms) {
    LOOM_TRY(add_term(terms, t, norm, "alias", weights, "", "user", false, Json(), Json()));
  }
  if (cfg.repo) LOOM_TRY(add_repo_terms(terms, norm, *cfg.repo, weights));
  return Json{{"terms", terms}, {"projects", projects}};
}

Json flatten_self_profile(const kb::Pack& pack, const kb::Normalizer& norm, const ProfileConfig& cfg) {
  auto result = flatten_self_profile_checked(pack, norm, cfg);
  if (!result) throw std::invalid_argument(result.error().message);
  return std::move(result).value();
}

int alias_context_window_tokens(const kb::Pack& pack) {
  const Json* cat = json::find(pack.policy("thresholds"), "catalog");
  auto value = cat ? json::get_int(*cat, "context_window_tokens", 30) : 30;
  if (value < 0) return 0;
  return static_cast<int>(std::min<std::int64_t>(value, std::numeric_limits<int>::max()));
}

Result<AliasIndex> AliasIndex::from_pack_checked(const kb::Pack& pack) {
  kb::Normalizer norm(pack);
  LOOM_TRY_ASSIGN(auto flat, flatten_self_profile_checked(pack, norm, ProfileConfig{}));
  SelfProfile p;
  p.terms = flat["terms"];
  p.projects = flat["projects"];
  return AliasIndex::from_profile(p, alias_context_window_tokens(pack));
}

AliasIndex AliasIndex::from_pack(const kb::Pack& pack) {
  auto result = from_pack_checked(pack);
  if (!result) throw std::invalid_argument(result.error().message);
  return std::move(result).value();
}

}  // namespace loom::catalog::internal

namespace loom::catalog {

Result<SelfProfile> Catalog::build_profile(const ProfileConfig& cfg) {
  kb::Normalizer norm(*pack_);
  LOOM_TRY_ASSIGN(auto flat, internal::flatten_self_profile_checked(*pack_, norm, cfg));
  LOOM_TRY(ensure_schema(rt_.db()));

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
