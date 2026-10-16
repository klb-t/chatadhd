// R39: a folder into the memory tree with the full content of its files (loom/memory_folder.h).
#include <doctest/doctest.h>

#include <fstream>

#include "loom/config.h"
#include "loom/memory_engine.h"
#include "loom/memory_folder.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

void write(const std::filesystem::path& p, const std::string& text) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

struct Folder {
  fsutil::TempDir td;
  std::filesystem::path root = td.path() / "Projekt";
  MemoryEngine mem{td.path() / "memory.json"};
  Folder() {
    write(root / "README.md", "# Projekt\nZażółć gęślą jaźń — pełna treść.\n");
    write(root / "src" / "main.py", "print('hello')\n");
    write(root / "src" / "deep" / "notes.txt", std::string(50, 'x'));
    write(root / "logo.png", std::string("\x89PNG\0\0\x01", 7));
    write(root / ".secret", "hidden\n");
  }
  std::optional<MemoryNode> by_path(const std::string& rel) const {
    for (const auto& n : mem.get_all())
      if (json::get_string(n.metadata, "path") == rel) return n;
    return std::nullopt;
  }
};

}  // namespace

TEST_SUITE("memory_folder") {
  TEST_CASE("a folder becomes dir and file nodes; text files keep their whole content") {
    Folder f;
    memory_folder::Options o;
    auto r = unwrap(memory_folder::attach(f.mem, f.root, o));
    CHECK(r["dirs"] == 3);           // Projekt, src, src/deep
    CHECK(r["files"] == 4);          // README.md, main.py, notes.txt, logo.png (.secret is hidden)
    CHECK(r["text_files"] == 3);
    CHECK(r["binary_files"] == 1);
    CHECK(r["skipped"] == 1);
    const auto root = f.mem.get_node(r["root_id"].get<std::string>());
    REQUIRE(root);
    CHECK(root->node_type == "dir");
    CHECK(root->content == "Projekt");
    const auto readme = f.by_path("README.md");
    REQUIRE(readme);
    CHECK(readme->node_type == "file");
    CHECK(readme->content == "# Projekt\nZażółć gęślą jaźń — pełna treść.\n");
    CHECK(readme->parent_id == root->id);
    const auto deep = f.by_path("src/deep/notes.txt");
    REQUIRE(deep);
    CHECK(deep->content.size() == 50);
    const auto src = f.by_path("src");
    REQUIRE(src);
    CHECK(src->node_type == "dir");
    CHECK(f.by_path("src/main.py")->parent_id == src->id);
    const auto png = f.by_path("logo.png");
    REQUIRE(png);
    CHECK(json::get_bool(png->metadata, "binary", false));
    CHECK(png->content == "logo.png");
    CHECK_FALSE(f.by_path(".secret"));
  }

  TEST_CASE("sizes are presets: a cut is recorded, 0 keeps everything, hidden files on request") {
    Folder f;
    memory_folder::Options o;
    o.file_chars = 10;
    o.include_hidden = true;
    auto r = unwrap(memory_folder::attach(f.mem, f.root, o));
    CHECK(r["truncated"] == 3);  // README.md, main.py and notes.txt are longer than 10 characters
    const auto notes = f.by_path("src/deep/notes.txt");
    REQUIRE(notes);
    CHECK(notes->content.size() == 10);
    CHECK(json::get_bool(notes->metadata, "truncated", false));
    CHECK(f.by_path(".secret"));
  }

  TEST_CASE("settings, then the request; max_files counts files; a path that is no folder is an error") {
    fsutil::TempDir td;
    Config cfg(td.path() / "config.json");
    cfg.set("memory_folder_file_chars", 5);
    cfg.set("memory_folder_max_files", 2);
    auto o = memory_folder::options_from(cfg, Json{{"max_files", 0}, {"title", "Mój folder"}});
    CHECK(o.file_chars == 5);
    CHECK(o.max_files == 0);
    CHECK(o.title == "Mój folder");
    Folder f;
    memory_folder::Options two;
    two.max_files = 2;
    auto r = unwrap(memory_folder::attach(f.mem, f.root, two));
    CHECK(r["files"] == 2);
    CHECK_FALSE(memory_folder::attach(f.mem, f.root / "README.md", two));
  }
}
