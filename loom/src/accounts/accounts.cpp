// Server account store (see loom/accounts.h).
#include "loom/accounts.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <thread>

#include "loom/runtime_profile.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/random.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::accounts {
namespace {

std::int64_t now_s() { return timeutil::unix_millis() / 1000; }

bool ct_equal(const std::string& a, const std::string& b) {
  unsigned char d = a.size() != b.size();
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
  return d == 0;
}

std::string token_hash(const std::string& token) { return Sha256::hex(token); }

std::string normalize_login(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

Status valid_login(const std::string& login, std::int64_t max_chars) {
  if (max_chars > 0) {
    std::int64_t chars = 0;
    for (unsigned char c : login) chars += (c & 0xC0) != 0x80;  // UTF-8 lead bytes
    if (chars > max_chars) return Error(Errc::InvalidArgument, "login is longer than " + std::to_string(max_chars) + " characters");
  }
  if (login.empty()) return Error(Errc::InvalidArgument, "login must not be empty");
  for (unsigned char c : login)
    if (c < 0x20 || c == 0x7f) return Error(Errc::InvalidArgument, "login contains control characters");
  return {};
}

User read_user(sql::Stmt& st) {
  User u;
  u.id = st.get_text(0);
  u.login = st.get_text(1);
  u.created = st.get_text(2);
  u.disabled = st.get_int(3) != 0;
  return u;
}
constexpr const char* kUserCols = "id, login, created, disabled";

std::string b64(const std::string& raw) { return base64::encode(raw); }

}  // namespace

Result<Settings> load_settings(const std::filesystem::path& root) {
  auto prof = RuntimeProfile::load("server_accounts", root);
  if (!prof) return prof.error();
  const Json& v = prof->values();
  Settings s;
  s.registration = v.value("registration", std::string());
  s.session_lifetime_seconds = v.value("session_lifetime_seconds", std::int64_t(0));
  s.session_idle_seconds = v.value("session_idle_seconds", std::int64_t(0));
  s.password_min_length = v.value("password_min_length", std::int64_t(0));
  s.argon2id_preset = v.value("argon2id_preset", std::string());
  s.login_max_length = v.value("login_max_length", std::int64_t(128));
  s.session_token_bytes = v.value("session_token_bytes", std::uint64_t(0));
  s.accounts_file = v.value("accounts_file", std::string());
  s.users_subdir = v.value("users_subdir", std::string());
  const Json& c = v.at("cookie");
  s.cookie.name = c.value("name", std::string());
  s.cookie.http_only = c.value("http_only", true);
  s.cookie.same_site = c.value("same_site", std::string());
  s.cookie.secure = c.value("secure", std::string());
  s.cookie.path = c.value("path", std::string());
  const Json& t = v.at("throttle");
  s.throttle.on = t.value("mode", std::string("on")) == "on";
  s.throttle.reset_seconds = t.value("reset_seconds", std::int64_t(0));
  s.throttle.trust_forwarded_for = t.value("trust_forwarded_for", false);
  auto lim = [&](const char* k, ThrottleLimit& l) {
    const Json& j = t.at(k);
    l.free_attempts = j.value("free_attempts", std::int64_t(0));
    l.base_delay_seconds = j.value("base_delay_seconds", std::int64_t(0));
    l.max_delay_seconds = j.value("max_delay_seconds", std::int64_t(0));
  };
  lim("per_login", s.throttle.per_login);
  lim("per_ip", s.throttle.per_ip);
  auto strings = [](const Json& j, const char* k) {
    std::vector<std::string> out;
    if (j.contains(k) && j[k].is_array())
      for (const auto& e : j[k])
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
  };
  s.isolation.confine_paths = v.value("confine_paths", true);
  s.isolation.path_roots = strings(v, "path_roots");
  s.isolation.block_private_hosts = v.value("block_private_hosts", true);
  s.isolation.allow_private_hosts = strings(v, "allow_private_hosts");
  s.isolation.expose_server_logs = v.value("expose_server_logs", false);
  if (v.contains("zip") && v["zip"].is_object()) {
    s.isolation.zip_max_total_bytes = v["zip"].value("max_total_bytes", std::int64_t(0));
    s.isolation.zip_max_ratio = v["zip"].value("max_ratio", 0.0);
  }
  if (v.contains("git") && v["git"].is_object()) s.isolation.git_allow_repo_config = v["git"].value("allow_repo_config", false);
  if (v.contains("csrf") && v["csrf"].is_object()) {
    s.isolation.csrf_check_origin = v["csrf"].value("check_origin", true);
    s.isolation.csrf_allowed_origins = strings(v["csrf"], "allowed_origins");
  }
  if (v.contains("hashing") && v["hashing"].is_object()) {
    s.hash_max_concurrent = v["hashing"].value("max_concurrent", std::int64_t(-1));
    s.hash_wait_seconds = v["hashing"].value("wait_seconds", std::int64_t(5));
  }
  // File names must stay inside the accounts root.
  for (const auto& p : {s.accounts_file, s.users_subdir})
    if (p.find('/') != std::string::npos || p.find('\\') != std::string::npos || p == ".." || p == ".")
      return Error(Errc::InvalidArgument, "accounts_file/users_subdir must be a single path component");

  auto sec = RuntimeProfile::load("storage_security", root);
  if (!sec) return sec.error();
  const Json* p = &sec->values();
  for (const char* k : {"crypto", "params", "argon2id_presets"}) {
    p = p->is_object() && p->contains(k) ? &(*p)[k] : nullptr;
    if (!p) return Error(Errc::NotFound, "storage_security has no crypto.params.argon2id_presets");
  }
  if (!p->contains(s.argon2id_preset) || !(*p)[s.argon2id_preset].is_object())
    return Error(Errc::NotFound, "unknown argon2id preset: " + s.argon2id_preset);
  const Json& a = (*p)[s.argon2id_preset];
  auto u32 = [&](const char* k, std::uint32_t& out) {
    if (!a.contains(k) || !a[k].is_number_integer() || a[k].get<std::int64_t>() <= 0) return false;
    out = static_cast<std::uint32_t>(a[k].get<std::int64_t>());
    return true;
  };
  if (!u32("t", s.argon2.t) || !u32("m_kib", s.argon2.m_kib) || !u32("p", s.argon2.p))
    return Error(Errc::InvalidArgument, "argon2id preset '" + s.argon2id_preset + "' has no concrete t/m_kib/p");
  return s;
}

Result<std::unique_ptr<AccountStore>> AccountStore::open(const std::filesystem::path& root, Settings settings) {
  std::error_code ec;
  std::filesystem::create_directories(root / settings.users_subdir, ec);
  if (ec) return Error(Errc::Io, "cannot create accounts root: " + ec.message());
  std::unique_ptr<AccountStore> st(new AccountStore());
  st->root_ = root;
  st->settings_ = std::move(settings);
  auto db = sql::Connection::open(root / st->settings_.accounts_file);
  if (!db) return db.error();
  st->db_ = std::move(*db);
  LOOM_TRY(st->db_.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
                        "CREATE TABLE IF NOT EXISTS users(id TEXT PRIMARY KEY, login TEXT NOT NULL UNIQUE,"
                        " pw_hash TEXT NOT NULL, created TEXT NOT NULL, disabled INTEGER NOT NULL DEFAULT 0);"
                        "CREATE TABLE IF NOT EXISTS sessions(token_hash TEXT PRIMARY KEY, user_id TEXT NOT NULL,"
                        " created TEXT NOT NULL, last_seen INTEGER NOT NULL, expires INTEGER NOT NULL);"
                        "CREATE INDEX IF NOT EXISTS sessions_user ON sessions(user_id);"
                        "CREATE TABLE IF NOT EXISTS invites(code_hash TEXT PRIMARY KEY, created TEXT NOT NULL,"
                        " used_by TEXT);"));
  return st;
}

std::filesystem::path AccountStore::user_dir(const std::string& user_id) const {
  return root_ / settings_.users_subdir / user_id;
}

// One Argon2id evaluation's right to run (R20). Holds a slot while alive; acquire() waits up to
// hashing.wait_seconds and fails with Errc::Busy, which the server answers with 503 + Retry-After.
class HashSlot {
 public:
  HashSlot() = default;
  HashSlot(const HashSlot&) = delete;
  HashSlot& operator=(const HashSlot&) = delete;
  ~HashSlot() {
    if (!store_) return;
    { std::lock_guard lk(store_->hash_mu_); --store_->hash_active_; }
    store_->hash_cv_.notify_one();
  }
  Status acquire(AccountStore& store) {
    std::int64_t cap = store.settings_.hash_max_concurrent;
    if (cap < 0) cap = std::max<std::int64_t>(1, std::thread::hardware_concurrency());
    if (cap == 0) return {};  // unlimited
    std::unique_lock lk(store.hash_mu_);
    const auto wait = std::chrono::seconds(std::max<std::int64_t>(0, store.settings_.hash_wait_seconds));
    if (!store.hash_cv_.wait_for(lk, wait, [&] { return store.hash_active_ < cap; }))
      return Error(Errc::Busy, "the server is busy hashing passwords; retry shortly");
    ++store.hash_active_;
    store_ = &store;
    return {};
  }

 private:
  AccountStore* store_ = nullptr;
};

Result<std::string> AccountStore::hash_password(const std::string& password) {
  const auto& a = settings_.argon2;
  std::string salt = random_bytes(16);
  HashSlot slot;
  LOOM_TRY(slot.acquire(*this));
  auto h = cs::backend().argon2id(password, salt, a, 32);
  if (!h) return h.error();
  return "argon2id$v=19$m=" + std::to_string(a.m_kib) + ",t=" + std::to_string(a.t) + ",p=" + std::to_string(a.p) +
         "$" + b64(salt) + "$" + b64(*h);
}

namespace {
Result<bool> check_hash(AccountStore& store, const std::string& stored, const std::string& password) {
  // argon2id$v=19$m=..,t=..,p=..$salt$hash
  std::vector<std::string> f;
  std::size_t pos = 0;
  for (;;) {
    auto n = stored.find('$', pos);
    f.push_back(stored.substr(pos, n == std::string::npos ? n : n - pos));
    if (n == std::string::npos) break;
    pos = n + 1;
  }
  cs::Argon2Params a;
  if (f.size() != 5 || f[0] != "argon2id" ||
      std::sscanf(f[2].c_str(), "m=%u,t=%u,p=%u", &a.m_kib, &a.t, &a.p) != 3)
    return Error(Errc::Parse, "malformed password hash");
  auto salt = base64::decode(f[3]), want = base64::decode(f[4]);
  if (!salt || !want) return Error(Errc::Parse, "malformed password hash");
  HashSlot slot;
  LOOM_TRY(slot.acquire(store));
  auto got = cs::backend().argon2id(password, *salt, a, want->size());
  if (!got) return got.error();
  return ct_equal(*got, *want);
}
}  // namespace

Status AccountStore::check_password_policy(const std::string& password) const {
  if (static_cast<std::int64_t>(password.size()) < settings_.password_min_length)
    return Error(Errc::InvalidArgument,
                 "password must be at least " + std::to_string(settings_.password_min_length) + " bytes");
  return {};
}

Result<User> AccountStore::create_user(const std::string& login_in, const std::string& password,
                                       const std::string& invite) {
  const std::string login = normalize_login(login_in);
  LOOM_TRY(valid_login(login, settings_.login_max_length));
  LOOM_TRY(check_password_policy(password));
  LOOM_TRY_ASSIGN(std::string h, hash_password(password));
  std::lock_guard lk(mu_);
  LOOM_TRY_ASSIGN(auto exists, db_.query_int("SELECT 1 FROM users WHERE login=?", login));
  if (exists) return Error(Errc::AlreadyExists, "login already exists");
  User u;
  u.id = gen_id("u_");
  u.login = login;
  u.created = timeutil::utc_now_iso() + "Z";
  LOOM_TRY(db_.exec("BEGIN IMMEDIATE"));
  auto rollback = [&](Error e) { (void)db_.exec("ROLLBACK"); return e; };
  if (!invite.empty()) {
    auto st = db_.run("UPDATE invites SET used_by=? WHERE code_hash=? AND used_by IS NULL", u.id, token_hash(invite));
    if (!st) return rollback(st.error());
    if (db_.changes() == 0) return rollback(Error(Errc::Auth, "invalid or used invite code"));
  }
  auto ins = db_.run("INSERT INTO users(id, login, pw_hash, created, disabled) VALUES(?,?,?,?,0)", u.id, u.login, h,
                     u.created);
  if (!ins) return rollback(ins.error());
  std::error_code ec;
  std::filesystem::create_directories(user_dir(u.id), ec);
  if (ec) return rollback(Error(Errc::Io, "cannot create user data dir: " + ec.message()));
  LOOM_TRY(db_.exec("COMMIT"));
  return u;
}

Result<std::optional<User>> AccountStore::find_user(const std::string& login) {
  std::lock_guard lk(mu_);
  LOOM_TRY_ASSIGN(auto st, db_.prepare(std::string("SELECT ") + kUserCols + " FROM users WHERE login=?"));
  st.bind_all(normalize_login(login));
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<User>{};
  return std::optional<User>(read_user(st));
}

Result<std::vector<User>> AccountStore::list_users() {
  std::lock_guard lk(mu_);
  LOOM_TRY_ASSIGN(auto st, db_.prepare(std::string("SELECT ") + kUserCols + " FROM users ORDER BY created, id"));
  std::vector<User> out;
  for (;;) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(read_user(st));
  }
  return out;
}

Result<std::int64_t> AccountStore::user_count() {
  std::lock_guard lk(mu_);
  LOOM_TRY_ASSIGN(auto n, db_.query_int("SELECT COUNT(*) FROM users"));
  return n.value_or(0);
}

bool AccountStore::login_too_long(const std::string& login) const {
  if (settings_.login_max_length <= 0) return false;
  std::int64_t chars = 0;
  for (unsigned char c : login) chars += (c & 0xC0) != 0x80;
  return chars > settings_.login_max_length;
}

Result<User> AccountStore::verify_password(const std::string& login, const std::string& password) {
  if (login_too_long(login)) return Error(Errc::InvalidArgument, "login is too long");
  std::string stored;
  User u;
  bool found = false;
  {
    std::lock_guard lk(mu_);
    LOOM_TRY_ASSIGN(auto st, db_.prepare(std::string("SELECT ") + kUserCols + ", pw_hash FROM users WHERE login=?"));
    st.bind_all(normalize_login(login));
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (row) {
      u = read_user(st);
      stored = st.get_text(4);
      found = true;
    }
  }
  // Unknown login still pays one Argon2id evaluation so timing does not reveal which logins exist.
  if (!found) {
    LOOM_TRY_ASSIGN(stored, hash_password(password));
  }
  LOOM_TRY_ASSIGN(bool ok, check_hash(*this, stored, password));
  if (!found || !ok || u.disabled) return Error(Errc::Auth, "invalid login or password");
  return u;
}

Status AccountStore::set_password(const std::string& login, const std::string& password) {
  LOOM_TRY(check_password_policy(password));
  LOOM_TRY_ASSIGN(std::string h, hash_password(password));
  std::lock_guard lk(mu_);
  LOOM_TRY(db_.run("UPDATE users SET pw_hash=? WHERE login=?", h, normalize_login(login)));
  if (db_.changes() == 0) return Error(Errc::NotFound, "no such user");
  return {};
}

Status AccountStore::set_disabled(const std::string& login, bool disabled) {
  std::lock_guard lk(mu_);
  LOOM_TRY(db_.run("UPDATE users SET disabled=? WHERE login=?", disabled ? 1 : 0, normalize_login(login)));
  if (db_.changes() == 0) return Error(Errc::NotFound, "no such user");
  if (disabled)
    LOOM_TRY(db_.run("DELETE FROM sessions WHERE user_id=(SELECT id FROM users WHERE login=?)", normalize_login(login)));
  return {};
}

Result<Session> AccountStore::create_session(const std::string& user_id) {
  Session s;
  s.token = cs::to_hex(random_bytes(settings_.session_token_bytes));
  s.user_id = user_id;
  const std::int64_t now = now_s();
  s.expires = now + settings_.session_lifetime_seconds;
  std::lock_guard lk(mu_);
  LOOM_TRY(db_.run("DELETE FROM sessions WHERE expires < ?", now));
  LOOM_TRY(db_.run("INSERT INTO sessions(token_hash, user_id, created, last_seen, expires) VALUES(?,?,?,?,?)",
                   token_hash(s.token), user_id, timeutil::utc_now_iso() + "Z", now, s.expires));
  return s;
}

Result<std::optional<User>> AccountStore::find_session(const std::string& token) {
  if (token.empty()) return std::optional<User>{};
  std::lock_guard lk(mu_);
  const std::string th = token_hash(token);
  LOOM_TRY_ASSIGN(auto st, db_.prepare("SELECT u.id, u.login, u.created, u.disabled, s.last_seen, s.expires "
                                       "FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.token_hash=?"));
  st.bind_all(th);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<User>{};
  User u = read_user(st);
  const std::int64_t last = st.get_int(4), exp = st.get_int(5), now = now_s();
  st = sql::Stmt();
  const bool idle = settings_.session_idle_seconds > 0 && now - last > settings_.session_idle_seconds;
  if (u.disabled || exp < now || idle) {
    LOOM_TRY(db_.run("DELETE FROM sessions WHERE token_hash=?", th));
    return std::optional<User>{};
  }
  LOOM_TRY(db_.run("UPDATE sessions SET last_seen=? WHERE token_hash=?", now, th));
  return std::optional<User>(u);
}

Status AccountStore::revoke_session(const std::string& token) {
  std::lock_guard lk(mu_);
  return db_.run("DELETE FROM sessions WHERE token_hash=?", token_hash(token));
}

Status AccountStore::revoke_user_sessions(const std::string& user_id) {
  std::lock_guard lk(mu_);
  return db_.run("DELETE FROM sessions WHERE user_id=?", user_id);
}

Result<std::string> AccountStore::create_invite() {
  std::string code = cs::to_hex(random_bytes(settings_.session_token_bytes));
  std::lock_guard lk(mu_);
  LOOM_TRY(db_.run("INSERT INTO invites(code_hash, created, used_by) VALUES(?,?,NULL)", token_hash(code),
                   timeutil::utc_now_iso() + "Z"));
  return code;
}

namespace {
std::int64_t wait_for(const ThrottleLimit& l, std::int64_t count, std::int64_t last, std::int64_t now) {
  if (count <= l.free_attempts) return 0;
  std::int64_t d = l.base_delay_seconds;
  for (std::int64_t i = l.free_attempts + 1; i < count && d < l.max_delay_seconds; ++i) d *= 2;
  d = std::min(d, l.max_delay_seconds);
  return std::max<std::int64_t>(0, last + d - now);
}
}  // namespace

std::int64_t AccountStore::throttle_wait(const std::string& login, const std::string& client) {
  const auto& t = settings_.throttle;
  if (!t.on) return 0;
  std::lock_guard lk(tmu_);
  const std::int64_t now = now_s();
  std::int64_t w = 0;
  auto look = [&](std::map<std::string, Fails>& m, const std::string& key, const ThrottleLimit& l) {
    auto it = m.find(key);
    if (it == m.end()) return;
    if (now - it->second.last > t.reset_seconds) { m.erase(it); return; }
    w = std::max(w, wait_for(l, it->second.count, it->second.last, now));
  };
  look(login_fails_, normalize_login(login), t.per_login);
  look(ip_fails_, client, t.per_ip);
  return w;
}

void AccountStore::record_failure(const std::string& login, const std::string& client) {
  const auto& t = settings_.throttle;
  if (!t.on) return;
  std::lock_guard lk(tmu_);
  const std::int64_t now = now_s();
  for (auto* m : {&login_fails_, &ip_fails_})  // bounded memory: forget expired entries
    for (auto it = m->begin(); it != m->end();) it = now - it->second.last > t.reset_seconds ? m->erase(it) : std::next(it);
  for (auto [m, key] : {std::pair{&login_fails_, normalize_login(login)}, std::pair{&ip_fails_, client}}) {
    auto& f = (*m)[key];
    ++f.count;
    f.last = now;
  }
}

void AccountStore::record_success(const std::string& login) {
  std::lock_guard lk(tmu_);
  login_fails_.erase(normalize_login(login));
}

}  // namespace loom::accounts
