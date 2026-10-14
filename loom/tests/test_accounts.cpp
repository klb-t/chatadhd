#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <vector>
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

  TEST_CASE("login length is a setting (preset 128 characters, 0 = no limit)") {
    fsutil::TempDir td;
    auto cfg = fast_settings(td.path());
    CHECK(cfg.login_max_length == 128);
    cfg.login_max_length = 5;
    auto store = unwrap(accounts::AccountStore::open(td.path(), cfg));
    CHECK(store->create_user("abcde", "correct horse"));
    CHECK_FALSE(store->create_user("abcdef", "correct horse"));
    CHECK(store->create_user("\xC5\xBC\xC3\xB3\xC5\x82w", "correct horse"));  // 4 characters, 7 bytes
    CHECK(store->login_too_long("abcdef"));
    CHECK_FALSE(store->verify_password("abcdef", "correct horse"));
    CHECK(store->verify_password("abcde", "correct horse"));
    auto cfg0 = fast_settings(td.path());
    cfg0.login_max_length = 0;
    auto open = unwrap(accounts::AccountStore::open(td.path() / "x", cfg0));
    CHECK(open->create_user(std::string(1000, 'a'), "correct horse"));
  }

  TEST_CASE("concurrent Argon2id hashes are bounded (hashing.max_concurrent), excess gets Busy") {
    fsutil::TempDir td;
    auto cfg = fast_settings(td.path());
    CHECK(cfg.hash_max_concurrent == -1);  // preset: number of CPUs
    cfg.argon2 = {3, 65536, 1};            // slow enough to overlap
    cfg.hash_max_concurrent = 1;
    cfg.hash_wait_seconds = 0;
    auto store = unwrap(accounts::AccountStore::open(td.path(), cfg));
    std::atomic<int> ok{0}, busy{0}, other{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 6; ++i)
      ts.emplace_back([&, i] {
        auto r = store->create_user("user" + std::to_string(i), "correct horse");
        if (r) ++ok;
        else if (r.error().code == Errc::Busy) ++busy;
        else ++other;
      });
    for (auto& t : ts) t.join();
    CHECK(other == 0);
    CHECK(ok >= 1);
    CHECK(busy >= 1);
    // waiting instead of refusing: nobody is refused when the wait is long enough
    auto cfg2 = cfg;
    cfg2.hash_wait_seconds = 60;
    auto store2 = unwrap(accounts::AccountStore::open(td.path() / "w", cfg2));
    std::atomic<int> refused{0};
    ts.clear();
    for (int i = 0; i < 4; ++i)
      ts.emplace_back([&, i] { if (!store2->create_user("w" + std::to_string(i), "correct horse")) ++refused; });
    for (auto& t : ts) t.join();
    CHECK(refused == 0);
    // 0 = unlimited
    auto cfg3 = cfg;
    cfg3.hash_max_concurrent = 0;
    auto store3 = unwrap(accounts::AccountStore::open(td.path() / "u", cfg3));
    refused = 0;
    ts.clear();
    for (int i = 0; i < 4; ++i)
      ts.emplace_back([&, i] { if (!store3->create_user("n" + std::to_string(i), "correct horse")) ++refused; });
    for (auto& t : ts) t.join();
    CHECK(refused == 0);
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
    // a failed creation (password policy / duplicate login) does not burn the code
    CHECK_FALSE(store->create_user("dave", "short", code));
    CHECK_FALSE(store->create_user("carol", "passw0rd!", code));
    CHECK_FALSE(store->create_user("dave", "passw0rd!", "wrong-code"));
    CHECK(store->create_user("dave", "passw0rd!", code));
    CHECK_FALSE(store->create_user("erin", "passw0rd!", code));  // single use
  }

  TEST_CASE("login throttle: backoff per login and per client, off allowed") {
    fsutil::TempDir td;
    auto cfg = fast_settings(td.path());
    cfg.throttle.per_login = {2, 5, 60};
    cfg.throttle.per_ip = {3, 1, 60};
    auto store = unwrap(accounts::AccountStore::open(td.path(), cfg));
    CHECK(store->throttle_wait("a", "ip1") == 0);
    store->record_failure("a", "ip1");
    store->record_failure("a", "ip1");
    CHECK(store->throttle_wait("a", "ip2") == 0);   // within free attempts
    store->record_failure("a", "ip1");
    store->record_failure("a", "ip1");
    CHECK(store->throttle_wait("A", "ip2") > 0);    // per login (case-insensitive), other address
    CHECK(store->throttle_wait("b", "ip1") > 0);    // per client, other login
    CHECK(store->throttle_wait("b", "ip3") == 0);
    store->record_success("a");
    CHECK(store->throttle_wait("a", "ip2") == 0);
    cfg.throttle.on = false;
    fsutil::TempDir td2;
    auto off = unwrap(accounts::AccountStore::open(td2.path(), cfg));
    for (int i = 0; i < 50; ++i) off->record_failure("x", "ip");
    CHECK(off->throttle_wait("x", "ip") == 0);
  }
}
