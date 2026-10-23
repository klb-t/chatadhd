// The dimension registry (K3-1). See include/loom/dimensions.h.
#include "loom/dimensions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>

#include "k3_packs_embedded.inc"
#include "loom/util/fs.h"

namespace loom::dimensions {

namespace {

const std::set<std::string> kNativeCodecs = {"integer", "double", "name", "datetime", "range", "levels", "predicate"};

// days since 1970-01-01 of a civil date (proleptic Gregorian).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool digits_at(std::string_view s, std::size_t at, std::size_t n) {
  if (at + n > s.size()) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (s[at + i] < '0' || s[at + i] > '9') return false;
  return true;
}
int num(std::string_view s, std::size_t at, std::size_t n) {
  int v = 0;
  for (std::size_t i = 0; i < n; ++i) v = v * 10 + (s[at + i] - '0');
  return v;
}

}  // namespace

std::string Dimension::between_mode() const { return json::get_string(between, "mode", "nearest"); }

std::optional<double> datetime_epoch(std::string_view s) {
  if (!digits_at(s, 0, 4) || s.size() < 10 || s[4] != '-' || !digits_at(s, 5, 2) || s[7] != '-' || !digits_at(s, 8, 2)) return std::nullopt;
  const int y = num(s, 0, 4), mo = num(s, 5, 2), d = num(s, 8, 2);
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return std::nullopt;
  double secs = static_cast<double>(days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d))) * 86400.0;
  std::size_t i = 10;
  if (i == s.size()) return secs;
  if (s[i] != 'T' || !digits_at(s, i + 1, 2) || i + 3 >= s.size() || s[i + 3] != ':' || !digits_at(s, i + 4, 2)) return std::nullopt;
  secs += num(s, i + 1, 2) * 3600.0 + num(s, i + 4, 2) * 60.0;
  i += 6;
  if (i < s.size() && s[i] == ':') {
    if (!digits_at(s, i + 1, 2)) return std::nullopt;
    secs += num(s, i + 1, 2);
    i += 3;
    if (i < s.size() && s[i] == '.') {
      std::size_t j = i + 1;
      double scale = 0.1;
      if (j >= s.size() || !digits_at(s, j, 1)) return std::nullopt;
      for (; j < s.size() && s[j] >= '0' && s[j] <= '9'; ++j, scale /= 10) secs += (s[j] - '0') * scale;
      i = j;
    }
  }
  if (i == s.size()) return secs;
  if (s[i] == 'Z' && i + 1 == s.size()) return secs;
  if ((s[i] == '+' || s[i] == '-') && digits_at(s, i + 1, 2)) {
    const int sign = s[i] == '+' ? 1 : -1;
    int hh = num(s, i + 1, 2), mm = 0;
    std::size_t j = i + 3;
    if (j < s.size() && s[j] == ':') ++j;
    if (j < s.size()) {
      if (!digits_at(s, j, 2) || j + 2 != s.size()) return std::nullopt;
      mm = num(s, j, 2);
    }
    return secs - sign * (hh * 3600.0 + mm * 60.0);
  }
  return std::nullopt;
}

Result<Registry> Registry::from_json(Json pack) {
  if (!pack.is_object() || json::get_string(pack, "schema") != "loom.dimensions/1")
    return Error(Errc::InvalidArgument, "dimension registry: schema must be loom.dimensions/1");
  Registry r;
  r.default_dimension_ = json::get_string(pack, "default_dimension");
  if (const Json* adv = json::find(pack, "advisories")) r.catalogue_ = advisory::Catalogue(*adv);
  const Json* dims = json::find(pack, "dimensions");
  if (!dims || !dims->is_array()) return Error(Errc::InvalidArgument, "dimension registry: `dimensions` must be an array");
  const Json default_words = pack.value("default_words", Json::object());
  advisory::Collector load(&r.catalogue_);
  for (const auto& e : *dims) {
    if (!e.is_object() || json::get_string(e, "id").empty()) return Error(Errc::InvalidArgument, "dimension registry: every dimension needs an id");
    Dimension d;
    d.id = json::get_string(e, "id");
    if (r.dims_.count(d.id)) return Error(Errc::AlreadyExists, "dimension registry: duplicate dimension " + d.id);
    d.label = e.value("label", Json::object());
    d.why = e.value("why", Json::object());
    d.codec = json::get_string(e, "codec", "name");
    d.codec_known = kNativeCodecs.count(d.codec) > 0;
    d.effective_codec = d.codec_known ? d.codec : "name";
    if (!d.codec_known) load.add("codec_unknown", Json{{"dimension", d.id}, {"codec", d.codec}});
    if (const Json* lv = json::find(e, "levels"); lv && lv->is_array())
      for (const auto& l : *lv) d.levels.push_back(l.is_string() ? l.get<std::string>() : json::dump(l));
    d.origin = json::get_string(e, "origin", "current");
    d.off_lineage_origin = json::get_string(e, "off_lineage_origin", "last");
    d.order = e.value("order", Json::object());
    d.walk = e.value("walk", Json::object());
    d.between = e.value("between", Json{{"mode", "nearest"}});
    d.step_from_fov = e.value("step_from_fov", Json::object());
    d.node_coordinate = json::get_bool(e, "node_coordinate", true);
    d.example = json::get_string(e, "kind", "example") == "example";
    d.words = default_words.is_object() ? default_words : Json::object();
    if (const Json* w = json::find(e, "words"); w && w->is_object())
      for (auto it = w->begin(); it != w->end(); ++it) {
        if (it.value().is_null()) d.words.erase(it.key());
        else d.words[it.key()] = it.value();
      }
    r.order_.push_back(d.id);
    r.dims_.emplace(d.id, std::move(d));
  }
  if (r.default_dimension_.empty() && !r.order_.empty()) r.default_dimension_ = r.order_.front();
  r.load_advisories_ = load.items();
  r.pack_ = std::move(pack);
  return r;
}

Result<Registry> Registry::load_file(const std::filesystem::path& file) {
  LOOM_TRY_ASSIGN(auto bytes, fsutil::read_file(file));
  LOOM_TRY_ASSIGN(auto doc, json::parse(bytes));
  return from_json(std::move(doc));
}

const Registry& Registry::builtin() {
  static const Registry reg = [] {
    auto doc = json::parse(kDimensionsPack);
    auto r = doc ? from_json(std::move(*doc)) : Result<Registry>(doc.error());
    if (!r) return Registry{};  // cannot happen for the shipped pack (tests parse it); an empty registry stays usable
    return std::move(*r);
  }();
  return reg;
}

const Dimension* Registry::find(std::string_view id) const {
  const auto it = dims_.find(id);
  return it == dims_.end() ? nullptr : &it->second;
}

std::vector<std::string> Registry::ids() const { return order_; }

bool Registry::between_available(std::string_view mode) const {
  const Json* modes = json::find(pack_, "between_modes");
  const Json* m = modes ? json::find(*modes, mode) : nullptr;
  return m && json::get_bool(*m, "available", false);
}

const Json* Registry::word(const Dimension& d, std::string_view w) const { return json::find(d.words, w); }

std::optional<double> Registry::numeric(const Dimension& d, const Json& v) const {
  if (v.is_number()) return v.get<double>();
  if (v.is_string() && d.effective_codec == "datetime") return datetime_epoch(v.get<std::string>());
  return std::nullopt;
}

std::optional<int> Registry::compare(const Dimension& d, const Json& a, const Json& b) const {
  auto sign = [](auto x, auto y) { return x < y ? -1 : (x > y ? 1 : 0); };
  if (a.is_number_integer() && b.is_number_integer()) return sign(a.get<std::int64_t>(), b.get<std::int64_t>());
  if (a.is_number() && b.is_number()) return sign(a.get<double>(), b.get<double>());
  if (a.is_string() && b.is_string()) {
    const std::string x = a.get<std::string>(), y = b.get<std::string>();
    if (d.effective_codec == "datetime") {
      const auto ex = datetime_epoch(x), ey = datetime_epoch(y);
      if (ex && ey) return sign(*ex, *ey);
    }
    if (json::get_string(d.order, "by") == "list") {
      if (const Json* l = json::find(d.order, "list"); l && l->is_array()) {
        std::ptrdiff_t ix = -1, iy = -1;
        for (std::size_t i = 0; i < l->size(); ++i) {
          if ((*l)[i] == x && ix < 0) ix = static_cast<std::ptrdiff_t>(i);
          if ((*l)[i] == y && iy < 0) iy = static_cast<std::ptrdiff_t>(i);
        }
        if (ix >= 0 && iy >= 0) return sign(ix, iy);
      }
    }
    return sign(x.compare(y), 0);
  }
  return std::nullopt;
}

}  // namespace loom::dimensions
