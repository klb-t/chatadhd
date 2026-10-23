// loom/dimensions.h — the dimension registry of the graph as data (K3-1; owner R74, R76, R77, R84).
//
// A dimension is an entry of loom/data/graph/dimensions.pack (schema loom.dimensions/1); there is no enum of dimensions in the
// code and the list is open. An entry declares its coordinate codec, the ordering of its nodes, how neighbours are reached
// (`walk`, data for the later database binding), what a point between two nodes means (`between`), the words an address may use
// and their meaning. Everything concrete in the shipped pack is an example.
//
// The codec registry is open in data: a codec this kernel does not know is not an error, the dimension then behaves like a
// `name` (a string compared by the dimension's order) and the registry reports the advisory `codec_unknown`. Only native
// comparison is a closed list; the reason, the date and the reversal are recorded in the pack (`closed_lists`).
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loom/advisory.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::dimensions {

struct Dimension {
  std::string id;
  Json label = Json::object();  // {pl,en}
  Json why = Json::object();    // {pl,en}
  std::string codec;            // as declared in the pack
  std::string effective_codec;  // the codec used for comparison: `codec` when the kernel knows it, else "name"
  bool codec_known = true;
  std::vector<std::string> levels;  // names of the levels, coarsest first (codec levels)
  std::string origin = "current";   // what 0 means: current | first | last (anything else: advisory origin_unknown, current)
  std::string off_lineage_origin = "last";  // origin of a relative coordinate on a finer level when the previous level left the current node's lineage
  Json order = Json::object();
  Json walk = Json::object();
  Json between = Json::object();  // {mode, ...}
  Json step_from_fov = Json::object();
  Json words = Json::object();  // the words valid here: registry default_words overlaid with the dimension's own
  bool node_coordinate = true;
  bool example = true;

  std::string between_mode() const;
};

// Seconds since the epoch of a `YYYY-MM-DD[Thh:mm[:ss[.fff]][Z|±hh[:]mm]]` text; nullopt when the text has another shape.
std::optional<double> datetime_epoch(std::string_view text);

class Registry {
 public:
  static Result<Registry> from_json(Json pack);
  static Result<Registry> load_file(const std::filesystem::path& file);
  // The pack embedded in the binary (a copy of loom/data/graph/dimensions.pack).
  static const Registry& builtin();

  const Dimension* find(std::string_view id) const;
  std::vector<std::string> ids() const;
  const std::string& default_dimension() const { return default_dimension_; }
  const advisory::Catalogue& catalogue() const { return catalogue_; }
  const Json& pack() const { return pack_; }
  // Advisories found while loading (an unknown codec, ...). Loading never fails because of them.
  const std::vector<Json>& load_advisories() const { return load_advisories_; }
  // between_modes[mode].available; a mode that is not in the pack is not available.
  bool between_available(std::string_view mode) const;
  // The operation of a word on a dimension ({"end":"last"}, {"offset":-1}, ...) or nullptr.
  const Json* word(const Dimension& d, std::string_view word) const;

  // Order of two coordinates (key values) on a dimension: -1, 0, 1; nullopt when the values do not compare under its codec.
  std::optional<int> compare(const Dimension& d, const Json& a, const Json& b) const;
  // A coordinate as a number on the dimension's axis (numbers as they are, datetime as epoch seconds); nullopt otherwise.
  std::optional<double> numeric(const Dimension& d, const Json& v) const;

 private:
  Json pack_;
  std::map<std::string, Dimension, std::less<>> dims_;
  std::vector<std::string> order_;
  std::string default_dimension_;
  advisory::Catalogue catalogue_;
  std::vector<Json> load_advisories_;
};

}  // namespace loom::dimensions
