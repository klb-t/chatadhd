// loom/format_registry.h — data-driven table of import formats (B-MOD-05).
//
// One ImportFormat entry = detection (extensions + optional content sniff) +
// parse body (+ optional lossless provider-export body). ConversationImporter
// owns an instance seeded with the built-in formats; adding a format is one
// add() call with one body, no `if` chain to edit. Detection order is
// registration order: extensions first (all entries), then content sniffing.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/db.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class ConversationImporter;
struct ImportOptions;

struct ImportFormat {
  using Body = std::function<Result<std::vector<Conversation>>(ConversationImporter&, const std::filesystem::path&,
                                                               const ImportOptions&)>;
  // Lossless provider-export parse. nullopt result = "not a provider export",
  // the caller then runs `body`. `report` is filled for the source outcome.
  using ExportBody = std::function<Result<std::optional<std::vector<Conversation>>>(
      ConversationImporter&, const std::filesystem::path&, const ImportOptions&, Json& report)>;

  std::string name;                      // "zip", "json", ...
  std::string mime = "application/octet-stream";
  std::vector<std::string> extensions;   // lower-case, with leading dot
  std::function<bool(std::string_view header)> sniff;  // first <=200 bytes; empty = never sniffed
  Body body;
  ExportBody export_body;                // empty = format has no provider-export path
  bool export_requires_on = false;       // export path only with ExportMode::On (else any mode != Off)
  bool live_original = false;            // parse the original path, not the captured blob (SQLite WAL)
  bool declared_image_format = false;    // record extension as the image format/mime (screenshots)
  std::string warning;                   // added to every result of this format
};

class FormatRegistry {
 public:
  // Replaces an entry of the same name, else appends (detection order).
  void add(ImportFormat format);
  const ImportFormat* find(std::string_view name) const;
  // Extension match first, then content sniff over `header`; "unknown" if none.
  std::string detect(const std::filesystem::path& path) const;
  std::string mime(std::string_view name) const;  // "application/octet-stream" if unknown
  const std::vector<ImportFormat>& entries() const noexcept { return formats_; }

 private:
  std::vector<ImportFormat> formats_;
};

}  // namespace loom
