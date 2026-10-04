#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/media_providers.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

using namespace loom;
template<class T> T must(Result<T> value) {
  if (!value) throw std::runtime_error(value.error().to_string());
  return std::move(value).value();
}

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::filesystem::path output = argv[1], input = argv[2];
  std::filesystem::create_directories(output);
  const auto original = must(fsutil::read_file(input));
  Json results = Json::array();
  for (const auto& name : {"no_provenance_control", "blob_import_file", "blob_import_screenshot"}) {
    const bool provenance = std::string(name) != "no_provenance_control";
    const bool direct = std::string(name) == "blob_import_screenshot";
    const auto root = output / name;
    std::filesystem::create_directories(root);
    const auto source = root / "synthetic.png";
    must(fsutil::write_file(source, original));
    DbOptions database_options; database_options.enable_fts = false;
    auto database = must(Database::open(root / "probe.db", database_options));
    BlobStore blobs(root / "blobs", *database);
    ProvenanceStore sources(*database);
    EventBus events;
    Secrets secrets(root / "never-written-secrets.json");
    secrets.set("ocr_space_api_key", "SYNTHETIC_ONLY_NOT_A_REAL_KEY");
    net::ScriptedTransport transport;
    transport.set_fallback(net::ScriptedTransport::Reply::json(200,
        Json{{"IsErroredOnProcessing", false}, {"ParsedResults", Json::array({Json{{"ParsedText", "User: synthetic offline OCR fixture"}}})}}));
    MediaProviders media(secrets, transport);
    ConversationImporter importer(*database, events, provenance ? &blobs : nullptr,
                                   provenance ? &sources : nullptr, &media);
    ImportOptions options;
    options.record_provenance = provenance;
    bool succeeded = false;
    std::string hash;
    if (direct) {
      auto result = importer.import_screenshot(source, options);
      succeeded = result && result->size() == 1;
      auto stored = must(sources.list_sources());
      if (!stored.empty()) hash = stored.front().blob_hash;
    } else {
      auto result = importer.import_file(source, options);
      succeeded = result && result->conversations.size() == 1;
      if (result) hash = result->blob_hash;
    }
    const auto requests = transport.requests();
    const std::string body = requests.empty() ? "" : requests[0].body;
    Json record{{"scenario", name}, {"import_succeeded_against_nonvalidating_mock", succeeded},
                {"mock_requests", requests.size()}, {"request_body", body},
                {"expected_png_uri_present", body.find("base64Image=data%3Aimage%2Fpng%3Bbase64%2C") != std::string::npos},
                {"empty_format_uri_present", body.find("base64Image=data%3Aimage%2F%3Bbase64%2C") != std::string::npos},
                {"live_provider_calls", 0}, {"source_sha256", Sha256::hex(original)}};
    if (provenance) {
      const auto blob_path = blobs.path_for(hash);
      record["blob_filename"] = blob_path.filename().string();
      record["blob_extension"] = blob_path.extension().string();
      record["blob_bytes_match_source"] = must(fsutil::read_file(blob_path)) == original;
    }
    results.push_back(std::move(record));
  }
  must(fsutil::write_file(output / "results.json", results.dump(2) + "\n"));
  std::cout << results.dump(2) << "\n";
  return 0;
}
