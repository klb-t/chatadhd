// Standalone offline regression for DIC-0395 using the actual native loader.
// Link against the freshly built loom_core archive; argv[1] is loom/data.
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "loom/kb.h"
#include "loom/util/fs.h"

namespace {

using loom::Json;
using loom::kb::Pack;
using Documents = std::map<std::string, Json>;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <class T>
T take(loom::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

void require_ok(loom::Status status) {
  if (!status) throw std::runtime_error(status.error().message);
}

Documents documents_of(const Pack& pack) {
  Documents documents;
  for (const auto& file : pack.files()) documents[file] = pack.file(file);
  return documents;
}

void write_window(const std::filesystem::path& dir, const Json& definition, const Json& window) {
  Json changed = definition;
  changed["window_tokens"] = window;
  require_ok(loom::fsutil::ensure_dir(dir / "lexicons"));
  require_ok(loom::fsutil::write_file(dir / "lexicons/version_patterns.json", loom::json::dump(changed)));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: pack_window.verify <loom/data>\n";
    return 2;
  }
  try {
    auto builtin = take(Pack::load_builtin());
    auto directory = take(Pack::load_dir(argv[1]));
    const Json definition = builtin->lexicon("version_patterns");
    require(definition["window_tokens"] == 12, "the existing data preset changed");
    require(directory->hash() == builtin->hash(), "directory and embedded default hashes differ");
    require(directory->manifest() == builtin->manifest(), "default manifests differ");
    const Documents original = documents_of(*builtin);
    auto in_memory = take(Pack::from_documents(original));
    require(in_memory->hash() == builtin->hash(), "in-memory default hash changed");

    loom::fsutil::TempDir overlay;
    require(overlay.valid(), "could not create synthetic overlay");
    require(take(Pack::load_with_overlay(overlay.path() / "missing"))->hash() == builtin->hash(),
            "missing overlay changed defaults");
    write_window(overlay.path(), definition, definition["window_tokens"]);
    require(take(Pack::load_with_overlay(overlay.path()))->hash() == builtin->hash(),
            "unchanged overlay changed the hash");

    // Preserve the original valid interval, then exercise an empty window,
    // the former ceiling
    // and the actual consumer's representable boundary without extraction work.
    for (const Json& window : std::vector<Json>{Json(0), Json(1), Json(12), Json(200), Json(201),
                                               Json(std::numeric_limits<int>::max())}) {
      Documents changed = original;
      changed["lexicons/version_patterns.json"]["window_tokens"] = window;
      auto pack = take(Pack::from_documents(std::move(changed)));
      require(pack->lexicon("version_patterns")["window_tokens"] == window, "window was clamped or changed");
      require((pack->hash() == builtin->hash()) == (window == definition["window_tokens"]),
              "effective window did not participate in the pack hash");
    }

    write_window(overlay.path(), definition, Json(201));
    auto over = take(Pack::load_with_overlay(overlay.path()));
    require(over->lexicon("version_patterns")["window_tokens"] == 201, "large overlay window rejected");
    require(over->hash() != builtin->hash(), "large overlay did not change the hash");
    require(over->files() == builtin->files(), "window overlay changed pack membership");
    for (const auto& file : builtin->files()) {
      if (file != "lexicons/version_patterns.json")
        require(over->file(file) == builtin->file(file), "window overlay changed another definition");
    }

    const auto above_int = static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1;
    for (const Json& invalid : std::vector<Json>{
             Json(-1), Json(201.0), Json(true), Json("201"), Json(nullptr),
             Json(above_int), Json(static_cast<std::uint64_t>(above_int)),
             Json(std::numeric_limits<std::int64_t>::max()),
             Json(std::numeric_limits<std::uint64_t>::max()),
             Json(std::numeric_limits<double>::infinity()),
             Json(std::numeric_limits<double>::quiet_NaN())}) {
      Documents changed = original;
      changed["lexicons/version_patterns.json"]["window_tokens"] = invalid;
      auto result = Pack::from_documents(std::move(changed));
      require(!result, "invalid or unrepresentable window accepted");
      require(result.error().message.find("lexicons/version_patterns.json#/window_tokens") != std::string::npos,
              "window rejection lost its source location");
    }
    Documents missing = original;
    missing["lexicons/version_patterns.json"].erase("window_tokens");
    require(!Pack::from_documents(std::move(missing)), "missing required window accepted");
    write_window(overlay.path(), definition, Json(above_int));
    require(!Pack::load_with_overlay(overlay.path()), "unrepresentable overlay window accepted");

    require(take(Pack::load_builtin())->hash() == builtin->hash(), "overlay changed immutable builtin defaults");
    std::cout << "pack window: defaults/hash parity, former ceiling, int boundary, overlays and malformed values passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "pack window: " << error.what() << '\n';
    return 1;
  }
}
