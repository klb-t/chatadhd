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

  Result<User> create_user(const std::string& login, const std::string& password);
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

  // registration=invite: a code is shown once and stored only as SHA-256; consume = single use.
  Result<std::string> create_invite();
  Status consume_invite(const std::string& code, const std::string& user_id);

 private:
  AccountStore() = default;
  Result<std::string> hash_password(const std::string& password) const;
  Status check_password_policy(const std::string& password) const;

  std::filesystem::path root_;
  Settings settings_;
  sql::Connection db_;
  std::mutex mu_;
};

}  // namespace loom::accounts
