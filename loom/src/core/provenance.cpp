#include "loom/provenance.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

#include "loom/db.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom {
namespace {
constexpr std::string_view kLog = "loom.blobs";
namespace sfs = std::filesystem;

std::string or_empty(const std::optional<std::string>& s) { return s.value_or(std::string()); }
std::optional<std::string> none_if_empty(const std::string& s) {
  if (s.empty()) return std::nullopt;
  return s;
}
}  // namespace

// ── JSON ───────────────────────────────────────────────────────────
Json BlobRef::to_json() const { return Json{{"hash", hash}, {"size", size}, {"existed", existed}}; }

Json SourceRecord::to_json() const {
  return Json{{"id", id},       {"kind", kind},     {"uri", uri},
              {"blob_hash", blob_hash}, {"size", size}, {"mime", mime},
              {"format", format}, {"title", title}, {"parser", parser},
              {"parser_version", parser_version}, {"imported_at", imported_at}, {"metadata", metadata}};
}

Json ProvenanceRecord::to_json() const {
  return Json{{"id", id},           {"subject_id", subject_id}, {"subject_kind", subject_kind},
              {"source_id", source_id}, {"locator", locator},   {"transform", transform},
              {"confidence", confidence}, {"created", created}};
}

Json ArtifactRecord::to_json() const {
  return Json{{"id", id},     {"kind", kind},       {"title", title},       {"blob_hash", blob_hash},
              {"mime", mime}, {"task_id", task_id}, {"metadata", metadata}, {"created", created}};
}

Json EventRecord::to_json() const {
  return Json{{"seq", seq},         {"id", id},           {"ts", ts},
              {"type", type},       {"subject_id", subject_id}, {"payload", payload},
              {"input_hash", input_hash}, {"output_hash", output_hash}};
}

// ── BlobStore ──────────────────────────────────────────────────────
BlobStore::BlobStore(sfs::path root, Database& db) : root_(std::move(root)), db_(db) {}

bool BlobStore::is_valid_hash(std::string_view h) noexcept {
  if (h.size() != 64) return false;
  for (char c : h) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

sfs::path BlobStore::path_for(std::string_view hash) const {
  std::string h(hash);
  if (h.size() < 4) return root_ / h;
  return root_ / h.substr(0, 2) / h.substr(2, 2) / h;
}

bool BlobStore::has(std::string_view hash) const {
  if (!is_valid_hash(hash)) return false;
  std::error_code ec;
  return sfs::exists(path_for(hash), ec);
}

Status BlobStore::register_blob(const BlobRef& ref, std::string_view mime) {
  auto lk = db_.lock();
  return db_.conn().run("INSERT OR IGNORE INTO loom_blobs (hash, size, mime, created) VALUES (?, ?, ?, ?)", ref.hash,
                        ref.size, none_if_empty(std::string(mime)), timeutil::utc_now_iso());
}

Result<BlobRef> BlobStore::put(std::string_view bytes, std::string_view mime) {
  BlobRef ref;
  ref.hash = Sha256::hex(bytes);
  ref.size = static_cast<std::int64_t>(bytes.size());
  sfs::path dst = path_for(ref.hash);
  std::error_code ec;
  if (sfs::exists(dst, ec)) {
    ref.existed = true;
  } else {
    LOOM_TRY(fsutil::ensure_dir(dst.parent_path()));
    sfs::path tmp = root_ / "tmp";
    LOOM_TRY(fsutil::ensure_dir(tmp));
    tmp /= ref.hash + "." + random_hex(8) + ".part";
    LOOM_TRY(fsutil::write_file(tmp, bytes));
    sfs::rename(tmp, dst, ec);
    if (ec) {
      sfs::remove(tmp, ec);
      if (!sfs::exists(dst)) return Error(Errc::Io, "blob rename failed: " + dst.string());
      ref.existed = true;  // lost a race with another writer - same content
    } else if (auto st = fsutil::make_read_only(dst); !st) {
      log::debug(kLog, "chmod 444 failed: {}", st.error().message);
    }
  }
  LOOM_TRY(register_blob(ref, mime));
  return ref;
}

Result<BlobRef> BlobStore::put_file(const sfs::path& file, std::string_view mime) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return Error(Errc::Io, "cannot open " + file.string());
  sfs::path tmpdir = root_ / "tmp";
  LOOM_TRY(fsutil::ensure_dir(tmpdir));
  sfs::path tmp = tmpdir / ("stream." + random_hex(12) + ".part");
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out) return Error(Errc::Io, "cannot create " + tmp.string());
  Sha256 sha;
  std::int64_t size = 0;
  std::vector<char> buf(1 << 16);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize got = in.gcount();
    if (got <= 0) break;
    sha.update(buf.data(), static_cast<std::size_t>(got));
    out.write(buf.data(), got);
    size += got;
  }
  out.close();
  std::error_code ec;
  if (in.bad() || !out) {
    sfs::remove(tmp, ec);
    return Error(Errc::Io, "copy failed: " + file.string());
  }
  BlobRef ref;
  ref.hash = sha.finish_hex();
  ref.size = size;
  sfs::path dst = path_for(ref.hash);
  if (sfs::exists(dst, ec)) {
    ref.existed = true;
    sfs::remove(tmp, ec);
  } else {
    LOOM_TRY(fsutil::ensure_dir(dst.parent_path()));
    sfs::rename(tmp, dst, ec);
    if (ec) {
      sfs::remove(tmp, ec);
      if (!sfs::exists(dst)) return Error(Errc::Io, "blob rename failed: " + dst.string());
      ref.existed = true;
    } else if (auto st = fsutil::make_read_only(dst); !st) {
      log::debug(kLog, "chmod 444 failed: {}", st.error().message);
    }
  }
  LOOM_TRY(register_blob(ref, mime));
  return ref;
}

Result<std::string> BlobStore::read(std::string_view hash) const {
  if (!is_valid_hash(hash)) return Error(Errc::InvalidArgument, "invalid blob hash");
  if (!has(hash)) return Error(Errc::NotFound, "blob not found: " + std::string(hash));
  return fsutil::read_file(path_for(hash));
}

Status BlobStore::verify(std::string_view hash) const {
  if (!has(hash)) return Error(Errc::NotFound, "blob not found: " + std::string(hash));
  LOOM_TRY_ASSIGN(std::string actual, sha256_file_hex(path_for(hash)));
  if (actual != hash) return Error(Errc::Conflict, "blob content does not match its hash: " + std::string(hash));
  return {};
}

// ── ProvenanceStore ────────────────────────────────────────────────
namespace {

SourceRecord read_source(const sql::Stmt& st) {
  SourceRecord r;
  r.id = st.get_text(0);
  r.kind = st.get_text(1);
  r.uri = or_empty(st.get_opt_text(2));
  r.blob_hash = or_empty(st.get_opt_text(3));
  r.size = st.is_null(4) ? 0 : st.get_int(4);
  r.mime = or_empty(st.get_opt_text(5));
  r.format = or_empty(st.get_opt_text(6));
  r.title = or_empty(st.get_opt_text(7));
  r.parser = or_empty(st.get_opt_text(8));
  r.parser_version = or_empty(st.get_opt_text(9));
  r.imported_at = st.get_text(10);
  r.metadata = json::parse_or(st.get_text(11), Json::object());
  return r;
}
constexpr std::string_view kSourceCols =
    "id, kind, uri, blob_hash, size, mime, format, title, parser, parser_version, imported_at, metadata";

ProvenanceRecord read_prov(const sql::Stmt& st) {
  ProvenanceRecord r;
  r.id = st.get_text(0);
  r.subject_id = st.get_text(1);
  r.subject_kind = st.get_text(2);
  r.source_id = or_empty(st.get_opt_text(3));
  r.locator = json::parse_or(st.get_text(4), Json::object());
  r.transform = st.get_text(5);
  r.confidence = st.get_double(6);
  r.created = st.get_text(7);
  return r;
}
constexpr std::string_view kProvCols = "id, subject_id, subject_kind, source_id, locator, transform, confidence, created";

ArtifactRecord read_artifact(const sql::Stmt& st) {
  ArtifactRecord r;
  r.id = st.get_text(0);
  r.kind = st.get_text(1);
  r.title = st.get_text(2);
  r.blob_hash = or_empty(st.get_opt_text(3));
  r.mime = or_empty(st.get_opt_text(4));
  r.task_id = or_empty(st.get_opt_text(5));
  r.metadata = json::parse_or(st.get_text(6), Json::object());
  r.created = st.get_text(7);
  return r;
}
constexpr std::string_view kArtifactCols = "id, kind, title, blob_hash, mime, task_id, metadata, created";

template <class T, class F>
Result<std::vector<T>> collect(sql::Stmt& st, F read) {
  std::vector<T> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    out.push_back(read(st));
  }
  return out;
}

Status insert_prov(sql::Connection& c, ProvenanceRecord& rec) {
  if (rec.id.empty()) rec.id = gen_id(id_prefix::kProvenance);
  if (rec.created.empty()) rec.created = timeutil::utc_now_iso();
  if (rec.subject_id.empty()) return Error(Errc::InvalidArgument, "provenance: subject_id required");
  return c.run("INSERT INTO loom_provenance (" + std::string(kProvCols) + ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)", rec.id,
               rec.subject_id, rec.subject_kind, none_if_empty(rec.source_id), json::py_dumps(rec.locator),
               rec.transform, rec.confidence, rec.created);
}

}  // namespace

Result<std::string> ProvenanceStore::add_source(SourceRecord rec) {
  if (rec.id.empty()) rec.id = gen_id(id_prefix::kSource);
  if (rec.imported_at.empty()) rec.imported_at = timeutil::utc_now_iso();
  if (rec.kind.empty()) return Error(Errc::InvalidArgument, "source: kind required");
  auto lk = db_.lock();
  LOOM_TRY(db_.conn().run("INSERT INTO loom_sources (" + std::string(kSourceCols) +
                              ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                          rec.id, rec.kind, none_if_empty(rec.uri), none_if_empty(rec.blob_hash), rec.size,
                          none_if_empty(rec.mime), none_if_empty(rec.format), none_if_empty(rec.title),
                          none_if_empty(rec.parser), none_if_empty(rec.parser_version), rec.imported_at,
                          json::py_dumps(rec.metadata)));
  return rec.id;
}

Result<std::optional<SourceRecord>> ProvenanceStore::get_source(std::string_view id) {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare("SELECT " + std::string(kSourceCols) + " FROM loom_sources WHERE id = ?"));
  st.bind(1, id);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<SourceRecord>{};
  return std::optional<SourceRecord>(read_source(st));
}

Result<std::vector<SourceRecord>> ProvenanceStore::find_sources_by_hash(std::string_view blob_hash) {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare("SELECT " + std::string(kSourceCols) +
                                              " FROM loom_sources WHERE blob_hash = ? ORDER BY imported_at"));
  st.bind(1, blob_hash);
  return collect<SourceRecord>(st, read_source);
}

Result<std::vector<SourceRecord>> ProvenanceStore::list_sources(int limit, std::optional<std::string_view> kind) {
  auto lk = db_.lock();
  std::string sql = "SELECT " + std::string(kSourceCols) + " FROM loom_sources";
  if (kind) sql += " WHERE kind = ?";
  sql += " ORDER BY imported_at DESC, rowid DESC LIMIT ?";
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare(sql));
  int i = 1;
  if (kind) st.bind(i++, *kind);
  st.bind(i, limit);
  return collect<SourceRecord>(st, read_source);
}

Result<std::string> ProvenanceStore::add(ProvenanceRecord rec) {
  auto lk = db_.lock();
  LOOM_TRY(insert_prov(db_.conn(), rec));
  return rec.id;
}

Result<int> ProvenanceStore::add_many(std::vector<ProvenanceRecord> recs) {
  auto lk = db_.lock();
  sql::Txn txn(db_.conn());
  LOOM_TRY(txn.begin_status());
  for (auto& r : recs) LOOM_TRY(insert_prov(db_.conn(), r));
  LOOM_TRY(txn.commit());
  return static_cast<int>(recs.size());
}

Result<std::vector<ProvenanceRecord>> ProvenanceStore::for_subject(std::string_view subject_id) {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare("SELECT " + std::string(kProvCols) +
                                              " FROM loom_provenance WHERE subject_id = ? ORDER BY created, rowid"));
  st.bind(1, subject_id);
  return collect<ProvenanceRecord>(st, read_prov);
}

Result<std::vector<ProvenanceRecord>> ProvenanceStore::for_source(std::string_view source_id, int limit) {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare("SELECT " + std::string(kProvCols) +
                                              " FROM loom_provenance WHERE source_id = ? ORDER BY rowid LIMIT ?"));
  st.bind_all(source_id, limit);
  return collect<ProvenanceRecord>(st, read_prov);
}

Result<std::string> ProvenanceStore::add_artifact(ArtifactRecord rec) {
  if (rec.id.empty()) rec.id = gen_id(id_prefix::kArtifact);
  if (rec.created.empty()) rec.created = timeutil::utc_now_iso();
  if (rec.kind.empty()) return Error(Errc::InvalidArgument, "artifact: kind required");
  auto lk = db_.lock();
  LOOM_TRY(db_.conn().run("INSERT INTO loom_artifacts (" + std::string(kArtifactCols) +
                              ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                          rec.id, rec.kind, rec.title, none_if_empty(rec.blob_hash), none_if_empty(rec.mime),
                          none_if_empty(rec.task_id), json::py_dumps(rec.metadata), rec.created));
  return rec.id;
}

Result<std::optional<ArtifactRecord>> ProvenanceStore::get_artifact(std::string_view id) {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto st,
                  db_.conn().prepare("SELECT " + std::string(kArtifactCols) + " FROM loom_artifacts WHERE id = ?"));
  st.bind(1, id);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<ArtifactRecord>{};
  return std::optional<ArtifactRecord>(read_artifact(st));
}

Result<std::vector<ArtifactRecord>> ProvenanceStore::list_artifacts(int limit, std::optional<std::string_view> kind) {
  auto lk = db_.lock();
  std::string sql = "SELECT " + std::string(kArtifactCols) + " FROM loom_artifacts";
  if (kind) sql += " WHERE kind = ?";
  sql += " ORDER BY created DESC, rowid DESC LIMIT ?";
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare(sql));
  int i = 1;
  if (kind) st.bind(i++, *kind);
  st.bind(i, limit);
  return collect<ArtifactRecord>(st, read_artifact);
}

// ── EventLog ───────────────────────────────────────────────────────
Result<std::int64_t> EventLog::append(std::string_view type, std::string_view subject_id, const Json& payload,
                                      std::string_view input_hash, std::string_view output_hash) {
  if (type.empty()) return Error(Errc::InvalidArgument, "event type required");
  auto lk = db_.lock();
  auto opt = [](std::string_view s) { return s.empty() ? std::optional<std::string>() : std::optional<std::string>(s); };
  LOOM_TRY(db_.conn().run(
      "INSERT INTO loom_events (id, ts, type, subject_id, payload, input_hash, output_hash) VALUES (?, ?, ?, ?, ?, ?, ?)",
      gen_id(id_prefix::kEvent), timeutil::utc_now_iso(), type, opt(subject_id), json::py_dumps(payload),
      opt(input_hash), opt(output_hash)));
  return db_.conn().last_insert_rowid();
}

Result<std::vector<EventRecord>> EventLog::query(const EventQuery& q) {
  auto lk = db_.lock();
  std::string sql =
      "SELECT seq, id, ts, type, subject_id, payload, input_hash, output_hash FROM loom_events WHERE seq > ?";
  std::string type_arg;
  if (q.type) {
    if (!q.type->empty() && q.type->back() == '*') {
      sql += " AND substr(type, 1, ?) = ?";
      type_arg = q.type->substr(0, q.type->size() - 1);
    } else {
      sql += " AND type = ?";
      type_arg = *q.type;
    }
  }
  if (q.subject_id) sql += " AND subject_id = ?";
  sql += " ORDER BY seq LIMIT ?";
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare(sql));
  int i = 1;
  st.bind(i++, q.after_seq);
  if (q.type) {
    if (!q.type->empty() && q.type->back() == '*') st.bind(i++, static_cast<std::int64_t>(type_arg.size()));
    st.bind(i++, type_arg);
  }
  if (q.subject_id) st.bind(i++, *q.subject_id);
  st.bind(i, q.limit);
  return collect<EventRecord>(st, [](const sql::Stmt& s) {
    EventRecord e;
    e.seq = s.get_int(0);
    e.id = s.get_text(1);
    e.ts = s.get_text(2);
    e.type = s.get_text(3);
    e.subject_id = or_empty(s.get_opt_text(4));
    e.payload = json::parse_or(s.get_text(5), Json::object());
    e.input_hash = or_empty(s.get_opt_text(6));
    e.output_hash = or_empty(s.get_opt_text(7));
    return e;
  });
}

Result<std::int64_t> EventLog::last_seq() {
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(auto v, db_.conn().query_int("SELECT COALESCE(MAX(seq), 0) FROM loom_events"));
  return v.value_or(0);
}

}  // namespace loom
