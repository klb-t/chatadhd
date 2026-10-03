// C API: extract + resolve (extract.h, resolve.h).
#include "context.h"
#include "loom/extract.h"
#include "loom/knowledge.h"
#include "loom/resolve.h"
#include "loom/runtime.h"

using namespace loom;
using namespace loom::capi;

extern "C" {

// Detect, segment and extract one file (or directory) without storing.
// options: {"max_units"?: n (default 200), "units"?: bool (per-unit stats)}
// -> Extraction JSON of all units merged (two-pass, like the stage).
LOOM_API const char* loom_extract_preview(LoomContext* ctx, const char* path, const char* options_json) {
  return guard_json("loom_extract_preview", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto p = opt_str(path);
    if (!p) return out_error(missing("path"));
    auto opts = parse_arg(options_json);
    if (!opts) return out_error(opts.error());
    auto pack = ctx->rt->knowledge().pack();
    if (!pack) return out_error(pack.error());
    auto units = extract::read_units(*p);
    if (!units) return out_error(units.error());
    std::int64_t max_units = json::get_int(*opts, "max_units", 200);
    if (max_units > 0 && static_cast<std::int64_t>(units->size()) > max_units) units->resize(static_cast<std::size_t>(max_units));
    extract::Extraction ex = extract::extract_units(*pack, *units);
    Json j = ex.to_json();
    if (!json::get_bool(*opts, "units")) j["stats"].erase("per_unit");
    return out(j);
  });
}

// {"snapshot": dir, "repo": dir, "label"?: str} -> LineageResult JSON
LOOM_API const char* loom_resolve_lineage(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_resolve_lineage", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto req = parse_arg(request_json);
    if (!req) return out_error(req.error());
    std::string snap = json::get_string(*req, "snapshot");
    std::string repo = json::get_string(*req, "repo");
    if (snap.empty()) return out_error(missing("snapshot"));
    if (repo.empty()) return out_error(missing("repo"));
    auto s = resolve::snapshot_revision(snap, json::get_string(*req, "label"));
    if (!s) return out_error(s.error());
    std::vector<std::string> paths;
    for (const auto& [path, c] : s->files) paths.push_back(path);
    auto hist = resolve::git_revisions(repo, paths);
    if (!hist) return out_error(hist.error());
    if (hist->empty()) return out_error(Errc::NotFound, "no revision of the repository touches the snapshot's files");
    auto r = resolve::code_lineage(*s, *hist);
    if (!r) return out_error(r.error());
    return out(r->to_json());
  });
}

}  // extern "C"
