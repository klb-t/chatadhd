// loom/advisory.h — the one shape of a non-blocking advisory (R58; K3 design 1i).
//
//   {code, level, text{pl,en}, params}            the same four keys as the `reason` of src/views/view_engine.cpp
//   + ask{key, question{pl,en}}                   only when the catalogue entry has a question: "is this intended?" and the
//                                                 preference key under which "do not ask next time" is remembered
//
// An advisory never blocks: the caller gets its answer and the advisories next to it. The texts are data (a catalogue of
// code -> {level, text{pl,en}, question{pl,en}}), `{name}` in a text is replaced by the param of that name. A code that
// the catalogue does not know still produces an advisory (the text is the code), so nothing is lost silently.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "loom/util/json.h"

namespace loom::advisory {

class Catalogue {
 public:
  Catalogue() = default;
  explicit Catalogue(Json entries) : entries_(entries.is_object() ? std::move(entries) : Json::object()) {}

  bool has(const std::string& code) const { return entries_.contains(code); }
  std::vector<std::string> codes() const;
  const Json& entries() const { return entries_; }

  // {pl,en} text of `code` with the params substituted.
  Json say(const std::string& code, const Json& params = Json::object()) const;
  // The advisory. `level` empty = the level of the catalogue entry ("info" when there is none).
  Json reason(const std::string& code, const std::string& level = "", const Json& params = Json::object()) const;

 private:
  Json entries_ = Json::object();
};

// Collects the advisories of one operation. An advisory identical to one already collected (same code and params) is kept once.
class Collector {
 public:
  explicit Collector(const Catalogue* catalogue = nullptr) : catalogue_(catalogue) {}

  void add(const std::string& code, const Json& params = Json::object(), const std::string& level = "");
  void append(const std::vector<Json>& items);
  const std::vector<Json>& items() const { return items_; }
  bool has(const std::string& code) const { return count(code) > 0; }
  std::size_t count(const std::string& code) const;
  const Catalogue* catalogue() const { return catalogue_; }

 private:
  const Catalogue* catalogue_;
  std::vector<Json> items_;
};

}  // namespace loom::advisory
