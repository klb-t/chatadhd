#include "loom/url_share_import.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <vector>
#if defined(__unix__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "loom/config.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

namespace loom::url_share {
namespace {
#include "url_share/policy.inc"
constexpr const char* kParser = "loom.importer.url_share";
constexpr const char* kVersion = "1";
#if defined(__unix__) && !defined(__ANDROID__)
#include "url_share/worker.inc"
// mkdtemp creates a private directory atomically, before any helper/input bytes.
// Destruction runs after WorkerGuard has reaped the child, on every return/throw.
struct WorkerDirectory {
  std::filesystem::path path;
  Status create() {
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec);
    if (ec) return Error(Errc::Io, "url worker temporary root unavailable");
    auto pattern = (root / "loom_url_XXXXXX").string();
    if (!::mkdtemp(pattern.data())) return Error(Errc::Io, "url worker staging failed");
    path = pattern;
    if (::chmod(path.c_str(), 0700) != 0) return Error(Errc::Io, "url worker directory permissions failed");
    return {};
  }
  ~WorkerDirectory() {
    if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path, ec); }
  }
};
Status private_write(const std::filesystem::path& path, std::string_view bytes) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) return Error(Errc::Io, "url worker private file creation failed");
  bool ok = ::fchmod(fd, 0600) == 0;
  std::size_t offset = 0;
  while (ok && offset < bytes.size()) {
    const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ok = false; break; }
    offset += static_cast<std::size_t>(n);
  }
  const bool closed = ::close(fd) == 0;
  if (!ok || !closed) return Error(Errc::Io, "url worker private file write failed");
  return {};
}
struct WorkerGuard {
  pid_t pid;
  int output;
  ~WorkerGuard() {
    if (output >= 0) ::close(output);
    if (pid > 0) {
      ::kill(pid, SIGKILL);
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
  }
};
#endif

Json failure(const Json& request, const std::string& status, const std::string& reason) {
  return {{"schema", "loom.url_snapshot/1"}, {"mode", request.value("mode", "page")}, {"llm_calls", 0},
          {"resource", {{"source_url", request.value("url", "")}, {"final_url", ""}, {"status", status},
                        {"reason", reason}, {"mime", ""}, {"body_base64", ""}}},
          {"parsed", {{"status", status}, {"reason", reason}, {"messages", Json::array()}}}};
}
Result<BlobRef> keep_bytes(BlobStore& blobs, Json& record) {
  LOOM_TRY_ASSIGN(auto bytes, base64::decode(json::get_string(record, "body_base64"), true));
  if (record.contains("sha256") && json::get_string(record, "sha256") != Sha256::hex(bytes))
    return Error(Errc::Conflict, "url snapshot hash mismatch");
  LOOM_TRY_ASSIGN(auto blob, blobs.put(bytes, json::get_string(record, "mime")));
  record.erase("body_base64");
  record["blob_hash"] = blob.hash;
  record["bytes"] = blob.size;
  return blob;
}
Status located(ProvenanceStore& prov, const std::string& subject, const std::string& kind,
               const std::string& source, const Json& locator) {
  ProvenanceRecord p;
  p.subject_id = subject; p.subject_kind = kind; p.source_id = source;
  p.locator = locator; p.transform = "import.url_share@1";
  LOOM_TRY(prov.add(std::move(p)));
  return {};
}
}  // namespace

Result<Json> policy(const Json& overrides) {
  LOOM_TRY_ASSIGN(auto result, json::parse(kPolicy));
  if (!overrides.is_object()) return Error(Errc::InvalidArgument, "url policy must be an object");
  for (auto it = overrides.begin(); it != overrides.end(); ++it)
    if (it.key() != "limits" && it.key() != "page_role" && it.key() != "projection")
      return Error(Errc::InvalidArgument, "unknown url policy field");
  result.merge_patch(overrides);
  if (!result["limits"].is_object() || result["limits"].size() != 7 || !result["page_role"].is_string() || result["page_role"] == "")
    return Error(Errc::InvalidArgument, "invalid url policy");
  for (const auto* key : {"timeout_ms", "worker_timeout_ms", "max_bytes", "max_total_bytes", "max_redirects", "max_attachments", "worker_output_bytes"}) {
    const auto& v = result["limits"][key];
    const bool zero = std::string_view(key) == "max_redirects" || std::string_view(key) == "max_attachments";
    if (!v.is_number_integer() || (v.is_number_unsigned() && v.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) ||
        v.get<std::int64_t>() < (zero ? 0 : 1))
      return Error(Errc::InvalidArgument, "invalid url limit representation");
  }
  if (!result["projection"].is_object() || result["projection"].size() != 6)
    return Error(Errc::InvalidArgument, "invalid url projection");
  for (const auto* key : {"source_kind", "source_node_kind", "attachment_node_kind", "conversation_source", "part_relation", "revision_relation"})
    if (!result["projection"][key].is_string() || result["projection"][key] == "")
      return Error(Errc::InvalidArgument, "invalid url projection");
  return result;
}

Result<Json> fetch(const Json& request, const Json& preset, const std::filesystem::path& isolated_worker) {
  if (!request.is_object() || request.value("schema", "") != "loom.url_import_request/1" ||
      !request.contains("url") || !request["url"].is_string() ||
      (request.value("mode", "") != "page" && request.value("mode", "") != "share") || request.size() != 3)
    return Error(Errc::InvalidArgument, "invalid url import descriptor");
#if defined(__unix__) && !defined(__ANDROID__)
  const auto python = json::get_string(preset["worker"], "python");
  if (!std::filesystem::is_regular_file(python))
    return failure(request, "unsupported", "url_worker_unavailable");
  WorkerDirectory temp;
  LOOM_TRY(temp.create());
  auto worker = isolated_worker;
  if (worker.empty()) {
    LOOM_TRY_ASSIGN(auto embedded, base64::decode(kWorkerBase64, true));
    if (Sha256::hex(embedded) != kWorkerSha256)
      return Error(Errc::Conflict, "embedded URL worker hash mismatch");
    worker = temp.path / "worker.py";
    LOOM_TRY(private_write(worker, embedded));
  } else if (!std::filesystem::is_regular_file(worker)) {
    return failure(request, "unsupported", "url_worker_unavailable");
  }
  const auto input = temp.path / "request.json";
  LOOM_TRY(private_write(input, Json{{"request", request}, {"policy", preset}}.dump()));
  const int in = ::open(input.c_str(), O_RDONLY | O_CLOEXEC);
  const int err = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
  int output[2];
  if (in < 0 || err < 0 || ::pipe(output) != 0) {
    if (in >= 0) ::close(in);
    if (err >= 0) ::close(err);
    return Error(Errc::Io, "url worker pipe failed");
  }
  ::fcntl(output[0], F_SETFD, FD_CLOEXEC);
  ::fcntl(output[1], F_SETFD, FD_CLOEXEC);
  const std::string path = worker.string();
  const char* argv[] = {python.c_str(), "-I", path.c_str(), nullptr};
  const auto started = std::chrono::steady_clock::now();
  const auto budget_ms = preset["limits"]["worker_timeout_ms"].get<std::int64_t>();
  auto left_ms = [&] { return budget_ms - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(); };
  const pid_t pid = ::fork();
  if (pid == 0) {
    if (::dup2(in, STDIN_FILENO) < 0 || ::dup2(output[1], STDOUT_FILENO) < 0 || ::dup2(err, STDERR_FILENO) < 0) ::_exit(127);
    if (in > STDERR_FILENO) ::close(in);
    if (err > STDERR_FILENO) ::close(err);
    if (output[0] > STDERR_FILENO) ::close(output[0]);
    if (output[1] > STDERR_FILENO) ::close(output[1]);
    ::execv(python.c_str(), const_cast<char* const*>(argv));
    ::_exit(127);
  }
  ::close(in); ::close(err); ::close(output[1]);
  if (pid < 0) { ::close(output[0]); return Error(Errc::Io, "url worker spawn failed"); }
  WorkerGuard child{pid, output[0]};
  std::string bytes;
  std::string failed;
  const auto cap = preset["limits"]["worker_output_bytes"].get<std::size_t>();
  while (true) {
    const auto left = left_ms();
    if (left <= 0) { failed = "timeout"; break; }
    pollfd fd{output[0], POLLIN, 0};
    const int rc = ::poll(&fd, 1, static_cast<int>(std::min<std::int64_t>(left, std::numeric_limits<int>::max())));
    if (rc < 0 && errno == EINTR) continue;
    if (rc == 0) continue;
    if (rc < 0) { failed = "worker_read_error"; break; }
    char buffer[8192];
    const auto got = ::read(output[0], buffer, sizeof(buffer));
    if (got < 0 && errno == EINTR) continue;
    if (got < 0) { failed = "worker_read_error"; break; }
    if (got == 0) break;
    if (static_cast<std::size_t>(got) > cap - bytes.size()) { failed = "worker_output_oversize"; break; }
    bytes.append(buffer, static_cast<std::size_t>(got));
  }
  ::close(output[0]); child.output = -1;
  // Child must never outlive this call, including closed stdout before exit.
  int status = 0;
  if (failed.empty()) {
    while (true) {
      const auto waited = ::waitpid(pid, &status, WNOHANG);
      if (waited == pid) { child.pid = -1; break; }
      if (waited < 0 && errno == EINTR) continue;
      if (waited < 0) { if (errno == ECHILD) child.pid = -1; failed = "worker_wait_error"; break; }
      if (left_ms() <= 0) { failed = "timeout"; break; }
      ::poll(nullptr, 0, 1);
    }
  }
  if (!failed.empty()) {
    // WorkerGuard kills and reaps this exact child on every failure/exception.
    return failure(request, "partial", failed);
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return failure(request, "unsupported", "url_worker_failed");
  auto parsed = json::parse(bytes);
  if (!parsed || !parsed->is_object() || parsed->value("schema", "") != "loom.url_snapshot/1")
    return failure(request, "unsupported", "url_worker_contract");
  return *parsed;
#else
  (void)preset; (void)isolated_worker;
  return failure(request, "unsupported", "url_worker_unavailable");
#endif
}

Result<Json> store(Database& db, BlobStore& blobs, ProvenanceStore& prov, Json snapshot, const std::string& title, const Json& projection) {
  LOOM_TRY_ASSIGN(auto configured, policy({{"projection", projection}}));
  const auto& mapping = configured["projection"];
  if (!snapshot.is_object() || snapshot.value("schema", "") != "loom.url_snapshot/1" ||
      !snapshot.contains("resource") || !snapshot["resource"].is_object() ||
      !snapshot.contains("parsed") || !snapshot["parsed"].is_object() ||
      !snapshot["parsed"].contains("messages") || !snapshot["parsed"]["messages"].is_array())
    return Error(Errc::InvalidArgument, "invalid url snapshot");
  // ordered_json stores object members in a vector: insert before taking references.
  snapshot["projection"] = mapping;
  auto& resource = snapshot["resource"];
  auto& parsed = snapshot["parsed"];
  const auto uri = json::get_string(resource, "normalized_url", json::get_string(resource, "source_url"));
  // Normalize the caller spelling out of identity; bytes/attachment outcome remain versioned.
  Json identity = snapshot;
  identity["resource"]["source_url"] = uri;
  const auto source_id = "src_url_" + Sha256::hex(identity.dump());
  auto lock = db.lock();
  sql::Txn txn(db.conn());
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto prior, prov.get_source(source_id));
  if (prior && prior->metadata.contains("result")) {
    auto cached = prior->metadata["result"];
    // User deletion is respected; a stale cached conversation is not returned as saved.
    bool present = true;
    for (const auto& c : cached["conversations"]) {
      LOOM_TRY_ASSIGN(auto found, db.get_conv(c.at("id").get<std::string>()));
      present = present && found.has_value();
    }
    if (present) { cached["already_imported"] = true; LOOM_TRY(txn.commit()); return cached; }
    return Error(Errc::Conflict, "url import conversation was deleted");
  }
  std::string previous;
  LOOM_TRY_ASSIGN(auto find, db.conn().prepare("SELECT id FROM loom_sources WHERE uri=? AND parser=? ORDER BY rowid DESC LIMIT 1"));
  find.bind_all(uri, kParser);
  LOOM_TRY_ASSIGN(auto row, find.step());
  if (row) previous = find.get_text(0);
  find = sql::Stmt{};
  LOOM_TRY_ASSIGN(auto root_blob, keep_bytes(blobs, resource));
  Json result{{"format", "url"}, {"source_id", source_id}, {"blob_hash", root_blob.hash},
              {"status", parsed.value("status", "unsupported")}, {"reason", parsed.value("reason", "unknown")},
              {"source_url", resource.value("source_url", "")}, {"final_url", resource.value("final_url", "")},
              {"conversations", Json::array()}, {"messages", 0}, {"already_imported", false}, {"llm_calls", 0},
              {"previous_source_id", previous}, {"attachments", Json::array()}};
  for (const auto* key : {"adapter", "branch", "attachments_mode", "locator"})
    if (parsed.contains(key)) result[key] = parsed[key];
  std::vector<std::pair<std::string, std::string>> subjects;
  std::vector<std::pair<std::string, Json>> asset_sources;
  if (!parsed["messages"].empty()) {
    auto name = title.empty() ? json::get_string(parsed, "title") : title;
    if (name.empty()) name = uri;
    LOOM_TRY_ASSIGN(auto conv, db.create_conv(name));
    ConvPatch cp; cp.source = mapping["conversation_source"].get<std::string>();
    cp.metadata = Json{{"url_import", {{"source_id", source_id}, {"status", result["status"]},
                      {"reason", result["reason"]}, {"source_url", result["source_url"]},
                      {"final_url", result["final_url"]}, {"scope", parsed.value("scope", "")},
                      {"previous_source_id", previous}, {"analysis", "not_requested"}}}};
    for (const auto* key : {"adapter", "branch", "attachments_mode", "locator"})
      if (parsed.contains(key)) (*cp.metadata)["url_import"][key] = parsed[key];
    LOOM_TRY(db.update_conv(conv.id, cp));
    std::optional<std::string> parent;
    std::map<std::string, std::string> message_ids;
    std::size_t sequence = 0;
    for (auto& item : parsed["messages"]) {
      if (!item.is_object() || !item.contains("role") || !item["role"].is_string() ||
          !item.contains("text") || !item["text"].is_string() || !item["attachments"].is_array())
        return Error(Errc::InvalidArgument, "invalid url message");
      Json attachments = Json::array();
      for (auto& attachment : item["attachments"]) {
        if (attachment.contains("body_base64")) {
          LOOM_TRY_ASSIGN(auto blob, keep_bytes(blobs, attachment));
          SourceRecord asset;
          asset.id = "src_url_asset_" + Sha256::hex(source_id + attachment.dump());
          asset.kind = mapping["source_kind"].get<std::string>(); asset.uri = json::get_string(attachment, "source_url");
          asset.blob_hash = blob.hash; asset.size = blob.size; asset.mime = json::get_string(attachment, "mime");
          asset.format = "attachment"; asset.parser = "loom.importer.url_share.attachment"; asset.parser_version = kVersion;
          asset.metadata = attachment;
          LOOM_TRY(prov.add_source(asset));
          asset_sources.emplace_back(asset.id, Json{{"json_pointer", attachment.value("pointer", "")}});
          attachment["source_id"] = asset.id;
          // Keep partial/denied bytes as evidence, but never expose them as an available attachment.
          if (attachment.value("status", "") == "complete") attachments.push_back(blobs.path_for(blob.hash).string());
        }
        result["attachments"].push_back(attachment);
      }
      NewMessage message;
      message.conv_id = conv.id; message.role = item["role"].get<std::string>();
      message.text = item["text"].get<std::string>(); message.parent_id = parent; message.attachments = attachments;
      if (item.contains("parent_pointer")) {
        message.parent_id.reset();
        if (!item["parent_pointer"].is_null()) {
          if (!item["parent_pointer"].is_string()) return Error(Errc::InvalidArgument, "invalid url parent pointer");
          const auto found = message_ids.find(item["parent_pointer"].get<std::string>());
          if (found == message_ids.end()) return Error(Errc::InvalidArgument, "unresolved url parent pointer");
          message.parent_id = found->second;
        }
      }
      message.metadata = {{"url_import", {{"source_id", source_id}, {"sequence", sequence++},
                          {"pointer", item.value("pointer", "")}, {"attachments", item["attachments"]},
                          {"analysis", "not_requested"}}}};
      for (const auto* key : {"provider_node_id", "provider_message_id", "parent_pointer", "active"})
        if (item.contains(key)) message.metadata["url_import"][key] = item[key];
      if (parsed.contains("locator")) message.metadata["url_import"]["locator"] = parsed["locator"];
      LOOM_TRY_ASSIGN(auto id, db.create_msg(message));
      // Explicit opt-out from the background semantic queue: import alone runs no model.
      MsgPatch mp; mp.semantic_status = "done";
      if (item.contains("active")) {
        if (!item["active"].is_boolean()) return Error(Errc::InvalidArgument, "invalid url branch status");
        if (!item["active"].get<bool>()) mp.status = std::string(msg_status::kVersion);
      }
      LOOM_TRY(db.update_msg(id, mp));
      if (item.contains("parent_pointer")) {
        const auto pointer = json::get_string(item, "pointer");
        if (pointer.empty() || !message_ids.emplace(pointer, id).second)
          return Error(Errc::InvalidArgument, "duplicate url message pointer");
      }
      parent = id; subjects.emplace_back(id, item.value("pointer", ""));
    }
    LOOM_TRY_ASSIGN(auto saved, db.get_conv(conv.id));
    if (!saved) return Error(Errc::Internal, "url import conversation missing");
    result["conversations"].push_back(saved->to_json()); result["messages"] = sequence;
  }
  SourceRecord source;
  source.id = source_id; source.kind = mapping["source_kind"].get<std::string>(); source.uri = uri; source.blob_hash = root_blob.hash;
  source.size = root_blob.size; source.mime = json::get_string(resource, "mime"); source.format = snapshot.value("mode", "page");
  source.parser = kParser; source.parser_version = kVersion;
  source.metadata = {{"result", result}, {"snapshot", snapshot}, {"previous_source_id", previous}};
  LOOM_TRY(prov.add_source(source));
  NodeOptions no; no.metadata = {{"source_id", source_id}, {"status", result["status"]}, {"reason", result["reason"]}};
  LOOM_TRY_ASSIGN(auto node, db.create_node(uri, mapping["source_node_kind"].get<std::string>(), no));
  LOOM_TRY(located(prov, node, "node", source_id, Json::object()));
  auto source_locator = [&](const std::string& pointer) {
    // An HTML script locator scopes the pointer into decoded embedded data;
    // it is never misrepresented as a JSON pointer into the HTML document.
    if (parsed.contains("locator")) {
      auto locator = parsed["locator"];
      locator["json_pointer"] = pointer;
      return Json{{"embedded_data", locator}};
    }
    return Json{{"json_pointer", pointer}};
  };
  for (const auto& c : result["conversations"])
    LOOM_TRY(located(prov, c.at("id").get<std::string>(), "conversation", source_id,
                     source_locator(parsed.value("conversation_pointer", "/conversation"))));
  for (const auto& subject : subjects) {
    LOOM_TRY(located(prov, subject.first, "message", source_id, source_locator(subject.second)));
    LOOM_TRY(db.create_link(subject.first, node, mapping["part_relation"].get<std::string>(), 1.0, {{"source_id", source_id}}));
  }
  for (const auto& asset : asset_sources) {
    LOOM_TRY(located(prov, asset.first, "source", source_id, asset.second));
    NodeOptions an; an.metadata = {{"source_id", asset.first}, {"locator", asset.second}};
    LOOM_TRY_ASSIGN(auto asset_node, db.create_node(asset.first, mapping["attachment_node_kind"].get<std::string>(), an));
    LOOM_TRY(located(prov, asset_node, "node", asset.first, Json::object()));
    LOOM_TRY(db.create_link(asset_node, node, mapping["part_relation"].get<std::string>(), 1.0, asset.second));
  }
  if (!previous.empty()) LOOM_TRY(located(prov, source_id, "source", previous, {{"relation", mapping["revision_relation"]}}));
  LOOM_TRY(txn.commit());
  return result;
}

Result<Json> import_request_file(Runtime& rt, const std::filesystem::path& path, const std::string& title) {
  LOOM_TRY_ASSIGN(auto preset, policy(rt.config().get("loom_url_import", Json::object())));
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > preset["limits"]["max_bytes"].get<std::uintmax_t>())
    return Error(Errc::InvalidArgument, "url descriptor unreadable or oversized");
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(path));
  LOOM_TRY_ASSIGN(auto request, json::parse(bytes));
  LOOM_TRY_ASSIGN(auto snapshot, fetch(request, preset));
  return store(rt.db(), rt.blobs(), rt.provenance(), std::move(snapshot), title, preset["projection"]);
}
}  // namespace loom::url_share
