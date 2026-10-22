#include "loom/memory_folder.h"

#include <algorithm>
#include <system_error>
#include <vector>

#include "loom/config.h"
#include "loom/memory_engine.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/log.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::memory_folder {

namespace fs = std::filesystem;

namespace {

std::size_t count_value(const Json& v, std::size_t fallback) {
  if (v.is_number_unsigned()) return v.get<std::size_t>();
  if (v.is_number_integer()) return v.get<long long>() < 0 ? fallback : static_cast<std::size_t>(v.get<long long>());
  return fallback;
}

bool hidden(const fs::path& p) {
  const std::string n = p.filename().string();
  return !n.empty() && n[0] == '.';
}

struct Walk {
  MemoryEngine& memory;
  const Options& opt;
  fs::path root;
  Json counts = Json{{"dirs", 0}, {"files", 0}, {"text_files", 0}, {"binary_files", 0}, {"truncated", 0}, {"chars", 0}, {"skipped", 0}};

  Status dir(const fs::path& path, const std::string& parent) {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
      entries.push_back(*it);
    if (ec) return Error(Errc::Io, "cannot list " + path.string() + ": " + ec.message());
    // Folders first, then files, each by name: the tree reads like the folder.
    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
      const bool ad = a.is_directory(), bd = b.is_directory();
      if (ad != bd) return ad;
      return a.path().filename().string() < b.path().filename().string();
    });
    for (const auto& e : entries) {
      if (!opt.include_hidden && hidden(e.path())) {
        counts["skipped"] = counts["skipped"].get<int>() + 1;
        continue;
      }
      const std::string rel = fs::relative(e.path(), root, ec).generic_string();
      if (e.is_symlink()) {  // a link may leave the folder or loop: it is listed, not followed
        counts["skipped"] = counts["skipped"].get<int>() + 1;
        continue;
      }
      if (e.is_directory()) {
        LOOM_TRY_ASSIGN(auto id, memory.add_node(e.path().filename().string(), parent, "dir",
                                                 memory.stamped(Json{{"source", "folder"}, {"path", rel}}, "folder", rel)));
        counts["dirs"] = counts["dirs"].get<int>() + 1;
        LOOM_TRY(dir(e.path(), id));
      } else if (e.is_regular_file()) {
        if (opt.max_files && static_cast<std::size_t>(counts["files"].get<int>()) >= opt.max_files) {
          counts["skipped"] = counts["skipped"].get<int>() + 1;
          continue;
        }
        LOOM_TRY(file(e.path(), rel, parent));
      }
    }
    return ok_status();
  }

  Status file(const fs::path& path, const std::string& rel, const std::string& parent) {
    auto bytes = fsutil::read_file(path);
    if (!bytes) {
      log::debug("loom.memory_folder", "skipped unreadable {}: {}", path.string(), bytes.error().message);
      counts["skipped"] = counts["skipped"].get<int>() + 1;
      return ok_status();
    }
    const std::string& data = *bytes;
    const bool binary = data.find('\0') != std::string::npos || !utf8::is_valid(data);
    Json meta{{"source", "folder"}, {"path", rel}, {"name", path.filename().string()}, {"size", data.size()},
              {"sha256", Sha256::hex(data)}, {"binary", binary}, {"truncated", false}};
    std::string content;
    if (!binary) {
      const std::size_t len = utf8::length(data);
      if (opt.file_chars && len > opt.file_chars) {
        content = std::string(utf8::prefix(data, opt.file_chars));
        meta["truncated"] = true;
        meta["chars_total"] = len;
        counts["truncated"] = counts["truncated"].get<int>() + 1;
      } else {
        content = data;
      }
      counts["text_files"] = counts["text_files"].get<int>() + 1;
      counts["chars"] = counts["chars"].get<long long>() + static_cast<long long>(utf8::length(content));
    } else {
      counts["binary_files"] = counts["binary_files"].get<int>() + 1;
    }
    // A text file with no text is still a file of the folder; the node shows its name when the content is empty.
    if (content.empty()) content = path.filename().string();
    LOOM_TRY_ASSIGN(auto id, memory.add_node(content, parent, "file", memory.stamped(std::move(meta), "folder", rel)));
    (void)id;
    counts["files"] = counts["files"].get<int>() + 1;
    return ok_status();
  }
};

}  // namespace

Options options_from(const Config& cfg, const Json& request) {
  Options o;
  o.file_chars = count_value(cfg.get("memory_folder_file_chars", 0), 0);
  o.max_files = count_value(cfg.get("memory_folder_max_files", 0), 0);
  o.include_hidden = json::truthy(cfg.get("memory_folder_include_hidden", false));
  if (request.is_object()) {
    if (request.contains("file_chars")) o.file_chars = count_value(request["file_chars"], o.file_chars);
    if (request.contains("max_files")) o.max_files = count_value(request["max_files"], o.max_files);
    if (request.contains("include_hidden")) o.include_hidden = json::truthy(request["include_hidden"]);
    const std::string parent = json::get_string(request, "parent_id");
    if (!parent.empty()) o.parent_id = parent;
    o.title = json::get_string(request, "title");
  }
  return o;
}

Result<Json> attach(MemoryEngine& memory, const fs::path& root, const Options& options) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return Error(Errc::InvalidArgument, "not a folder: " + root.string());
  if (options.parent_id && !memory.get_node(*options.parent_id))
    return Error(Errc::NotFound, "memory node not found: " + *options.parent_id);
  const std::string name = options.title.empty() ? fs::weakly_canonical(root, ec).filename().string() : options.title;
  LOOM_TRY_ASSIGN(auto root_id, memory.add_node(name.empty() ? root.string() : name, options.parent_id, "dir",
                                               memory.stamped(Json{{"source", "folder"}, {"path", "."}, {"folder", root.string()}}, "folder", root.string())));
  Walk walk{memory, options, root};
  LOOM_TRY(walk.dir(root, root_id));
  Json out = walk.counts;
  out["root_id"] = root_id;
  out["folder"] = root.string();
  out["dirs"] = out["dirs"].get<int>() + 1;
  return out;
}

Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request) {
  if (action != "attach_folder") return Error(Errc::InvalidArgument, "unknown memory_folder action '" + std::string(action) + "'");
  const std::string path = json::get_string(request, "path");
  if (path.empty()) return Error(Errc::InvalidArgument, "attach_folder needs a path (the folder a device picked is materialised first)");
  return attach(rt.memory(), fs::path(path), options_from(rt.config(), request));
}

}  // namespace loom::memory_folder
