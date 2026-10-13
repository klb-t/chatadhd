// FormatRegistry (B-MOD-05): extension + content-sniff detection over a table.
#include "loom/format_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace loom {

void FormatRegistry::add(ImportFormat format) {
  for (auto& f : formats_) {
    if (f.name == format.name) {
      f = std::move(format);
      return;
    }
  }
  formats_.push_back(std::move(format));
}

const ImportFormat* FormatRegistry::find(std::string_view name) const {
  for (const auto& f : formats_)
    if (f.name == name) return &f;
  return nullptr;
}

std::string FormatRegistry::detect(const std::filesystem::path& path) const {
  std::string ext = path.extension().string();
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!ext.empty()) {
    for (const auto& f : formats_)
      if (std::find(f.extensions.begin(), f.extensions.end(), ext) != f.extensions.end()) return f.name;
  }
  std::ifstream in(path, std::ios::binary);
  if (in) {
    std::string header(200, '\0');
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
    for (const auto& f : formats_)
      if (f.sniff && f.sniff(header)) return f.name;
  }
  return "unknown";
}

std::string FormatRegistry::mime(std::string_view name) const {
  const ImportFormat* f = find(name);
  return f ? f->mime : "application/octet-stream";
}

}  // namespace loom
