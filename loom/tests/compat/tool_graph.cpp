// Compat command: run a message sequence through GraphEngine (regex-only,
// exactly as engine/graph_engine.py's GraphEngine(db, semantic_llm=None)
// behaves) and GraphMemorySelector, on a fresh DB, and print an
// ID-independent (normalised) dump so it can be compared against the same
// scenario run through the real Python engine.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

#include "compat_registry.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/graph_engine.h"
#include "loom/graph_memory.h"
#include "loom/semantic_analyzer.h"

using namespace loom;
using namespace loom::compat;
namespace fs = std::filesystem;

namespace {

std::string node_key(std::string_view kind, std::string_view label) {
  return "NODE:" + std::string(kind) + ":" + std::string(label);
}
std::string msg_key(std::size_t i) { return "MSG:" + std::to_string(i); }
std::string conv_key(std::string_view title) { return "CONV:" + std::string(title); }

double round6(double v) { return std::round(v * 1e6) / 1e6; }

}  // namespace

// <dir> @spec.json : spec = {"messages":[{"conv":str,"role":str,"text":str}],
// "queries":[{"query":str,"exclude_conv":str|null,"depth":int?}]}.
// Prints {"nodes":[[kind,label],...], "links":[[src_key,dst_key,type,weight],...],
// "contexts":[str,...]}, all sorted for order-independent comparison.
LOOM_COMPAT_COMMAND(cmd_graph_run, "graph-run", "<dir> @spec.json : run messages through GraphEngine, print a normalised dump") {
  if (args.size() < 2) return fail("usage: graph-run <dir> @spec.json");
  fs::path dir = args[0];
  auto spec = arg_json(args[1]);
  if (!spec || !spec->is_object()) return fail("expected a JSON object");

  auto dbr = Database::open(dir / "g.db");
  if (!dbr) return fail("Database::open failed: " + dbr.error().to_string());
  auto db = std::move(dbr).value();
  EventBus bus;
  auto anr = SemanticAnalyzer::create();
  if (!anr) return fail("SemanticAnalyzer::create failed: " + anr.error().to_string());
  auto an = std::move(anr).value();
  GraphEngine ge(*db, bus, *an);
  ge.start();
  Config cfg(dir / "config.json");
  GraphMemorySelector gm(*db, cfg, *an);

  std::map<std::string, std::string> conv_title_to_id;
  std::map<std::string, std::string> msg_id_to_key;
  std::map<std::string, std::string> conv_id_to_key;

  const Json* messages = json::find(*spec, "messages");
  if (!messages || !messages->is_array()) return fail("spec.messages must be an array");
  std::size_t i = 0;
  for (const auto& m : *messages) {
    std::string conv_title = json::get_string(m, "conv", "default");
    std::string role = json::get_string(m, "role", "user");
    std::string text = json::get_string(m, "text");
    auto it = conv_title_to_id.find(conv_title);
    std::string cid;
    if (it == conv_title_to_id.end()) {
      auto conv = db->create_conv(conv_title);
      if (!conv) return fail("create_conv failed: " + conv.error().to_string());
      cid = conv->id;
      conv_title_to_id[conv_title] = cid;
      conv_id_to_key[cid] = conv_key(conv_title);
    } else {
      cid = it->second;
    }
    NewMessage nm;
    nm.conv_id = cid;
    nm.text = text;
    nm.role = role;
    auto mid = db->create_msg(nm);
    if (!mid) return fail("create_msg failed: " + mid.error().to_string());
    msg_id_to_key[*mid] = msg_key(i);
    bus.emit(events::kMsgCreated, Json{{"id", *mid}, {"text", text}, {"conv_id", cid}, {"role", role}});
    i++;
  }

  auto nodes = db->list_nodes(std::nullopt, 1'000'000);
  if (!nodes) return fail("list_nodes failed: " + nodes.error().to_string());
  std::map<std::string, std::string> node_id_to_key;
  for (const auto& n : *nodes) node_id_to_key[n.id] = node_key(n.kind, n.label);

  auto resolve = [&](const std::string& id) -> std::string {
    if (auto it = node_id_to_key.find(id); it != node_id_to_key.end()) return it->second;
    if (auto it = msg_id_to_key.find(id); it != msg_id_to_key.end()) return it->second;
    if (auto it = conv_id_to_key.find(id); it != conv_id_to_key.end()) return it->second;
    return "UNKNOWN:" + id;
  };

  auto links = db->get_links();
  if (!links) return fail("get_links failed: " + links.error().to_string());

  std::vector<Json> node_rows;
  for (const auto& n : *nodes) node_rows.push_back(Json::array({n.kind, n.label}));
  std::sort(node_rows.begin(), node_rows.end(),
           [](const Json& a, const Json& b) { return a.dump() < b.dump(); });

  std::vector<Json> link_rows;
  for (const auto& l : *links) {
    link_rows.push_back(Json::array({resolve(l.src), resolve(l.dst), l.link_type, round6(l.weight)}));
  }
  std::sort(link_rows.begin(), link_rows.end(),
           [](const Json& a, const Json& b) { return a.dump() < b.dump(); });

  Json contexts = Json::array();
  if (const Json* queries = json::find(*spec, "queries"); queries && queries->is_array()) {
    for (const auto& q : *queries) {
      std::string query = json::get_string(q, "query");
      GraphSelectOptions opts;
      if (auto ec = json::get_opt_string(q, "exclude_conv")) {
        if (auto it = conv_title_to_id.find(*ec); it != conv_title_to_id.end()) opts.current_conv_id = it->second;
      }
      if (const Json* d = json::find(q, "depth"); d && d->is_number_integer()) opts.depth = d->get<int>();
      contexts.push_back(gm.select_context(query, opts));
    }
  }

  ge.stop();
  print_json(Json{{"nodes", node_rows}, {"links", link_rows}, {"contexts", contexts}});
  return 0;
}
