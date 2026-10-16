// P24 adapter: recognized relational patterns as an overlay on the edge-kind effective view (no registry of their own).
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "internal.h"
#include "loom/effective_view.h"
#include "loom/generalize.h"
#include "loom/util/fs.h"

namespace loom::generalize {

Json recognition_overlay(const std::vector<model::Instance>& instances, std::string_view promotion) {
  Json ops = Json::array();
  if (promotion == "off") return ops;
  struct Group {
    Json instances = Json::array();
    Json receipt;
  };
  std::map<std::string, Group> groups;
  for (const auto& in : instances) {
    if (in.recognition.is_null() || json::get_string(in.recognition, "status") != "candidate") continue;
    auto& g = groups[in.paradigm];
    if (g.receipt.is_null()) g.receipt = in.recognition;
    g.instances.push_back(Json{{"instance", in.id}, {"subject", in.subject}});
  }
  for (const auto& [paradigm, g] : groups) {
    const std::string id = "structure:" + paradigm;
    Json entry{{"meaning", Json{{"en", "Recognized structure '" + paradigm + "' (a hypothesis until confirmed)."},
                                {"pl", "Rozpoznana struktura '" + paradigm + "' (hipoteza do potwierdzenia)."}}},
               {"direction", "directed"},
               {"parent", "relation"},
               {"perspectives", Json::array()},
               {"hypothesis_kind", "structure_recognition"},
               {"provenance", Json{{"origin", "generalize"},
                                   {"via", "recognition"},
                                   {"method", json::get_string(g.receipt, "method")},
                                   {"version", json::get_string(g.receipt, "version")},
                                   {"pack_hash", json::get_string(g.receipt, "pack_hash")},
                                   {"instances", g.instances}}}};
    ops.push_back(Json{{"view", "edge_kinds"}, {"op", "add"}, {"id", id}, {"entry", entry}});
    if (promotion != "auto") ops.push_back(Json{{"view", "edge_kinds"}, {"op", "disable"}, {"id", id}});
  }
  return ops;
}

Status write_recognition_overlay(const std::filesystem::path& root, const std::vector<model::Instance>& instances, std::string_view promotion) {
  const auto file = root / views::kOverlayDir / "recognition.json";
  Json ops = recognition_overlay(instances, promotion);
  std::error_code ec;
  if (ops.empty()) {
    std::filesystem::remove(file, ec);  // no recognition holds (or mode off): the overlay is gone
    if (ec) return Error(Errc::Io, "recognition overlay: cannot remove " + file.string() + ": " + ec.message());
    return Status();
  }
  Json doc{{"schema", views::kOverlaySchema}, {"name", "recognition"}, {"ops", ops}};
  LOOM_TRY(fsutil::ensure_dir(root / views::kOverlayDir));
  return fsutil::atomic_write(file, json::dump(doc, 2) + "\n");
}

}  // namespace loom::generalize
