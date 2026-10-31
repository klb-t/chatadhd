#include "source.h"
#include "loom/db.h"
#include "loom/provenance.h"

namespace loom::chat {
Result<std::string> record_docx_source(Database& db, std::string_view attachment,
                                       std::string_view title, const Json& extraction,
                                       const Json& reader) {
  // Direct file paths keep the extractor's source descriptor; the upload path
  // below has an immutable original in BlobStore and gets a SourceRecord too.
  if (attachment.rfind("blob:", 0) != 0) return std::string();
  const auto slash = attachment.find('/');
  const std::string hash(attachment.substr(5, slash == std::string_view::npos ? slash : slash - 5));
  const auto& source = extraction.at("source");
  if (!BlobStore::is_valid_hash(hash) || json::get_string(source, "sha256") != hash)
    return Error(Errc::Conflict, "docx_source_mismatch");
  auto lock = db.lock();
  ProvenanceStore store(db);
  const std::string id = "src_docx_" + hash;
  LOOM_TRY_ASSIGN(auto existing, store.get_source(id));
  if (existing) {
    if (existing->blob_hash != hash) return Error(Errc::Conflict, "docx_source_mismatch");
    return id;
  }
  SourceRecord record;
  record.id = id;
  record.kind = json::get_string(reader, "source_kind");
  if (record.kind.empty()) return Error(Errc::InvalidArgument, "docx_configuration_invalid");
  record.uri = "blob:" + hash;
  record.blob_hash = hash;
  record.size = json::get_int(source, "size", 0);
  record.mime = json::get_string(reader, "mime");
  record.format = "docx";
  record.title = std::string(title);
  record.parser = json::get_string(extraction, "extractor");
  record.metadata = Json{{"source", source}, {"partial", true}, {"warnings", extraction.at("warnings")}};
  auto added = store.add_source(std::move(record));
  if (added) return *added;
  // A second process may have registered this hash after our read. The unique
  // source id makes retry idempotent without overwriting anyone's provenance.
  auto raced = store.get_source(id);
  if (raced && *raced && (**raced).blob_hash == hash) return id;
  return added.error();
}
}
