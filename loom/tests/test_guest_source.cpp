// Guest model source selection (R104, R108, R111-R113): a guest without an account is given only cost-free sources.
// Synthetic data on purpose (generated provider tables, fake key values, a fake resolver); no real key, no network.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include "../server/src/guest_source.h"

using namespace loom_server;

namespace {
json preset_pools() {
  return {{"cost_free", {{"model_source", "free"}, {"tiers", {"free"}}, {"audience", {"guest", "account"}}}},
          {"paid", {{"model_source", "paid"}, {"tiers", {"paid_credits"}}, {"audience", {"account_trial"}}}}};
}
json prov(const std::string& id, const std::string& tier, const std::string& when, const std::string& env, bool enabled = true) {
  return {{"id", id}, {"tier", tier}, {"active_when", when}, {"auth_env", env}, {"enabled", enabled},
          {"base_url", "http://127.0.0.1/" + id}, {"data_terms", {{"training", "no"}}}};
}
struct KeysFile {
  std::filesystem::path p;
  explicit KeysFile(const std::string& text) {
    static int n = 0;
    p = std::filesystem::temp_directory_path() / ("loom-guest-keys-" + std::to_string(::getpid()) + "-" + std::to_string(n++) + ".env");
    std::ofstream(p) << text;
  }
  ~KeysFile() { std::error_code ec; std::filesystem::remove(p, ec); }
};
const SourceResolver kVertexOk = [](const json&, std::string& key, std::string&) -> std::string { key = "fake-token"; return {}; };
}  // namespace

TEST_CASE("guest without an own key gets the free provider, not the paid pool, even when the paid one ranks higher") {
  KeysFile keys("GROQ_API_KEY=fake-groq\n");
  const json fp = {{"source_pools", preset_pools()},
                   {"providers", {prov("vertex_gemini", "paid_credits", "service_account", ""), prov("groq", "free", "key_present", "GROQ_API_KEY")}},
                   {"free_model_choice", {{"table", {{{"provider", "vertex_gemini"}, {"model", "m1"}, {"quality", 0.99}},
                                                      {{"provider", "groq"}, {"model", "m2"}, {"quality", 0.1}}}},
                                          {"tier_order", {"paid_credits", "free"}}}}};
  const auto tiers = guest_allowed_tiers(fp, json::array({"byok", "free", "local"}));
  CHECK(tiers == std::vector<std::string>{"free"});
  std::map<std::string, std::string> overlay;
  const json r = choose_free_model(fp, keys.p.string(), tiers, overlay, kVertexOk);
  CHECK(r["provider"] == "groq");
  CHECK(overlay.count("api_key") == 1);
  bool vertex_skipped = false;
  for (const auto& s : r["skipped"]) if (s["provider"] == "vertex_gemini" && s["why"] == "tier_not_allowed") vertex_skipped = true;
  CHECK(vertex_skipped);
}

TEST_CASE("no free key: explicit none state with a message, the paid source is not taken in its place") {
  KeysFile keys("# no keys\n");
  const json fp = {{"source_pools", preset_pools()},
                   {"providers", {prov("vertex_gemini", "paid_credits", "service_account", ""), prov("groq", "free", "key_present", "GROQ_API_KEY")}},
                   {"free_model_choice", {{"table", {{{"provider", "vertex_gemini"}, {"model", "m1"}, {"quality", 0.9}},
                                                      {{"provider", "groq"}, {"model", "m2"}, {"quality", 0.5}}}},
                                          {"none_message", {{"pl", "brak"}, {"en", "none"}}}}}};
  std::map<std::string, std::string> overlay;
  const json r = choose_free_model(fp, keys.p.string(), guest_allowed_tiers(fp, json::array({"byok", "free", "local"})), overlay, kVertexOk);
  CHECK(r["provider"].is_null());
  CHECK(r["state"] == "none");
  CHECK(r["message"]["pl"] == "brak");
  CHECK(overlay.empty());
}

TEST_CASE("model_sources is read: without 'free' a guest gets nothing; 'paid' and unknown names never serve a guest") {
  const json fp = {{"source_pools", preset_pools()}};
  CHECK(guest_allowed_tiers(fp, json::array({"byok", "local"})).empty());
  CHECK(guest_allowed_tiers(fp, json::array({"paid"})).empty());
  CHECK(guest_allowed_tiers(fp, json::array({"pool", 7, nullptr, "paid", "free"})) == std::vector<std::string>{"free"});
  CHECK(guest_allowed_tiers(fp, json::array({"paid"}), "account_trial") == std::vector<std::string>{"paid_credits"});
  CHECK(guest_allowed_tiers(fp, json::array({"free"}), "account_trial").empty());  // the trial audience needs the trial pool listed
}

TEST_CASE("generative: a guest is never given a tier outside the cost-free pool, in any table/key/budget/source-list state") {
  std::mt19937 rng(20261030);
  auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
  const std::vector<std::string> tiers = {"free", "paid_credits", "paid", "premium", "", "Free", "paid_credits "};
  const std::vector<std::string> whens = {"key_present", "none", "service_account", "weird"};
  const std::vector<std::string> names = {"byok", "free", "local", "paid", "pool", "credits", ""};
  int chosen_free = 0, chosen_none = 0;
  for (int iter = 0; iter < 400; ++iter) {
    json providers = json::array(), table = json::array();
    std::string keytext;
    std::map<std::string, std::string> tier_of;
    const std::size_t np = 1 + pick(6);
    for (std::size_t i = 0; i < np; ++i) {
      const std::string id = "p" + std::to_string(i), env = "KEY_" + std::to_string(i);
      const std::string t = tiers[pick(tiers.size())];
      tier_of[id] = t;
      providers.push_back(prov(id, t, whens[pick(whens.size())], pick(2) ? env : "", pick(4) != 0));
      if (pick(2)) keytext += env + "=fake-" + std::to_string(i) + "\n";
      for (std::size_t k = 0, nk = 1 + pick(2); k < nk; ++k)
        table.push_back({{"provider", id}, {"model", "m" + std::to_string(i)}, {"quality", (pick(100)) / 100.0}});
    }
    json sources = json::array();
    for (std::size_t i = 0, n = pick(6); i < n; ++i) sources.push_back(names[pick(names.size())]);
    json order = json::array();
    for (std::size_t i = 0, n = pick(4); i < n; ++i) order.push_back(tiers[pick(tiers.size())]);
    json choice = {{"table", table}, {"tier_order", order}};
    if (pick(2)) choice["require"] = {{"training", "no"}};
    const json fp = {{"source_pools", preset_pools()}, {"providers", providers}, {"free_model_choice", choice}};
    KeysFile keys(keytext);
    const bool block_all = pick(3) == 0;  // a budget / cool-down state
    const SourceResolver resolve = [&](const json&, std::string& key, std::string&) -> std::string {
      if (pick(2)) return "no_service_account";
      key = "fake-token";
      return {};
    };
    const auto blocked = [&](const json& p) -> std::string { return block_all && p["id"] == "p0" ? "budget_day" : ""; };
    std::map<std::string, std::string> overlay;
    const json r = choose_free_model(fp, keys.p.string(), guest_allowed_tiers(fp, sources), overlay, resolve, blocked);
    if (r["provider"].is_null()) {
      ++chosen_none;
      CHECK(r["state"] == "none");
      CHECK(overlay.empty());
    } else {
      ++chosen_free;
      CHECK(tier_of[r["provider"].get<std::string>()] == "free");
      CHECK(r["tier"] == "free");
      bool listed_free = false;
      for (const auto& s : sources) if (s == "free") listed_free = true;
      CHECK(listed_free);
    }
  }
  CHECK(chosen_free > 20);  // the generator really reaches both outcomes
  CHECK(chosen_none > 20);
}

TEST_CASE("an operator pool table without source_pools still gives a guest only the tier 'free'") {
  KeysFile keys("A=1\nB=2\n");
  const json fp = {{"providers", {prov("a", "paid_credits", "key_present", "A"), prov("b", "free", "key_present", "B")}},
                   {"free_model_choice", {{"table", {{{"provider", "a"}, {"model", "x"}, {"quality", 1.0}},
                                                      {{"provider", "b"}, {"model", "y"}, {"quality", 0.1}}}}}}};
  std::map<std::string, std::string> overlay;
  const json r = choose_free_model(fp, keys.p.string(), guest_allowed_tiers(fp, json::array({"free"})), overlay);
  CHECK(r["provider"] == "b");
}
