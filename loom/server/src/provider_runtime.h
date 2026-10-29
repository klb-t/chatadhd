// provider_runtime.h — R103 B3: what the server keeps for the guests' model sources while it runs.
//
//  * MetadataTokens: a short-lived OAuth access token of the machine's service account, read from the metadata
//    server (Google Compute Engine: /computeMetadata/v1/instance/service-accounts/default/token, header
//    "Metadata-Flavor: Google"). Kept in memory only, refreshed `refresh_before` seconds before it expires; never
//    written to a file, a log or an answer. The metadata address is data (provider entry auth.metadata_base) so a
//    test points it at a local fake.
//  * SpendLedger: per provider and UTC day, the tokens and the money spent on the operator's paid sources (from the
//    provider's usage × the dated price in the provider entry). A small JSON file in the accounts root (no content,
//    only numbers), so a restart does not reset the day's budget.
//  * Exhaustion: a provider that answered with an exhaustion marker (429, quota) is skipped until a cool-down ends
//    (R103 §4.4); the guest gets the next source by fallback order and is told so.
#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace loom_server {

inline std::int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string utc_day(std::int64_t t) {
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char buf[16];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
  return buf;
}

class MetadataTokens {
 public:
  struct Token {
    std::string value;
    std::int64_t expires_at = 0;
    std::string project;
  };

  // why (optional) receives a short reason without any secret when no token is available.
  std::optional<Token> get(const std::string& base, std::int64_t refresh_before, std::string* why = nullptr) {
    std::lock_guard lk(mu_);
    const std::int64_t now = unix_now();
    auto& t = cache_[base];
    if (!t.value.empty() && t.expires_at - now > refresh_before) return t;
    if (fail_until_[base] > now) {
      if (!t.value.empty() && t.expires_at > now + 30) return t;
      if (why) *why = last_why_[base];
      return std::nullopt;
    }
    httplib::Client cli(base);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(5, 0);
    const httplib::Headers h{{"Metadata-Flavor", "Google"}};
    std::string reason;
    auto r = cli.Get("/computeMetadata/v1/instance/service-accounts/default/token", h);
    if (!r) reason = "metadata_unreachable";
    else if (r->status != 200) reason = "metadata_status_" + std::to_string(r->status);
    else {
      const auto j = nlohmann::json::parse(r->body, nullptr, false);
      if (!j.is_object() || !j.contains("access_token") || !j["access_token"].is_string()) reason = "metadata_bad_token";
      else {
        Token fresh;
        fresh.value = j["access_token"].get<std::string>();
        fresh.expires_at = now + (j.contains("expires_in") && j["expires_in"].is_number() ? j["expires_in"].get<std::int64_t>() : 300);
        fresh.project = t.project;
        if (fresh.project.empty()) {
          auto p = cli.Get("/computeMetadata/v1/project/project-id", h);
          if (p && p->status == 200) fresh.project = p->body;
        }
        ++refreshes_;
        t = fresh;
        return t;
      }
    }
    fail_until_[base] = now + 30;
    last_why_[base] = reason;
    if (!t.value.empty() && t.expires_at > now + 30) return t;
    if (why) *why = reason;
    return std::nullopt;
  }

  std::int64_t refreshes() {
    std::lock_guard lk(mu_);
    return refreshes_;
  }

 private:
  std::mutex mu_;
  std::map<std::string, Token> cache_;
  std::map<std::string, std::int64_t> fail_until_;
  std::map<std::string, std::string> last_why_;
  std::int64_t refreshes_ = 0;
};

class SpendLedger {
 public:
  void open(std::filesystem::path file) {
    std::lock_guard lk(mu_);
    file_ = std::move(file);
    std::ifstream in(file_);
    if (in) state_ = nlohmann::json::parse(in, nullptr, false);
    if (!state_.is_object()) state_ = nlohmann::json::object();
  }

  // One answer of a paid source: tokens from the provider's usage, money from the entry's dated price.
  void charge(const std::string& provider, double in_tokens, double out_tokens, double eur) {
    std::lock_guard lk(mu_);
    auto& p = day_entry(provider);
    p["requests"] = p.value("requests", 0) + 1;
    p["input_tokens"] = p.value("input_tokens", 0.0) + in_tokens;
    p["output_tokens"] = p.value("output_tokens", 0.0) + out_tokens;
    p["eur"] = p.value("eur", 0.0) + eur;
    auto& tot = state_["total"][provider];
    if (!tot.is_object()) tot = nlohmann::json::object();
    tot["eur"] = tot.value("eur", 0.0) + eur;
    tot["requests"] = tot.value("requests", 0) + 1;
    save();
  }

  double eur_today(const std::string& provider) {
    std::lock_guard lk(mu_);
    return day_entry(provider).value("eur", 0.0);
  }
  double eur_total(const std::string& provider) {
    std::lock_guard lk(mu_);
    const auto& tot = state_["total"][provider];
    return tot.is_object() ? tot.value("eur", 0.0) : 0.0;
  }
  nlohmann::json snapshot() {
    std::lock_guard lk(mu_);
    roll();
    return state_;
  }

 private:
  void roll() {
    const std::string d = utc_day(unix_now());
    if (state_.value("day", std::string()) != d) {
      state_["day"] = d;
      state_["providers"] = nlohmann::json::object();
    }
    if (!state_.contains("total") || !state_["total"].is_object()) state_["total"] = nlohmann::json::object();
  }
  nlohmann::json& day_entry(const std::string& provider) {
    roll();
    auto& p = state_["providers"][provider];
    if (!p.is_object()) p = nlohmann::json::object();
    return p;
  }
  void save() {
    if (file_.empty()) return;
    std::error_code ec;
    const auto tmp = file_.string() + ".tmp";
    {
      std::ofstream out(tmp, std::ios::trunc);
      if (!out) return;
      out << state_.dump(2);
    }
    std::filesystem::rename(tmp, file_, ec);
  }

  std::mutex mu_;
  std::filesystem::path file_;
  nlohmann::json state_ = nlohmann::json::object();
};

class ProviderCooldowns {
 public:
  void exhaust(const std::string& provider, std::int64_t seconds, const std::string& why) {
    std::lock_guard lk(mu_);
    until_[provider] = unix_now() + seconds;
    why_[provider] = why;
  }
  // "" when usable now, else the reason.
  std::string blocked(const std::string& provider) {
    std::lock_guard lk(mu_);
    auto it = until_.find(provider);
    if (it == until_.end() || it->second <= unix_now()) return {};
    return why_[provider];
  }
  nlohmann::json snapshot() {
    std::lock_guard lk(mu_);
    nlohmann::json out = nlohmann::json::object();
    const auto now = unix_now();
    for (const auto& [p, u] : until_)
      if (u > now) out[p] = {{"why", why_[p]}, {"seconds_left", u - now}};
    return out;
  }

 private:
  std::mutex mu_;
  std::map<std::string, std::int64_t> until_;
  std::map<std::string, std::string> why_;
};

}  // namespace loom_server
