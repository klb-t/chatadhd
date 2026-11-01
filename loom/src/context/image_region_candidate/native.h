#pragma once
#include <functional>
#include "loom/db.h"
#include "loom/provenance.h"

namespace loom::context::image_region {
// Access belongs to the caller's account/runtime. A denial or missing original
// must propagate, never fall back to node.content or an unmanaged disk path.
struct Access {
  std::function<Result<std::optional<SourceRecord>>(std::string_view)> source;
  std::function<Result<std::string>(std::string_view)> read_blob;
  std::function<Status(std::string_view)> verify_blob;
};
bool is_region(const Node& node);
Result<Json> candidate(const Node& node, const Access& access,
                       std::size_t detail_chars, std::size_t relation_hops);
Result<Json> candidate(Database& db, BlobStore& blobs, const Node& node,
                       std::size_t detail_chars, std::size_t relation_hops);
} // namespace loom::context::image_region
