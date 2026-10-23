// loom/address.h — a relative address in the graph (K3-1; owner R74, R76, R77): data first, text as one representation.
//
// An address is a list of steps from the outside in; a step names a dimension (empty = the default dimension) and a coordinate:
//   Address = {"steps":[{"dim":"message","coord":{"int":-1}}, {"dim":"version","coord":{"levels":[{"word":"max"},{"word":"last"}]}}]}
//   Coord   = {"int":n} | {"double":x} | {"name":"s"} | {"word":"last"} | {"datetime":"2026-10-22T10:00Z"} | {"pred":"text"}
//           | {"range":[Coord,Coord]} | {"levels":[Coord,...]} | {"<any other codec>":"payload"}
// (levels parts are int, word, name, datetime, pred or typed; range ends are those or levels.) 0 is the current node on a dimension,
// -1 the previous one: a coordinate relative to the position, not a label. Steps compose by flat-map: every node of the result of a
// step is the position of the next step.
//
// The text notation is a preset in loom/data/graph/address_syntax.pack (the tokens, the owner's example alias): several may exist
// at once, parse and print take the one that is named. What is written in the pack is an example of a notation, not a requirement.
//
// resolve() runs an address against a Graph: a small struct of ports (no database in this package). An address that points outside
// the graph gives an empty set and an advisory, never an error (R58); an error is returned only for text that cannot be read at all.
#pragma once

#include <filesystem>
#include <functional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "loom/advisory.h"
#include "loom/dimensions.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::address {

struct Step {
  std::string dim;  // empty = the default dimension
  Json coord = Json::object();
  bool operator==(const Step& o) const { return dim == o.dim && coord == o.coord; }
};

struct Address {
  std::vector<Step> steps;

  Json to_json() const;
  static Result<Address> from_json(const Json& value);
  bool operator==(const Address& o) const { return steps == o.steps; }
};

// a/b: the steps of a, then the steps of b. Associative; not commutative.
Address compose(const Address& a, const Address& b);

struct Alias {
  std::string id;
  std::string match;    // text, recognised outside quotes and brackets
  std::string expands;  // text in the same notation that replaces it
};

struct Syntax {
  std::string id;
  std::string kind;
  Json label = Json::object();
  Json why = Json::object();
  std::string step = "/", dim = ":", range = "..", level = ".", quote = "\"", escape = "\\";
  std::string group_open = "(", group_close = ")", pred_open = "[", pred_close = "]";
  std::string datetime_pattern;
  std::regex datetime_re;
  std::vector<Alias> aliases;
  bool print_with_aliases = false;
  Json examples = Json::array();
};

class Syntaxes {
 public:
  static Result<Syntaxes> from_json(Json pack);
  static Result<Syntaxes> load_file(const std::filesystem::path& file);
  static const Syntaxes& builtin();

  // The named syntax; an unknown id gives the default syntax and the advisory `syntax_unknown` (when a collector is given).
  const Syntax& get(std::string_view id, advisory::Collector* advisories = nullptr) const;
  const Syntax& default_syntax() const;
  std::vector<std::string> ids() const;
  const Json& pack() const { return pack_; }

 private:
  Json pack_;
  std::vector<Syntax> list_;
  std::string default_id_;
};

struct Context {
  const dimensions::Registry* dims = nullptr;   // when given, a dimension that is not in it raises `dimension_unknown`
  advisory::Collector* advisories = nullptr;    // when given, receives the advisories (degenerate address, alias applied, ...)
};

// Text -> address. Errors only for text that cannot be read (unterminated quote, unexpected character); a valid but useless
// address (`-0`, `all/all`) is accepted with an advisory.
Result<Address> parse(std::string_view text, const Syntax& syntax, const Context& ctx = {});
std::string print(const Address& address, const Syntax& syntax, const Context& ctx = {});

using NodeId = std::string;

// The ports of a graph that an address can be resolved against. Every port is optional (a missing one sees an empty graph).
struct Graph {
  // The sequence a node sits in along a dimension, in walk order, the node included; empty when it has no place on the dimension.
  // On a hierarchical dimension it is the siblings of the node (the nodes under the same parent).
  std::function<std::vector<NodeId>(const std::string& dim, const NodeId& node)> neighbours;
  // One level finer under a node of a hierarchical dimension, in walk order.
  std::function<std::vector<NodeId>(const std::string& dim, const NodeId& node)> children;
  // The chain from the coarsest level down to the node itself (the node last); empty when it has no place on the dimension.
  std::function<std::vector<NodeId>(const std::string& dim, const NodeId& node)> lineage;
  // The ordering key of a node on a dimension (a number, or a string); null when there is none.
  std::function<Json(const std::string& dim, const NodeId& node)> key;
  // The dimension a step without a name means for this node; absent = the registry's default dimension.
  std::function<std::string(const NodeId& node)> default_dimension;
};

struct Resolution {
  std::vector<NodeId> nodes;      // in the order found, each once
  std::vector<Json> advisories;   // {code, level, text{pl,en}, params, ask?}
  Json trace = Json::array();     // per step: {step, dim, coord, in, out}
};

// Never fails for an address that points outside the graph or uses a dimension, word or coordinate kind that does not fit:
// the set is then empty (or partial) and the advisories say why.
Result<Resolution> resolve(const Address& address, const NodeId& anchor, const Graph& graph, const dimensions::Registry& registry);

}  // namespace loom::address
