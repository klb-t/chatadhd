// R18 security review: what one account's runtime may touch (path_policy.h, net/host_guard.h) and the
// hardening that does not depend on the HTTP layer.
#include <doctest/doctest.h>

#include <fstream>

#include "loom/archive.h"
#include "loom/catalog.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/net/host_guard.h"
#include "loom/path_policy.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/storage_backend.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::archive;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {
void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

std::unique_ptr<Runtime> open_account(const fs::path& dir, std::vector<std::string> roots = {}) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  o.confine_paths = true;
  o.path_roots = std::move(roots);
  return unwrap(Runtime::open(o));
}
}  // namespace

TEST_SUITE("security_isolation") {
  TEST_CASE("PathPolicy: unconfined allows everything; confined allows roots only, resolves .. and symlinks") {
    PathPolicy open;
    CHECK(open.allows("/etc/passwd"));
    fsutil::TempDir td;
    const fs::path mine = td.path() / "mine", other = td.path() / "other";
    fs::create_directories(mine / "sub");
    fs::create_directories(other);
    PathPolicy p;
    p.confine({mine});
    CHECK(p.allows(mine));
    CHECK(p.allows(mine / "sub" / "file.txt"));      // not yet existing is fine
    CHECK_FALSE(p.allows(other));
    CHECK_FALSE(p.allows(mine / ".." / "other"));
    CHECK_FALSE(p.allows(td.path() / "mine-evil"));  // sibling sharing a name prefix
    CHECK_FALSE(p.allows("/etc/passwd"));
    CHECK_FALSE(p.allows(""));
    std::error_code ec;
    fs::create_directory_symlink(other, mine / "link", ec);
    if (!ec) CHECK_FALSE(p.allows(mine / "link" / "secret"));  // a link inside the root cannot lead out
  }

  TEST_CASE("an account runtime cannot scan, archive or ingest another user's folder") {
    fsutil::TempDir td;
    const fs::path a = td.path() / "users" / "u_a", b = td.path() / "users" / "u_b";
    fs::create_directories(a);
    write(b / "secret.md", "# B's private note\n");
    auto rt = open_account(a);

    Catalog catalog(*rt, unwrap(rt->knowledge().pack()));
    ScanConfig scan;
    scan.sources = {(b / "secret.md").string()};
    auto r = catalog.scan(scan);
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("outside the folders") != std::string::npos);

    ArchiveConfig cfg;
    cfg.sources = {b.string()};
    CHECK_FALSE(rt->archive().run(cfg));
    ArchiveConfig repo;
    repo.repo = b.string();
    CHECK_FALSE(rt->archive().run(repo));
    ArchiveConfig out;
    out.out_dir = (b / "exports").string();
    CHECK_FALSE(rt->archive().run(out));

    // ...but its own folder and an operator-granted root work
    write(a / "own.md", "# mine\n");
    ScanConfig own;
    own.sources = {(a / "own.md").string()};
    CHECK(catalog.scan(own));
    auto shared = open_account(td.path() / "users" / "u_c", {b.string()});
    Catalog shared_catalog(*shared, unwrap(shared->knowledge().pack()));
    CHECK(shared_catalog.scan(scan));
    CHECK_FALSE(fs::exists(b / "exports"));
  }

  TEST_CASE("a user-written storage placement cannot aim a local target at another user's data") {
    fsutil::TempDir td;
    const fs::path a = td.path() / "u_a", b = td.path() / "u_b";
    fs::create_directories(b);
    write(b / "memory.json", "{}");
    storage::BackendEnv env;
    env.data_dir = a;
    auto policy = std::make_shared<PathPolicy>();
    policy->confine({a});
    env.path_policy = policy;
    const auto& registry = storage::BackendRegistry::builtin();
    auto bad = registry.create("evil", Json{{"kind", "local"}, {"path", b.string()}}, env);
    CHECK_FALSE(bad);
    auto good = registry.create("fine", Json{{"kind", "local"}, {"path", "{dir}/store"}}, env);
    CHECK(good);
    env.path_policy = nullptr;  // the owner's own runtime stays unconfined
    CHECK(registry.create("evil", Json{{"kind", "local"}, {"path", b.string()}}, env));
  }

  TEST_CASE("BlobStore never builds a path outside its root from a hostile hash") {
    fsutil::TempDir td;
    auto rt = open_account(td.path() / "u_a");
    for (const char* h : {"../../etc/passwd", "ab/../../x", "..", "a\\..\\b", "ab/cd/ef"}) {
      const fs::path p = rt->blobs().path_for(h);
      CHECK(p.lexically_normal().string().rfind(rt->blobs().root().lexically_normal().string(), 0) == 0);
      CHECK_FALSE(rt->blobs().has(h));
      CHECK_FALSE(rt->blobs().read(h));
    }
  }

  TEST_CASE("host guard refuses private and local destinations, allows public ones and listed hosts") {
    CHECK(net::is_non_public_address("127.0.0.1"));
    CHECK(net::is_non_public_address("10.1.2.3"));
    CHECK(net::is_non_public_address("172.16.0.1"));
    CHECK_FALSE(net::is_non_public_address("172.32.0.1"));
    CHECK(net::is_non_public_address("192.168.1.1"));
    CHECK(net::is_non_public_address("169.254.169.254"));
    CHECK(net::is_non_public_address("::1"));
    CHECK(net::is_non_public_address("fd00::1"));
    CHECK(net::is_non_public_address("fe80::1"));
    CHECK(net::is_non_public_address("::ffff:127.0.0.1"));
    CHECK_FALSE(net::is_non_public_address("93.184.216.34"));
    CHECK_FALSE(net::check_public_url("http://127.0.0.1:11434/v1"));
    CHECK_FALSE(net::check_public_url("http://localhost/x"));
    CHECK_FALSE(net::check_public_url("http://user:pw@169.254.169.254/latest/meta-data"));
    CHECK_FALSE(net::check_public_url("http://[::1]:8080/"));
    CHECK(net::check_public_url("http://127.0.0.1:11434/v1", {"127.0.0.1"}));
    CHECK(net::check_public_url("https://93.184.216.34/"));
  }

  TEST_CASE("an account runtime sends no request to the private network") {
    fsutil::TempDir td;
    RuntimeOptions o;
    o.data_dir = (td.path() / "u_a").string();
    o.start_workers = false;
    o.block_private_hosts = true;
    auto transport = std::make_shared<net::ScriptedTransport>();
    o.http = transport;
    auto rt = unwrap(Runtime::open(o));
    net::HttpRequest req;
    req.url = "http://169.254.169.254/latest/meta-data/";
    auto r = rt->http().send(req);
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("private or local") != std::string::npos);
  }
}
