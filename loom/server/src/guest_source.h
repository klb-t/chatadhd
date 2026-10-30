// Guest model source selection (R103 §4.5, R104, R108, R111-R113), header-only so a unit test can drive it without a server.
#pragma once
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace loom_server {
using json = nlohmann::json;

// Tiers a person of `audience` ("guest" = no account; "account_trial" = an account that consciously tries paid features)
// may be given, in the order they are tried. DATA: free_providers `source_pools` = {pool: {model_source, tiers[],
// audience[]}}; `model_sources` (guests.model_sources, server_accounts.pack) lists, in order, the model sources offered
// to the person: a pool counts only when its `model_source` name is listed AND its `audience` names this audience.
// So model_sources decides eligibility and the order of pools; free_model_choice.tier_order only orders the tiers inside
// one pool. A tier claimed by a pool that does not serve the audience, or by no pool, is never allowed (an unknown tier
// may cost money). Without `source_pools` only the cost-free tier "free" exists. A missing/non-array model_sources = ["free"].
inline std::vector<std::string> guest_allowed_tiers(const json& fp, const json& model_sources, const std::string& audience = "guest") {
  json pools = fp.value("source_pools", json::object());
  if (!pools.is_object() || pools.empty())
    pools = {{"cost_free", {{"model_source", "free"}, {"tiers", json::array({"free"})}, {"audience", json::array({"guest", "account"})}}}};
  json listed = model_sources.is_array() ? model_sources : json::array({"free"});
  const json tier_order = fp.value("free_model_choice", json::object()).value("tier_order", json::array());
  auto serves = [&](const json& pool) {
    const json aud = pool.value("audience", json::array());
    if (!aud.is_array()) return false;
    for (const auto& a : aud)
      if (a.is_string() && a.get<std::string>() == audience) return true;
    return false;
  };
  std::vector<std::string> banned;  // tiers of pools that do not serve this audience
  for (auto it = pools.begin(); it != pools.end(); ++it) {
    if (!it.value().is_object() || serves(it.value())) continue;
    const json t = it.value().value("tiers", json::array());
    if (t.is_array()) for (const auto& x : t) if (x.is_string()) banned.push_back(x.get<std::string>());
  }
  std::vector<std::string> out;
  for (const auto& src : listed) {
    if (!src.is_string()) continue;
    for (auto it = pools.begin(); it != pools.end(); ++it) {
      const json& pool = it.value();
      if (!pool.is_object() || pool.value("model_source", it.key()) != src.get<std::string>() || !serves(pool)) continue;
      std::vector<std::string> tiers;
      const json t = pool.value("tiers", json::array());
      if (t.is_array()) for (const auto& x : t) if (x.is_string()) tiers.push_back(x.get<std::string>());
      auto idx = [&](const std::string& s) {
        if (tier_order.is_array())
          for (std::size_t i = 0; i < tier_order.size(); ++i) if (tier_order[i].is_string() && tier_order[i].get<std::string>() == s) return i;
        return tier_order.is_array() ? tier_order.size() : std::size_t{0};
      };
      std::stable_sort(tiers.begin(), tiers.end(), [&](const std::string& a, const std::string& b) { return idx(a) < idx(b); });
      for (const auto& x : tiers)
        if (std::find(banned.begin(), banned.end(), x) == banned.end() && std::find(out.begin(), out.end(), x) == out.end()) out.push_back(x);
    }
  }
  return out;
}

// The operator's keys file (KEY=VALUE lines, optional "export ", quotes, # comments). Only the names asked for are
// kept; values never reach a log or an answer.
std::map<std::string, std::string> read_keys_file(std::string path, const std::vector<std::string>& wanted) {
  std::map<std::string, std::string> out;
  if (path.empty() || wanted.empty()) return out;
  if (path.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    if (!home) return out;
    path = std::string(home) + path.substr(1);
  }
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    std::size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos || line[b] == '#') continue;
    line = line.substr(b);
    if (line.rfind("export ", 0) == 0) line = line.substr(7);
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
    if (!v.empty() && std::find(wanted.begin(), wanted.end(), k) != wanted.end()) out[k] = v;
  }
  return out;
}

// R103 §4.5: the best free model a guest without his own key gets, from the free_model_choice data. An entry counts
// when its provider is enabled, its condition holds (key_present = its auth_env is in the keys file, none = no key)
// and its data terms satisfy `require`. Returns {provider, model, base_url, data_terms, quality, why} or
// {provider: null, why}; `secret`/`secret_name` (not part of the public answer) carry the overlay.
// R103 B3: `resolve` handles any other active_when (e.g. service_account: a token from the metadata server) and may
// rewrite base_url ({project}); it returns "" when the entry is usable, else the reason it is skipped. `blocked` gives
// the reason a provider is out right now (exhausted, over budget), "" when it is not.
using SourceResolver = std::function<std::string(const json& prov, std::string& key, std::string& base_url)>;
json choose_free_model(const json& fp, const std::string& keys_file, const std::vector<std::string>& allowed_tiers,
                       std::map<std::string, std::string>& overlay, const SourceResolver& resolve = {},
                       const std::function<std::string(const json&)>& blocked = {}) {
  const json providers = fp.value("providers", json::array());
  const json choice = fp.value("free_model_choice", json::object());
  std::vector<std::string> wanted;
  for (const auto& p : providers)
    if (p.is_object() && !p.value("auth_env", std::string()).empty()) wanted.push_back(p.value("auth_env", std::string()));
  const auto keys = read_keys_file(keys_file, wanted);
  const json require = choice.value("require", json::object());
  json table = choice.value("table", json::array());
  if (!table.is_array()) table = json::array();
  // Which tiers may be chosen at all, and in which order, is decided by the caller (guest_allowed_tiers): a provider
  // whose tier is not in `allowed_tiers` is never chosen, whatever the table says (R104/R111/R112).
  auto tier_of = [&](const json& row) -> std::string {
    for (const auto& p : providers)
      if (p.is_object() && p.value("id", std::string()) == row.value("provider", std::string())) return p.value("tier", std::string());
    return {};
  };
  auto tier_rank = [&](const json& row) -> std::size_t {
    const std::string tier = tier_of(row);
    for (std::size_t i = 0; i < allowed_tiers.size(); ++i)
      if (allowed_tiers[i] == tier) return i;
    return allowed_tiers.size();
  };
  std::stable_sort(table.begin(), table.end(), [&](const json& a, const json& b) {
    const auto ta = tier_rank(a), tb = tier_rank(b);
    if (ta != tb) return ta < tb;
    return a.value("quality", 0.0) > b.value("quality", 0.0);
  });
  json skipped = json::array();
  for (const auto& row : table) {
    const std::string pid = row.value("provider", std::string());
    const json* prov = nullptr;
    for (const auto& p : providers)
      if (p.is_object() && p.value("id", std::string()) == pid) prov = &p;
    if (!prov) { skipped.push_back({{"provider", pid}, {"why", "no_provider_entry"}}); continue; }
    if (tier_rank(row) >= allowed_tiers.size()) { skipped.push_back({{"provider", pid}, {"why", "tier_not_allowed"}, {"tier", prov->value("tier", std::string())}}); continue; }
    if (!prov->value("enabled", false)) { skipped.push_back({{"provider", pid}, {"why", "disabled"}}); continue; }
    if (blocked) {
      if (const std::string b = blocked(*prov); !b.empty()) { skipped.push_back({{"provider", pid}, {"why", b}}); continue; }
    }
    const std::string when = prov->value("active_when", std::string("key_present"));
    const std::string env = prov->value("auth_env", std::string());
    std::string key;
    std::string base_url = prov->value("base_url", std::string());
    const json terms = prov->value("data_terms", json::object());
    bool ok = true;
    for (auto it = require.begin(); it != require.end(); ++it)
      if (!terms.contains(it.key()) || terms[it.key()] != it.value()) ok = false;
    if (!ok) { skipped.push_back({{"provider", pid}, {"why", "data_terms_require"}}); continue; }
    if (when == "key_present") {
      auto it = keys.find(env);
      if (it == keys.end()) { skipped.push_back({{"provider", pid}, {"why", "no_key"}, {"key_name", env}}); continue; }
      key = it->second;
    } else if (when != "none") {
      const std::string why = resolve ? resolve(*prov, key, base_url) : when;
      if (!why.empty()) { skipped.push_back({{"provider", pid}, {"why", why}}); continue; }
    }
    if (!key.empty()) overlay[prov->value("auth_secret", std::string("api_key"))] = key;
    return {{"provider", pid}, {"display_name", prov->value("display_name", pid)}, {"model", row.value("model", std::string())},
            {"base_url", base_url}, {"data_terms", terms}, {"quality", row.value("quality", json(nullptr))},
            {"auth_kind", prov->value("auth_kind", std::string("key"))}, {"tier", prov->value("tier", std::string())},
            {"observed_at", row.value("observed_at", json(nullptr))}, {"source", row.value("source", json(nullptr))},
            {"why", "best_in_table"}, {"skipped", skipped}};
  }
  // No source: an explicit state with a plain message (data: free_model_choice.none_message, open map of languages)
  // that leads to the person's own account or key (R108) - never a paid source in its place.
  return {{"provider", nullptr}, {"why", "no_free_provider_active"}, {"state", "none"},
          {"message", choice.value("none_message", json::object())}, {"skipped", skipped}};
}
}  // namespace loom_server
