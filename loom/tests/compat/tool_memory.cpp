// Compat commands for MemoryEngine (engine/memory_engine.py port).
#include <filesystem>

#include "compat_registry.h"
#include "loom/memory_engine.h"
#include "loom/semantic_analyzer.h"

using namespace loom;
using namespace loom::compat;
namespace fs = std::filesystem;

namespace {

Json dump(MemoryEngine& mem) {
  Json nodes = Json::array();
  for (const auto& n : mem.get_all()) nodes.push_back(n.to_json());
  return Json{{"nodes", nodes}, {"context", mem.get_active_context()}, {"graph", mem.get_graph_data()}};
}

}  // namespace

// <dir> @spec.json : spec is a list of steps, applied in order:
//   {"kind":"add","content":str,"parent_ref":int|null,"node_type":str?,
//    "metadata":{}?,"tags":[...]?, "auto_tag": bool?}
//   {"kind":"update","ref":int,"fields":{...}}
//   {"kind":"delete","ref":int,"recursive":bool?}
// `ref`/`parent_ref` index into the sequence of "add" steps seen so far
// (0-based, in the order they appear in `spec`). Writes <dir>/memory.json
// and prints the resulting dump (nodes + get_active_context() + graph data).
LOOM_COMPAT_COMMAND(cmd_memory_build, "memory-build", "<dir> @spec.json : build memory.json, print the dump") {
  if (args.size() < 2) return fail("usage: memory-build <dir> @spec.json");
  fs::path dir = args[0];
  auto spec = arg_json(args[1]);
  if (!spec || !spec->is_array()) return fail("expected a JSON array");

  std::unique_ptr<SemanticAnalyzer> an;
  {
    auto r = SemanticAnalyzer::create();
    if (!r) return fail("SemanticAnalyzer::create failed: " + r.error().to_string());
    an = std::move(r).value();
  }
  MemoryEngine mem(dir / "memory.json", an.get());

  std::vector<std::string> add_ids;  // ids in "add" order, indexable by ref
  for (const auto& step : *spec) {
    std::string kind = json::get_string(step, "kind", "add");
    if (kind == "add") {
      std::optional<std::string> parent;
      if (const Json* pr = json::find(step, "parent_ref"); pr && pr->is_number_integer()) {
        auto idx = pr->get<std::size_t>();
        if (idx < add_ids.size()) parent = add_ids[idx];
      }
      std::vector<std::string> tags;
      if (const Json* t = json::find(step, "tags"); t && t->is_array()) {
        for (const auto& x : *t) tags.push_back(x.get<std::string>());
      }
      Json meta = Json::object();
      if (const Json* m = json::find(step, "metadata"); m && m->is_object()) meta = *m;
      auto id = mem.add_node(json::get_string(step, "content"), parent, json::get_string(step, "node_type", "text"),
                             meta, tags);
      if (!id) return fail("add_node failed: " + id.error().to_string());
      add_ids.push_back(*id);
    } else if (kind == "update") {
      auto idx = json::get_int(step, "ref", -1);
      if (idx < 0 || static_cast<std::size_t>(idx) >= add_ids.size()) return fail("bad ref");
      const Json* fields = json::find(step, "fields");
      if (!fields) return fail("update needs fields");
      if (auto st = mem.update_node(add_ids[static_cast<std::size_t>(idx)], *fields); !st) {
        return fail("update_node failed: " + st.error().to_string());
      }
    } else if (kind == "delete") {
      auto idx = json::get_int(step, "ref", -1);
      if (idx < 0 || static_cast<std::size_t>(idx) >= add_ids.size()) return fail("bad ref");
      bool recursive = json::get_bool(step, "recursive", false);
      if (auto st = mem.delete_node(add_ids[static_cast<std::size_t>(idx)], recursive); !st) {
        return fail("delete_node failed: " + st.error().to_string());
      }
    } else {
      return fail("unknown step kind: " + kind);
    }
  }
  print_json(dump(mem));
  return 0;
}

// <dir> : read <dir>/memory.json (written by anyone) and print the same
// dump shape as memory-build.
LOOM_COMPAT_COMMAND(cmd_memory_dump, "memory-dump", "<dir> : read memory.json, print the dump") {
  if (args.empty()) return fail("usage: memory-dump <dir>");
  fs::path dir = args[0];
  MemoryEngine mem(dir / "memory.json");
  print_json(dump(mem));
  return 0;
}
