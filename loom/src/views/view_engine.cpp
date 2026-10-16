// The visualization engine: view declarations as graph data -> a layout-independent view model. See include/loom/view_engine.h.
#include "loom/view_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "loom/db.h"
#include "loom/log.h"
#include "loom/util/time.h"

namespace loom::views {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<std::string> strs(const Json& v) {
  std::vector<std::string> out;
  if (v.is_array())
    for (const auto& s : v)
      if (s.is_string()) out.push_back(s.get<std::string>());
  return out;
}

double round6(double x) { return std::round(x * 1e6) / 1e6; }

std::string numstr(double x) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3g", x);
  return buf;
}

// Average ranks (ties share the mean rank).
std::vector<double> ranks_of(const std::vector<double>& v) {
  std::vector<std::size_t> idx(v.size());
  for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
  std::vector<double> r(v.size());
  for (std::size_t i = 0; i < idx.size();) {
    std::size_t j = i;
    while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]]) ++j;
    for (std::size_t k = i; k <= j; ++k) r[idx[k]] = (static_cast<double>(i) + static_cast<double>(j)) / 2.0 + 1.0;
    i = j + 1;
  }
  return r;
}

bool constant(const std::vector<double>& v) {
  for (double x : v)
    if (x != v.front()) return false;
  return true;
}

// Spearman rank correlation; nullopt when it is not defined (fewer than 3 pairs or a constant side).
std::optional<double> spearman(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size() || a.size() < 3 || constant(a) || constant(b)) return std::nullopt;
  const auto ra = ranks_of(a), rb = ranks_of(b);
  double ma = 0, mb = 0;
  for (std::size_t i = 0; i < ra.size(); ++i) { ma += ra[i]; mb += rb[i]; }
  ma /= static_cast<double>(ra.size());
  mb /= static_cast<double>(rb.size());
  double sab = 0, saa = 0, sbb = 0;
  for (std::size_t i = 0; i < ra.size(); ++i) {
    sab += (ra[i] - ma) * (rb[i] - mb);
    saa += (ra[i] - ma) * (ra[i] - ma);
    sbb += (rb[i] - mb) * (rb[i] - mb);
  }
  if (saa <= 0 || sbb <= 0) return std::nullopt;
  return sab / std::sqrt(saa * sbb);
}

struct Src {
  std::string id, perspective, role, direction, rendered_direction;
  double weight = 1.0;
  Json depth = nullptr;
  bool ok = false;
  std::map<std::string, int> hops;
  std::map<std::string, std::string> parent;
  std::map<std::string, std::vector<std::string>> children;
  std::map<std::string, int> diverge;
  std::map<std::string, double> offset;  // branch offset in steps (sum over forks of index - (k-1)/2)
  std::map<std::string, std::string> branch;
  std::vector<Json> edges;
};

struct NodeInfo {
  std::string label, kind, created;
};

struct Measure {
  std::string id, kind, evaluator, data_type, unit;
  bool ok = false;
  std::map<std::string, Json> values;
  Json stats = Json::object();
};

struct Encoding {
  std::string id, channel, measure;
  bool ok = false;
  Json scale = Json::object();
  std::map<std::string, Json> values;  // element id -> channel value
};

}  // namespace

struct Evaluator {
  const ViewEngine& eng;
  const RenderContext& ctx;
  Json decl, request;
  Json issues = Json::array(), unsupported = Json::array();
  std::map<std::string, Json> messages;

  std::vector<Json> src_el, meas_el, coord_el, enc_el, comp_el, rules_el;
  std::vector<Src> srcs;
  std::string focus;
  std::map<std::string, NodeInfo> info;
  std::map<std::string, Measure> meas;
  std::map<std::string, int> meas_state;
  std::vector<Encoding> encs;
  std::map<std::string, Json> coords;  // element id -> {coordinates element id: {...}}
  Json coord_report = Json::array();
  struct SectorHit { std::string primary; double angle = 0; bool single = false; bool valid = false; };
  std::map<std::string, SectorHit> sector_cache;
  const Json* sector_el = nullptr;

  Evaluator(const ViewEngine& e, const RenderContext& c, Json d, Json r) : eng(e), ctx(c), decl(std::move(d)), request(std::move(r)) {
    for (const auto* entry : eng.rules_.active())
      if (json::get_string(entry->value, "kind") == "messages") {
        const Json texts = entry->value.value("texts", Json::object());
        for (auto it = texts.begin(); it != texts.end(); ++it) messages[it.key()] = it.value();
      }
  }

  // ── reasons, visible and bilingual (the texts are data: view_rules `messages`) ──
  Json say(const std::string& code, const Json& params = Json::object()) const {
    Json out = Json::object();
    const auto it = messages.find(code);
    for (const char* lang : {"en", "pl"}) {
      std::string s = code;
      if (it != messages.end() && it->second.is_object()) s = json::get_string(it->second, lang, json::get_string(it->second, "en", code));
      if (params.is_object())
        for (auto p = params.begin(); p != params.end(); ++p) {
          const std::string needle = "{" + p.key() + "}";
          const std::string value = p.value().is_string() ? p.value().get<std::string>() : json::dump(p.value());
          for (std::size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + value.size())) s.replace(at, needle.size(), value);
        }
      out[lang] = s;
    }
    return out;
  }
  Json reason(const std::string& code, const std::string& level, const Json& params = Json::object()) const {
    return Json{{"code", code}, {"level", level}, {"text", say(code, params)}, {"params", params}};
  }
  void unsupp(const std::string& element, const std::string& kind, const std::string& code, const Json& params = Json::object()) {
    unsupported.push_back(Json{{"element", element}, {"kind", kind}, {"reason", reason(code, "warning", params)}});
  }
  void issue(const std::string& code, const Json& params = Json::object()) { issues.push_back(Json{{"code", code}, {"text", say(code, params)}, {"params", params}}); }

  // ── sources ──
  Status load_sources() {
    const Json overrides = request.value("sources", Json::object());
    for (const auto& el : src_el) {
      Src s;
      s.id = json::get_string(el, "id");
      s.perspective = json::get_string(el, "perspective");
      s.role = json::get_string(el, "role", "side");
      s.direction = json::get_string(el, "direction");
      s.weight = json::get_number(el, "weight", 1.0);
      s.depth = el.contains("depth") ? el["depth"] : Json(nullptr);
      const Json ov = overrides.is_object() ? overrides.value(s.id, Json::object()) : Json::object();
      if (ov.is_object()) {
        if (ov.contains("weight") && ov["weight"].is_number()) s.weight = ov["weight"].get<double>();
        if (ov.contains("depth")) s.depth = ov["depth"];
      }
      const EffectiveEntry* p = eng.persp_.view().find(s.perspective);
      if (!p || p->disabled) {
        unsupp(s.id, "source", "unknown_perspective", Json{{"perspective", s.perspective}});
        srcs.push_back(std::move(s));
        continue;
      }
      Json req{{"seeds", Json::array({focus})}, {"hops", s.depth}};
      if (!s.direction.empty()) req["direction"] = s.direction;
      auto rendered = eng.persp_.render(ctx, p->id, req);
      if (!rendered) {
        unsupp(s.id, "source", "source_failed", Json{{"perspective", s.perspective}, {"error", rendered.error().message}});
        srcs.push_back(std::move(s));
        continue;
      }
      const Json nodes = rendered->value("nodes", Json::array());
      if (!nodes.is_array() || !rendered->contains("edges")) {
        unsupp(s.id, "source", "source_shape", Json{{"perspective", s.perspective}});
        srcs.push_back(std::move(s));
        continue;
      }
      for (const auto& n : nodes) {
        NodeInfo& ni = info[json::get_string(n, "id")];
        ni.label = json::get_string(n, "label");
        ni.kind = json::get_string(n, "kind");
      }
      s.rendered_direction = json::get_string(*rendered, "direction", "out");
      for (const auto& e : rendered->value("edges", Json::array())) s.edges.push_back(e);
      s.ok = true;
      srcs.push_back(std::move(s));  // the walk is built after the node details are known (sibling order uses the creation time)
    }
    return Status{};
  }

  Status load_details() {
    for (auto& [id, ni] : info) {
      LOOM_TRY_ASSIGN(auto n, ctx.db->get_node(id));
      if (n) {
        ni.created = n->created;
        if (ni.label.empty()) ni.label = n->label;
        if (ni.kind.empty()) ni.kind = n->kind;
      }
    }
    return Status{};
  }

  void build_walks() {
    for (auto& s : srcs) {
      if (!s.ok) continue;
      const std::string dir = s.rendered_direction;
      std::map<std::string, std::vector<std::string>> adj;
      for (const auto& e : s.edges) {
        const std::string a = json::get_string(e, "src"), b = json::get_string(e, "dst");
        if (dir == "out" || dir == "both") adj[a].push_back(b);
        if (dir == "in" || dir == "both") adj[b].push_back(a);
      }
      auto order = [&](const std::string& x, const std::string& y) {
        const std::string& cx = info[x].created;
        const std::string& cy = info[y].created;
        if (cx != cy) return cx < cy;
        return x < y;
      };
      for (auto& [n, v] : adj) {
        std::sort(v.begin(), v.end(), order);
        v.erase(std::unique(v.begin(), v.end()), v.end());
      }
      s.hops[focus] = 0;
      s.diverge[focus] = 0;
      s.offset[focus] = 0;
      s.branch[focus] = "";
      std::deque<std::string> todo{focus};
      while (!todo.empty()) {
        const std::string cur = todo.front();
        todo.pop_front();
        std::vector<std::string> kids;
        for (const auto& nb : adj[cur])
          if (!s.hops.count(nb)) kids.push_back(nb);
        const std::size_t n = kids.size();
        for (std::size_t i = 0; i < n; ++i) {
          const std::string& kid = kids[i];
          s.hops[kid] = s.hops[cur] + 1;
          s.parent[kid] = cur;
          s.diverge[kid] = s.diverge[cur] + (n > 1 ? 1 : 0);
          s.offset[kid] = s.offset[cur] + (n > 1 ? static_cast<double>(i) - (static_cast<double>(n) - 1.0) / 2.0 : 0.0);
          s.branch[kid] = n > 1 ? (s.branch[cur].empty() ? "" : s.branch[cur] + ".") + std::to_string(i) : s.branch[cur];
          s.children[cur].push_back(kid);
          todo.push_back(kid);
        }
      }
    }
  }

  const Src* source_by_id(const std::string& id) const {
    for (const auto& s : srcs)
      if (s.id == id) return &s;
    return nullptr;
  }
  const Src* main_source(const std::string& wanted) const {
    if (!wanted.empty()) return source_by_id(wanted);
    for (const auto& s : srcs)
      if (s.ok && s.role == "main") return &s;
    for (const auto& s : srcs)
      if (s.ok) return &s;
    return nullptr;
  }

  // ── sectors: a direction per source group, the angle blended by how strongly each source reaches the element ──
  void find_sector_element() {
    for (const auto& c : coord_el)
      if (json::get_string(c, "system") == "sector") { sector_el = &c; return; }
  }
  const SectorHit& sector_of(const std::string& id) {
    auto it = sector_cache.find(id);
    if (it != sector_cache.end()) return it->second;
    SectorHit hit;
    if (sector_el && id != focus) {
      const Json sectors = sector_el->value("sectors", Json::array());
      double sx = 0, sy = 0, best = 0;
      std::size_t present = 0;
      for (const auto& sec : sectors) {
        double a = 0;
        for (const auto& sid : strs(sec.value("sources", Json::array()))) {
          const Src* s = source_by_id(sid);
          if (!s || !s->ok || s->weight <= 0) continue;
          const auto h = s->hops.find(id);
          if (h != s->hops.end()) a = std::max(a, s->weight / (1.0 + h->second));
        }
        if (a <= 0) continue;
        ++present;
        const double c = json::get_number(sec, "center_deg", 0.0) * kPi / 180.0;
        sx += a * std::cos(c);
        sy += a * std::sin(c);
        if (a > best) { best = a; hit.primary = json::get_string(sec, "id"); }
      }
      if (present > 0) {
        hit.valid = true;
        hit.single = present == 1;
        hit.angle = std::atan2(sy, sx) * 180.0 / kPi;
        if (std::fabs(sx) < 1e-12 && std::fabs(sy) < 1e-12) hit.angle = 0;
      }
    }
    return sector_cache.emplace(id, hit).first->second;
  }
  // ── measures ──
  const Measure* measure(const std::string& id) {
    auto it = meas.find(id);
    if (it != meas.end() && meas_state[id] == 2) return it->second.ok ? &it->second : nullptr;
    if (meas_state[id] == 1) { unsupp(id, "measure", "measure_cycle", Json{{"measure", id}}); return nullptr; }
    const Json* el = nullptr;
    for (const auto& m : meas_el)
      if (json::get_string(m, "id") == id) el = &m;
    if (!el) return nullptr;
    meas_state[id] = 1;
    Measure m;
    m.id = id;
    m.kind = json::get_string(*el, "measure");
    const EffectiveEntry* me = eng.measures_.find(m.kind);
    if (!me || me->disabled) {
      unsupp(id, "measure", "unsupported_measure_kind", Json{{"measure", m.kind}});
    } else {
      m.evaluator = json::get_string(me->value, "evaluator", me->id);
      m.data_type = json::get_string(*el, "data_type", json::get_string(me->value, "data_type", "quantitative"));
      m.unit = json::get_string(me->value, "unit");
      Json params = me->value.value("params", Json::object());
      const Json own = el->value("params", Json::object());
      if (own.is_object())
        for (auto p = own.begin(); p != own.end(); ++p) params[p.key()] = p.value();
      m.ok = evaluate_measure(m, params);
      if (m.ok) summarize(m);
    }
    meas_state[id] = 2;
    auto [pos, ins] = meas.insert_or_assign(id, std::move(m));
    return pos->second.ok ? &pos->second : nullptr;
  }

  std::set<std::string> element_ids() const {
    std::set<std::string> ids{focus};
    for (const auto& s : srcs)
      if (s.ok)
        for (const auto& [id, h] : s.hops) ids.insert(id);
    return ids;
  }

  bool evaluate_measure(Measure& m, const Json& params) {
    const std::set<std::string> ids = element_ids();
    if (m.evaluator == "distance_from_focus") {
      const std::string combine = json::get_string(params, "combine", "weighted_sum");
      const EffectiveEntry* ce = eng.combines_.find(combine);
      const std::string rule = ce && !ce->disabled ? json::get_string(ce->value, "evaluator", ce->id) : std::string();
      if (rule != "weighted_sum" && rule != "min") {
        unsupp(m.id, "measure", "unsupported_combine", Json{{"combine", combine}});
        return false;
      }
      const std::vector<std::string> only = strs(params.value("sources", Json::array()));
      for (const auto& id : ids) {
        std::optional<double> acc;
        for (const auto& s : srcs) {
          if (!s.ok || s.weight <= 0) continue;
          if (!only.empty() && std::find(only.begin(), only.end(), s.id) == only.end()) continue;
          const auto h = s.hops.find(id);
          if (h == s.hops.end()) continue;
          const double c = static_cast<double>(h->second) / s.weight;
          acc = !acc ? c : (rule == "min" ? std::min(*acc, c) : *acc + c);
        }
        if (acc) m.values[id] = round6(*acc);
      }
      return true;
    }
    if (m.evaluator == "age") {
      const std::string ref = json::get_string(params, "reference", "focus");
      std::optional<timeutil::Clock::time_point> base;
      if (ref == "now") base = timeutil::Clock::now();
      else base = timeutil::parse_iso_utc(info[focus].created);
      if (!base) { issue("age_reference_unparseable", Json{{"reference", ref}}); return true; }
      std::size_t bad = 0;
      for (const auto& id : ids) {
        const auto t = timeutil::parse_iso_utc(info[id].created);
        if (!t) { ++bad; continue; }
        m.values[id] = round6(std::chrono::duration<double>(*base - *t).count() / 86400.0);
      }
      if (bad) issue("age_unparseable", Json{{"count", bad}});
      return true;
    }
    if (m.evaluator == "branch_divergence") {
      const Src* s = main_source(json::get_string(params, "source"));
      if (!s || !s->ok) { unsupp(m.id, "measure", "measure_needs_source", Json{{"measure", m.kind}}); return false; }
      for (const auto& [id, d] : s->diverge) m.values[id] = d;
      return true;
    }
    if (m.evaluator == "degree_of_interest") {
      std::string dist = json::get_string(params, "distance");
      if (dist.empty())
        for (const auto& e : meas_el) {
          const EffectiveEntry* k = eng.measures_.find(json::get_string(e, "measure"));
          if (k && json::get_string(k->value, "evaluator", k->id) == "distance_from_focus") { dist = json::get_string(e, "id"); break; }
        }
      const Measure* d = dist.empty() ? nullptr : measure(dist);
      if (!d) { unsupp(m.id, "measure", "measure_needs_distance", Json{{"measure", m.kind}}); return false; }
      if (json::get_string(params, "api", "degree") != "degree") { unsupp(m.id, "measure", "unsupported_api", Json{{"api", json::get_string(params, "api")}}); return false; }
      std::set<std::string> seen;
      std::map<std::string, int> degree;
      for (const auto& s : srcs)
        if (s.ok)
          for (const auto& e : s.edges) {
            const std::string a = json::get_string(e, "src"), b = json::get_string(e, "dst"), t = json::get_string(e, "type");
            if (!seen.insert(a + "\x1f" + b + "\x1f" + t).second) continue;
            ++degree[a];
            ++degree[b];
          }
      const double api_scale = json::get_number(params, "api_scale", 1.0), dist_scale = json::get_number(params, "distance_scale", 1.0);
      for (const auto& id : ids) {
        const auto dv = d->values.find(id);
        if (dv == d->values.end() || !dv->second.is_number()) continue;
        m.values[id] = round6(api_scale * degree[id] - dist_scale * dv->second.get<double>());
      }
      return true;
    }
    if (m.evaluator == "membership") {
      const bool by_sector = json::get_string(params, "by", "sector") == "sector" && sector_el;
      for (const auto& id : ids) {
        if (id == focus) { m.values[id] = "focus"; continue; }
        if (by_sector) {
          const auto& h = sector_of(id);
          if (h.valid) m.values[id] = h.primary;
          continue;
        }
        const Src* best = nullptr;
        double bd = 0;
        for (const auto& s : srcs) {
          if (!s.ok || s.weight <= 0 || !s.hops.count(id)) continue;
          const double c = s.hops.at(id) / s.weight;
          if (!best || c < bd) { best = &s; bd = c; }
        }
        if (best) m.values[id] = best->id;
      }
      return true;
    }
    if (m.evaluator == "node_kind") {
      for (const auto& id : ids) m.values[id] = info[id].kind;
      return true;
    }
    unsupp(m.id, "measure", "unsupported_measure_kind", Json{{"measure", m.kind}});
    return false;
  }

  void summarize(Measure& m) {
    std::vector<double> nums;
    std::set<std::string> cats;
    for (const auto& [id, v] : m.values) {
      if (v.is_number()) nums.push_back(v.get<double>());
      else if (v.is_string()) cats.insert(v.get<std::string>());
    }
    m.stats = Json{{"kind", m.kind}, {"data_type", m.data_type}, {"unit", m.unit}, {"count", m.values.size()}};
    if (!nums.empty()) {
      m.stats["min"] = *std::min_element(nums.begin(), nums.end());
      m.stats["max"] = *std::max_element(nums.begin(), nums.end());
    }
    if (!cats.empty()) m.stats["categories"] = Json(std::vector<std::string>(cats.begin(), cats.end()));
  }

  // ── coordinate systems ──
  void compute_coordinates() {
    for (const auto& c : coord_el) {
      const std::string id = json::get_string(c, "id"), sys = json::get_string(c, "system");
      const EffectiveEntry* se = eng.systems_.find(sys);
      const std::string evaluator = se && !se->disabled ? json::get_string(se->value, "evaluator", se->id) : std::string();
      Json rep{{"id", id}, {"system", sys}, {"axes", se ? se->value.value("axes", Json::array()) : Json::array()}, {"bind", c.value("bind", Json::object())}, {"settings", c}};
      auto done = [&](bool ok) { rep["supported"] = ok; coord_report.push_back(rep); };
      if (evaluator != "cartesian" && evaluator != "sector" && evaluator != "depth_from_distance" && evaluator != "tree_branch_angles") {
        unsupp(id, "coordinates", "unsupported_coordinate_system", Json{{"system", sys}});
        done(false);
        continue;
      }
      const Json bind = c.value("bind", Json::object());
      auto bound = [&](const char* axis, const char* fallback_evaluator) -> const Measure* {
        std::string mid = bind.is_object() ? json::get_string(bind, axis) : std::string();
        if (mid.empty() && fallback_evaluator)
          for (const auto& e : meas_el) {
            const EffectiveEntry* k = eng.measures_.find(json::get_string(e, "measure"));
            if (k && json::get_string(k->value, "evaluator", k->id) == fallback_evaluator) { mid = json::get_string(e, "id"); break; }
          }
        if (mid.empty()) return nullptr;
        const Measure* m = measure(mid);
        if (!m) unsupp(id, "coordinates", "unknown_measure", Json{{"axis", axis}, {"measure", mid}});
        return m;
      };
      auto put = [&](const std::string& eid, const Json& v) { coords[eid][id] = v; };
      if (evaluator == "cartesian") {
        const Measure* mx = bound("x", nullptr);
        const Measure* my = bound("y", nullptr);
        for (const auto& eid : element_ids()) {
          Json v = Json::object();
          if (mx && mx->values.count(eid)) v["x"] = mx->values.at(eid);
          if (my && my->values.count(eid)) v["y"] = my->values.at(eid);
          if (!v.empty()) put(eid, v);
        }
      } else if (evaluator == "sector") {
        const Measure* r = bound("radius", "distance_from_focus");
        const double scale = json::get_number(c, "radius_scale", 1.0);
        const double step = json::get_number(c, "branch_step_deg", 0.0);
        for (const auto& eid : element_ids()) {
          Json v = Json::object();
          if (eid == focus) {
            v = Json{{"angle_deg", json::get_number(c, "focus_angle_deg", 0.0)}, {"radius", 0.0}, {"sector", nullptr}};
          } else {
            const SectorHit& h = sector_of(eid);
            if (!h.valid) continue;
            double angle = h.angle;
            if (h.single && step != 0) {
              const Json sec = sector_def_copy(h.primary);
              for (const auto& sid : strs(sec.value("sources", Json::array()))) {
                const Src* s = source_by_id(sid);
                if (s && s->ok && s->offset.count(eid)) { angle += s->offset.at(eid) * step; break; }
              }
            }
            v["angle_deg"] = round6(angle);
            v["sector"] = h.primary;
            if (r && r->values.count(eid) && r->values.at(eid).is_number()) v["radius"] = round6(r->values.at(eid).get<double>() * scale);
          }
          put(eid, v);
        }
      } else if (evaluator == "depth_from_distance") {
        const Measure* d = bound("depth", "distance_from_focus");
        if (d)
          for (const auto& eid : element_ids()) {
            if (!d->values.count(eid) || !d->values.at(eid).is_number()) continue;
            double sign = 1.0;
            if (sector_el && eid != focus) {
              const Json sec = sector_def_copy(sector_of(eid).primary);
              sign = json::get_number(sec, "depth_sign", 1.0);
            }
            put(eid, Json{{"depth", round6(sign * d->values.at(eid).get<double>())}});
          }
      } else {  // tree_branch_angles
        const Src* s = main_source(json::get_string(c, "source"));
        if (!s || !s->ok) {
          unsupp(id, "coordinates", "measure_needs_source", Json{{"measure", sys}});
          done(false);
          continue;
        }
        const double base = json::get_number(c, "base_deg", 0.0), step = json::get_number(c, "step_deg", 10.0);
        for (const auto& [eid, h] : s->hops)
          put(eid, Json{{"depth", h}, {"angle_deg", round6(base + s->offset.at(eid) * step)}, {"branch", s->branch.at(eid)}, {"parent", s->parent.count(eid) ? Json(s->parent.at(eid)) : Json(nullptr)}});
      }
      done(true);
    }
  }
  Json sector_def_copy(const std::string& sid) const {
    if (sector_el)
      for (const auto& sec : sector_el->value("sectors", Json::array()))
        if (json::get_string(sec, "id") == sid) return sec;
    return Json::object();
  }

  // ── encodings: channel <- measure ──
  void compute_encodings() {
    for (const auto& el : enc_el) {
      Encoding e;
      e.id = json::get_string(el, "id");
      e.channel = json::get_string(el, "channel");
      e.measure = json::get_string(el, "measure");
      e.scale = el.value("scale", Json::object());
      const EffectiveEntry* ch = eng.channels_.find(e.channel);
      if (!ch || ch->disabled) {
        unsupp(e.id, "encoding", "unsupported_channel", Json{{"channel", e.channel}});
        encs.push_back(std::move(e));
        continue;
      }
      const Measure* m = measure(e.measure);
      if (!m) {
        const bool declared = std::any_of(meas_el.begin(), meas_el.end(), [&](const Json& x) { return json::get_string(x, "id") == e.measure; });
        unsupp(e.id, "encoding", declared ? "measure_unavailable" : "unknown_measure", Json{{"measure", e.measure}});
        encs.push_back(std::move(e));
        continue;
      }
      e.ok = true;
      const std::string type = json::get_string(ch->value, "type", "continuous");
      Json range = e.scale.value("range", ch->value.value("range", Json::array({0, 1})));
      const double lo = range.is_array() && range.size() > 0 && range[0].is_number() ? range[0].get<double>() : 0.0;
      const double hi = range.is_array() && range.size() > 1 && range[1].is_number() ? range[1].get<double>() : 1.0;
      const bool invert = json::get_bool(e.scale, "invert", false);
      double dmin = m->stats.value("min", 0.0), dmax = m->stats.value("max", 0.0);
      const Json domain = e.scale.value("domain", Json::array());
      if (domain.is_array() && domain.size() == 2 && domain[0].is_number() && domain[1].is_number()) { dmin = domain[0].get<double>(); dmax = domain[1].get<double>(); }
      const std::vector<std::string> cats = strs(m->stats.value("categories", Json::array()));
      for (const auto& [id, v] : m->values) {
        if (type == "categorical") {
          if (v.is_string()) e.values[id] = v;
          else if (v.is_number()) e.values[id] = numstr(v.get<double>());
          continue;
        }
        double t = 0.5;
        if (v.is_string()) {
          const auto pos = std::find(cats.begin(), cats.end(), v.get<std::string>());
          if (pos == cats.end()) continue;
          const double i = static_cast<double>(pos - cats.begin()), n = static_cast<double>(cats.size());
          t = type == "circular" ? i / n : (n > 1 ? i / (n - 1) : 0.5);
        } else if (v.is_number()) {
          t = dmax > dmin ? std::clamp((v.get<double>() - dmin) / (dmax - dmin), 0.0, 1.0) : 0.5;
        } else {
          continue;
        }
        if (invert) t = 1.0 - t;
        e.values[id] = round6(lo + t * (hi - lo));
      }
      encs.push_back(std::move(e));
    }
  }

  // ── the rules layer ──
  struct Importance { std::map<std::string, double> values; std::string measure, sense; };
  std::optional<Importance> importance() {
    std::string wanted;
    for (const auto& r : rules_el) wanted = json::get_string(r, "importance", wanted);
    auto try_measure = [&](const Json& el, const std::string& want_sense) -> std::optional<Importance> {
      const EffectiveEntry* k = eng.measures_.find(json::get_string(el, "measure"));
      if (!k) return std::nullopt;
      const std::string sense = json::get_string(k->value, "importance_sense", wanted.empty() ? "" : "direct");
      if (sense.empty() || (!want_sense.empty() && sense != want_sense)) return std::nullopt;
      const Measure* m = measure(json::get_string(el, "id"));
      if (!m) return std::nullopt;
      Importance imp;
      imp.measure = m->id;
      imp.sense = sense;
      for (const auto& [id, v] : m->values)
        if (v.is_number()) imp.values[id] = sense == "inverse" ? -v.get<double>() : v.get<double>();
      return imp;
    };
    if (!wanted.empty())
      for (const auto& el : meas_el)
        if (json::get_string(el, "id") == wanted) return try_measure(el, "");
    for (const char* sense : {"direct", "inverse"})
      for (const auto& el : meas_el)
        if (auto imp = try_measure(el, sense)) return imp;
    return std::nullopt;
  }

  bool overlap_active() {
    for (const auto& c : comp_el)
      if (json::get_string(c, "composition") == "layers") return true;
    for (const auto& rep : coord_report) {
      if (!json::get_bool(rep, "supported")) continue;
      std::set<std::string> seen;
      for (const auto& [eid, per] : coords) {
        const std::string cid = json::get_string(rep, "id");
        if (!per.contains(cid)) continue;
        Json r = per[cid];
        for (auto it = r.begin(); it != r.end(); ++it)
          if (it.value().is_number()) it.value() = std::round(it.value().get<double>() * 1000.0) / 1000.0;
        if (!seen.insert(json::dump(r)).second) return true;
      }
    }
    return false;
  }

  Json encodings_report() {
    // merge the rule entries: suitability rankings per data type (later entries win), pairs, the budget
    std::set<std::string> disabled;
    std::size_t promote_top = 5;
    for (const auto& r : rules_el) {
      for (const auto& d : strs(r.value("disable", Json::array()))) disabled.insert(d);
      if (r.contains("promote_top") && r["promote_top"].is_number()) promote_top = static_cast<std::size_t>(r["promote_top"].get<std::int64_t>());
    }
    std::map<std::string, std::vector<std::string>> ranking;
    bool have_ranking = false;
    std::vector<const EffectiveEntry*> pairs;
    for (const auto* entry : eng.rules_.active()) {
      if (disabled.count(entry->id)) continue;
      const std::string kind = json::get_string(entry->value, "kind");
      if (kind == "channel_suitability") {
        have_ranking = true;
        if (entry->value.contains("promote_top") && entry->value["promote_top"].is_number() && std::none_of(rules_el.begin(), rules_el.end(), [](const Json& r) { return r.contains("promote_top"); }))
          promote_top = static_cast<std::size_t>(entry->value["promote_top"].get<std::int64_t>());
        const Json rk = entry->value.value("ranking", Json::object());
        for (auto it = rk.begin(); it != rk.end(); ++it) ranking[it.key()] = strs(it.value());
      } else if (kind == "interference") {
        pairs.push_back(entry);
      }
    }
    std::optional<Importance> imp = importance();
    std::set<std::string> used;
    for (const auto& e : encs)
      if (e.ok) used.insert(e.channel);
    // coordinate binds are encodings on the position channel
    Json out = Json::array();
    struct Row { std::string id, channel, measure, data_type; bool position = false; const Encoding* enc = nullptr; };
    std::vector<Row> rows;
    for (const auto& e : encs) {
      if (!e.ok) continue;
      const Measure* m = measure(e.measure);
      rows.push_back({e.id, e.channel, e.measure, m ? m->data_type : "quantitative", false, &e});
    }
    for (const auto& c : coord_el) {
      const Json bind = c.value("bind", Json::object());
      if (!bind.is_object()) continue;
      bool supported = false;
      for (const auto& rep : coord_report)
        if (json::get_string(rep, "id") == json::get_string(c, "id")) supported = json::get_bool(rep, "supported");
      if (!supported) continue;
      for (auto it = bind.begin(); it != bind.end(); ++it) {
        if (!it.value().is_string()) continue;
        const Measure* m = measure(it.value().get<std::string>());
        if (!m) continue;
        rows.push_back({json::get_string(c, "id") + "." + it.key(), "position", m->id, m->data_type, true, nullptr});
        used.insert("position");
      }
    }
    const bool overlap = overlap_active();
    for (const auto& row : rows) {
      Json reasons = Json::array();
      bool promoted = true;
      int rank = -1;
      const auto rk = ranking.find(row.data_type);
      if (have_ranking && rk != ranking.end()) {
        const auto pos = std::find(rk->second.begin(), rk->second.end(), row.channel);
        if (pos == rk->second.end()) {
          promoted = false;
          reasons.push_back(reason("channel_unranked", "info", Json{{"channel", row.channel}, {"data_type", row.data_type}}));
        } else {
          rank = static_cast<int>(pos - rk->second.begin()) + 1;
          if (static_cast<std::size_t>(rank) > promote_top) {
            promoted = false;
            reasons.push_back(reason("channel_low_rank", "warning", Json{{"channel", row.channel}, {"data_type", row.data_type}, {"rank", rank}, {"of", rk->second.size()}, {"best", rk->second.empty() ? std::string() : rk->second.front()}}));
          }
        }
      } else if (have_ranking) {
        promoted = false;
        reasons.push_back(reason("data_type_unranked", "info", Json{{"data_type", row.data_type}}));
      }
      // interference
      for (const auto* pr : pairs) {
        const std::string interfered = json::get_string(pr->value, "interfered");
        const std::vector<std::string> interfering = strs(pr->value.value("interfering", Json::array()));
        const bool feature = !eng.channels_.find(interfered);
        const Json when = pr->value.value("active_when", Json::object());
        bool active = true;
        if (when.is_object())
          for (auto w = when.begin(); w != when.end(); ++w) {
            if (w.key() == "overlap" && w.value().is_boolean()) active = active && (w.value().get<bool>() == overlap);
            else if (w.key() == "channel_used" && w.value().is_string()) active = active && used.count(w.value().get<std::string>());
            else { issue("interference_predicate_unknown", Json{{"rule", pr->id}, {"predicate", w.key()}}); active = false; }
          }
        if (!active) continue;
        // which encoding carries the verdict: the interfered channel, or (a feature such as overlap) the interfering channel itself
        std::vector<std::string> mine;
        if (!feature) {
          if (row.channel != interfered) continue;
          for (const auto& f : interfering)
            if (used.count(f)) mine.push_back(f);
        } else {
          if (std::find(interfering.begin(), interfering.end(), row.channel) == interfering.end()) continue;
          mine.push_back(row.channel);
        }
        for (const auto& f : mine) {
          const Json base{{"rule", pr->id}, {"interfered", interfered}, {"interfering", f}};
          const std::string condition = json::get_string(pr->value, "condition", "monotone_with_importance");
          if (condition != "monotone_with_importance") {
            Json p = base;
            p["condition"] = condition;
            reasons.push_back(reason("interference_condition_unknown", "info", p));
            promoted = false;
            continue;
          }
          std::vector<double> iv, wv;
          std::map<std::string, double> fvals;
          for (const auto& other : encs)
            if (other.ok && other.channel == f)
              for (const auto& [id, v] : other.values)
                if (v.is_number()) fvals[id] = v.get<double>();
          if (!fvals.empty() && !imp) {
            reasons.push_back(reason("interference_unverifiable", "warning", base));
            promoted = false;
            continue;
          }
          for (const auto& [id, v] : fvals) {
            const auto w = imp->values.find(id);
            if (w == imp->values.end()) continue;
            iv.push_back(v);
            wv.push_back(w->second);
          }
          if (iv.size() >= 2 && constant(iv)) {
            Json p = base;
            p["rho"] = nullptr;
            reasons.push_back(reason("interference_cleared_constant", "info", p));
            continue;
          }
          const auto rho = spearman(iv, wv);
          const double need = json::get_number(pr->value, "min_rank_correlation", 0.5);
          Json p = base;
          p["importance"] = imp ? imp->measure : std::string();
          p["need"] = need;
          if (!rho) {
            p["rho"] = nullptr;
            reasons.push_back(reason("interference_unverifiable", "warning", p));
            promoted = false;
          } else if (*rho >= need) {
            p["rho"] = round6(*rho);
            reasons.push_back(reason("interference_cleared", "info", p));
          } else {
            p["rho"] = round6(*rho);
            reasons.push_back(reason("interference_unmet", "warning", p));
            promoted = false;
          }
        }
      }
      Json o{{"id", row.id}, {"channel", row.channel}, {"measure", row.measure}, {"data_type", row.data_type}, {"status", promoted ? "promoted" : "allowed"}, {"reasons", reasons}};
      if (rank > 0) o["rank"] = rank;
      if (row.position) o["position"] = true;
      out.push_back(std::move(o));
    }
    return out;
  }

  // ── the node budget and its collapse order ──
  Json budget_report(std::vector<std::string>& visible, std::map<std::string, Json>& collapsed_groups) {
    Json cfg = Json::object();
    for (const auto* entry : eng.rules_.active())
      if (json::get_string(entry->value, "kind") == "node_budget") cfg = entry->value;
    for (const auto& r : rules_el)
      if (r.contains("budget") && r["budget"].is_object()) {
        const Json b = r["budget"];
        for (auto it = b.begin(); it != b.end(); ++it) cfg[it.key()] = it.value();
      }
    std::optional<std::int64_t> max;
    if (cfg.contains("max_nodes") && cfg["max_nodes"].is_number()) max = cfg["max_nodes"].get<std::int64_t>();
    if (request.contains("budget")) max = request["budget"].is_number() ? std::optional<std::int64_t>(request["budget"].get<std::int64_t>()) : std::nullopt;
    if (max && *max <= 0) max.reset();
    const std::size_t before = visible.size();
    Json rep{{"max_nodes", max ? Json(*max) : Json(nullptr)}, {"elements", before}, {"collapsed", 0}};
    if (!max || before <= static_cast<std::size_t>(*max)) return rep;
    // collapse order: criteria in order, each {by: measure kind evaluator, order: desc | asc | abs_desc}; the focus is never collapsed
    struct Crit { const Measure* m; std::string order; };
    std::vector<Crit> crits;
    for (const auto& c : cfg.value("collapse_order", Json::array())) {
      const std::string by = json::get_string(c, "by");
      for (const auto& el : meas_el) {
        const EffectiveEntry* k = eng.measures_.find(json::get_string(el, "measure"));
        if (k && json::get_string(k->value, "evaluator", k->id) == by) {
          if (const Measure* m = measure(json::get_string(el, "id"))) { crits.push_back({m, json::get_string(c, "order", "desc")}); break; }
        }
      }
    }
    auto key = [&](const Crit& c, const std::string& id) -> std::optional<double> {
      const auto v = c.m->values.find(id);
      if (v == c.m->values.end() || !v->second.is_number()) return std::nullopt;
      const double x = v->second.get<double>();
      return c.order == "asc" ? -x : (c.order == "abs_desc" ? std::fabs(x) : x);
    };
    std::vector<std::string> order = visible;
    std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
      if (a == focus || b == focus) return a != focus && b == focus;  // the focus goes last: collapsed last
      for (const auto& c : crits) {
        const auto ka = key(c, a), kb = key(c, b);
        if (ka == kb) continue;
        if (!ka) return true;   // unknown value collapses first
        if (!kb) return false;
        return *ka > *kb;       // the larger key collapses first
      }
      return a < b;
    });
    const std::size_t drop = before - static_cast<std::size_t>(*max);
    std::set<std::string> gone(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(drop));
    std::vector<std::string> keep;
    for (const auto& id : visible) {
      if (!gone.count(id)) { keep.push_back(id); continue; }
      const auto& hit = sector_of(id);
      const std::string g = hit.valid ? hit.primary : std::string("none");
      Json& grp = collapsed_groups.try_emplace("collapsed:" + g, Json{{"id", "collapsed:" + g}, {"kind", "collapsed"}, {"sector", g}, {"count", 0}, {"members", Json::array()}}).first->second;
      grp["count"] = grp["count"].get<int>() + 1;
      grp["members"].push_back(id);
    }
    visible = keep;
    rep["collapsed"] = drop;
    Json crit_names = Json::array();
    for (const auto& c : crits) crit_names.push_back(Json{{"measure", c.m->id}, {"order", c.order}});
    rep["order"] = crit_names;
    rep["reason"] = reason("budget_applied", "info", Json{{"max", *max}, {"collapsed", drop}});
    return rep;
  }

  // ── composition ──
  Json compose_groups(const std::set<std::string>& visible) {
    Json groups = Json::array();
    for (const auto& c : comp_el) {
      const std::string id = json::get_string(c, "id"), kind = json::get_string(c, "composition");
      const EffectiveEntry* ck = eng.compositions_.find(kind);
      const std::string evaluator = ck && !ck->disabled ? json::get_string(ck->value, "evaluator", ck->id) : std::string();
      if (evaluator == "sectors") {
        if (!sector_el) { unsupp(id, "composition", "composition_needs_sectors", Json{{"composition", kind}}); continue; }
        for (const auto& sec : sector_el->value("sectors", Json::array())) {
          Json members = Json::array();
          for (const auto& eid : visible)
            if (eid != focus && sector_of(eid).valid && sector_of(eid).primary == json::get_string(sec, "id")) members.push_back(eid);
          groups.push_back(Json{{"id", json::get_string(sec, "id")}, {"kind", "sector"}, {"composition", id}, {"label", sec.value("label", Json(json::get_string(sec, "id")))},
                                {"center_deg", sec.value("center_deg", 0)}, {"width_deg", sec.value("width_deg", 0)}, {"members", members}});
        }
      } else if (evaluator == "layers" || evaluator == "small_multiples") {
        for (const auto& s : srcs) {
          if (!s.ok) continue;
          Json members = Json::array();
          for (const auto& eid : visible)
            if (s.hops.count(eid)) members.push_back(eid);
          groups.push_back(Json{{"id", s.id}, {"kind", evaluator == "layers" ? "layer" : "panel"}, {"composition", id}, {"perspective", s.perspective}, {"members", members}});
        }
      } else if (evaluator == "lanes") {
        const Measure* m = measure(json::get_string(c, "by"));
        if (!m) { unsupp(id, "composition", "unknown_measure", Json{{"measure", json::get_string(c, "by")}}); continue; }
        const int bins = std::max<int>(1, static_cast<int>(json::get_int(c, "bins", 3)));
        const double lo = m->stats.value("min", 0.0), hi = m->stats.value("max", 0.0);
        std::vector<Json> lanes(static_cast<std::size_t>(bins), Json::array());
        for (const auto& eid : visible) {
          const auto v = m->values.find(eid);
          if (v == m->values.end() || !v->second.is_number()) continue;
          int b = hi > lo ? static_cast<int>((v->second.get<double>() - lo) / (hi - lo) * bins) : 0;
          b = std::clamp(b, 0, bins - 1);
          lanes[static_cast<std::size_t>(b)].push_back(eid);
        }
        for (int b = 0; b < bins; ++b)
          groups.push_back(Json{{"id", id + ":" + std::to_string(b)}, {"kind", "lane"}, {"composition", id}, {"measure", m->id},
                                {"from", round6(lo + (hi - lo) * b / bins)}, {"to", round6(lo + (hi - lo) * (b + 1) / bins)}, {"members", lanes[static_cast<std::size_t>(b)]}});
      } else {
        unsupp(id, "composition", "unsupported_composition", Json{{"composition", kind}});
      }
    }
    return groups;
  }

  // ── the whole evaluation ──
  Result<Json> run() {
    focus = json::get_string(request, "focus");
    if (focus.empty()) return Error(Errc::InvalidArgument, "a view needs a focus (request.focus: a node id)");
    if (!ctx.db) return Error(Errc::InvalidArgument, "evaluating a view needs a graph");
    LOOM_TRY_ASSIGN(auto fnode, ctx.db->get_node(focus));
    if (!fnode) return Error(Errc::NotFound, "no such node: " + focus);
    info[focus] = NodeInfo{fnode->label, fnode->kind, fnode->created};
    std::set<std::string> ids_seen;
    for (const auto& el : decl.value("elements", Json::array())) {
      const std::string id = json::get_string(el, "id"), kind = json::get_string(el, "kind");
      if (id.empty() || !ids_seen.insert(id).second) {
        issue("element_id_missing_or_duplicate", Json{{"id", id}, {"kind", kind}});
        if (id.empty()) continue;
      }
      if (kind == "source") src_el.push_back(el);
      else if (kind == "measure") meas_el.push_back(el);
      else if (kind == "coordinates") coord_el.push_back(el);
      else if (kind == "encoding") enc_el.push_back(el);
      else if (kind == "composition") comp_el.push_back(el);
      else if (kind == "rules") rules_el.push_back(el);
      else unsupp(id, kind, "unsupported_element_kind", Json{{"kind", kind}});
    }
    LOOM_TRY(load_sources());
    LOOM_TRY(load_details());
    build_walks();
    find_sector_element();
    // every measure is evaluated (the report lists them all), in the declared order; the ones others need come first by recursion
    for (const auto& m : meas_el) measure(json::get_string(m, "id"));
    compute_coordinates();
    compute_encodings();
    Json enc_report = encodings_report();

    std::vector<std::string> visible_vec;
    for (const auto& id : element_ids()) visible_vec.push_back(id);
    std::map<std::string, Json> collapsed;
    Json budget = budget_report(visible_vec, collapsed);
    const std::set<std::string> visible(visible_vec.begin(), visible_vec.end());

    Json elements = Json::array();
    for (const auto& id : visible_vec) {
      Json el{{"id", id}, {"label", info[id].label}, {"kind", info[id].kind}};
      if (id == focus) el["focus"] = true;
      Json mv = Json::object();
      for (const auto& m : meas_el) {
        const auto it = meas.find(json::get_string(m, "id"));
        if (it != meas.end() && it->second.ok && it->second.values.count(id)) mv[it->first] = it->second.values.at(id);
      }
      el["measures"] = mv;
      const auto& hit = sector_of(id);
      if (hit.valid) el["sector"] = hit.primary;
      el["coords"] = coords.count(id) ? coords[id] : Json::object();
      Json ch = Json::object();
      for (const auto& e : encs)
        if (e.ok && e.values.count(id)) ch[e.channel] = e.values.at(id);
      el["channels"] = ch;
      Json from = Json::array();
      for (const auto& s : srcs)
        if (s.ok && s.hops.count(id)) from.push_back(s.id);
      el["sources"] = from;
      elements.push_back(std::move(el));
    }
    Json edges = Json::array();
    for (const auto& s : srcs)
      if (s.ok)
        for (const auto& e : s.edges) {
          const std::string a = json::get_string(e, "src"), b = json::get_string(e, "dst");
          if (!visible.count(a) || !visible.count(b)) continue;
          edges.push_back(Json{{"src", a}, {"dst", b}, {"type", json::get_string(e, "type")}, {"kind", json::get_string(e, "kind")}, {"source", s.id}});
        }
    Json groups = compose_groups(visible);
    for (auto& [gid, g] : collapsed) groups.push_back(g);
    Json stats = Json::object();
    for (const auto& m : meas_el) {
      const auto it = meas.find(json::get_string(m, "id"));
      if (it != meas.end() && it->second.ok) stats[it->first] = it->second.stats;
    }
    Json src_report = Json::array();
    for (const auto& s : srcs) {
      Json r{{"id", s.id}, {"perspective", s.perspective}, {"weight", s.weight}, {"depth", s.depth}, {"role", s.role}, {"ok", s.ok}, {"reached", s.hops.size()}};
      src_report.push_back(std::move(r));
    }
    const Json meta = decl.value("_meta", Json::object());
    return Json{{"schema", "loom.view_model/1"}, {"declaration", meta}, {"focus", focus}, {"sources", src_report}, {"elements", elements}, {"edges", edges},
                {"groups", groups}, {"measure_stats", stats}, {"coordinate_systems", coord_report}, {"encodings", enc_report}, {"unsupported", unsupported},
                {"budget", budget}, {"issues", issues}};
  }
};

Result<ViewEngine> ViewEngine::open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs) {
  ViewEngine v;
  LOOM_TRY_ASSIGN(v.persp_, Perspectives::open(root, extra_dirs));
  LOOM_TRY_ASSIGN(v.declarations_, effective(root, "view_declarations", extra_dirs));
  LOOM_TRY_ASSIGN(v.measures_, effective(root, "measure_kinds", extra_dirs));
  LOOM_TRY_ASSIGN(v.systems_, effective(root, "coordinate_systems", extra_dirs));
  LOOM_TRY_ASSIGN(v.channels_, effective(root, "view_channels", extra_dirs));
  LOOM_TRY_ASSIGN(v.compositions_, effective(root, "composition_kinds", extra_dirs));
  LOOM_TRY_ASSIGN(v.combines_, effective(root, "combination_rules", extra_dirs));
  LOOM_TRY_ASSIGN(v.rules_, effective(root, "view_rules", extra_dirs));
  LOOM_TRY_ASSIGN(v.renderers_, effective(root, "view_renderers", extra_dirs));
  return v;
}

Json ViewEngine::list() const {
  Json issues = Json::array();
  for (const Effective* e : {&declarations_, &measures_, &systems_, &channels_, &compositions_, &combines_, &rules_, &renderers_})
    for (const auto& i : e->issues) issues.push_back(i);
  auto entries = [](const Effective& e) { return e.to_json(false); };
  return Json{{"schema", "loom.view_engine/1"}, {"declarations", entries(declarations_)}, {"measure_kinds", entries(measures_)}, {"coordinate_systems", entries(systems_)},
              {"channels", entries(channels_)}, {"composition_kinds", entries(compositions_)}, {"combination_rules", entries(combines_)}, {"rules", entries(rules_)},
              {"renderers", entries(renderers_)}, {"issues", issues}};
}

Result<Json> ViewEngine::evaluate(const RenderContext& ctx, const std::string& declaration_id, const Json& request) const {
  const EffectiveEntry* d = declarations_.find(declaration_id);
  if (!d) return Error(Errc::NotFound, "no such view declaration: " + declaration_id);
  if (d->disabled) return Error(Errc::Unavailable, "view declaration " + d->id + " is disabled");
  Json decl = d->value;
  decl["_meta"] = Json{{"id", d->id}, {"label", d->value.value("label", Json(d->id))}, {"origin", d->origin}, {"provenance", d->provenance}};
  return evaluate_declaration(ctx, decl, request);
}

Result<Json> ViewEngine::evaluate_declaration(const RenderContext& ctx, const Json& declaration, const Json& request) const {
  if (!declaration.is_object()) return Error(Errc::InvalidArgument, "a view declaration is an object {label?, elements: [...]}");
  Json decl = declaration;
  if (!decl.contains("_meta")) decl["_meta"] = Json{{"id", decl.value("id", std::string("inline"))}, {"origin", "request"}};
  Evaluator ev(*this, ctx, std::move(decl), request);
  return ev.run();
}

}  // namespace loom::views
