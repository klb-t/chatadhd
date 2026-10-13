// Where media outputs of the use-model action go: the blob store (whose
// placement is the storage registry's), a graph node and an artifact record.
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/use_model.h"

namespace loom::llm {

namespace {

class BlobOutputStore final : public OutputStore {
 public:
  BlobOutputStore(BlobStore& blobs, ProvenanceStore* provenance, Database* db)
      : blobs_(blobs), provenance_(provenance), db_(db) {}

  Result<Json> store(std::string_view bytes, std::string_view mime, const Json& provenance) override {
    LOOM_TRY_ASSIGN(BlobRef ref, blobs_.put(bytes, mime));
    Json out{{"blob_hash", ref.hash}, {"size", ref.size}, {"mime", std::string(mime)},
             {"node_id", nullptr}, {"artifact_id", nullptr}};
    const std::string kind = json::get_string(provenance, "output_kind", "media");
    if (provenance_) {
      ArtifactRecord artifact;
      artifact.kind = "model_output";
      artifact.title = kind + " from " + json::get_string(provenance, "requested_model");
      artifact.blob_hash = ref.hash;
      artifact.mime = std::string(mime);
      artifact.metadata = provenance;
      LOOM_TRY_ASSIGN(std::string artifact_id, provenance_->add_artifact(std::move(artifact)));
      out["artifact_id"] = artifact_id;
    }
    if (db_) {
      NodeOptions opts;
      opts.tags = Json::array({"model_output", kind});
      opts.metadata = Json{{"blob_hash", ref.hash}, {"size", ref.size}, {"mime", std::string(mime)},
                           {"output_kind", kind}, {"produced_by", provenance}};
      LOOM_TRY_ASSIGN(std::string node_id, db_->get_or_create_node("blob:" + ref.hash, "blob", opts));
      out["node_id"] = node_id;
    }
    return out;
  }

 private:
  BlobStore& blobs_;
  ProvenanceStore* provenance_;
  Database* db_;
};

}  // namespace

std::unique_ptr<OutputStore> make_output_store(BlobStore& blobs, ProvenanceStore* provenance, Database* db) {
  return std::make_unique<BlobOutputStore>(blobs, provenance, db);
}

}  // namespace loom::llm
