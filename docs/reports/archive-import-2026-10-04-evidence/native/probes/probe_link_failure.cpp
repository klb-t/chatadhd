#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "loom/db.h"
#include "loom/importer.h"
#include "loom/event_bus.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include <miniz.h>
using namespace loom;
template<class T> T take(Result<T> r) {
  if (!r) throw std::runtime_error(r.error().message);
  return std::move(*r);
}
void require(Status s) { if (!s) throw std::runtime_error(s.error().message); }
int main(int argc, char** argv) {
  const std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir);
  auto db = take(Database::open(dir / "probe.db"));
  EventBus bus;
  BlobStore blobs(dir / "blobs", *db);
  ProvenanceStore provenance(*db);
  ConversationImporter importer(*db, bus, &blobs, &provenance);
  const std::string conversations = R"([{"uuid":"synthetic-c","name":"Synthetic conversation","chat_messages":[{"uuid":"synthetic-m","sender":"human","text":"synthetic placeholder"}]}])";
  const std::string malformed = R"([{"uuid":"synthetic-project","name":"Synthetic project","docs":[{"filename":"synthetic.txt","content":"placeholder"}]}])";
  const auto source = dir / "source.zip";
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, source.c_str(), 0) ||
      !mz_zip_writer_add_mem(&zip, "conversations.json", conversations.data(), conversations.size(), MZ_DEFAULT_COMPRESSION) ||
      !mz_zip_writer_add_mem(&zip, "projects.json", malformed.data(), malformed.size(), MZ_DEFAULT_COMPRESSION) ||
      !mz_zip_writer_finalize_archive(&zip) || !mz_zip_writer_end(&zip)) throw std::runtime_error("zip generation failed");
  ImportOptions options;
  options.resume = true;
  require(db->conn().exec("CREATE TRIGGER injected_link_failure BEFORE INSERT ON links BEGIN SELECT RAISE(FAIL, 'synthetic link failure'); END;"));
  const auto first = take(importer.import_file(source, options));
  require(db->conn().exec("DROP TRIGGER injected_link_failure"));
  const auto second = take(importer.import_file(source, options));
  const auto third = take(importer.import_file(source, options));
  auto count = [&](const char* query) { return take(db->conn().query_int(query)).value_or(0); };
  auto journal = take(db->conn().query_text("SELECT metadata FROM loom_import_checkpoints WHERE member='projects.json' AND source_index=-1"));
  auto final_source = take(provenance.get_source(first.source_id));
  Json result{{"first", first.to_json()}, {"second", second.to_json()}, {"third", third.to_json()},
      {"counts", {{"project_nodes", count("SELECT COUNT(*) FROM nodes WHERE kind='export:project'")},
                   {"unknown_member_nodes", count("SELECT COUNT(*) FROM nodes WHERE kind='export:member'")}}},
      {"project_checkpoint", journal ? Json::parse(*journal) : Json(nullptr)},
      {"final_source_metadata", final_source ? final_source->metadata : Json(nullptr)}};
  require(fsutil::write_file(dir / "results.json", result.dump(2)));
  std::cout << "first_partial=" << first.export_report["partial"] << " second_partial=" << second.export_report["partial"]
            << " third_cached=" << third.already_imported << '\n';
  return 0;
}
