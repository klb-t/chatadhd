// loom/accounts.h — server accounts (P1): users, Argon2id password hashes, opaque session tokens.
//
// Storage: <accounts_root>/<accounts_file> (SQLite), separate from every per-user data directory.
// Each user owns <accounts_root>/<users_subdir>/<user id>/ (KOD != DANE: that is a normal Loom data dir).
// All tunables (registration mode, session lifetime, Argon2id preset, cookie flags) come from the
// server_accounts runtime profile; nothing is a constant here. Session tokens are random and only their
// SHA-256 is stored. Password hashes are self-describing ("argon2id$v=19$m=..,t=..,p=..$salt$hash"),
// so changing the preset never invalidates stored passwords.
#pragma once

#include <cstdint>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "loom/crypto_suite.h"
#include "loom/result.h"
#include "loom/sqlite.h"
#include "loom/util/json.h"

namespace loom::accounts {

struct CookieSettings {
  std::string name, same_site, secure, path;
  bool http_only = true;
};

struct ThrottleLimit {
  std::int64_t free_attempts = 0, base_delay_seconds = 0, max_delay_seconds = 0;
};
struct Throttle {
  bool on = true, trust_forwarded_for = false;
  std::int64_t reset_seconds = 0;
  ThrottleLimit per_login, per_ip;
};

// Isolation between accounts (R18). See the server_accounts profile for each field.
struct Isolation {
  bool confine_paths = true, block_private_hosts = true, csrf_check_origin = true, expose_server_logs = false;
  std::vector<std::string> path_roots, allow_private_hosts, csrf_allowed_origins;
  // Operator ceilings over the account's own config.json (more restrictive wins); 0 = none.
  std::int64_t zip_max_total_bytes = 0;
  double zip_max_ratio = 0;
  bool git_allow_repo_config = false;
};

// Guest sessions (R103 B1): use without an account. Every value is a preset of the server_accounts profile
// (`guests`), nothing here is a rule. A guest gets a session (same cookie as an account) and its own working data
// directory <accounts_root>/<workdir_subdir>/<guest id>/, removed after idle_ttl_seconds without a request.
struct GuestSettings {
  bool enabled = false;                       // preset false: the public address gets guests only after B2 (abuse protection)
  std::string start = "auto";                 // auto = the web starts a guest session on the first visit; explicit = a button
  std::string workdir_subdir = "guests";
  std::int64_t idle_ttl_seconds = 86400;      // 0 = never by itself
  std::int64_t sweep_interval_seconds = 60;   // how often expired guests are looked for (on requests)
  std::string keys_file;                      // provider keys of the operator (KEY=VALUE), read into memory only
  // Test hook (2026-10-29): `guests.protection.clock.file` (absent in the pack; only a test overlay sets it) names a
  // file holding one number of seconds that is "now" for the protection buckets and challenges (guest_guard.h) and, from
  // here, for the guest sessions (start, idle TTL, lifetime, sweep, live count) and the server's guest timing (sweep
  // schedule, service-account token refresh, provider cool-downs). Empty = the real clocks (production unchanged).
  std::string clock_file;
  Json raw = Json::object();                  // the whole `guests` object (notice, workdir, model_sources ...) for the web
};

struct Settings {
  std::string registration;  // open | invite | closed
  std::int64_t session_lifetime_seconds = 0;
  std::int64_t session_idle_seconds = 0;  // 0 = none
  std::int64_t password_min_length = 0;
  std::string argon2id_preset;
  cs::Argon2Params argon2;  // resolved from storage_security
  std::size_t session_token_bytes = 0;
  std::string accounts_file, users_subdir;
  CookieSettings cookie;
  Throttle throttle;
  Isolation isolation;
  // R20: bound on simultaneous Argon2id evaluations (each holds its full memory). -1 = CPU count, 0 = unlimited.
  std::int64_t hash_max_concurrent = -1, hash_wait_seconds = 5;
  std::int64_t login_max_length = 128;  // R20: characters; 0 = no limit
  GuestSettings guests;
};

// Effective server_accounts profile (+ <root>/profiles overlay) and the named Argon2id preset.
Result<Settings> load_settings(const std::filesystem::path& accounts_root);

struct User {
  std::string id, login, created;
  bool disabled = false;
};

struct Session {
  std::string token;  // raw token, returned ONCE by create_session
  std::string user_id;
  std::int64_t expires = 0;  // unix seconds
};

struct GuestSession {
  std::string token;  // raw token, returned ONCE by create_guest_session
  std::string guest_id;
  std::int64_t started = 0, expires = 0;  // unix seconds; expires = the idle deadline at creation
};
struct Guest {
  std::string id;
  std::int64_t started = 0, last_seen = 0;
};

class AccountStore {
 public:
  static Result<std::unique_ptr<AccountStore>> open(const std::filesystem::path& root, Settings settings);
  const Settings& settings() const noexcept { return settings_; }
  const std::filesystem::path& root() const noexcept { return root_; }
  std::filesystem::path user_dir(const std::string& user_id) const;

  // With a non-empty `invite` the code is checked and consumed in the same transaction as the insert,
  // so a failed creation never burns it (Errc::Auth for a bad/used code).
  Result<User> create_user(const std::string& login, const std::string& password, const std::string& invite = {});
  Result<std::optional<User>> find_user(const std::string& login);
  Result<std::vector<User>> list_users();
  Result<std::int64_t> user_count();
  // Errc::Auth for an unknown login, a wrong password or a disabled account (one message for all three).
  // True when the login exceeds settings().login_max_length characters (0 = no limit).
  bool login_too_long(const std::string& login) const;
  Result<User> verify_password(const std::string& login, const std::string& password);
  Status set_password(const std::string& login, const std::string& password);
  // Disabling also revokes every session of the user.
  Status set_disabled(const std::string& login, bool disabled);

  Result<Session> create_session(const std::string& user_id);
  // Valid, unexpired session of an enabled user; refreshes last_seen.
  Result<std::optional<User>> find_session(const std::string& token);
  Status revoke_session(const std::string& token);
  Status revoke_user_sessions(const std::string& user_id);

  // Login throttling (settings().throttle). Seconds the caller must wait before another attempt for this
  // login / client address (0 = may try). Call record_failure after a failed attempt, record_success after
  // a good one (clears the login's counter).
  std::int64_t throttle_wait(const std::string& login, const std::string& client);
  void record_failure(const std::string& login, const std::string& client);
  void record_success(const std::string& login);

  // Guests (R103 B1). A guest session lives in its own table (token stored only as SHA-256, like account
  // sessions); find_guest_session refreshes last_seen and drops a session idle longer than guests.idle_ttl_seconds
  // or older than session_lifetime_seconds.
  Result<GuestSession> create_guest_session();
  Result<std::optional<Guest>> find_guest_session(const std::string& token);
  Status revoke_guest_session(const std::string& token);
  std::filesystem::path guest_dir(const std::string& guest_id) const;
  std::filesystem::path guests_root() const;
  // Deletes the expired guest sessions and returns the ids whose working directory must go: those just expired and
  // every directory under guests_root() without a live session (left by a restart). The caller closes their runtimes
  // first and then removes the directories (remove_guest_dir).
  Result<std::vector<std::string>> sweep_guests();
  // R103 B2: guest sessions alive now (not expired, not idle past the TTL) - the load measure of the guest protection.
  Result<std::int64_t> live_guest_count();
  // R103 B2: true when `code` is an unused invitation (checked, not consumed) - an invited guest skips the proof of work.
  Result<bool> invite_valid(const std::string& code);
  Status remove_guest_dir(const std::string& guest_id);
  // "Now" in unix seconds for everything about guests: guests.clock.file when set (an unreadable file keeps the last
  // value read; never read yet = the real clock), else the real clock.
  std::int64_t guest_now() const;
  static bool valid_guest_id(const std::string& id);

  // registration=invite: a code is shown once and stored only as SHA-256; consume = single use.
  Result<std::string> create_invite();

 private:
  AccountStore() = default;
  Result<std::string> hash_password(const std::string& password);
  Status check_password_policy(const std::string& password) const;

  std::filesystem::path root_;
  Settings settings_;
  sql::Connection db_;
  std::mutex mu_;
  // Slots for Argon2id evaluations (settings_.hash_max_concurrent); see HashSlot in accounts.cpp.
  std::mutex hash_mu_;
  std::condition_variable hash_cv_;
  std::int64_t hash_active_ = 0;
  mutable std::mutex clock_mu_;
  mutable std::int64_t clock_last_ = -1;
  friend class HashSlot;
  struct Fails { std::int64_t count = 0, last = 0; };
  std::mutex tmu_;
  std::map<std::string, Fails> login_fails_, ip_fails_;
};

}  // namespace loom::accounts
