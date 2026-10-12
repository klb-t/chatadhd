// Read-only resource-economy report (R50). See include/loom/economy.h.
#include "loom/economy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "loom/sqlite.h"
#include "loom/util/sha256.h"

namespace loom::economy {
namespace {

constexpr double kGiB = 1073741824.0;

Json unknown(const std::string& reason, std::vector<std::string> missing = {}) {
  Json out{{"known", false}, {"reason", reason}};
  if (!missing.empty()) out["missing"] = missing;
  return out;
}

bool safe_identifier(const std::string& s) {
  if (s.empty()) return false;
  return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

std::string normalize(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool space = false;
  for (unsigned char c : text) {
    if (std::isspace(c)) {
      space = !out.empty();
    } else {
      if (space) out.push_back(' ');
      space = false;
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

// Preset values, resolved once. Null coefficients stay nullopt (= unknown).
struct Params {
  std::string mode;
  std::vector<std::string> statuses;
  std::int64_t min_copies = 2, min_message_bytes = 0, min_conversation_messages = 0;
  std::int64_t min_block_bytes = 0, min_block_copies = 2, min_prefix_bytes = 0;
  std::size_t examples = 3, max_findings = 100;
  double storage_cost = 0, horizon = 0, indirection = 0, dedupe_per_mb = 0, independence = 0, chars_per_token = 4;
  std::optional<double> compression_ratio, token_price, inclusions;
  std::vector<std::array<std::string, 3>> json_columns;

  static std::optional<double> opt(const Json& v, const char* key) {
    if (!v.contains(key) || v[key].is_null()) return std::nullopt;
    return v[key].get<double>();
  }
  static Params from(const Json& v) {
    Params p;
    p.mode = v.at("mode").get<std::string>();
    for (const auto& s : v.at("message_statuses")) p.statuses.push_back(s.get<std::string>());
    p.min_copies = v.at("min_copies").get<std::int64_t>();
    p.min_message_bytes = v.at("min_message_bytes").get<std::int64_t>();
    p.min_conversation_messages = v.at("min_conversation_messages").get<std::int64_t>();
    p.min_block_bytes = v.at("min_block_bytes").get<std::int64_t>();
    p.min_block_copies = v.at("min_block_copies").get<std::int64_t>();
    p.min_prefix_bytes = v.at("min_prefix_bytes").get<std::int64_t>();
    p.examples = static_cast<std::size_t>(v.at("examples_per_finding").get<std::int64_t>());
    p.max_findings = static_cast<std::size_t>(v.at("max_findings_per_class").get<std::int64_t>());
    p.storage_cost = v.at("storage_cost_usd_per_gb_month").get<double>();
    p.horizon = v.at("horizon_months").get<double>();
    p.indirection = v.at("indirection_cost_usd_per_reference").get<double>();
    p.dedupe_per_mb = v.at("dedupe_compute_usd_per_mb").get<double>();
    p.independence = v.at("independence_loss_usd_per_copy").get<double>();
    p.chars_per_token = v.at("chars_per_token").get<double>();
    p.compression_ratio = opt(v, "cold_compression_ratio");
    p.token_price = opt(v, "token_price_usd_per_million");
    p.inclusions = opt(v, "expected_prompt_inclusions");
    for (const auto& c : v.at("json_columns"))
      p.json_columns.push_back({c.at("table").get<std::string>(), c.at("column").get<std::string>(),
                                c.at("id").get<std::string>()});
    return p;
  }
};

// One duplicate group before it becomes a finding.
struct Group {
  std::int64_t copies = 0, total = 0, largest = 0;
  std::vector<std::string> examples;
  void add(std::int64_t bytes, const std::string& example, std::size_t keep) {
    ++copies;
    total += bytes;
    largest = std::max(largest, bytes);
    if (examples.size() < keep) examples.push_back(example);
  }
  std::int64_t redundant() const { return total - largest; }
};

// Benefit/cost balance per section 9. Unknown inputs make a field {known:false, missing}.
Json evaluate_actions(const Params& p, std::int64_t copies, std::int64_t total, std::int64_t redundant,
                      bool prompt_visible, bool cache_applies = false) {
  Json actions = Json::array();
  const double storage_usd = static_cast<double>(redundant) / kGiB * p.storage_cost * p.horizon;
  {  // pin / link: one immutable content-addressed blob, many references.
    Json cost{{"indirection_usd", static_cast<double>(copies) * p.indirection},
              {"dedupe_compute_usd", static_cast<double>(total) / 1e6 * p.dedupe_per_mb},
              {"independence_loss_usd", static_cast<double>(copies - 1) * p.independence}};
    const double cost_usd = cost["indirection_usd"].get<double>() + cost["dedupe_compute_usd"].get<double>() +
                            cost["independence_loss_usd"].get<double>();
    Json benefit{{"storage_usd", storage_usd}};
    Json balance{{"benefit", benefit}, {"cost", cost}};
    std::vector<std::string> missing;
    if (prompt_visible) {
      if (!p.token_price) missing.push_back("token_price_usd_per_million");
      if (!p.inclusions) missing.push_back("expected_prompt_inclusions");
    }
    if (prompt_visible && missing.empty()) {
      const double tokens = static_cast<double>(redundant) / p.chars_per_token;
      const double saved = tokens / 1e6 * *p.token_price * *p.inclusions;
      balance["benefit"]["saved_tokens_usd"] = saved;
      balance["net_usd"] = storage_usd + saved - cost_usd;
    } else if (prompt_visible) {
      balance["benefit"]["saved_tokens_usd"] = unknown("input_missing", missing);
      balance["net_usd"] = unknown("input_missing", missing);
      balance["net_without_tokens_usd"] = storage_usd - cost_usd;
    } else {
      balance["net_usd"] = storage_usd - cost_usd;
    }
    balance["known"] = missing.empty();
    if (!missing.empty()) balance["missing"] = missing;
    actions.push_back(Json{{"action", "pin_link"}, {"balance", balance}});
  }
  {  // cold compression of all copies.
    if (!p.compression_ratio) {
      actions.push_back(Json{{"action", "compression"}, {"balance", unknown("input_missing", {"cold_compression_ratio"})}});
    } else {
      const double saved = static_cast<double>(total) * (1.0 - 1.0 / *p.compression_ratio) / kGiB * p.storage_cost * p.horizon;
      const double cost_usd = static_cast<double>(total) / 1e6 * p.dedupe_per_mb;
      actions.push_back(Json{{"action", "compression"},
                             {"balance", Json{{"known", true}, {"benefit", Json{{"storage_usd", saved}}},
                                              {"cost", Json{{"compression_compute_usd", cost_usd}}},
                                              {"net_usd", saved - cost_usd}}}});
    }
  }
  if (cache_applies) {  // same method version + same input recomputed: needs per-method cost, never guessed.
    actions.push_back(Json{{"action", "cache"},
                           {"balance", unknown("input_missing", {"regeneration_cost_usd", "expected_reads"})}});
    actions.push_back(Json{{"action", "generator_recipe"},
                           {"balance", unknown("input_missing", {"regeneration_cost_usd", "expected_reads"})}});
  }
  actions.push_back(Json{{"action", "nothing"},
                         {"balance", Json{{"known", true}, {"net_usd", 0.0}}},
                         {"note", "the duplicate stays by choice: independence, provenance, simplicity"}});
  // Best known action by net; unknown alternatives are listed, never assumed worse.
  std::string best = "nothing";
  double best_net = 0;
  Json undecided = Json::array();
  for (const auto& a : actions) {
    const auto& b = a["balance"];
    if (b.contains("net_usd") && b["net_usd"].is_number()) {
      if (b["net_usd"].get<double>() > best_net) { best_net = b["net_usd"].get<double>(); best = a["action"]; }
    } else {
      undecided.push_back(a["action"]);
    }
  }
  return Json{{"proposed", actions}, {"best_known", Json{{"action", best}, {"net_usd", best_net}}},
              {"balance_unknown_for", undecided}};
}

Json finding(const Params& p, const std::string& kind, const std::string& key, const Group& g,
             bool prompt_visible, bool cache_applies = false) {
  Json f{{"kind", kind}, {"key", key}, {"copies", g.copies}, {"bytes_total", g.total},
         {"bytes_each_max", g.largest}, {"redundant_bytes", g.redundant()}, {"examples", g.examples}};
  f["actions"] = evaluate_actions(p, g.copies, g.total, g.redundant(), prompt_visible, cache_applies);
  return f;
}

// Sorts, applies the display preset, and keeps totals over every group.
Json measure(const Params& p, const std::string& what, std::vector<std::pair<std::string, Group>> groups,
             const std::string& kind, bool prompt_visible, bool cache_applies = false) {
  std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
    if (a.second.redundant() != b.second.redundant()) return a.second.redundant() > b.second.redundant();
    return a.first < b.first;
  });
  std::int64_t copies = 0, redundant = 0;
  for (const auto& g : groups) { copies += g.second.copies; redundant += g.second.redundant(); }
  Json findings = Json::array();
  for (const auto& [key, g] : groups) {
    if (p.max_findings && findings.size() >= p.max_findings) break;
    findings.push_back(finding(p, kind, key, g, prompt_visible, cache_applies));
  }
  return Json{{"known", true}, {"measure", what},
              {"totals", Json{{"groups", groups.size()}, {"copies", copies}, {"redundant_bytes", redundant},
                              {"findings_listed", findings.size()}}},
              {"findings", findings}};
}

std::string in_clause(std::size_t n) {
  std::string s = "(";
  for (std::size_t i = 0; i < n; ++i) s += i ? ",?" : "?";
  return s + ")";
}

// ── class 1: repeated imports / duplicate content ────────────────────────────
Json source_reimports(sql::Connection& c, const Params& p) {
  if (!c.has_table("loom_sources")) return unknown("table_absent:loom_sources");
  auto st = c.prepare("SELECT blob_hash, COUNT(*), COALESCE(MAX(size),0), COALESCE(SUM(size),0) FROM loom_sources "
                      "WHERE blob_hash IS NOT NULL AND blob_hash<>'' GROUP BY blob_hash HAVING COUNT(*)>=?");
  if (!st) return unknown("query_failed");
  st->bind(1, p.min_copies);
  std::vector<std::pair<std::string, Group>> groups;
  while (true) {
    auto row = st->step();
    if (!row) return unknown("query_failed");
    if (!*row) break;
    Group g;
    g.copies = st->get_int(1);
    g.largest = st->get_int(2);
    g.total = st->get_int(3);
    groups.emplace_back(st->get_text(0), std::move(g));
  }
  for (auto& [hash, g] : groups) {
    auto ex = c.prepare("SELECT id FROM loom_sources WHERE blob_hash=? ORDER BY imported_at, rowid LIMIT ?");
    if (!ex) continue;
    ex->bind(1, hash);
    ex->bind(2, static_cast<std::int64_t>(p.examples));
    while (true) {
      auto row = ex->step();
      if (!row || !*row) break;
      g.examples.push_back(ex->get_text(0));
    }
  }
  Json out = measure(p, "source_reimports", std::move(groups), "same_source_imported_repeatedly", false);
  // The raw bytes live once in the content-addressed BlobStore; the waste of a re-import is the derived
  // conversations/messages, measured under import_fingerprints and duplicate_conversations.
  for (auto& f : out["findings"]) {
    f["redundant_bytes"] = 0;
    f["actions"] = Json{{"proposed", Json::array()}, {"best_known", Json{{"action", "nothing"}, {"net_usd", 0.0}}},
                        {"balance_unknown_for", Json::array()},
                        {"note", "raw source bytes are already one content-addressed blob; see import_fingerprints"}};
  }
  out["totals"]["redundant_bytes"] = 0;
  out["raw_source_bytes_stored_once"] = true;
  return out;
}

Json import_fingerprints(sql::Connection& c, const Params& p) {
  if (!c.has_table("loom_import_fingerprints")) return unknown("table_absent:loom_import_fingerprints");
  auto st = c.prepare("SELECT f.fingerprint, f.subject_kind, f.subject_id, "
                      "COALESCE((SELECT SUM(LENGTH(CAST(m.text AS BLOB))) FROM messages m WHERE m.conv_id=f.subject_id),0) "
                      "FROM loom_import_fingerprints f ORDER BY f.fingerprint, f.subject_kind, f.rowid");
  if (!st) return unknown("query_failed");
  std::map<std::string, Group> groups;
  while (true) {
    auto row = st->step();
    if (!row) return unknown("query_failed");
    if (!*row) break;
    const bool conv = st->get_text(1) == "conversation";
    groups[st->get_text(1) + ":" + st->get_text(0)].add(conv ? st->get_int(3) : 0, st->get_text(2), p.examples);
  }
  std::vector<std::pair<std::string, Group>> kept;
  for (auto& [k, g] : groups)
    if (g.copies >= p.min_copies) kept.emplace_back(k, std::move(g));
  return measure(p, "import_fingerprints", std::move(kept), "same_content_fingerprint_imported_repeatedly", true);
}

Json duplicate_links(sql::Connection& c) {
  auto st = c.prepare("SELECT metadata FROM links WHERE link_type='derived_from' AND metadata LIKE '%duplicate_of%'");
  if (!st) return unknown("query_failed");
  std::map<std::string, std::int64_t> by_level;
  std::int64_t total = 0;
  while (true) {
    auto row = st->step();
    if (!row) return unknown("query_failed");
    if (!*row) break;
    auto meta = json::parse(st->get_text(0));
    if (!meta || !meta->is_object() || json::get_string(*meta, "relation") != "duplicate_of") continue;
    ++total;
    ++by_level[json::get_string(*meta, "level", "unspecified")];
  }
  Json levels = Json::object();
  for (const auto& [k, v] : by_level) levels[k] = v;
  return Json{{"known", true}, {"measure", "recorded_duplicate_links"}, {"count", total}, {"by_level", levels}};
}

// Messages and conversations by normalized content hash, one pass.
std::pair<Json, Json> content_duplicates(sql::Connection& c, const Params& p) {
  if (p.statuses.empty()) {
    const Json none = unknown("message_statuses_empty");
    return {none, none};
  }
  auto st = c.prepare("SELECT id, conv_id, role, text, LENGTH(CAST(text AS BLOB)) FROM messages WHERE status IN " +
                      in_clause(p.statuses.size()) + " ORDER BY conv_id, created, rowid");
  if (!st) return {unknown("query_failed"), unknown("query_failed")};
  for (std::size_t i = 0; i < p.statuses.size(); ++i) st->bind(static_cast<int>(i + 1), p.statuses[i]);
  std::unordered_map<std::string, Group> messages;
  std::unordered_map<std::string, Group> conversations;
  std::string current;
  Sha256 sig;
  std::int64_t count = 0, bytes = 0;
  auto flush = [&] {
    if (count > 0 && count >= p.min_conversation_messages)
      conversations[sig.finish_hex()].add(bytes, current, p.examples);
    else
      sig.reset();
    count = 0;
    bytes = 0;
  };
  while (true) {
    auto row = st->step();
    if (!row) return {unknown("query_failed"), unknown("query_failed")};
    if (!*row) break;
    const std::string conv = st->get_text(1);
    if (conv != current) { flush(); current = conv; }
    const std::string digest = Sha256::hex(st->get_text(2) + '\x1f' + normalize(st->get_text(3)));
    sig.update(digest);
    ++count;
    bytes += st->get_int(4);
    if (st->get_int(4) >= p.min_message_bytes) messages[digest].add(st->get_int(4), st->get_text(0), p.examples);
  }
  flush();
  auto keep = [&](std::unordered_map<std::string, Group>& all) {
    std::vector<std::pair<std::string, Group>> kept;
    for (auto& [k, g] : all)
      if (g.copies >= p.min_copies) kept.emplace_back(k, std::move(g));
    return kept;
  };
  Json m = measure(p, "duplicate_messages", keep(messages), "same_role_and_normalized_text", true);
  Json v = measure(p, "duplicate_conversations", keep(conversations), "same_ordered_message_content", true);
  m["note"] = "includes the messages of duplicate conversations; do not add this measure to duplicate_conversations";
  return {m, v};
}

// ── class 3: repeated canonical-JSON subtrees ────────────────────────────────
struct Block { std::int64_t copies = 0; std::int64_t size = 0; };

void walk_blocks(const Json& v, std::int64_t min_bytes, const std::function<bool(const std::string&, std::int64_t, const std::string&)>& visit,
                 const std::string& pointer) {
  if (!(v.is_object() || v.is_array() || v.is_string())) return;
  const std::string canon = json::canonical(v);
  const auto size = static_cast<std::int64_t>(canon.size());
  if (size < min_bytes) return;  // children of a smaller subtree are smaller still
  if (visit(Sha256::hex(canon), size, pointer)) return;
  auto esc = [](const std::string& s) {
    std::string o;
    for (char ch : s) { if (ch == '~') o += "~0"; else if (ch == '/') o += "~1"; else o.push_back(ch); }
    return o;
  };
  if (v.is_object()) for (auto it = v.begin(); it != v.end(); ++it) walk_blocks(*it, min_bytes, visit, pointer + "/" + esc(it.key()));
  else if (v.is_array()) for (std::size_t i = 0; i < v.size(); ++i) walk_blocks(v[i], min_bytes, visit, pointer + "/" + std::to_string(i));
}

Json repeated_blocks(sql::Connection& c, const Params& p) {
  Json skipped = Json::array();
  std::vector<std::array<std::string, 3>> columns;
  for (const auto& col : p.json_columns) {
    const auto& [table, column, id] = col;
    if (!safe_identifier(table) || !safe_identifier(column) || !safe_identifier(id)) {
      skipped.push_back(Json{{"column", table + "." + column}, {"reason", "unsafe_identifier"}});
    } else if (!c.has_table(table) || !c.has_column(table, column) || !c.has_column(table, id)) {
      skipped.push_back(Json{{"column", table + "." + column}, {"reason", "table_or_column_absent"}});
    } else {
      columns.push_back(col);
    }
  }
  if (columns.empty()) return unknown("no_readable_json_columns");
  auto scan = [&](const std::function<void(const std::string&, const std::string&, const Json&)>& per_row) -> bool {
    for (const auto& [table, column, id] : columns) {
      auto st = c.prepare("SELECT " + id + ", " + column + " FROM " + table + " WHERE " + column +
                          " IS NOT NULL AND LENGTH(CAST(" + column + " AS BLOB))>=?");
      if (!st) return false;
      st->bind(1, p.min_block_bytes);
      while (true) {
        auto row = st->step();
        if (!row) return false;
        if (!*row) break;
        auto doc = json::parse(st->get_text(1));
        if (doc) per_row(table + "." + column, st->get_text(0), *doc);
      }
    }
    return true;
  };
  std::unordered_map<std::string, Block> seen;
  if (!scan([&](const std::string&, const std::string&, const Json& doc) {
        walk_blocks(doc, p.min_block_bytes, [&](const std::string& h, std::int64_t size, const std::string&) {
          auto& b = seen[h];
          ++b.copies;
          b.size = size;
          return false;
        }, "");
      }))
    return unknown("query_failed");
  // Second pass: only the outermost repeated subtree of each occurrence is a finding, so a repeated
  // object is not also reported through every repeated child.
  std::unordered_map<std::string, Group> outer;
  if (!scan([&](const std::string& where, const std::string& id, const Json& doc) {
        walk_blocks(doc, p.min_block_bytes, [&](const std::string& h, std::int64_t size, const std::string& ptr) {
          if (seen[h].copies < p.min_block_copies) return false;
          outer[h].add(size, where + "#" + id + (ptr.empty() ? "" : " " + ptr), p.examples);
          return true;
        }, "");
      }))
    return unknown("query_failed");
  std::vector<std::pair<std::string, Group>> kept;
  for (auto& [h, g] : outer)
    if (g.copies >= p.min_block_copies) kept.emplace_back(h, std::move(g));
  Json out = measure(p, "repeated_json_blocks", std::move(kept), "repeated_canonical_json_block", false);
  out["columns_scanned"] = Json::array();
  for (const auto& col : columns) out["columns_scanned"].push_back(col[0] + "." + col[1]);
  out["columns_skipped"] = skipped;
  return out;
}

// ── class 2: derived result recomputed from the same method and input ───────
Json recomputed_results(sql::Connection& c, const Params& p) {
  if (!c.has_table("loom_tasks")) return unknown("table_absent:loom_tasks");
  auto st = c.prepare("SELECT kind, input_hash, params, COUNT(*), COALESCE(SUM(LENGTH(COALESCE(result,''))),0), "
                      "COALESCE(MAX(LENGTH(COALESCE(result,''))),0), MIN(id) FROM loom_tasks "
                      "WHERE input_hash IS NOT NULL AND input_hash<>'' GROUP BY kind, input_hash, params HAVING COUNT(*)>=?");
  if (!st) return unknown("query_failed");
  st->bind(1, p.min_copies);
  std::vector<std::pair<std::string, Group>> groups;
  while (true) {
    auto row = st->step();
    if (!row) return unknown("query_failed");
    if (!*row) break;
    Group g;
    g.copies = st->get_int(3);
    g.total = st->get_int(4);
    g.largest = st->get_int(5);
    g.examples.push_back(st->get_text(6));
    groups.emplace_back(st->get_text(0) + ":" + st->get_text(1) + ":" + Sha256::hex(st->get_text(2)).substr(0, 12), std::move(g));
  }
  Json out = measure(p, "recomputed_tasks", std::move(groups), "same_task_kind_input_and_params", false, true);
  out["scope"] = "loom_tasks (kind, input_hash, params); per-message analysis attempts are not keyed by method version and input";
  return out;
}

// ── class 5: representation overhead ─────────────────────────────────────────
Json representation_overhead(sql::Connection& c) {
  auto text = c.query_int("SELECT COALESCE(SUM(LENGTH(CAST(text AS BLOB))),0) FROM messages");
  auto nodes = c.query_int("SELECT COALESCE(SUM(LENGTH(CAST(label||content||tags||metadata AS BLOB))),0) FROM nodes");
  auto links = c.query_int("SELECT COALESCE(SUM(LENGTH(CAST(src||dst||link_type||metadata AS BLOB))),0) FROM links");
  if (!text || !nodes || !links) return unknown("query_failed");
  const std::int64_t t = text->value_or(0), g = nodes->value_or(0) + links->value_or(0);
  Json out{{"known", true}, {"measure", "graph_vs_text_bytes"}, {"text_bytes", t}, {"graph_bytes", g},
           {"note", "informational: stored form, not a duplicate; no action is proposed"}};
  out["ratio"] = t > 0 ? Json(static_cast<double>(g) / static_cast<double>(t)) : unknown("no_text_bytes");
  return out;
}

// ── class 8: versions and branches with shared prefixes ──────────────────────
Json version_prefixes(sql::Connection& c, const Params& p) {
  auto st = c.prepare("SELECT version_group_id, id, text FROM messages WHERE version_group_id IS NOT NULL "
                      "ORDER BY version_group_id, version_num, rowid");
  if (!st) return unknown("query_failed");
  std::vector<std::pair<std::string, Group>> groups;
  std::string current, first;
  Group g;
  std::int64_t members = 0;
  auto flush = [&] {
    if (members >= 2 && g.copies >= 1) {
      g.copies += 1;  // the first version the prefixes are measured against
      groups.emplace_back(current, g);
    }
    g = Group{};
    members = 0;
  };
  while (true) {
    auto row = st->step();
    if (!row) return unknown("query_failed");
    if (!*row) break;
    if (st->get_text(0) != current) { flush(); current = st->get_text(0); first.clear(); }
    ++members;
    const std::string text = st->get_text(2);
    if (members == 1) { first = text; g.examples.push_back(st->get_text(1)); continue; }
    std::size_t n = 0;
    while (n < first.size() && n < text.size() && first[n] == text[n]) ++n;
    if (static_cast<std::int64_t>(n) >= p.min_prefix_bytes) {
      ++g.copies;
      g.total += static_cast<std::int64_t>(n);
      g.largest = 0;
      if (g.examples.size() < p.examples) g.examples.push_back(st->get_text(1));
    }
  }
  flush();
  return measure(p, "version_shared_prefixes", std::move(groups), "versions_share_prefix_bytes", false);
}

Json blob_store(sql::Connection& c) {
  if (!c.has_table("loom_blobs")) return unknown("table_absent:loom_blobs");
  auto st = c.prepare("SELECT COUNT(*), COALESCE(SUM(size),0) FROM loom_blobs");
  if (!st) return unknown("query_failed");
  auto row = st->step();
  if (!row || !*row) return unknown("query_failed");
  return Json{{"known", true}, {"measure", "blob_store_hash_duplicates"}, {"blobs", st->get_int(0)},
              {"bytes", st->get_int(1)}, {"duplicate_hash_groups", 0},
              {"basis", "loom_blobs.hash is the primary key: identical bytes cannot be stored twice"}};
}

Json klass(int id, const char* label, Json measures) {
  bool any = false;
  for (const auto& [k, m] : measures.items()) any = any || m.value("known", false);
  return Json{{"class", id}, {"label", label}, {"known", any}, {"measures", measures}};
}

}  // namespace

Result<RuntimeProfile> load_profile(const std::filesystem::path& data_dir, const Json& overrides) {
  return RuntimeProfile::load("economy", data_dir, overrides);
}

Result<Json> report(const std::filesystem::path& db_path, const RuntimeProfile& profile) {
  Params p;
  try {
    p = Params::from(profile.values());
  } catch (const std::exception& e) {
    return Error(Errc::InvalidArgument, std::string("economy profile is malformed: ") + e.what());
  }
  Json out{{"schema", "loom.economy_report/1"}, {"mode", p.mode}, {"read_only", true}, {"writes", 0},
           {"profile", Json{{"domain", "economy"}, {"hash", profile.hash()}}}, {"thresholds", profile.values()}};
  out["open_items"] = Json::array({"mode auto (applying a proposal) is not implemented: this report never writes",
                                   "costs of recomputation and expected reads are per-method data and stay unknown until supplied"});
  if (p.mode == "off") {
    out["status"] = "disabled";
    out["classes"] = Json::object();
    return out;
  }
  sql::OpenOptions opts;
  opts.create = false;
  opts.read_only = true;
  // A checkpointed WAL database has no -wal/-shm and no active writer: open it immutable so that even
  // SQLite's own WAL bookkeeping files are not created. If they exist a writer may be live; open normally.
  opts.immutable = !std::filesystem::exists(db_path.string() + "-wal") && !std::filesystem::exists(db_path.string() + "-shm");
  LOOM_TRY_ASSIGN(auto conn, sql::Connection::open(db_path, opts));
  LOOM_TRY(conn.exec("PRAGMA query_only=1"));
  out["status"] = "ok";
  out["proposals"] = p.mode != "report";

  Json c1{{"source_reimports", source_reimports(conn, p)}, {"import_fingerprints", import_fingerprints(conn, p)},
          {"recorded_duplicate_links", duplicate_links(conn)}};
  auto [messages, conversations] = content_duplicates(conn, p);
  c1["duplicate_messages"] = std::move(messages);
  c1["duplicate_conversations"] = std::move(conversations);
  c1["blob_store"] = blob_store(conn);

  Json classes = Json::object();
  classes["1"] = klass(1, "repeated import of the same source / same conversation in several exports", c1);
  classes["2"] = klass(2, "derived result recomputed for the same method version and input", Json{{"recomputed_tasks", recomputed_results(conn, p)}});
  classes["3"] = klass(3, "repeated blocks in metadata", Json{{"repeated_json_blocks", repeated_blocks(conn, p)}});
  classes["4"] = klass(4, "identical prompt prefixes in every call",
                       Json{{"prompt_prefix", unknown("not_recorded: needs per-call context traces; see the chat_context_order preset")}});
  classes["5"] = klass(5, "representation overhead (graph form vs text)", Json{{"graph_vs_text", representation_overhead(conn)}});
  classes["6"] = klass(6, "evidence and ZIPs duplicating repository files",
                       Json{{"zip_vs_repo_files", unknown("needs_filesystem_scan: outside the database; not measured")}});
  classes["7"] = klass(7, "deterministic indexes (FTS, embeddings): store or recompute",
                       Json{{"derived_indexes", unknown("needs_index_cost_model: index size and rebuild cost are not recorded")}});
  classes["8"] = klass(8, "message versions and branches sharing prefixes", Json{{"version_shared_prefixes", version_prefixes(conn, p)}});
  out["classes"] = classes;

  Json measured = Json::array(), unmeasured = Json::array();
  for (const auto& [id, k] : classes.items()) (k["known"].get<bool>() ? measured : unmeasured).push_back(id);
  out["summary"] = Json{{"classes_with_measurements", measured}, {"classes_not_measured", unmeasured}};
  return out;
}

Result<Json> report_for_data_dir(const std::filesystem::path& data_dir, const std::filesystem::path& db_path,
                                 const Json& command) {
  Json overrides = Json::object();
  if (command.is_object() && command.contains("overrides")) overrides = command["overrides"];
  if (!overrides.is_object()) return Error(Errc::InvalidArgument, "overrides must be an object");
  LOOM_TRY_ASSIGN(auto profile, load_profile(data_dir, overrides));
  return report(db_path, profile);
}

}  // namespace loom::economy
