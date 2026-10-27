// Layout fingerprint (P2): see layout_fingerprint.h.
#include "import/layout_fingerprint.h"

#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include "loom/util/sha256.h"

namespace loom::import_graph {

namespace fs = std::filesystem;

namespace {

// A compiled pattern, or nothing when the data holds an invalid one (reported as an advisory by the caller).
struct Pattern {
  bool ok = false;
  std::regex re;
};

class Patterns {
 public:
  const Pattern& get(const std::string& source, Json& advisories) {
    auto it = cache_.find(source);
    if (it != cache_.end()) return it->second;
    Pattern p;
    try {
      p.re = std::regex(source, std::regex::ECMAScript);
      p.ok = true;
    } catch (const std::regex_error&) {
      advisories.push_back(Json{{"code", "layout_bad_pattern"}, {"pattern", source}});
    }
    return cache_.emplace(source, std::move(p)).first->second;
  }

 private:
  std::map<std::string, Pattern> cache_;
};

bool matches(const Pattern& p, const std::string& s) { return p.ok && std::regex_search(s, p.re); }

std::string lower(std::string s) {
  for (auto& ch : s)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  return s;
}

std::string extension_of(const std::string& name) {
  const auto slash = name.find_last_of('/');
  const std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
  const auto dot = base.find_last_of('.');
  return dot == std::string::npos || dot + 1 >= base.size() ? std::string() : lower(base.substr(dot + 1));
}

// Member regexes the content descriptors of every layout name (what the evidence reader must read).
std::vector<std::string> content_member_patterns(const Json& config) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  const Json layouts = config.value("layouts", Json::object());
  if (!layouts.is_object()) return out;
  for (const auto& it : layouts.items()) {
    const Json ds = it.value().is_object() ? it.value().value("descriptors", Json::array()) : Json::array();
    if (!ds.is_array()) continue;
    for (const auto& d : ds) {
      const std::string member = json::get_string(d, "member");
      if (!member.empty() && seen.insert(member).second) out.push_back(member);
    }
  }
  return out;
}

// Object keys seen in a (possibly truncated) JSON prefix down to `max_depth` nesting levels of objects/arrays.
std::set<std::string> keys_in_prefix(const std::string& text, std::int64_t max_depth) {
  std::set<std::string> keys;
  std::int64_t depth = 0;
  std::vector<char> stack;
  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    const char c = text[i];
    if (c == '"') {
      std::string s;
      ++i;
      while (i < n && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < n) {
          s += text[i + 1];
          i += 2;
          continue;
        }
        s += text[i++];
      }
      ++i;  // closing quote
      std::size_t j = i;
      while (j < n && (text[j] == ' ' || text[j] == '\n' || text[j] == '\r' || text[j] == '\t')) ++j;
      if (j < n && text[j] == ':' && !stack.empty() && stack.back() == '{' && depth <= max_depth) keys.insert(s);
      continue;
    }
    if (c == '{' || c == '[') {
      stack.push_back(c);
      ++depth;
    } else if ((c == '}' || c == ']') && !stack.empty()) {
      stack.pop_back();
      --depth;
    }
    ++i;
  }
  return keys;
}

std::string json_top_kind(const std::string& text) {
  for (const char c : text) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (static_cast<unsigned char>(c) == 0xEF) continue;  // UTF-8 BOM bytes
    if (static_cast<unsigned char>(c) == 0xBB || static_cast<unsigned char>(c) == 0xBF) continue;
    if (c == '[') return "array";
    if (c == '{') return "object";
    return "scalar";
  }
  return std::string();
}

std::string html_title(const std::string& text) {
  const std::string low = lower(text);
  const auto a = low.find("<title");
  if (a == std::string::npos) return std::string();
  const auto gt = low.find('>', a);
  if (gt == std::string::npos) return std::string();
  const auto b = low.find("</title", gt);
  return text.substr(gt + 1, (b == std::string::npos ? text.size() : b) - gt - 1);
}

double reliability_of(const Json& claims, const Json& d) {
  if (const Json* r = json::find(d, "r"); r && r->is_number()) return r->get<double>();
  const std::string claim = json::get_string(d, "claim");
  const Json* c = json::find(claims, claim);
  if (!c || !c->is_object()) return 0.5;  // an unknown claim knows nothing: logit 0
  const double a = json::get_number(*c, "alpha", 1.0), b = json::get_number(*c, "beta", 1.0);
  return a + b > 0 ? a / (a + b) : 0.5;
}

std::vector<std::string> strings_of(const Json& v) {
  std::vector<std::string> out;
  if (v.is_string()) out.push_back(v.get<std::string>());
  if (v.is_array())
    for (const auto& x : v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

}  // namespace

void normalise_layout_evidence(LayoutEvidence& ev, const Json& config) {
  if (!json::get_bool(config, "strip_common_root", true) || ev.members.size() < 1) return;
  std::string root;
  for (const auto& m : ev.members) {
    const auto slash = m.name.find('/');
    if (slash == std::string::npos || slash == 0) return;  // a member at the top: no common root
    const std::string first = m.name.substr(0, slash);
    if (root.empty()) root = first;
    else if (root != first) return;
  }
  if (root.empty()) return;
  for (auto& m : ev.members) m.name = m.name.substr(root.size() + 1);
  ev.root = ev.root.empty() ? root : ev.root + "/" + root;
}

LayoutEvidence layout_evidence(const fs::path& path, const std::string& display_name, const Json& config) {
  LayoutEvidence ev;
  const std::int64_t prefix_bytes = std::max<std::int64_t>(0, json::get_int(config, "read_prefix_bytes", 262144));
  const std::int64_t max_reads = std::max<std::int64_t>(0, json::get_int(config, "max_content_members", 32));
  Patterns patterns;
  std::vector<const Pattern*> wanted;
  for (const auto& p : content_member_patterns(config)) wanted.push_back(&patterns.get(p, ev.advisories));
  const auto wants = [&](const std::string& name) {
    for (const Pattern* p : wanted)
      if (matches(*p, name)) return true;
    return false;
  };

  mz_zip_archive zip{};
  if (mz_zip_reader_init_file(&zip, path.string().c_str(), 0)) {
    ev.container = "zip";
    std::vector<mz_uint> index_of;  // archive index of every listed member
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
      if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
      mz_zip_archive_file_stat st;
      if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
      ev.members.push_back(LayoutMember{st.m_filename, std::string(), false});
      index_of.push_back(i);
    }
    // which members to read is decided on the normalised names, then read by their index in the archive
    LayoutEvidence names = ev;
    normalise_layout_evidence(names, config);
    std::int64_t reads = 0;
    for (std::size_t k = 0; k < ev.members.size() && reads < max_reads; ++k) {
      if (!wants(names.members[k].name)) continue;
      if (auto* it = mz_zip_reader_extract_iter_new(&zip, index_of[k], 0)) {
        std::string buf(static_cast<std::size_t>(prefix_bytes), '\0');
        std::size_t got = 0;
        while (got < buf.size()) {
          const std::size_t r = mz_zip_reader_extract_iter_read(it, buf.data() + got, buf.size() - got);
          if (r == 0) break;
          got += r;
        }
        mz_zip_reader_extract_iter_free(it);
        buf.resize(got);
        ev.members[k].prefix = std::move(buf);
        ev.members[k].has_prefix = true;
        ++reads;
      }
    }
    mz_zip_reader_end(&zip);
    normalise_layout_evidence(ev, config);
    return ev;
  }
  ev.container = "file";
  LayoutMember m;
  m.name = !display_name.empty() ? fs::path(display_name).filename().string() : path.filename().string();
  std::ifstream in(path, std::ios::binary);
  if (in) {
    std::string buf(static_cast<std::size_t>(prefix_bytes), '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    m.prefix = std::move(buf);
    m.has_prefix = true;
  }
  ev.members.push_back(std::move(m));
  return ev;
}

Json fingerprint_layout(const LayoutEvidence& ev, const Json& config) {
  Json advisories = ev.advisories.is_array() ? ev.advisories : Json::array();
  Patterns patterns;
  const Json claims = config.value("claims", Json::object());
  const Json layouts = config.value("layouts", Json::object());
  const double unknown_evidence = json::get_number(config, "unknown_evidence", 2.0);
  const double confident_at = json::get_number(config, "confident_at", 0.8);
  const double tie_within = json::get_number(config, "tie_within", 0.02);
  const std::int64_t max_depth = json::get_int(config, "json_key_depth", 4);

  const auto first_with_prefix = [&](const Pattern& p) -> const LayoutMember* {
    for (const auto& m : ev.members)
      if (m.has_prefix && matches(p, m.name)) return &m;
    return nullptr;
  };

  struct Cand {
    std::string key;
    Json profile;
    double evidence = 0.0;
    bool excluded = false;
    Json satisfied = Json::array(), failed = Json::array(), optional_missing = Json::array();
  };
  std::vector<Cand> cands;
  if (layouts.is_object()) {
    for (const auto& it : layouts.items()) {
      if (!it.value().is_object() || !json::get_bool(it.value(), "enabled", true)) continue;
      Cand c;
      c.key = it.key();
      c.profile = it.value();
      c.evidence = json::get_number(c.profile, "prior", 0.0);
      const Json ds = c.profile.value("descriptors", Json::array());
      if (!ds.is_array()) continue;
      for (const auto& d : ds) {
        if (!d.is_object()) continue;
        const std::string kind = json::get_string(d, "d");
        const std::string arg = json::get_string(d, "arg");
        std::optional<bool> sat;
        if (kind == "container") {
          sat = matches(patterns.get(arg, advisories), ev.container);
        } else if (kind == "member_present" || kind == "member_absent" || kind == "member_count") {
          const Pattern& p = patterns.get(arg, advisories);
          std::int64_t count = 0;
          for (const auto& m : ev.members)
            if (matches(p, m.name)) ++count;
          if (kind == "member_present") sat = count > 0;
          else if (kind == "member_absent") sat = p.ok && count == 0;
          else {
            const std::int64_t lo = json::get_int(d, "min", 0);
            const Json* hi = json::find(d, "max");
            sat = count >= lo && (!hi || !hi->is_number() || count <= hi->get<std::int64_t>());
          }
        } else if (kind == "root_folder") {
          sat = matches(patterns.get(arg, advisories), ev.root) || (arg.empty() && ev.root.empty());
        } else if (kind == "ext_set") {
          std::set<std::string> allowed;
          for (const auto& e : strings_of(d.value("arg", Json())))
            allowed.insert(lower(e));
          for (const auto& e : strings_of(d.value("exts", Json())))
            allowed.insert(lower(e));
          bool all = !ev.members.empty();
          for (const auto& m : ev.members)
            if (!allowed.count(extension_of(m.name))) all = false;
          sat = all;
        } else if (kind == "json_top" || kind == "json_keys" || kind == "js_marker" || kind == "html_title") {
          const Pattern& mp = patterns.get(json::get_string(d, "member"), advisories);
          const LayoutMember* m = first_with_prefix(mp);
          if (!m) {
            sat = false;
          } else if (kind == "json_top") {
            sat = json_top_kind(m->prefix) == arg;
          } else if (kind == "json_keys") {
            const auto keys = keys_in_prefix(m->prefix, max_depth);
            const std::string mode = json::get_string(d, "mode", "all");
            const auto wanted = strings_of(d.value("keys", Json()));
            std::size_t hit = 0;
            for (const auto& k : wanted)
              if (keys.count(k)) ++hit;
            sat = mode == "any" ? hit > 0 : mode == "none" ? hit == 0 : (!wanted.empty() && hit == wanted.size());
          } else if (kind == "js_marker") {
            bool found = false;
            for (const auto& mm : ev.members)
              if (mm.has_prefix && matches(mp, mm.name) && mm.prefix.find(arg) != std::string::npos) found = true;
            sat = found;
          } else {
            sat = matches(patterns.get(arg, advisories), html_title(m->prefix));
          }
        } else {
          advisories.push_back(Json{{"code", "layout_unknown_descriptor"}, {"layout", c.key}, {"descriptor", kind}});
          continue;
        }
        const bool optional = json::get_bool(d, "optional", false);
        Json item{{"d", kind}};
        if (!arg.empty()) item["arg"] = arg;
        if (d.contains("member")) item["member"] = d["member"];
        if (d.contains("claim")) item["claim"] = d["claim"];
        if (*sat) {
          const double r = std::clamp(reliability_of(claims, d), 1e-6, 1.0 - 1e-6);
          const double gain = json::get_number(d, "weight", 1.0) * std::max(0.0, std::log(r / (1.0 - r)));
          c.evidence += gain;
          item["gain"] = std::round(gain * 1000.0) / 1000.0;
          c.satisfied.push_back(item);
        } else if (optional) {
          c.optional_missing.push_back(item);
        } else {
          c.excluded = true;
          c.failed.push_back(item);
        }
      }
      cands.push_back(std::move(c));
    }
  }

  // softmax over the non-excluded candidates and the pseudo-candidate "unknown layout"
  double top = unknown_evidence;
  for (const auto& c : cands)
    if (!c.excluded) top = std::max(top, c.evidence);
  double total = std::exp(unknown_evidence - top);
  for (const auto& c : cands)
    if (!c.excluded) total += std::exp(c.evidence - top);
  const auto share = [&](double e) { return std::exp(e - top) / total; };
  const auto round3 = [](double x) { return std::round(x * 1000.0) / 1000.0; };

  std::vector<std::size_t> order(cands.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    if (cands[a].excluded != cands[b].excluded) return !cands[a].excluded;
    return cands[a].evidence > cands[b].evidence;
  });

  Json list = Json::array();
  std::map<std::string, double> by_class;
  for (const std::size_t i : order) {
    const Cand& c = cands[i];
    const double s = c.excluded ? 0.0 : share(c.evidence);
    const std::string cls = json::get_string(c.profile, "class");
    if (!c.excluded) by_class[cls] += s;
    Json descriptors = c.profile.value("descriptors", Json::array());
    const std::string hash = "sha256:" + Sha256::hex(json::canonical(descriptors)).substr(0, 16);
    Json item{{"key", c.key},
              {"id", hash},
              {"class", cls},
              {"label", c.profile.value("label", Json(c.key))},
              {"share", round3(s)},
              {"evidence", round3(c.evidence)},
              {"excluded", c.excluded},
              {"satisfied", c.satisfied},
              {"failed", c.failed},
              {"optional_missing", c.optional_missing}};
    for (const char* k : {"class_id", "observed", "unknowns", "reader", "source_level"})
      if (c.profile.contains(k)) item[k] = c.profile[k];
    list.push_back(std::move(item));
  }
  const double unknown_share = share(unknown_evidence);

  Json chosen = nullptr;
  std::string state = "unknown";
  bool tie = false;
  if (!list.empty() && !list[0].value("excluded", true)) {
    const double s0 = list[0].value("share", 0.0);
    const double s1 = list.size() > 1 && !list[1].value("excluded", true) ? list[1].value("share", 0.0) : 0.0;
    tie = s0 - s1 <= tie_within;
    if (s0 > unknown_share) {
      chosen = list[0];
      state = s0 >= confident_at && !tie ? "confident" : "uncertain";
    }
  }
  Json cls = nullptr;
  if (!by_class.empty()) {
    auto best = std::max_element(by_class.begin(), by_class.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    if (best->second > unknown_share)
      cls = Json{{"id", best->first}, {"share", round3(best->second)},
                 {"state", best->second >= confident_at ? "confident" : "uncertain"}};
  }
  return Json{{"schema", "loom.export_layout_fingerprint/1"},
              {"state", state},
              {"chosen", chosen},
              {"class", cls},
              {"tie", tie},
              {"unknown_share", round3(unknown_share)},
              {"candidates", list},
              {"evidence", Json{{"container", ev.container}, {"root", ev.root},
                                {"members", static_cast<std::int64_t>(ev.members.size())}}},
              {"advisories", advisories}};
}

}  // namespace loom::import_graph
