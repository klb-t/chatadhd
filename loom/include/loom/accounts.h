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

  // registration=invite: a code is shown once and stored only as SHA-256; consume = single use.
  Result<std::string> create_invite();

 private:
  AccountStore() = default;
  Result<std::string> hash_password(const std::string& password) const;
  Status check_password_policy(const std::string& password) const;

  std::filesystem::path root_;
  Settings settings_;
  sql::Connection db_;
  std::mutex mu_;
  struct Fails { std::int64_t count = 0, last = 0; };
  std::mutex tmu_;
  std::map<std::string, Fails> login_fails_, ip_fails_;
};

}  // namespace loom::accounts
