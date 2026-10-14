// The bytes a client brings or takes (see loom/client_blobs.h).
#include "loom/client_blobs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/use_model.h"
#include "loom/util/base64.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"

namespace loom::actions::client_blobs {

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kDefaultChunk = 262144;

fs::path upload_dir(Runtime& rt) { return rt.paths().root / "tmp" / "uploads"; }

// "up_" + 12 hex: what `begin` hands out; nothing else names a file of the upload folder here.
bool is_upload_id(const std::string& id) {
  if (id.size() != 15 || id.rfind("up_", 0) != 0) return false;
  return std::all_of(id.begin() + 3, id.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string mime_of(Runtime& rt, const std::string& hash) {
  auto lock = rt.db().lock();
  auto st = rt.db().conn().prepare("SELECT mime FROM loom_blobs WHERE hash = ?");
  if (!st) return {};
  st->bind(1, hash);
  auto row = st->step();
  return row && *row ? st->get_text(0) : std::string();
}

Json described(const BlobRef& ref, const std::string& mime) {
  return Json{{"hash", ref.hash}, {"size", ref.size}, {"mime", mime}};
}

// Moves a file of the upload folder into the blob store.
Result<Json> store_upload(Runtime& rt, const fs::path& file, const std::string& mime) {
  LOOM_TRY_ASSIGN(BlobRef ref, rt.blobs().put_file(file, mime));
  std::error_code ignored;
  fs::remove(file, ignored);
  return described(ref, mime.empty() ? mime_of(rt, ref.hash) : mime);
}

Result<std::string> hash_of(const Json& request) {
  const std::string hash = json::get_string(request, "hash");
  if (!BlobStore::is_valid_hash(hash)) return Error(Errc::InvalidArgument, "hash must be a blob hash (64 hex)");
  return hash;
}

}  // namespace

Result<Json> put(Runtime& rt, const Json& request) {
  const std::string given = json::get_string(request, "path");
  if (given.empty()) return Error(Errc::InvalidArgument, "path is required (a file in the upload folder)");
  std::error_code ec;
  const fs::path file(given);
  if (fs::is_symlink(fs::symlink_status(file, ec)) || !fs::is_regular_file(file, ec))
    return Error(Errc::InvalidArgument, "path must be a regular file in the upload folder");
  const fs::path folder = fs::weakly_canonical(upload_dir(rt), ec);
  if (ec || fs::weakly_canonical(file, ec).parent_path() != folder)
    return Error(Errc::InvalidArgument, "path must be a file directly in the upload folder of this data directory");
  return store_upload(rt, file, json::get_string(request, "mime"));
}

Result<Json> path(Runtime& rt, const Json& request) {
  LOOM_TRY_ASSIGN(std::string hash, hash_of(request));
  if (!rt.blobs().has(hash)) return Error(Errc::NotFound, "no blob " + hash);
  const fs::path file = rt.blobs().local_path(hash);
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  if (ec) return Error(Errc::NotFound, "the blob " + hash + " has no readable file");
  return Json{{"hash", hash}, {"size", static_cast<std::int64_t>(size)}, {"mime", mime_of(rt, hash)}, {"path", file.string()}};
}

Result<Json> begin(Runtime& rt, const Json&) {
  const fs::path folder = upload_dir(rt);
  std::error_code ec;
  fs::create_directories(folder, ec);
  fs::permissions(folder, fs::perms::owner_all, fs::perm_options::replace, ec);
  const std::string id = gen_id("up_");
  std::ofstream created(folder / (id + ".part"), std::ios::binary | std::ios::trunc);
  if (!created) return Error(Errc::Io, "cannot create the upload");
  return Json{{"upload_id", id}};
}

Result<Json> append(Runtime& rt, const Json& request) {
  const std::string id = json::get_string(request, "upload_id");
  if (!is_upload_id(id)) return Error(Errc::InvalidArgument, "upload_id must be one that client.blob_begin gave");
  const fs::path file = upload_dir(rt) / (id + ".part");
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return Error(Errc::NotFound, "no upload " + id);
  LOOM_TRY_ASSIGN(std::string bytes, base64::decode(json::get_string(request, "data_base64"), true));
  std::ofstream out(file, std::ios::binary | std::ios::app);
  if (!out) return Error(Errc::Io, "cannot write the upload");
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out) return Error(Errc::Io, "cannot write the upload (disk full?)");
  return Json{{"upload_id", id}, {"size", static_cast<std::int64_t>(fs::file_size(file, ec))}};
}

Result<Json> commit(Runtime& rt, const Json& request) {
  const std::string id = json::get_string(request, "upload_id");
  if (!is_upload_id(id)) return Error(Errc::InvalidArgument, "upload_id must be one that client.blob_begin gave");
  const fs::path file = upload_dir(rt) / (id + ".part");
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return Error(Errc::NotFound, "no upload " + id);
  if (json::get_bool(request, "abort", false)) {
    fs::remove(file, ec);
    return Json{{"upload_id", id}, {"aborted", true}};
  }
  return store_upload(rt, file, json::get_string(request, "mime"));
}

Result<Json> read(Runtime& rt, const Json& request) {
  LOOM_TRY_ASSIGN(std::string hash, hash_of(request));
  if (!rt.blobs().has(hash)) return Error(Errc::NotFound, "no blob " + hash);
  const fs::path file = rt.blobs().local_path(hash);
  std::error_code ec;
  const std::int64_t size = static_cast<std::int64_t>(fs::file_size(file, ec));
  if (ec) return Error(Errc::NotFound, "the blob " + hash + " has no readable file");
  const std::int64_t offset = json::get_int(request, "offset", 0);
  const std::int64_t wanted = json::get_int(request, "length", kDefaultChunk);
  if (offset < 0 || offset > size) return Error(Errc::InvalidArgument, "offset is outside the blob");
  if (wanted <= 0) return Error(Errc::InvalidArgument, "length must be positive");
  const std::int64_t length = std::min(wanted, size - offset);
  std::string chunk(static_cast<std::size_t>(length), '\0');
  std::ifstream in(file, std::ios::binary);
  if (!in) return Error(Errc::Io, "cannot open the blob");
  in.seekg(offset);
  in.read(chunk.data(), static_cast<std::streamsize>(length));
  if (in.gcount() != static_cast<std::streamsize>(length)) return Error(Errc::Io, "cannot read the blob");
  return Json{{"hash", hash}, {"size", size}, {"mime", mime_of(rt, hash)}, {"offset", offset}, {"length", length},
              {"eof", offset + length >= size}, {"data_base64", base64::encode(chunk)}};
}

Result<Json> parts(Runtime& rt, const Json& list, const Json& origin) {
  Json out = Json::array();
  if (!list.is_array()) return out;
  for (const auto& p : list) {
    if (!p.is_object()) return Error(Errc::InvalidArgument, "a part must be an object");
    const std::string kind = json::get_string(p, "kind");
    if (!llm::known_part_kind(kind)) return Error(Errc::InvalidArgument, "unknown part kind '" + kind + "'");
    Json part = Json{{"kind", kind}};
    if (const std::string mime = json::get_string(p, "mime"); !mime.empty()) part["mime"] = mime;
    if (kind == "text" || kind == "json" || kind == "embedding") {
      if (const std::string text = json::get_string(p, "text"); !text.empty()) part["text"] = text;
      if (const Json* v = json::find(p, "value")) part["value"] = *v;
      out.push_back(std::move(part));
      continue;
    }
    if (json::find(p, "data_base64"))
      return Error(Errc::InvalidArgument, "a " + kind + " part names a stored blob (blob_hash), it does not carry bytes: upload them first "
                                          "(POST /api/client/blob/upload, or client.blob_begin / append / commit)");
    const std::string hash = json::get_string(p, "blob_hash");
    const std::string url = json::get_string(p, "url");
    if (hash.empty() && url.empty()) return Error(Errc::InvalidArgument, "a " + kind + " part needs blob_hash or url");
    if (!hash.empty()) {
      // The blob must be in THIS user's store: a hash that only exists elsewhere (another account's data directory) is not found here.
      if (!BlobStore::is_valid_hash(hash) || !rt.blobs().has(hash)) return Error(Errc::NotFound, "the blob " + hash + " is not in the blob store");
      std::error_code ec;
      const std::int64_t size = static_cast<std::int64_t>(fs::file_size(rt.blobs().local_path(hash), ec));
      const std::string mime = json::get_string(p, "mime", mime_of(rt, hash));
      if (!mime.empty()) part["mime"] = mime;
      if (!ec) part["size"] = size;
      NodeOptions o;
      o.tags = Json::array({"blob", "client_capture", kind});
      Json meta{{"blob_hash", hash}, {"mime", mime}, {"output_kind", kind}, {"produced_by", origin}};
      if (!ec) meta["size"] = size;
      if (const std::string name = json::get_string(p, "name"); !name.empty()) meta["name"] = name;
      o.metadata = std::move(meta);
      LOOM_TRY_ASSIGN(std::string node, rt.db().get_or_create_node("blob:" + hash, "blob", o));
      part["node_id"] = node;
      ArtifactRecord artifact;
      artifact.kind = "client_capture";
      artifact.title = kind + " from " + json::get_string(origin, "action");
      artifact.blob_hash = hash;
      artifact.mime = mime;
      artifact.metadata = origin;
      if (auto made = rt.provenance().add_artifact(std::move(artifact))) part["artifact_id"] = *made;
      part["blob_hash"] = hash;
    }
    if (!url.empty()) part["url"] = url;
    out.push_back(std::move(part));
  }
  return out;
}

namespace {

// A path of the manifest as relative segments, or an error: nothing absolute, nothing that climbs, no empty or control-character names.
Result<std::vector<std::string>> safe_segments(const std::string& raw) {
  std::string path = raw;
  std::replace(path.begin(), path.end(), '\\', '/');
  if (path.empty() || path.front() == '/') return Error(Errc::InvalidArgument, "a path of the folder must be relative: '" + raw + "'");
  std::vector<std::string> segments;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::string segment = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (segment == "..") return Error(Errc::InvalidArgument, "a path of the folder must stay inside it: '" + raw + "'");
    if (!segment.empty() && segment != ".") {
      for (unsigned char c : segment)
        if (c < 0x20 || c == 0x7f) return Error(Errc::InvalidArgument, "a path of the folder has a control character: '" + raw + "'");
      segments.push_back(segment);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  if (segments.empty()) return Error(Errc::InvalidArgument, "a path of the folder is empty: '" + raw + "'");
  return segments;
}

// The name of the picked folder as one safe segment ("folder" when it has none).
std::string folder_name(const std::string& raw) {
  auto segments = safe_segments(raw.empty() ? std::string("folder") : raw);
  if (!segments || segments->size() != 1) return "folder";
  return segments->front();
}

}  // namespace

Result<Json> materialize_folder(Runtime& rt, const Json& manifest) {
  if (!manifest.is_object() || json::get_string(manifest, "schema") != kFolderManifestSchema)
    return Error(Errc::InvalidArgument, "not a folder manifest (" + std::string(kFolderManifestSchema) + ")");
  const Json* files = json::find(manifest, "files");
  if (!files || !files->is_array()) return Error(Errc::InvalidArgument, "the folder manifest has no list of files");
  const std::string hash12 = Sha256::hex(json::canonical(manifest)).substr(0, 12);
  const fs::path folder = rt.paths().root / "tmp" / "uploads" / ("folder_" + hash12) / folder_name(json::get_string(manifest, "name"));
  std::int64_t count = 0, bytes = 0;
  // Check everything before writing anything: a manifest with one bad entry writes nothing.
  struct Item {
    fs::path target;
    std::string hash;
  };
  std::vector<Item> items;
  for (const auto& entry : *files) {
    if (!entry.is_object()) return Error(Errc::InvalidArgument, "an entry of the folder manifest must be an object");
    LOOM_TRY_ASSIGN(auto segments, safe_segments(json::get_string(entry, "path")));
    const std::string hash = json::get_string(entry, "blob_hash");
    if (!BlobStore::is_valid_hash(hash) || !rt.blobs().has(hash)) return Error(Errc::NotFound, "the blob " + hash + " of '" + json::get_string(entry, "path") + "' is not in the blob store");
    fs::path target = folder;
    for (const auto& segment : segments) target /= segment;
    items.push_back(Item{std::move(target), hash});
  }
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) return Error(Errc::Io, "cannot create the folder for the picked files: " + ec.message());
  for (const auto& item : items) {
    const fs::path source = rt.blobs().local_path(item.hash);
    const auto size = fs::file_size(source, ec);
    if (ec) return Error(Errc::Io, "cannot read the blob " + item.hash + ": " + ec.message());
    fs::create_directories(item.target.parent_path(), ec);
    if (ec) return Error(Errc::Io, "cannot create a folder for the picked files: " + ec.message());
    const bool same = fs::is_regular_file(item.target, ec) && fs::file_size(item.target, ec) == size;
    if (!same) {
      fs::copy_file(source, item.target, fs::copy_options::overwrite_existing, ec);
      if (ec) return Error(Errc::Io, "cannot write a picked file: " + ec.message());
    }
    ++count;
    bytes += static_cast<std::int64_t>(size);
  }
  return Json{{"path", folder.string()}, {"files", count}, {"bytes", bytes}};
}

}  // namespace loom::actions::client_blobs
