// Explicit exchange-packet selection into the existing KnowledgeStore.
// Packet history is preserved verbatim as JSON; the native validation scope
// covers head identity, selected native rows, provenance, references and quotes.
// Python callers additionally validate reversible history using packet.py.
#pragma once

#include "loom/db.h"
#include "loom/knowledge_store.h"

namespace loom::kb {

class GraphPacketStore {
 public:
  explicit GraphPacketStore(Database& db) : db_(db), store_(db) {}
  // accept: explicit acceptance, target, packet, selection and expected_rows.
  // read: immutable receipt plus row-drift status. replay: verifies every row
  // and returns the original receipt; missing rows are drift, never repaired.
  Result<Json> execute(const Json& request);

 private:
  Database& db_;
  KnowledgeStore store_;
};

}  // namespace loom::kb
