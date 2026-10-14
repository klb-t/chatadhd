// R20 safety settings: zip bomb limits (config loom_zip_*), git hardening, symlink-race-safe opens.
#include <doctest/doctest.h>

#include <fstream>
#include <memory>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/provenance.h"
#include "loom/path_policy.h"
#include "loom/util/fs.h"
#include "../third_party/miniz/miniz.h"
#include "test_helpers.h"
#include "archive/archive_runtime.h"
#include "util/safety.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {
void write_text(const std::filesystem::path& p, const std::string& s) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << s;
}

struct ZipFixture {
  fsutil::TempDir temporary;
  std::unique_ptr<Database> db = open_db(temporary.path() / "z.db");
  EventBus bus;
  BlobStore blobs{temporary.path() / "blobs", *db};
  ProvenanceStore provenance{*db};
  ConversationImporter importer{*db, bus, &blobs, &provenance};

  std::filesystem::path bomb(const std::string& member, std::size_t bytes) {
    const auto path = temporary.path() / "bomb.zip";
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    const std::string zeros(bytes, '\0');
    REQUIRE(mz_zip_writer_add_mem(&zip, member.c_str(), zeros.data(), zeros.size(), MZ_BEST_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));
    return path;
  }
  std::filesystem::path normal() {
    const auto path = temporary.path() / "normal.zip";
    const std::string md = "user: hello\n\nassistant: hi\n";
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    REQUIRE(mz_zip_writer_add_mem(&zip, "chat.md", md.data(), md.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));
    return path;
  }
};
}  // namespace

TEST_SUITE("safety") {
  TEST_CASE("zip limits: presets are generous, config.json overrides, 0 lifts") {
    fsutil::TempDir td;
    auto l = safety::zip_limits(td.path());
    CHECK(l.max_total_bytes == 64ull * 1024 * 1024 * 1024);
    CHECK(l.max_ratio == 1000);
    write_text(td.path() / "config.json", R"({"loom_zip_max_total_bytes": 5000, "loom_zip_max_ratio": 0})");
    l = safety::zip_limits(td.path());
    CHECK(l.max_total_bytes == 5000);
    CHECK(l.max_ratio == 0);
    write_text(td.path() / "config.json", "not json");
    CHECK(safety::zip_limits(td.path()).max_ratio == 1000);
  }

  TEST_CASE("a zip bomb is refused by ratio before anything is imported") {
    ZipFixture f;
    const auto path = f.bomb("bomb.md", 8 * 1024 * 1024);  // 8 MiB of zeros -> a few KiB
    auto r = f.importer.import_file(path, ImportOptions{});
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("loom_zip_max_ratio") != std::string::npos);
    CHECK(unwrap(f.db->list_convs()).empty());
  }

  TEST_CASE("a zip beyond the total-bytes limit is refused; 0 = no limit imports it") {
    ZipFixture f;
    write_text(f.temporary.path() / "config.json", R"({"loom_zip_max_total_bytes": 1000000, "loom_zip_max_ratio": 0})");
    const auto path = f.bomb("big.md", 4 * 1024 * 1024);
    auto r = f.importer.import_file(path, ImportOptions{});
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("loom_zip_max_total_bytes") != std::string::npos);
    write_text(f.temporary.path() / "config.json", R"({"loom_zip_max_total_bytes": 0, "loom_zip_max_ratio": 0})");
    CHECK(f.importer.import_file(path, ImportOptions{}));
  }

  TEST_CASE("an ordinary zip is unaffected by the presets") {
    ZipFixture f;
    CHECK(f.importer.import_file(f.normal(), ImportOptions{}));
  }

  TEST_CASE("streaming cuts off an entry whose real size exceeds the limit") {
    // The central directory claims a size; extraction counts the bytes that actually arrive.
    ZipFixture f;
    const auto path = f.bomb("liar.md", 3 * 1024 * 1024);
    mz_zip_archive zip{};
    REQUIRE(mz_zip_reader_init_file(&zip, path.string().c_str(), 0));
    safety::ZipBudget budget({1000000, 0});
    const auto dest = f.temporary.path() / "out.bin";
    auto st = budget.extract_to_file(zip, 0, dest);  // admit() inside refuses first
    CHECK_FALSE(st);
    CHECK(budget.refused());
    CHECK_FALSE(std::filesystem::exists(dest));
    mz_zip_reader_end(&zip);
  }

  TEST_CASE("git: repository-named programs are switched off unless the setting allows them") {
    fsutil::TempDir td;
    const auto repo = td.path() / "repo";
    std::filesystem::create_directories(repo);
    REQUIRE(std::system(("git init -q '" + repo.string() + "' 2>/dev/null").c_str()) == 0);
    const std::string probe = "touch '" + (td.path() / "ran").string() + "'";
    std::ofstream(repo / ".git" / "config", std::ios::app)
        << "[core]\n\tfsmonitor = " << probe << "\n\thooksPath = " << (repo / "hooks").string() << "\n";
    auto hardened = archive::run_git(repo.string(), {"config", "--get", "core.fsmonitor"});
    REQUIRE(hardened);
    CHECK(*hardened == "false\n");
    auto hooks = archive::run_git(repo.string(), {"config", "--get", "core.hooksPath"});
    REQUIRE(hooks);
    CHECK(*hooks == "/dev/null\n");
    auto allowed = archive::run_git(repo.string(), {"config", "--get", "core.fsmonitor"}, true);
    REQUIRE(allowed);
    CHECK(allowed->find("touch") != std::string::npos);
    // the setting: preset false, config.json opts in
    CHECK_FALSE(safety::git_allow_repo_config(td.path()));
    write_text(td.path() / "config.json", R"({"loom_git_allow_repo_config": true})");
    CHECK(safety::git_allow_repo_config(td.path()));
  }

  TEST_CASE("PathPolicy::open_read: no symlink escape, and a swap after the open changes nothing") {
    fsutil::TempDir td;
    const auto in = td.path() / "mine", out = td.path() / "other";
    write_text(in / "f.txt", "mine");
    write_text(out / "secret.txt", "secret");
    std::filesystem::create_symlink(out / "secret.txt", in / "link.txt");
    std::filesystem::create_directory_symlink(out, in / "dir");

    PathPolicy open_policy;  // unconfined: the path comes back untouched
    CHECK(unwrap(open_policy.open_read(out / "secret.txt")).path() == out / "secret.txt");

    PathPolicy policy;
    policy.confine({in});
    auto ok = unwrap(policy.open_read(in / "f.txt"));
    auto slurp = [](const std::filesystem::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); };
    CHECK(slurp(ok.path()) == "mine");
    CHECK_FALSE(policy.open_read(in / "link.txt"));            // link leading out
    CHECK_FALSE(policy.open_read(in / "dir" / "secret.txt"));  // link in a parent directory
    CHECK_FALSE(policy.open_read(out / "secret.txt"));         // plainly outside

    // check -> swap -> use: the descriptor still names the file that was checked
    std::filesystem::remove(in / "f.txt");
    std::filesystem::create_symlink(out / "secret.txt", in / "f.txt");
    CHECK(slurp(ok.path()) == "mine");
    CHECK_FALSE(policy.open_read(in / "f.txt"));  // by name it is now refused
  }
}
