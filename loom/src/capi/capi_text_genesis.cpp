// C API: text genesis in the graph (text_genesis.h, R102 G2). Every function is wrapped by the exception firewall and
// returns a caller-freed JSON string (loom_free_string).
#include "context.h"
#include "loom/runtime.h"
#include "loom/text_genesis.h"

using namespace loom;
using namespace loom::capi;

namespace {

// Segmenter + options from the embedded pack, with an optional overlay ({"pack": {...}} deep-merged) and the
// language / level / alignment the request names ("" = the pack's presets).
struct Setup {
  text::Segmenter seg;
  text::genesis::Options opt;
};

Result<Setup> setup(const Json& req) {
  Json pack = text::builtin_pack();
  if (const Json* overlay = json::find(req, "pack"); overlay && overlay->is_object()) pack = text::merge_pack(pack, *overlay);
  LOOM_TRY_ASSIGN(auto opt, text::genesis::Options::from_pack(pack));
  if (const Json* l = json::find(req, "level"); l && l->is_string() && !l->get<std::string>().empty()) opt.level = l->get<std::string>();
  if (const Json* l = json::find(req, "language"); l && l->is_string()) opt.language = l->get<std::string>();
  const std::string language = opt.language.empty() ? json::get_string(pack, "default_language") : opt.language;
  LOOM_TRY_ASSIGN(auto seg, text::Segmenter::from_pack(pack, language));
  return Setup{std::move(seg), std::move(opt)};
}

}  // namespace

extern "C" {

// Writes the genesis of one source into the graph (idempotent). request:
//   {"message_id"|"version_group_id"|"conv_id": ..., "level"?, "language"?, "pack"?: overlay}
// -> {"chains": n, "summary": {versions, fragments, events, nodes_created, nodes_reused, links_written, advisories}, "per_chain": [...]}
LOOM_API const char* loom_text_genesis_materialize(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_text_genesis_materialize", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto req = parse_arg(request_json);
    if (!req) return out_error(req.error());
    auto su = setup(*req);
    if (!su) return out_error(su.error());
    auto& db = ctx->rt->db();
    std::vector<std::vector<text::genesis::VersionText>> sources;
    const std::string mid = json::get_string(*req, "message_id"), vg = json::get_string(*req, "version_group_id"), cid = json::get_string(*req, "conv_id");
    if (!mid.empty()) {
      auto v = text::genesis::versions_of_message(db, mid);
      if (!v) return out_error(v.error());
      sources.push_back(std::move(*v));
    } else if (!vg.empty()) {
      auto v = text::genesis::versions_of_group(db, vg);
      if (!v) return out_error(v.error());
      sources.push_back(std::move(*v));
    } else if (!cid.empty()) {
      auto v = text::genesis::chains_of_conversation(db, cid);
      if (!v) return out_error(v.error());
      sources = std::move(*v);
    } else {
      return out_error(missing("message_id, version_group_id or conv_id"));
    }
    text::genesis::MaterializeSummary total;
    Json per = Json::array();
    for (const auto& s : sources) {
      auto chain = text::genesis::compute_chain(su->seg, s, su->opt);
      if (!chain) return out_error(chain.error());
      auto sum = text::genesis::materialize(db, *chain, su->opt);
      if (!sum) return out_error(sum.error());
      total.versions += sum->versions;
      total.fragments += sum->fragments;
      total.events += sum->events;
      total.nodes_created += sum->nodes_created;
      total.nodes_reused += sum->nodes_reused;
      total.links_written += sum->links_written;
      for (const auto& a : sum->advisories) total.advisories.push_back(a);
      Json row = sum->to_json();
      row["group"] = s.empty() ? std::string() : s.front().source.group;
      per.push_back(std::move(row));
    }
    return out(Json{{"chains", sources.size()}, {"summary", total.to_json()}, {"per_chain", per}, {"level", su->opt.level}, {"language", su->seg.language()}});
  });
}

// Read-only question about a sentence (nothing is written). request:
//   {"question": "origin"|"replaced"|"missing"|"events" (the pack lists them), "text"?, "fingerprint"?, "message_id"?,
//    "version_group_id"?, "conv_id"?, "from_id"?, "to_id"?, "limit"?, "level"?, "language"?, "pack"?: overlay}
// -> the answer JSON of text::genesis::query (always says how it was computed: "live" and/or "graph").
LOOM_API const char* loom_text_genesis_query(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_text_genesis_query", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    auto req = parse_arg(request_json);
    if (!req) return out_error(req.error());
    auto su = setup(*req);
    if (!su) return out_error(su.error());
    text::genesis::Query q;
    q.question = json::get_string(*req, "question");
    if (q.question.empty()) {
      Json known = text::genesis::known_questions(su->opt);
      Json e = error_json(missing("question"));
      e["questions"] = known;
      return out(e);
    }
    q.text = json::get_string(*req, "text");
    q.fingerprint = json::get_string(*req, "fingerprint");
    q.message_id = json::get_string(*req, "message_id");
    q.version_group_id = json::get_string(*req, "version_group_id");
    q.conv_id = json::get_string(*req, "conv_id");
    q.from_id = json::get_string(*req, "from_id");
    q.to_id = json::get_string(*req, "to_id");
    const std::int64_t limit = json::get_int(*req, "limit", 50);
    if (limit > 0) q.limit = static_cast<std::size_t>(limit);
    auto r = text::genesis::query(ctx->rt->db(), su->seg, q, su->opt);
    if (!r) return out_error(r.error());
    return out(*r);
  });
}

}  // extern "C"
