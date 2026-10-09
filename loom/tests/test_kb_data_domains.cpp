// An explicit physical-root partition preserves strict KB validation without
// activating another domain's examples or policies. The inventory checks exact
// bytes/schema ownership; D/E suites retain their separate semantic validation.
#include <doctest/doctest.h>

#include <filesystem>
#include <map>

#include "loom/kb.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using test::unwrap;
namespace fs = std::filesystem;

namespace {
struct DomainFixture {
  fsutil::TempDir temporary;
  std::shared_ptr<const kb::Pack> baseline = unwrap(kb::Pack::load_builtin());
  const std::string foreign_path = "another_domain/example.json";
  Json foreign{{"schema", "synthetic.fixture/1"}, {"unknown", Json{{"retained", true}}}};
  Json inventory;
  DomainFixture() {
    for (const auto& path : baseline->files()) write(path, baseline->file(path).dump());
    const auto raw = foreign.dump();
    write(foreign_path, raw);
    inventory = Json{{"schema", "loom.data_domains/1"}, {"revision", 1},
        {"files", Json::array({Json{{"path", foreign_path}, {"schema", "synthetic.fixture/1"},
          {"sha256", Sha256::hex(raw)}, {"domain", "another_domain"}, {"role", "example"},
          {"validation_refs", Json::array({"synthetic fixture; no semantic activation"})}}})}};
    save_inventory();
  }
  void write(const std::string& path, const std::string& bytes) {
    fs::create_directories((temporary.path() / path).parent_path());
    LOOM_REQUIRE_OK(fsutil::write_file(temporary.path() / path, bytes));
  }
  void save_inventory() { write("data_domains.pack", inventory.dump()); }
  Result<std::shared_ptr<const kb::Pack>> load() { return kb::Pack::load_dir(temporary.path()); }
  void rejects(std::string_view expected) {
    const auto loaded = load();
    REQUIRE_FALSE(loaded.has_value());
    INFO(loaded.error().message);
    CHECK(loaded.error().message.find(expected) != std::string::npos);
  }
};
}  // namespace

TEST_SUITE("kb_data_domains") {
  TEST_CASE("exact external inventory preserves builtin KB files and identity without activating examples") {
    DomainFixture fixture;
    const auto directory = unwrap(fixture.load());
    CHECK(directory->hash() == fixture.baseline->hash());
    CHECK(directory->files() == fixture.baseline->files());
    CHECK(directory->file(fixture.foreign_path).is_null());
    CHECK(directory->manifest() == fixture.baseline->manifest());
    for (const auto& file : directory->files()) CHECK(directory->file(file) == fixture.baseline->file(file));
  }

  TEST_CASE("unlisted sibling JSON is rejected even inside an inventoried domain") {
    DomainFixture fixture;
    fixture.write("another_domain/unregistered.json", fixture.foreign.dump());
    fixture.rejects("not listed in pack.json");
  }

  TEST_CASE("changed content, schema and missing domain source fail instead of disappearing") {
    DomainFixture fixture;
    SUBCASE("changed unknown field changes digest") {
      fixture.foreign["unknown"]["retained"] = false;
      fixture.write(fixture.foreign_path, fixture.foreign.dump());
      fixture.rejects("sha256 mismatch");
    }
    SUBCASE("schema identity changes") {
      fixture.foreign["schema"] = "synthetic.fixture/2";
      fixture.write(fixture.foreign_path, fixture.foreign.dump());
      fixture.rejects("schema mismatch");
    }
    SUBCASE("missing source") {
      REQUIRE(fs::remove(fixture.temporary.path() / fixture.foreign_path));
      fixture.rejects("declared file is missing");
    }
    SUBCASE("malformed json") {
      fixture.write(fixture.foreign_path, "{invalid-json");
      fixture.rejects("kb pack file");
    }
  }

  TEST_CASE("inventory cannot remove KB files from validation or alias unsafe paths") {
    DomainFixture fixture;
    SUBCASE("KB overlap") {
      fixture.inventory["files"][0]["path"] = "policy/relevance.json";
      fixture.save_inventory();
      fixture.rejects("overlaps KB pack");
    }
    SUBCASE("duplicate") {
      fixture.inventory["files"].push_back(fixture.inventory["files"][0]);
      fixture.save_inventory();
      fixture.rejects("duplicate file");
    }
    SUBCASE("parent traversal") {
      fixture.inventory["files"][0]["path"] = "../example.json";
      fixture.save_inventory();
      fixture.rejects("unsafe file path");
    }
    SUBCASE("absolute path") {
      fixture.inventory["files"][0]["path"] = (fixture.temporary.path() / fixture.foreign_path).string();
      fixture.save_inventory();
      fixture.rejects("unsafe file path");
    }
    SUBCASE("noncanonical path") {
      fixture.inventory["files"][0]["path"] = "another_domain/./example.json";
      fixture.save_inventory();
      fixture.rejects("unsafe file path");
    }
    SUBCASE("missing validation delegation") {
      fixture.inventory["files"][0].erase("validation_refs");
      fixture.save_inventory();
      fixture.rejects("validation_refs are required");
    }
  }

  TEST_CASE("KB in-memory documents and overlays remain strict regardless of the physical inventory") {
    DomainFixture fixture;
    std::map<std::string, Json> docs;
    for (const auto& path : fixture.baseline->files()) docs[path] = fixture.baseline->file(path);
    docs[fixture.foreign_path] = fixture.foreign;
    auto in_memory = kb::Pack::from_documents(std::move(docs));
    REQUIRE_FALSE(in_memory.has_value());
    CHECK(in_memory.error().message.find("not listed in pack.json") != std::string::npos);
    auto overlay = kb::Pack::load_with_overlay(fixture.temporary.path());
    REQUIRE_FALSE(overlay.has_value());
    CHECK(overlay.error().message.find("not declared in the overlay pack.json") != std::string::npos);
  }
}
