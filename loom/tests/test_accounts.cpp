#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "loom/accounts.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
accounts::Settings fast_settings(const std::filesystem::path& root) {
  auto s = unwrap(accounts::load_settings(root));
  s.argon2 = {1, 64, 1};  // test-only cost; the real value comes from the preset
  return s;
}
}  // namespace

TEST_SUITE("accounts") {
  TEST_CASE("settings come from the pack: closed registration, named Argon2id preset") {
    fsutil::TempDir td;
    auto s = unwrap(accounts::load_settings(td.path()));
    CHECK(s.registration == "closed");
    CHECK(s.cookie.same_site == "Strict");
    CHECK(s.cookie.http_only);
    CHECK(s.argon2.m_kib == 65536);  // rfc9106_second preset
  }

  TEST_CASE("users, passwords, sessions") {
    fsutil::TempDir td;
    auto store = unwrap(accounts::AccountStore::open(td.path(), fast_settings(td.path())));
    auto a = unwrap(store->create_user("Alice", "correct horse"));
    CHECK(a.id.rfind("u_", 0) == 0);
    CHECK(a.id.size() == 14);
    CHECK(a.login == "alice");
    CHECK(std::filesystem::is_directory(store->user_dir(a.id)));
    CHECK_FALSE(store->create_user("ALICE", "another one 1"));
    CHECK_FALSE(store->create_user("bob", "short"));

    CHECK(unwrap(store->verify_password("alice", "correct horse")).id == a.id);
    CHECK_FALSE(store->verify_password("alice", "wrong password"));
    CHECK_FALSE(store->verify_password("nobody", "correct horse"));

    auto s = unwrap(store->create_session(a.id));
    CHECK(s.token.size() == 64);
    CHECK(unwrap(store->find_session(s.token))->id == a.id);
    CHECK_FALSE(unwrap(store->find_session("deadbeef")).has_value());
    unwrap(store->revoke_session(s.token));
    CHECK_FALSE(unwrap(store->find_session(s.token)).has_value());

    // the token itself is never stored
    auto s2 = unwrap(store->create_session(a.id));
    std::string raw = unwrap(fsutil::read_file(td.path() / store->settings().accounts_file));
    CHECK(raw.find(s2.token) == std::string::npos);

    unwrap(store->set_password("alice", "new password!"));
    CHECK_FALSE(store->verify_password("alice", "correct horse"));
    CHECK(store->verify_password("alice", "new password!"));

    unwrap(store->set_disabled("alice", true));
    CHECK_FALSE(unwrap(store->find_session(s2.token)).has_value());
    CHECK_FALSE(store->verify_password("alice", "new password!"));
    unwrap(store->set_disabled("alice", false));
    CHECK(store->verify_password("alice", "new password!"));
  }

  TEST_CASE("session expiry and invites") {
    fsutil::TempDir td;
    auto cfg = fast_settings(td.path());
    cfg.session_lifetime_seconds = 1;
    auto store = unwrap(accounts::AccountStore::open(td.path(), cfg));
    auto u = unwrap(store->create_user("carol", "passw0rd!"));
    auto s = unwrap(store->create_session(u.id));
    CHECK(unwrap(store->find_session(s.token)).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    CHECK_FALSE(unwrap(store->find_session(s.token)).has_value());

    auto code = unwrap(store->create_invite());
    unwrap(store->consume_invite(code, "carol"));
    CHECK_FALSE(store->consume_invite(code, "carol"));
    CHECK_FALSE(store->consume_invite("nope", "carol"));
  }
}
