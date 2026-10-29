// guest_guard.h — protection of the public guest endpoint against abuse and cost (R103 B2).
//
// Every number comes from server_accounts.pack -> guests.protection and is a preset; 0 = no limit (that mechanism is
// off). Mechanisms (an open list, design §7): proof of work when a guest session starts (stateless challenge signed
// with a per-process key, single use), token buckets per session (model requests per minute/day, output tokens per
// day, parallel answers, every API request per minute), per client address (sessions per hour, model requests per
// minute — looser, an address is a hint, not a person), global (live guest sessions, the daily ceiling of the
// operator's free-key pool). Rejections are counted per mechanism, never with content. Nothing here sees a message.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "loom/util/sha256.h"

namespace loom_server {

class GuestGuard {
 public:
  using json = nlohmann::json;

  struct Verdict {
    std::string mechanism;  // empty = admitted
    std::int64_t retry_after = 0;
    std::string scope;  // session | address | global | start
    explicit operator bool() const { return mechanism.empty(); }
  };

  void configure(const json& protection, std::int64_t sweep_interval_seconds) {
    std::lock_guard lk(mu_);
    p_ = protection.is_object() ? protection : json::object();
    sweep_interval_ = std::max<std::int64_t>(1, sweep_interval_seconds);
    std::random_device rd;
    key_.clear();
    for (int i = 0; i < 32; ++i) key_.push_back(static_cast<char>(rd() & 0xff));
  }

  // A preset under protection.<group>.<name>; missing = 0 = no limit.
  double num(const char* group, const char* name) const {
    const auto g = p_.find(group);
    if (g == p_.end() || !g->is_object()) return 0;
    const auto v = g->find(name);
    return v != g->end() && v->is_number() ? v->get<double>() : 0;
  }
  const json& presets() const { return p_; }

  // ── Proof of work ─────────────────────────────────────────────────────
  int pow_bits(std::int64_t live_sessions) const {
    std::lock_guard lk(mu_);
    const int base = static_cast<int>(num("pow", "bits"));
    if (base <= 0) return 0;
    const double cap = num("global", "live_sessions"), share = num("pow", "load_share");
    const int heavy = static_cast<int>(num("pow", "bits_under_load"));
    if (cap > 0 && share > 0 && heavy > base && static_cast<double>(live_sessions) >= cap * share) return heavy;
    return base;
  }
  bool pow_skipped_for_invited() const {
    std::lock_guard lk(mu_);
    const auto g = p_.find("pow");
    return g != p_.end() && g->is_object() && g->value("invited", std::string("off")) == "off";
  }
  // "<unix seconds>.<bits>.<16 hex random>.<16 hex mac>" — short enough that challenge:nonce fits one SHA-256 block.
  json challenge(int bits) {
    std::lock_guard lk(mu_);
    if (bits <= 0) return {{"bits", 0}};
    std::random_device rd;
    char rnd[17];
    std::snprintf(rnd, sizeof rnd, "%08x%08x", rd(), rd());
    const std::string body = std::to_string(wall()) + "." + std::to_string(bits) + "." + rnd;
    return {{"challenge", body + "." + mac(body)}, {"bits", bits}, {"algorithm", "sha256_leading_zero_bits"},
            {"input", "challenge:nonce"}, {"expires_in", static_cast<std::int64_t>(num("pow", "challenge_ttl_seconds"))}};
  }
  // Empty = valid (and now spent); otherwise the mechanism to report.
  std::string verify(const std::string& challenge, const std::string& nonce, int min_bits) {
    std::lock_guard lk(mu_);
    if (challenge.empty() || nonce.empty() || nonce.size() > 32) return "pow_required";
    const auto last = challenge.rfind('.');
    if (last == std::string::npos) return "pow_invalid";
    const std::string body = challenge.substr(0, last);
    if (mac(body) != challenge.substr(last + 1)) return "pow_invalid";
    const auto d1 = body.find('.'), d2 = body.find('.', d1 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos) return "pow_invalid";
    const std::int64_t at = std::atoll(body.substr(0, d1).c_str());
    const int bits = std::atoi(body.substr(d1 + 1, d2 - d1 - 1).c_str());
    const std::int64_t ttl = static_cast<std::int64_t>(num("pow", "challenge_ttl_seconds"));
    const std::int64_t now = wall();
    if (bits < min_bits || (ttl > 0 && now - at > ttl)) return "pow_invalid";
    for (auto it = spent_.begin(); it != spent_.end();) it = (ttl > 0 && now - it->second > ttl) ? spent_.erase(it) : std::next(it);
    if (spent_.count(challenge)) return "pow_invalid";
    const auto d = loom::Sha256::digest(challenge + ":" + nonce);
    for (int i = 0; i < bits; ++i)
      if ((d[i / 8] >> (7 - i % 8)) & 1) return "pow_invalid";
    spent_[challenge] = now;
    return {};
  }

  // ── Admission ─────────────────────────────────────────────────────────
  Verdict admit_start(const std::string& address, std::int64_t live_sessions) {
    std::lock_guard lk(mu_);
    const double live_cap = num("global", "live_sessions");
    if (live_cap > 0 && static_cast<double>(live_sessions) >= live_cap) return reject({"global_live_sessions", sweep_interval_, "global"});
    const double per_hour = num("per_address", "sessions_per_hour");
    Bucket& b = addresses_[address].sessions;
    if (const auto w = peek(b, per_hour, 3600, 1); w > 0) return reject({"address_sessions_hour", w, "address"});
    take(b, per_hour, 3600, 1);
    return {};
  }

  Verdict admit_request(const std::string& guest, const std::string& address, bool model) {
    std::lock_guard lk(mu_);
    Session& s = sessions_[guest];
    const double api = num("per_session", "api_per_minute");
    if (const auto w = peek(s.api, api, 60, 1); w > 0) return reject({"session_api_minute", w, "session"});
    if (model) {
      Address& a = addresses_[address];
      const double cm = num("per_session", "chat_per_minute"), cd = num("per_session", "chat_per_day"),
                   ot = num("per_session", "output_tokens_per_day"), ps = num("per_session", "parallel_streams"),
                   am = num("per_address", "chat_per_minute"), pc = num("global", "pool_chat_per_day"),
                   po = num("global", "pool_output_tokens_per_day");
      if (const auto w = peek(s.chat_minute, cm, 60, 1); w > 0) return reject({"session_chat_minute", w, "session"});
      if (const auto w = peek(s.chat_day, cd, 86400, 1); w > 0) return reject({"session_chat_day", w, "session"});
      if (const auto w = peek(s.output_day, ot, 86400, 1); w > 0) return reject({"session_output_tokens_day", w, "session"});
      if (ps > 0 && static_cast<double>(s.streams) >= ps) return reject({"session_parallel_streams", 1, "session"});
      if (const auto w = peek(a.chat_minute, am, 60, 1); w > 0) return reject({"address_chat_minute", w, "address"});
      if (const auto w = peek(pool_chat_, pc, 86400, 1); w > 0) return reject({"pool_chat_day", w, "global"});
      if (const auto w = peek(pool_output_, po, 86400, 1); w > 0) return reject({"pool_output_tokens_day", w, "global"});
      take(s.chat_minute, cm, 60, 1);
      take(s.chat_day, cd, 86400, 1);
      take(a.chat_minute, am, 60, 1);
      take(pool_chat_, pc, 86400, 1);
      ++s.streams;
      ++admitted_["model_requests"];
    }
    take(s.api, api, 60, 1);
    ++s.inflight;
    return {};
  }
  // The end of a request admitted above (after its answer, a stream included, was written).
  void finish(const std::string& guest, bool model) {
    std::lock_guard lk(mu_);
    auto it = sessions_.find(guest);
    if (it == sessions_.end()) return;
    if (it->second.inflight > 0) --it->second.inflight;
    if (model && it->second.streams > 0) --it->second.streams;
  }
  // Output tokens of one answer (the provider's usage, or characters / estimate_chars_per_token).
  void charge_output(const std::string& guest, double tokens) {
    std::lock_guard lk(mu_);
    if (tokens <= 0) return;
    charge(sessions_[guest].output_day, num("per_session", "output_tokens_per_day"), 86400, tokens);
    charge(pool_output_, num("global", "pool_output_tokens_per_day"), 86400, tokens);
    admitted_["output_tokens"] += static_cast<std::int64_t>(tokens);
  }
  double chars_per_token() const {
    std::lock_guard lk(mu_);
    const double c = num("per_session", "estimate_chars_per_token");
    return c > 0 ? c : 4;
  }
  std::int64_t inflight(const std::string& guest) const {
    std::lock_guard lk(mu_);
    auto it = sessions_.find(guest);
    return it == sessions_.end() ? 0 : it->second.inflight;
  }
  void forget(const std::string& guest) {
    std::lock_guard lk(mu_);
    auto it = sessions_.find(guest);
    if (it != sessions_.end() && it->second.inflight == 0) sessions_.erase(it);
  }
  // Addresses whose buckets are full again carry no information; dropping them bounds the memory.
  void prune() {
    std::lock_guard lk(mu_);
    const double now = mono();
    for (auto it = addresses_.begin(); it != addresses_.end();)
      it = (now - it->second.sessions.last > 3600 && now - it->second.chat_minute.last > 60) ? addresses_.erase(it) : std::next(it);
  }
  void count_rejection(const std::string& mechanism) {
    std::lock_guard lk(mu_);
    ++rejections_[mechanism];
  }

  json summary() const {
    std::lock_guard lk(mu_);
    json r = json::object(), a = json::object();
    for (const auto& [k, v] : rejections_) r[k] = v;
    for (const auto& [k, v] : admitted_) a[k] = v;
    return {{"rejections", r}, {"admitted", a}, {"tracked_sessions", sessions_.size()}, {"tracked_addresses", addresses_.size()},
            {"pool_today", {{"chat_requests_left", left(pool_chat_, num("global", "pool_chat_per_day"), 86400)},
                            {"output_tokens_left", left(pool_output_, num("global", "pool_output_tokens_per_day"), 86400)}}}};
  }

  // Words of a mechanism in `lang` (else en), with {{wait}} / {{limit}} filled.
  json words(const std::string& lang, const std::string& mechanism, std::int64_t wait, const std::string& limit) const {
    std::lock_guard lk(mu_);
    const json all = p_.value("words", json::object());
    std::string use = all.contains(lang) ? lang : "en";
    const json w = all.value(use, json::object());
    auto fill = [&](std::string t) {
      for (const auto& [k, v] : {std::pair<std::string, std::string>{"{{wait}}", human_wait(wait)}, {"{{limit}}", limit}})
        for (auto pos = t.find(k); pos != std::string::npos; pos = t.find(k)) t.replace(pos, k.size(), v);
      return t;
    };
    return {{"language", use}, {"text", fill(w.value(mechanism, mechanism))}, {"alternatives", w.value("alternatives", std::string())}};
  }
  static std::string human_wait(std::int64_t s) {
    if (s < 60) return std::to_string(std::max<std::int64_t>(1, s)) + " s";
    if (s < 3600) return std::to_string((s + 59) / 60) + " min";
    const std::int64_t h = s / 3600, m = (s % 3600 + 59) / 60;
    return std::to_string(h) + " h" + (m ? " " + std::to_string(m) + " min" : "");
  }

 private:
  struct Bucket { double tokens = -1, last = 0; };  // tokens < 0 = full (never used)
  struct Session { Bucket api, chat_minute, chat_day, output_day; std::int64_t streams = 0, inflight = 0; };
  struct Address { Bucket sessions, chat_minute; };

  static double mono() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static std::int64_t wall() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  }
  static void refill(Bucket& b, double cap, double period) {
    const double now = mono();
    if (b.tokens < 0 && b.last == 0) b.tokens = cap;
    else b.tokens = std::min(cap, b.tokens + (now - b.last) * cap / period);
    b.last = now;
  }
  // Seconds until `cost` tokens are there (0 = now); cap 0 = no limit.
  static std::int64_t peek(Bucket& b, double cap, double period, double cost) {
    if (cap <= 0) return 0;
    refill(b, cap, period);
    if (b.tokens >= cost) return 0;
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil((cost - b.tokens) * period / cap)));
  }
  static void take(Bucket& b, double cap, double period, double cost) {
    if (cap <= 0) return;
    refill(b, cap, period);
    b.tokens -= cost;
  }
  static void charge(Bucket& b, double cap, double period, double cost) { take(b, cap, period, cost); }
  static json left(Bucket b, double cap, double period) {
    if (cap <= 0) return nullptr;  // no limit
    refill(b, cap, period);
    return static_cast<std::int64_t>(std::max(0.0, b.tokens));
  }
  Verdict reject(Verdict v) {
    ++rejections_[v.mechanism];
    return v;
  }
  std::string mac(const std::string& body) const {
    // HMAC-SHA-256 with the per-process key, first 8 bytes in hex.
    std::string k = key_;
    k.resize(64, '\0');
    std::string ipad(64, '\0'), opad(64, '\0');
    for (int i = 0; i < 64; ++i) { ipad[i] = static_cast<char>(k[i] ^ 0x36); opad[i] = static_cast<char>(k[i] ^ 0x5c); }
    const auto inner = loom::Sha256::digest(ipad + body);
    const auto outer = loom::Sha256::digest(opad + std::string(reinterpret_cast<const char*>(inner.data()), inner.size()));
    return loom::to_hex(outer.data(), 8);
  }

  mutable std::mutex mu_;
  json p_ = json::object();
  std::int64_t sweep_interval_ = 60;
  std::string key_;
  std::unordered_map<std::string, Session> sessions_;
  std::unordered_map<std::string, Address> addresses_;
  std::unordered_map<std::string, std::int64_t> spent_;
  Bucket pool_chat_, pool_output_;
  std::map<std::string, std::int64_t> rejections_, admitted_;
};

}  // namespace loom_server
