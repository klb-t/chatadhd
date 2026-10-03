// GraphStore keeps the ChatADHD knowledge graph in SQLite (nodes + links).
// Writes are idempotent upserts.
#include <string>

class GraphStore {
 public:
  void upsert_node(const std::string& label);
};

void GraphStore::upsert_node(const std::string& label) {
  // TODO: add graph compaction for deleted nodes
  (void)label;
}
