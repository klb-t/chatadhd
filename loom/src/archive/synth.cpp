// Synthesis: renders the archive artifacts (MASTER.md, source_map.csv,
// timeline.json, items.jsonl, graph.json, project_manifest.json,
// gap_report.md) from the stage outputs. Pure and deterministic.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "archive/archive_runtime.h"
#include "archive/profile.h"
#include "loom/util/utf8.h"

namespace loom::archive {

namespace {

std::string esc_ref(std::string s) {
  for (auto& c : s) {
    if (c == '[') c = '(';
    if (c == ']') c = ')';
    if (c == '\n' || c == '\r' || c == '|') c = ' ';
  }
  return s;
}

std::string markdown_text(std::string_view s, std::size_t max_cp) {
  std::string t = clip(s, max_cp);
  std::string out;
  for (char c : t) {
    if (c == '|') {
      out += "\\|";
    } else {
      out.push_back(c);
    }
  }
  // strip leading markdown emphasis/markers that would render oddly in bullets
  while (!out.empty() && (out[0] == '#' || out[0] == '>')) out.erase(0, 1);
  return std::string(utf8::strip(out));
}

std::string csv(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += "\"\"";
    else if (c == '\n' || c == '\r') out.push_back(' ');
    else out.push_back(c);
  }
  return out + "\"";
}

std::string format_number(double v, int precision) {
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(precision);
  os << v;
  return os.str();
}

std::string kind_name(const std::string& k, const ArchiveProfile& policy) {
  const auto& labels = policy.value("/synthesis/kind_labels");
  auto it = labels.find(k);
  return it == labels.end() ? policy.text("/synthesis_rendering/unknown_kind_label") : it->get<std::string>();
}

std::string render_text(const ArchiveProfile& policy, std::string_view name, const Json& variables) {
  const auto& values = policy.value("/synthesis_rendering/strings");
  auto result = render_profile_template(values.at(std::string(name)).get<std::string>(), variables);
  if (!result) throw std::invalid_argument(result.error().to_string());
  return std::move(*result);
}

std::optional<std::size_t> interface_prefix(const std::string& name, const ArchiveProfile& policy) {
  if (name.size() < static_cast<std::size_t>(policy.integer("/synthesis_rendering/interfaces/minimum_bytes"))) return std::nullopt;
  const bool uppercase = policy.value("/synthesis_rendering/interfaces/require_uppercase_after_prefix").get<bool>();
  for (const auto& value : policy.value("/synthesis_rendering/interfaces/prefixes")) {
    const auto& prefix = value.get_ref<const std::string&>();
    if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size()) continue;
    if (uppercase && !std::isupper(static_cast<unsigned char>(name[prefix.size()]))) continue;
    return prefix.size();
  }
  return std::nullopt;
}

std::string reference(const Doc& d, const ArchiveProfile& policy) {
  const auto location = !d.label.empty() && d.label != d.title
                            ? render_text(policy, "source_location", Json{{"label", esc_ref(d.label)}}) : "";
  const auto day = date_only(d.date);
  const auto date = day.empty() ? "" : render_text(policy, "source_date", Json{{"date", day}});
  return render_text(policy, "source_reference", Json{{"title", esc_ref(d.title)}, {"location", location}, {"date", date}});
}

struct CodeIndex {
  struct File {
    std::string uri;
    std::set<std::string> strong;  // path + symbol words
    std::set<std::string> weak;    // + comment words
    std::set<std::string> symbols; // exact symbol names (and last segment)
  };
  std::vector<File> files;
  std::map<std::string, std::vector<std::size_t>> symbol_owner;  // lower symbol -> files
  std::map<std::string, std::vector<std::size_t>> used_by;       // identifier used in code lines -> files
  std::set<std::string> uris;
};

std::string norm_word(const std::string& t, const ArchiveProfile& policy) { return stem(gloss(t, &policy), &policy); }

CodeIndex build_code_index(const Corpus& c, const ArchiveProfile& policy) {
  CodeIndex ix;
  for (const auto& d : c.docs) {
    if (d.kind != "code") continue;
    CodeIndex::File f;
    f.uri = d.uri;
    for (const auto& t : tokenize(d.uri)) f.strong.insert(norm_word(t, policy));
    for (const auto& t : split_identifier(d.uri)) f.strong.insert(norm_word(t, policy));
    if (const Json* syms = json::find(d.extra, "symbols"); syms && syms->is_array()) {
      for (const auto& s : *syms) {
        std::string name = s.get<std::string>();
        std::string last = name;
        if (auto p = last.rfind(':'); p != std::string::npos) last = last.substr(p + 1);
        if (auto p = last.rfind('.'); p != std::string::npos) last = last.substr(p + 1);
        f.symbols.insert(name);
        f.symbols.insert(last);
        for (const auto& w : split_identifier(name)) f.strong.insert(norm_word(w, policy));
      }
    }
    f.weak = f.strong;
    for (const auto& t : content_tokens(d.text, &policy)) f.weak.insert(norm_word(t, policy));
    for (const auto& id : camel_identifiers(d.text, &policy)) f.weak.insert(norm_word(utf8::to_lower(id), policy));
    std::size_t idx = ix.files.size();
    for (const auto& s : f.symbols) ix.symbol_owner[utf8::to_lower(s)].push_back(idx);
    if (const Json* uses = json::find(d.extra, "uses"); uses && uses->is_array()) {
      for (const auto& u : *uses) ix.used_by[utf8::to_lower(u.get<std::string>())].push_back(idx);
    }
    ix.uris.insert(f.uri);
    ix.files.push_back(std::move(f));
  }
  return ix;
}

struct Evidence {
  std::string status;  // implemented | mentioned | partial | missing
  std::vector<std::string> files;
  std::vector<std::string> matched;
  std::size_t words = 0;
};

Evidence phrase_evidence(const CodeIndex& ix, std::string_view phrase, const ArchiveProfile& policy) {
  Evidence ev;
  std::vector<std::string> words;
  for (const auto& t : content_tokens(phrase, &policy)) {
    std::string w = norm_word(t, policy);
    if (std::find(words.begin(), words.end(), w) == words.end()) words.push_back(w);
  }
  ev.words = words.size();
  if (words.empty() || ix.files.empty()) {
    ev.status = words.empty() ? "" : "missing";
    return ev;
  }
  std::size_t best_strong = 0;
  std::vector<std::pair<std::size_t, std::string>> full_strong, full_weak;
  std::string best_file;
  std::vector<std::string> best_matched;
  for (const auto& f : ix.files) {
    std::size_t s = 0, w = 0;
    std::vector<std::string> m;
    auto has_prefix = [&](const std::set<std::string>& set, const std::string& word) {
      // same 6-letter stem ("retrieval" ~ "retrieve", "normalizacja" ~ "normalize")
      if (word.size() < static_cast<std::size_t>(policy.integer("/synthesis/prefix_match_bytes"))) return false;
      std::string pre = word.substr(0, static_cast<std::size_t>(policy.integer("/synthesis/prefix_match_bytes")));
      auto it = set.lower_bound(pre);
      return it != set.end() && it->compare(0, pre.size(), pre) == 0;
    };
    for (const auto& x : words) {
      if (f.strong.count(x) || has_prefix(f.strong, x)) {
        ++s;
        m.push_back(x);
      }
      if (f.weak.count(x) || has_prefix(f.weak, x)) ++w;
    }
    if (s == words.size()) full_strong.emplace_back(f.strong.size(), f.uri);
    else if (w == words.size()) full_weak.emplace_back(f.weak.size(), f.uri);
    if (s > best_strong || (s == best_strong && s > 0 && f.uri < best_file)) {
      best_strong = s;
      best_file = f.uri;
      best_matched = m;
    }
  }
  auto take = [&](std::vector<std::pair<std::size_t, std::string>>& v) {
    std::sort(v.begin(), v.end());  // smaller (more specific) files first, then path
    for (std::size_t i = 0; i < v.size() && i < static_cast<std::size_t>(policy.integer("/synthesis/evidence_files")); ++i) ev.files.push_back(v[i].second);
  };
  if (!full_strong.empty()) {
    ev.status = "implemented";
    take(full_strong);
  } else if (!full_weak.empty()) {
    ev.status = "mentioned";
    take(full_weak);
  } else if (static_cast<double>(best_strong) >= policy.number("/synthesis/partial_fraction") * static_cast<double>(words.size()) && best_strong > 0) {
    ev.status = "partial";
    ev.files.push_back(best_file);
    ev.matched = best_matched;
  } else {
    ev.status = "missing";
  }
  return ev;
}

bool is_spec_doc(const Doc& d, const ArchiveProfile& policy) { return policy.contains("/synthesis/spec_kinds", d.kind); }

// Words that are products/formats rather than components of this project.
bool is_brand(const std::string& id, const ArchiveProfile& policy) { return policy.contains("/synthesis/external_names", id); }

}  // namespace

std::string source_ref(const Doc& d) {
  ProfileScope scope(nullptr);
  return reference(d, scope.get());
}

SynthesisOutput synthesize(const SynthesisInput& in) {
  ProfileScope scope(in.profile);
  const auto& policy = scope.get();
  SynthesisOutput out;
  const Corpus& c = *in.corpus;
  const Json variables{{"files", policy.value("/synthesis_rendering/files")}, {"project", in.project},
                       {"pipeline_version", std::string(kPipelineVersion)}, {"round", in.round}};
  auto text = [&](std::string_view name) { return render_text(policy, name, variables); };
  auto length = [&](std::string_view name) {
    return static_cast<std::size_t>(policy.integer("/synthesis_rendering/display/" + std::string(name)));
  };
  auto md_text = [&](std::string_view value, std::optional<std::size_t> maximum = std::nullopt) {
    return markdown_text(value, maximum.value_or(length("item_codepoints")));
  };
  auto ref = [&](const std::string& key) {
    const Doc* d = c.find(key);
    return d ? reference(*d, policy) : render_text(policy, "missing_source", Json{{"key", key}});
  };
  std::map<std::string, std::string> theme_label;
  for (const auto& t : in.themes) theme_label[json::get_string(t, "id")] = json::get_string(t, "label");

  // ── item selection helpers ───────────────────────────────────────
  std::unordered_map<std::string, const Item*> item_by_id;
  for (const auto& it : in.items) item_by_id[it.id] = &it;
  std::map<std::string, std::vector<const ItemEdge*>> edges_to, edges_from;
  for (const auto& e : in.edges) {
    edges_to[e.dst].push_back(&e);
    edges_from[e.src].push_back(&e);
  }
  auto pick = [&](const std::string& type, std::size_t cap, bool include_inactive, std::size_t* total) {
    std::vector<const Item*> v;
    for (const auto& it : in.items) {
      if (it.type != type) continue;
      if (!include_inactive && !policy.contains("/synthesis_rendering/selection/active_statuses", it.status)) continue;
      v.push_back(&it);
    }
    std::sort(v.begin(), v.end(), [&](const Item* a, const Item* b) {
      const Doc* da = c.find(a->doc);
      const Doc* db = c.find(b->doc);
      int pa = da && policy.contains("/synthesis_rendering/selection/deprioritized_doc_kinds", da->kind) ? 1 : 0;
      int pb = db && policy.contains("/synthesis_rendering/selection/deprioritized_doc_kinds", db->kind) ? 1 : 0;
      if (pa != pb) return pa < pb;
      if (a->confidence != b->confidence) return a->confidence > b->confidence;
      if (a->date != b->date) return a->date > b->date;
      return a->id < b->id;
    });
    std::vector<const Item*> uniq;
    std::set<std::string> seen;
    for (const Item* it : v) {
      std::string norm = utf8::to_lower(clip(it->text, length("dedup_codepoints")));
      if (!seen.insert(norm).second) continue;
      uniq.push_back(it);
    }
    *total = uniq.size();
    if (uniq.size() > cap) uniq.resize(cap);
    return uniq;
  };
  auto item_line = [&](const Item* it) {
    std::string s = "- " + md_text(it->text);
    s += " — " + ref(it->doc);
    if (!it->theme.empty()) s += " `" + it->theme + "`";
    if (it->status == "superseded") {
      for (const auto* e : edges_to[it->id]) {
        if (e->type == "supersedes") {
          s += text("superseded_by") + md_text(item_by_id.count(e->src) ? item_by_id[e->src]->text : e->src, length("superseded_codepoints"));
          if (item_by_id.count(e->src)) s += " " + ref(item_by_id[e->src]->doc);
          break;
        }
      }
    } else if (it->status == "contested") {
      s += text("contested_see_11");
    } else if (it->status == "resolved") {
      for (const auto* e : edges_to[it->id]) {
        if (e->type == "resolves" && item_by_id.count(e->src)) {
          s += text("resolved_by") + ref(item_by_id[e->src]->doc);
          break;
        }
      }
    }
    return s + "\n";
  };

  // ── Gap analysis ─────────────────────────────────────────────────
  CodeIndex ix = build_code_index(c, policy);
  Json gap = Json::object();
  std::string gap_md;
  {
    std::ostringstream g;
    std::size_t spec_docs = 0;
    std::map<std::string, int> langs;
    for (const auto& d : c.docs) {
      if (is_spec_doc(d, policy) || d.kind == "chat") ++spec_docs;
      if (d.kind == "code") ++langs[json::get_string(d.extra, "language")];
    }
    g << text("gap_report_spec_repository") << in.project << ")\n\n";
    g << text("generated_by_loom_archive_deterministic_spec_side") << spec_docs
      << text("document_sections_chat_messages_repository_side") << ix.files.size() << text("code_files");
    if (!langs.empty()) {
      g << " (";
      bool first = true;
      for (const auto& [l, n] : langs) {
        g << (first ? "" : ", ") << l << " " << n;
        first = false;
      }
      g << ")";
    }
    g << text("evidence_levels_implemented_every_word_of_the_component_appears");

    // 1. named components / interfaces
    struct Named {
      std::string name;
      int mentions = 0;
      std::string first_key;
    };
    std::map<std::string, Named> named;
    for (const auto& d : c.docs) {
      if (!is_spec_doc(d, policy) && d.kind != "chat") continue;
      for (const auto& id : camel_identifiers(d.text, &policy)) {
        if (id.empty() || id.size() < static_cast<std::size_t>(policy.integer("/synthesis/named_min_bytes")) || is_brand(id, policy) || (policy.value("/synthesis_rendering/identifiers/exclude_project_name").get<bool>() && utf8::to_lower(id) == utf8::to_lower(in.project))) continue;
        if (policy.value("/synthesis_rendering/identifiers/require_uppercase_initial").get<bool>() &&
            !std::isupper(static_cast<unsigned char>(id[0]))) continue;
        bool plural_acronym = false;
        if (policy.value("/synthesis_rendering/identifiers/exclude_plural_acronyms").get<bool>()) {
          for (const auto& value : policy.value("/synthesis_rendering/identifiers/plural_acronym_suffixes")) {
            const auto& suffix = value.get_ref<const std::string&>();
            if (id.size() < suffix.size() || id.compare(id.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            if (std::all_of(id.begin(), id.end() - static_cast<std::ptrdiff_t>(suffix.size()), [](char ch) {
                  return std::isupper(static_cast<unsigned char>(ch));
                })) plural_acronym = true;
          }
        }
        if (plural_acronym) continue;
        auto& n = named[id];
        if (n.name.empty()) {
          n.name = id;
          n.first_key = d.key;
        }
        ++n.mentions;
      }
    }
    // Polish inflections ("WatchDoga", "GitHuba") fold into the base name.
    for (auto it = named.begin(); it != named.end();) {
      bool merged = false;
      for (std::size_t cut = 1; cut <= static_cast<std::size_t>(policy.integer("/synthesis/inflection_max_bytes")) &&
                              it->first.size() > cut && it->first.size() - cut >=
                              static_cast<std::size_t>(policy.integer("/synthesis_rendering/identifiers/inflection_min_base_bytes")); ++cut) {
        std::string base = it->first.substr(0, it->first.size() - cut);
        if (policy.value("/synthesis_rendering/identifiers/inflection_require_lowercase_suffix").get<bool>() &&
            !std::islower(static_cast<unsigned char>(it->first.back()))) break;
        auto b = named.find(base);
        if (b != named.end() && b != it) {
          b->second.mentions += it->second.mentions;
          merged = true;
          break;
        }
        if (is_brand(base, policy)) {
          merged = true;
          break;
        }
      }
      it = merged ? named.erase(it) : std::next(it);
    }
    Json comps = Json::array();
    struct Row {
      int rank;
      bool iface;
      int mentions;
      std::string name;
      std::string text;
    };
    std::vector<Row> rows_bad;
    std::vector<std::string> rows_ok, rows_used;
    std::size_t n_impl = 0, n_part = 0, n_miss = 0;
    for (const auto& [id, n] : named) {
      std::string low = utf8::to_lower(id);
      std::string status, note;
      std::vector<std::string> files;
      const auto prefix = interface_prefix(id, policy);
      bool iface = prefix.has_value();
      if (auto it = ix.symbol_owner.find(low); it != ix.symbol_owner.end()) {
        status = "implemented";
        for (std::size_t k = 0; k < it->second.size() && k < static_cast<std::size_t>(policy.integer("/synthesis_rendering/component_evidence/declaration_files")); ++k) files.push_back(ix.files[it->second[k]].uri);
      } else if (iface && ix.symbol_owner.count(utf8::to_lower(id.substr(*prefix)))) {
        status = "partial";
        const auto& own = ix.symbol_owner.at(utf8::to_lower(id.substr(*prefix)));
        for (std::size_t k = 0; k < own.size() && k < static_cast<std::size_t>(policy.integer("/synthesis_rendering/component_evidence/declaration_files")); ++k) files.push_back(ix.files[own[k]].uri);
        note = text("concrete") + id.substr(*prefix) + text("exists_no") + id + text("interface");
      } else if (auto u = ix.used_by.find(low); u != ix.used_by.end()) {
        status = "used";  // external API used by the code (declared elsewhere)
        for (std::size_t k = 0; k < u->second.size() && k < static_cast<std::size_t>(policy.integer("/synthesis_rendering/component_evidence/used_files")); ++k) files.push_back(ix.files[u->second[k]].uri);
      } else {
        status = "missing";
        for (const auto& f : ix.files) {
          if (files.size() >= static_cast<std::size_t>(policy.integer("/synthesis_rendering/component_evidence/comment_files"))) break;
          if (f.weak.count(norm_word(low, policy))) {
            files.push_back(f.uri);
          }
        }
        if (!files.empty()) note = text("no_declaration_only_named_in_code_comments");
      }
      comps.push_back(Json{{"name", id}, {"kind", "identifier"}, {"status", status}, {"evidence", files},
                           {"mentions", n.mentions}, {"source", n.first_key}, {"note", note}});
      std::string ev;
      for (const auto& f : files) ev += (ev.empty() ? "" : ", ") + ("`" + f + "`");
      std::string row = "| `" + id + "` | " + status + " | " + (note.empty() ? ev : note + (ev.empty() ? "" : ": " + ev)) +
                        " | " + std::to_string(n.mentions) + " × " + ref(n.first_key) + " |\n";
      if (status == "used") {
        rows_used.push_back("`" + id + "`");
      } else if (status == "implemented") {
        ++n_impl;
        rows_ok.push_back(row);
      } else {
        (status == "partial" ? n_part : n_miss) += 1;
        rows_bad.push_back({status == "missing" ? 0 : 1, iface, n.mentions, id, row});
      }
    }
    std::sort(rows_bad.begin(), rows_bad.end(), [](const Row& a, const Row& b) {
      if (a.rank != b.rank) return a.rank < b.rank;
      if (a.iface != b.iface) return a.iface;
      if (a.mentions != b.mentions) return a.mentions > b.mentions;
      return a.name < b.name;
    });

    // 1b. named technologies / products: capitalised multi-word phrases
    std::map<std::string, std::pair<int, std::string>> phrases;  // phrase -> (mentions, first doc)
    for (const auto& d : c.docs) {
      if (!is_spec_doc(d, policy) && d.kind != "chat") continue;
      std::vector<std::string> run;
      auto flush_run = [&] {
        if (run.size() >= static_cast<std::size_t>(policy.integer("/synthesis/title_phrase_min_tokens")) && run.size() <= static_cast<std::size_t>(policy.integer("/synthesis/title_phrase_max_tokens"))) {
          std::string ph;
          for (const auto& w : run) ph += (ph.empty() ? "" : " ") + w;
          auto& e = phrases[ph];
          if (e.first++ == 0) e.second = d.key;
        }
        run.clear();
      };
      // body text without heading lines (Title Case headings are not names)
      std::string t;
      {
        std::size_t p = 0;
        while (p < d.text.size()) {
          std::size_t e = d.text.find('\n', p);
          if (e == std::string::npos) e = d.text.size();
          std::string_view line = utf8::lstrip(std::string_view(d.text).substr(p, e - p));
          if (line.empty() || line.front() != '#') {
            t.append(line);
          }
          t.push_back('\n');
          p = e + 1;
        }
      }
      std::size_t i = 0;
      bool single_space = false;  // separator since the previous word was exactly " "
      while (i < t.size()) {
        std::size_t b = i;
        while (i < t.size() && std::isalpha(static_cast<unsigned char>(t[i]))) ++i;
        if (i == b) {
          // separator run
          std::size_t sb = i;
          while (i < t.size() && !std::isalpha(static_cast<unsigned char>(t[i]))) ++i;
          single_space = (i - sb == 1 && t[sb] == ' ');
          if (!single_space) flush_run();
          continue;
        }
        std::string w = t.substr(b, i - b);
        bool glued = i < t.size() && static_cast<unsigned char>(t[i]) >= 0x80;  // Polish letter follows
        bool cap = !glued && w.size() >= static_cast<std::size_t>(policy.integer("/synthesis/title_word_min_bytes")) && std::isupper(static_cast<unsigned char>(w[0])) &&
                   std::all_of(w.begin() + 1, w.end(), [](char ch) { return std::islower(static_cast<unsigned char>(ch)); }) &&
                   !is_stopword(utf8::to_lower(w), &policy);
        if (!cap) {
          flush_run();
          continue;
        }
        if (!run.empty() && !single_space) flush_run();
        run.push_back(w);
      }
      flush_run();
    }
    std::vector<std::string> phrase_missing, phrase_ok;
    for (const auto& [ph, e] : phrases) {
      std::string joined;
      for (const auto& w : tokenize(ph)) joined += w;
      bool sym = ix.symbol_owner.count(joined) > 0;
      Evidence ev = phrase_evidence(ix, ph, policy);
      std::string status = sym || ev.status == "implemented" ? "implemented" : "missing";
      std::string where = sym ? ix.files[ix.symbol_owner.at(joined).front()].uri : (ev.files.empty() ? "" : ev.files[0]);
      comps.push_back(Json{{"name", ph}, {"kind", "named_phrase"}, {"status", status},
                           {"evidence", where.empty() ? Json::array() : Json::array({where})},
                           {"mentions", e.first}, {"source", e.second}, {"note", ""}});
      if (status == "implemented") {
        phrase_ok.push_back(ph + " → `" + where + "`");
      } else {
        phrase_missing.push_back("- **" + ph + "** — " + std::to_string(e.first) + " × " + ref(e.second) + "\n");
      }
    }

    // 2. spec sections: bullet features
    struct SecRow {
      std::string heading;
      std::string key;
      std::vector<std::pair<std::string, Evidence>> feats;
    };
    std::vector<SecRow> secs;
    std::size_t f_total = 0, f_impl = 0, f_part = 0, f_missing = 0;
    for (const auto& d : c.docs) {
      if (!is_spec_doc(d, policy)) continue;
      SecRow row;
      row.heading = d.title + " › " + d.label;
      row.key = d.key;
      std::size_t pos = 0;
      bool fence = false;
      while (pos < d.text.size()) {
        std::size_t nl = d.text.find('\n', pos);
        if (nl == std::string::npos) nl = d.text.size();
        std::string_view line = utf8::strip(std::string_view(d.text).substr(pos, nl - pos));
        pos = nl + 1;
        if (line.substr(0, 3) == "```") fence = !fence;
        if (fence) continue;
        if (line.size() < 3 || !((line[0] == '-' || line[0] == '*') && line[1] == ' ')) continue;
        std::string feat(utf8::strip(line.substr(2)));
        // drop trailing punctuation and bold markers
        feat.erase(std::remove(feat.begin(), feat.end(), '*'), feat.end());
        feat.erase(std::remove(feat.begin(), feat.end(), '`'), feat.end());
        while (!feat.empty() && (feat.back() == ',' || feat.back() == '.' || feat.back() == ';' ||
                                 feat.back() == ':')) {
          feat.pop_back();
        }
        // "Term — description" / "Term: description": the term is the feature
        for (const auto& value : policy.value("/synthesis_rendering/feature_terms/separators")) {
          const auto& sep = value.get_ref<const std::string&>();
          std::size_t k = feat.find(sep);
          if (k == std::string::npos || k == 0) continue;
          std::string head = feat.substr(0, k);
          std::size_t n = tokenize(head).size();
          if (n >= static_cast<std::size_t>(policy.integer("/synthesis_rendering/feature_terms/minimum_tokens")) &&
              n <= static_cast<std::size_t>(policy.integer("/synthesis_rendering/feature_terms/maximum_tokens"))) {
            feat = head;
            break;
          }
        }
        if (utf8::length(feat) > static_cast<std::size_t>(policy.integer("/synthesis/feature_max_codepoints")) || content_tokens(feat, &policy).empty()) continue;
        Evidence ev = phrase_evidence(ix, feat, policy);
        if (ev.status.empty()) continue;
        row.feats.emplace_back(feat, ev);
      }
      if (row.feats.size() >= static_cast<std::size_t>(policy.integer("/synthesis/section_min_features"))) secs.push_back(std::move(row));
    }
    // worst coverage first
    auto covered = [](const SecRow& r) {
      std::size_t ok = 0;
      for (const auto& [f, e] : r.feats) ok += e.status == "implemented" || e.status == "mentioned";
      return ok;
    };
    std::stable_sort(secs.begin(), secs.end(), [&](const SecRow& a, const SecRow& b) {
      double ra = a.feats.empty() ? 0.0 : static_cast<double>(covered(a)) / static_cast<double>(a.feats.size());
      double rb = b.feats.empty() ? 0.0 : static_cast<double>(covered(b)) / static_cast<double>(b.feats.size());
      if (ra != rb) return ra < rb;
      if (a.feats.size() != b.feats.size()) return a.feats.size() > b.feats.size();
      return a.heading < b.heading;
    });
    Json sections = Json::array();
    std::ostringstream secmd;
    for (const auto& s : secs) {
      std::size_t ok = 0;
      for (const auto& [f, e] : s.feats) {
        ++f_total;
        if (e.status == "implemented" || e.status == "mentioned") {
          ++ok;
          ++f_impl;
        } else if (e.status == "partial") {
          ++f_part;
        } else {
          ++f_missing;
        }
      }
      Json feats = Json::array();
      secmd << "### " << md_text(s.heading, length("section_heading_codepoints")) << " — " << ok << "/" << s.feats.size() << text("with_code_evidence");
      std::string implemented_list;
      for (const auto& [f, e] : s.feats) {
        feats.push_back(Json{{"feature", f}, {"status", e.status}, {"evidence", e.files}, {"matched", e.matched}});
        if (e.status == "missing") {
          secmd << text("missing") << md_text(f, length("feature_codepoints")) << "\n";
        } else if (e.status == "partial") {
          std::string m;
          for (const auto& x : e.matched) m += (m.empty() ? "" : ", ") + x;
          secmd << text("partial") << md_text(f, length("feature_codepoints")) << text("closest") << (e.files.empty() ? "" : e.files[0])
                << text("matches") << m << ")\n";
        } else {
          implemented_list += (implemented_list.empty() ? "" : "; ") + md_text(f, length("evidence_feature_codepoints")) + " → `" +
                              (e.files.empty() ? "" : e.files[0]) + "`" +
                              (e.status == "mentioned" ? text("comment") : "");
        }
      }
      if (!implemented_list.empty()) secmd << text("evidence") << implemented_list << "\n";
      secmd << text("source") << ref(s.key) << "\n\n";
      sections.push_back(Json{{"section", s.heading}, {"source", s.key}, {"covered", ok},
                              {"features", feats}});
    }

    // 3. referenced files that do not exist
    std::set<std::string> known = ix.uris;
    for (const auto& d : c.docs) known.insert(d.uri);
    std::map<std::string, std::string> missing_files;  // path -> first doc key
    for (const auto& d : c.docs) {
      if (!is_spec_doc(d, policy) && d.kind != "chat") continue;
      const std::string& t = d.text;
      std::size_t i = 0;
      while (i < t.size()) {
        std::size_t b = i;
        while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '/' || t[i] == '_' ||
                                t[i] == '.' || t[i] == '-')) {
          ++i;
        }
        if (i > b) {
          std::string tok = t.substr(b, i - b);
          while (!tok.empty() && (tok.back() == '.' || tok.back() == '/')) tok.pop_back();
          std::string ext = tok.find('.') != std::string::npos ? tok.substr(tok.rfind('.')) : "";

          if (tok.find('/') != std::string::npos && policy.contains("/synthesis/source_extensions", ext) && tok.find("//") == std::string::npos &&
              tok.find("..") == std::string::npos && tok[0] != '/' && tok.find("http") != 0) {
            bool found = false;
            for (const auto& k : known) {
              if (k == tok || (k.size() > tok.size() && k.compare(k.size() - tok.size(), tok.size(), tok) == 0 &&
                               k[k.size() - tok.size() - 1] == '/')) {
                found = true;
                break;
              }
            }
            if (!found && !missing_files.count(tok)) missing_files[tok] = d.key;
          }
        }
        ++i;
      }
    }

    // 4. themes without code
    std::vector<std::string> theme_rows;
    for (const auto& t : in.themes) {
      int code = 0, total = 0;
      for (const auto& k : t["docs"]) {
        const Doc* d = c.find(k.get<std::string>());
        if (!d) continue;
        ++total;
        if (d->kind == "code") ++code;
      }
      if (code == 0 && total >= policy.integer("/synthesis/theme_gap_min_docs")) {
        theme_rows.push_back("- `" + json::get_string(t, "id") + "` " + json::get_string(t, "label") + " — " +
                             std::to_string(total) + text("documents_no_code_file_in_the_theme"));
      }
    }

    // 5. TODO / FIXME
    std::vector<std::string> todos;
    for (const auto& d : c.docs) {
      if (d.kind != "code") continue;
      if (const Json* td = json::find(d.extra, "todos"); td && td->is_array()) {
        for (const auto& x : *td) {
          todos.push_back("- `" + d.uri + ":" + std::to_string(json::get_int(x, "line")) + "` " +
                          md_text(json::get_string(x, "text"), length("todo_codepoints")) + "\n");
        }
      }
    }

    g << text("summary_check_result");
    g << text("named_components_interfaces_in_the_spec") << n_impl << text("implemented") << n_part << text("partial_2")
      << n_miss << text("missing_2");
    g << text("named_technologies_products_capitalised_phrases") << phrase_ok.size() << text("with_code_evidence_2")
      << phrase_missing.size() << text("without");
    g << text("spec_features_bullets_in") << secs.size() << text("sections") << f_impl << text("with_evidence") << f_part
      << text("partial_2") << f_missing << text("missing_of") << f_total << ") |\n";
    g << text("files_referenced_by_the_spec_but_absent") << missing_files.size() << " |\n";
    g << text("themes_without_any_code") << theme_rows.size() << " |\n";
    g << text("todo_fixme_markers_in_code") << todos.size() << " |\n\n";

    g << text("1_named_components_and_interfaces");
    g << text("camelcase_names_used_in_documents_and_conversations_checked_against");
    if (!rows_bad.empty()) {
      g << text("missing_or_partial_interfaces_first_name_status_evidence_mentioned");
      for (const auto& r : rows_bad) g << r.text;
      g << "\n";
    }
    if (!rows_used.empty()) {
      g << text("external_apis_used_by_the_code_not_gaps");
      for (std::size_t i = 0; i < rows_used.size(); ++i) g << (i ? ", " : "") << rows_used[i];
      g << "\n\n";
    }
    if (!rows_ok.empty()) {
      g << text("implemented_name_status_evidence_mentioned_in");
      for (const auto& r : rows_ok) g << r;
      g << "\n";
    }
    g << text("named_technologies_without_code_evidence");
    if (phrase_missing.empty()) g << text("none");
    for (const auto& r : phrase_missing) g << r;
    if (!phrase_ok.empty()) {
      g << text("with_evidence_2");
      for (std::size_t i = 0; i < phrase_ok.size(); ++i) g << (i ? "; " : "") << phrase_ok[i];
      g << ".\n";
    }
    g << text("2_spec_sections_feature_coverage");
    g << text("each_bullet_of_a_spec_section_is_treated_as");
    g << secmd.str();
    g << text("3_files_referenced_but_not_present");
    if (missing_files.empty()) g << text("none_2");
    for (const auto& [p, k] : missing_files) g << "- `" << p << "` — " << ref(k) << "\n";
    if (!missing_files.empty()) g << "\n";
    g << text("4_themes_without_code");
    if (theme_rows.empty()) g << text("none");
    for (const auto& r : theme_rows) g << r;
    g << text("5_open_work_in_code_todo_fixme");
    if (todos.empty()) g << text("none");
    for (std::size_t i = 0; i < todos.size() && i < static_cast<std::size_t>(policy.integer("/synthesis/todo_limit")); ++i) g << todos[i];
    if (todos.size() > static_cast<std::size_t>(policy.integer("/synthesis/todo_limit"))) g << "- … " << todos.size() - static_cast<std::size_t>(policy.integer("/synthesis/todo_limit")) << text("more_in_files_items");
    gap_md = g.str();
    Json mf = Json::array();
    for (const auto& [p, k] : missing_files) mf.push_back(Json{{"path", p}, {"source", k}});
    gap = Json{{"components", comps},
               {"sections", sections},
               {"missing_files", mf},
               {"summary", Json{{"components_implemented", n_impl},
                                {"components_partial", n_part},
                                {"components_missing", n_miss},
                                {"features_total", f_total},
                                {"features_with_evidence", f_impl},
                                {"features_partial", f_part},
                                {"features_missing", f_missing},
                                {"todos", todos.size()}}}};
  }

  // ── MASTER.md ────────────────────────────────────────────────────
  std::ostringstream m;
  std::map<std::string, int> kinds;
  std::string dmin, dmax;
  for (const auto& d : c.docs) {
    ++kinds[d.kind];
    std::string day = date_only(d.date);
    if (day.empty()) continue;
    if (dmin.empty() || day < dmin) dmin = day;
    if (dmax.empty() || day > dmax) dmax = day;
  }
  m << "# " << in.project << text("master_generated");
  m << text("generated_by_loom_archive_pipeline_v") << kPipelineVersion << text("synthesis_round") << in.round
    << text("from") << c.docs.size() << text("documents_in") << c.sources.size()
    << text("sources_deterministic_the_same_inputs_give_the_same_file");
  m << text("1_corpus_kind_documents");
  for (const auto& [k, n] : kinds) m << "| " << kind_name(k, policy) << " | " << n << " |\n";
  m << text("date_range") << (dmin.empty() ? text("unknown") : dmin + " → " + dmax) << text("retrieved_relevant_documents")
    << in.hits.size() << text("forks_found") << c.forks.size() << ".\n\n";

  m << text("2_retrieval_passes_and_vocabulary_pass_terms_hits_new");
  for (const auto& p : in.passes) {
    std::string added;
    if (const Json* a = json::find(p, "added"); a && a->is_array()) {
      for (const auto& t : *a) added += (added.empty() ? "" : ", ") + t.get<std::string>();
    }
    m << "| " << json::get_int(p, "pass") << " | " << json::get_int(p, "terms") << " | " << json::get_int(p, "hits")
      << " | " << json::get_int(p, "new_hits") << " | " << (added.empty() ? "—" : added) << " |\n";
  }
  m << text("seed_terms");
  {
    std::string seeds;
    for (const auto& v : in.vocab) {
      if (v.origin == "seed") seeds += (seeds.empty() ? "" : ", ") + ("`" + v.term + "`");
    }
    m << seeds << "\n\n";
  }
  m << text("expansion_why_each_term_was_added");
  for (const auto& v : in.vocab) {
    if (v.origin == "seed") continue;
    m << "- `" << v.term << "` (" << v.origin << text("pass") << v.pass << ") — ";
    for (std::size_t i = 0; i < v.reasons.size() && i < static_cast<std::size_t>(policy.integer("/synthesis/reason_limit")); ++i) m << (i ? "; " : "") << v.reasons[i];
    if (!v.evidence.empty()) m << text("e_g") << ref(v.evidence[0]);
    m << "\n";
  }
  m << text("3_themes");
  std::map<std::string, const Json*> tl_by_theme;
  for (const auto& t : in.timeline) tl_by_theme[json::get_string(t, "id")] = &t;
  std::map<std::string, double> hit_score;
  for (const auto& h : in.hits) hit_score[json::get_string(h, "key")] = json::get_number(h, "score");
  for (const auto& t : in.themes) {
    std::string id = json::get_string(t, "id");
    std::string terms;
    int k = 0;
    for (const auto& x : t["terms"]) {
      if (k++ >= policy.integer("/synthesis/theme_term_limit")) break;
      terms += (terms.empty() ? "" : ", ") + x.get<std::string>();
    }
    std::string span;
    if (auto it = tl_by_theme.find(id); it != tl_by_theme.end()) {
      std::string a = json::get_string(*it->second, "first"), b = json::get_string(*it->second, "last");
      if (!a.empty()) span = ", " + a + " → " + b;
    }
    std::map<std::string, int> tk;
    std::vector<std::pair<double, std::string>> top;
    for (const auto& x : t["docs"]) {
      const Doc* d = c.find(x.get<std::string>());
      if (!d) continue;
      ++tk[d->kind];
      top.emplace_back(-hit_score[d->key], d->key);
    }
    std::sort(top.begin(), top.end());
    std::string mix;
    for (const auto& [kk, n] : tk) mix += (mix.empty() ? "" : ", ") + std::to_string(n) + " " + kind_name(kk, policy);
    m << "### " << id << " · " << json::get_string(t, "label") << "\n\n";
    m << json::get_int(t, "size") << text("documents") << mix << ")" << span << text("key_terms") << terms << ".\n\n";
    m << text("top_sources");
    std::set<std::string> shown_units;
    int shown = 0;
    for (const auto& [s, key] : top) {
      if (shown >= policy.integer("/synthesis/theme_doc_limit")) break;
      const Doc* d = c.find(key);
      if (!d || !shown_units.insert(d->unit).second) continue;
      m << "- " << ref(key) << "\n";
      if (++shown >= policy.integer("/synthesis/theme_doc_limit")) break;
    }
    m << "\n";
  }
  if (!in.global_terms.empty()) {
    m << text("project_wide_terms_present_across_most_themes_not_clustered");
    std::string g;
    for (const auto& x : in.global_terms) g += (g.empty() ? "" : ", ") + x.get<std::string>();
    m << g << ".\n\n";
  }

  m << text("4_timeline_and_forks");
  for (const auto& t : in.timeline) {
    const Json& ev = t["events"];
    if (ev.empty()) continue;
    m << "### " << json::get_string(t, "id") << " · " << json::get_string(t, "label") << "\n\n";
    std::size_t n = ev.size();
    auto line = [&](const Json& e) {
      std::string day = json::get_string(e, "date");
      const Doc* d = c.find(json::get_string(e, "key"));
      m << "- " << (day.empty() ? text("undated") : day) << " — " << (d ? kind_name(d->kind, policy) : "") << ": "
        << ref(json::get_string(e, "key")) << "\n";
    };
    const auto head = static_cast<std::size_t>(policy.integer("/synthesis/timeline_head"));
    const auto tail = static_cast<std::size_t>(policy.integer("/synthesis/timeline_tail"));
    if (n <= head + tail) {
      for (const auto& e : ev) line(e);
    } else {
      for (std::size_t i = 0; i < head; ++i) line(ev[i]);
      m << "- … " << (n - head - tail) << text("more_events_files_timeline");
      for (std::size_t i = n - tail; i < n; ++i) line(ev[i]);
    }
    if (const Json* fk = json::find(t, "forks"); fk && !fk->empty()) {
      m << text("forks");
      for (const auto& f : *fk) {
        std::string after = json::get_string(f, "after");
        m << "- " << json::get_string(f, "origin") << text("fork_in") << md_text(json::get_string(f, "title"), length("fork_title_codepoints"));
        if (!after.empty()) m << text("after") << ref(after);
        m << ": ";
        int ai = 0;
        for (const auto& a : f["alternatives"]) {
          std::string first = json::get_string(a, "first");
          m << (ai++ ? " | " : "") << (json::get_bool(a, "current") ? text("kept") : text("abandoned"))
            << (first.empty() ? "" : ref(first)) << " (" << json::get_int(a, "messages") << text("message_count_suffix");
        }
        m << "\n";
      }
    }
    m << "\n";
  }

  for (const auto& section : policy.value("/synthesis/sections")) {
    const auto title = section.at("title").get<std::string>();
    const auto type = section.at("type").get<std::string>();
    const auto cap = section.at("cap").get<std::size_t>();
    const auto inactive = section.at("inactive").get<bool>();
    std::size_t total = 0;
    auto v = pick(type, cap, inactive, &total);
    m << "## " << title << "\n\n";
    if (policy.contains("/synthesis_rendering/selection/question_summary_types", type)) {
      std::size_t resolved = 0;
      for (const auto& it : in.items) resolved += it.type == type && policy.contains("/synthesis_rendering/selection/answered_statuses", it.status);
      m << text("unresolved_questions") << total << "); " << resolved
        << text("earlier_questions_were_answered_by_later_decisions_and_are");
    }
    if (v.empty()) m << text("none_found");
    for (const Item* it : v) m << item_line(it);
    if (total > v.size()) m << "- … " << total - v.size() << text("more_in_files_items");
    m << "\n";
  }
  m << text("11_supersessions_and_contradictions");
  if (in.edges.empty()) m << text("none_detected");
  std::size_t shown_edges = 0;
  for (const auto& e : in.edges) {
    if (e.type == "resolves") continue;
    if (++shown_edges > static_cast<std::size_t>(policy.integer("/synthesis/edge_limit"))) break;
    const Item* a = item_by_id.count(e.src) ? item_by_id[e.src] : nullptr;
    const Item* b = item_by_id.count(e.dst) ? item_by_id[e.dst] : nullptr;
    if (!a || !b) continue;
    m << "- **" << e.type << "**: " << md_text(a->text, length("edge_codepoints")) << " " << ref(a->doc) << "  \n  "
      << (e.type == "supersedes" ? text("edge_over") : text("edge_vs")) << ": " << md_text(b->text, length("edge_codepoints")) << " " << ref(b->doc)
      << text("blank") << e.reason << "_\n";
  }
  m << text("12_gaps_spec_code");
  const Json& gs = gap["summary"];
  m << text("named_components_and_technologies") << json::get_int(gs, "components_implemented") << text("implemented")
    << json::get_int(gs, "components_partial") << text("partial_2") << json::get_int(gs, "components_missing")
    << text("missing_3");
  m << text("spec_features") << json::get_int(gs, "features_with_evidence") << " of " << json::get_int(gs, "features_total")
    << text("have_code_evidence") << json::get_int(gs, "features_missing") << text("have_none");
  std::vector<const Json*> bad;
  std::vector<std::pair<std::int64_t, std::string>> phrase_gaps;
  for (const auto& comp : gap["components"]) {
    std::string st = json::get_string(comp, "status");
    if (st != "missing" && st != "partial") continue;
    if (json::get_string(comp, "kind") == "named_phrase") {
      phrase_gaps.emplace_back(-json::get_int(comp, "mentions"), json::get_string(comp, "name"));
      continue;
    }
    bad.push_back(&comp);
  }
  std::sort(phrase_gaps.begin(), phrase_gaps.end());
  std::string missing_phrases;
  for (std::size_t i = 0; i < phrase_gaps.size() && i < static_cast<std::size_t>(policy.integer("/synthesis/phrase_gap_limit")); ++i) {
    missing_phrases += (i ? ", " : "") + phrase_gaps[i].second;
  }
  if (phrase_gaps.size() > static_cast<std::size_t>(policy.integer("/synthesis/phrase_gap_limit"))) missing_phrases += " (+" + std::to_string(phrase_gaps.size() - static_cast<std::size_t>(policy.integer("/synthesis/phrase_gap_limit"))) + text("more");
  std::stable_sort(bad.begin(), bad.end(), [&](const Json* a, const Json* b) {
    auto key = [&](const Json* x) {
      std::string n = json::get_string(*x, "name");
      bool iface = interface_prefix(n, policy).has_value();
      return std::make_tuple(json::get_string(*x, "status") == "missing" ? 0 : 1, iface ? 0 : 1,
                             -json::get_int(*x, "mentions"), n);
    };
    return key(a) < key(b);
  });
  int listed = 0;
  for (const Json* cp : bad) {
    const Json& comp = *cp;
    if (listed++ >= policy.integer("/synthesis/component_gap_limit")) break;
    m << "- `" << json::get_string(comp, "name") << "`: " << json::get_string(comp, "status");
    std::string note = json::get_string(comp, "note");
    if (!note.empty()) m << " — " << note;
    m << " " << ref(json::get_string(comp, "source")) << "\n";
  }
  if (!missing_phrases.empty()) m << text("named_technologies_without_code_evidence_2") << missing_phrases << ".\n";
  {
    int shown_secs = 0;
    for (const auto& sec : gap["sections"]) {
      std::size_t total = sec["features"].size();
      std::int64_t cov = json::get_int(sec, "covered");
      if (total < static_cast<std::size_t>(policy.integer("/synthesis/section_gap_min_features")) || static_cast<double>(cov) >= policy.number("/synthesis/section_gap_coverage_fraction") * static_cast<double>(total)) continue;
      if (shown_secs++ == 0) m << text("spec_sections_with_the_least_code_evidence");
      if (shown_secs > policy.integer("/synthesis/section_gap_limit")) break;
      std::string missing;
      int k = 0;
      for (const auto& f : sec["features"]) {
        if (json::get_string(f, "status") != "missing") continue;
        if (k++ >= policy.integer("/synthesis/missing_feature_limit")) break;
        missing += (missing.empty() ? "" : "; ") + json::get_string(f, "feature");
      }
      m << "  - " << cov << "/" << total << " " << ref(json::get_string(sec, "source"));
      if (!missing.empty()) m << text("missing_4") << md_text(missing, length("missing_features_codepoints"));
      m << "\n";
    }
  }
  m << text("full_details_files_gap");

  // ── source_map.csv ───────────────────────────────────────────────
  std::ostringstream sm;
  const auto& columns = policy.value("/synthesis_rendering/csv/columns");
  for (std::size_t i = 0; i < columns.size(); ++i) {
    if (i) sm << ",";
    const auto name = columns[i].at("name").get<std::string>();
    sm << (name.find_first_of(",\"\r\n") == std::string::npos ? name : csv(name));
  }
  sm << "\n";
  std::set<std::string> cited;
  for (const auto& h : in.hits) cited.insert(json::get_string(h, "key"));
  for (const auto& it : in.items) cited.insert(it.doc);
  for (const auto& v : in.vocab) {
    for (const auto& e : v.evidence) cited.insert(e);
  }
  std::map<std::string, const Json*> hit_by_key;
  for (const auto& h : in.hits) hit_by_key[json::get_string(h, "key")] = &h;
  for (const auto& d : c.docs) {
    if (!cited.count(d.key)) continue;
    std::string terms;
    double score = 0;
    if (auto it = hit_by_key.find(d.key); it != hit_by_key.end()) {
      score = json::get_number(*it->second, "score");
      for (const auto& t : (*it->second)["terms"]) terms += (terms.empty() ? "" : policy.text("/synthesis_rendering/csv/term_separator")) + t.get<std::string>();
    }
    const Json row{{"ref", reference(d, policy)}, {"key", d.key}, {"kind", d.kind}, {"title", d.title},
                   {"location", d.label}, {"date", date_only(d.date)}, {"uri", d.uri},
                   {"theme", json::get_string(in.doc_theme, d.key)},
                   {"score", format_number(score, policy.integer("/synthesis_rendering/csv/score_precision"))}, {"terms", terms}};
    for (std::size_t i = 0; i < columns.size(); ++i) {
      if (i) sm << ",";
      const auto& field = columns[i];
      const auto pointer = field.at("path").get<std::string>();
      const Json::json_pointer path(pointer);
      if (!row.contains(path)) throw std::invalid_argument("archive CSV selects missing row field " + pointer);
      const auto& value = row.at(path);
      const auto serialized = value.is_string() ? value.get<std::string>() : json::dump(value);
      sm << (field.at("quote").get<bool>() ? csv(serialized) : serialized);
    }
    sm << "\n";
  }

  // ── items.jsonl ──────────────────────────────────────────────────
  std::string items_jsonl;
  {
    std::vector<const Item*> v;
    for (const auto& it : in.items) v.push_back(&it);
    std::sort(v.begin(), v.end(), [](const Item* a, const Item* b) {
      return std::tie(a->theme, a->type, a->date, a->id) < std::tie(b->theme, b->type, b->date, b->id);
    });
    for (const Item* it : v) {
      Json j = it->to_json();
      j["source"] = ref(it->doc);
      Json rel = Json::array();
      for (const auto* e : edges_from[it->id]) rel.push_back(Json{{"type", e->type}, {"target", e->dst}});
      for (const auto* e : edges_to[it->id]) rel.push_back(Json{{"type", e->type + "_by"}, {"target", e->src}});
      if (!rel.empty()) j["relations"] = rel;
      items_jsonl += json::dump(j) + "\n";
    }
  }

  // ── graph.json ───────────────────────────────────────────────────
  Json nodes = Json::array(), gedges = Json::array();
  for (const auto& t : in.themes) {
    nodes.push_back(Json{{"id", json::get_string(t, "id")}, {"kind", "theme"}, {"label", json::get_string(t, "label")},
                         {"size", json::get_int(t, "size")}});
    for (const auto& term : t["terms"]) {
      nodes.push_back(Json{{"id", "term:" + term.get<std::string>()}, {"kind", "term"}, {"label", term}});
      gedges.push_back(Json{{"src", "term:" + term.get<std::string>()}, {"dst", json::get_string(t, "id")},
                            {"type", "part_of"}});
    }
  }
  for (const auto& h : in.hits) {
    std::string key = json::get_string(h, "key");
    const Doc* d = c.find(key);
    if (!d) continue;
    nodes.push_back(Json{{"id", key}, {"kind", d->kind}, {"label", reference(*d, policy)}, {"date", date_only(d->date)}});
    std::string th = json::get_string(in.doc_theme, key);
    if (!th.empty()) gedges.push_back(Json{{"src", key}, {"dst", th}, {"type", "part_of"}});
  }
  for (const auto& it : in.items) {
    if (policy.contains("/synthesis_rendering/selection/graph_excluded_item_types", it.type)) continue;  // commits are already doc nodes
    nodes.push_back(Json{{"id", it.id}, {"kind", it.type}, {"label", clip(it.text, length("graph_item_codepoints"))}, {"status", it.status}});
    gedges.push_back(Json{{"src", it.id}, {"dst", it.doc}, {"type", "derived_from"}});
  }
  for (const auto& e : in.edges) gedges.push_back(Json{{"src", e.src}, {"dst", e.dst}, {"type", e.type}});
  for (const auto& f : c.forks) {
    std::string after = json::get_string(f, "after");
    for (const auto& a : f["alternatives"]) {
      std::string first = json::get_string(a, "first");
      if (!after.empty() && !first.empty()) {
        gedges.push_back(Json{{"src", first}, {"dst", after}, {"type", json::get_bool(a, "current") ? "reply_to" : "fork_of"}});
      }
    }
  }
  Json graph{{"project", in.project}, {"nodes", nodes}, {"edges", gedges}};

  // ── project_manifest.json ────────────────────────────────────────
  auto items_of = [&](const std::string& type, bool active_only) {
    Json a = Json::array();
    std::size_t total = 0;
    for (const Item* it : pick(type, static_cast<std::size_t>(policy.integer("/synthesis_rendering/selection/manifest_items")), !active_only, &total)) {
      a.push_back(Json{{"id", it->id}, {"text", clip(it->text, length("manifest_item_codepoints"))}, {"theme", it->theme}, {"status", it->status},
                       {"confidence", it->confidence}, {"source", ref(it->doc)}});
    }
    return a;
  };
  Json vocab_j = Json::array();
  for (const auto& v : in.vocab) vocab_j.push_back(v.term);
  Json themes_j = Json::array();
  for (const auto& t : in.themes) {
    themes_j.push_back(Json{{"id", t["id"]}, {"label", t["label"]}, {"terms", t["terms"]}, {"size", t["size"]}});
  }
  Json manifest{{"project", in.project},
                {"generated_by", text("manifest_generated_by")},
                {"settings", in.settings},
                {"vocabulary", vocab_j},
                {"themes", themes_j},
                {"components", gap["components"]},
                {"requirements", items_of("requirement", true)},
                {"invariants", items_of("invariant", true)},
                {"decisions", items_of("decision", false)},
                {"rejected_options", items_of("rejected_option", false)},
                {"open_questions", items_of("open_question", true)},
                {"missing_files", gap["missing_files"]},
                {"sources", c.sources}};

  // ── timeline.json ────────────────────────────────────────────────
  Json timeline{{"project", in.project}, {"themes", in.timeline}, {"forks", c.forks}};

  // ── discovered terms (feed the next round) ───────────────────────
  {
    std::set<std::string> vstems;
    for (const auto& v : in.vocab) {
      vstems.insert(stem(v.term, &policy));
      for (const auto& t : tokenize(v.term)) vstems.insert(stem(t, &policy));
    }
    std::map<std::string, int> cnt;
    for (const auto& it : in.items) {
      if (!policy.contains("/synthesis_rendering/selection/discovery_item_types", it.type)) {
        continue;
      }
      std::set<std::string> seen;
      for (const auto& id : camel_identifiers(it.text, &policy)) {
        std::string low = utf8::to_lower(id);
        if (id.size() < static_cast<std::size_t>(policy.integer("/synthesis/named_min_bytes")) || is_brand(id, policy) || vstems.count(stem(low, &policy)) || !seen.insert(low).second) continue;
        ++cnt[low];
      }
    }
    std::vector<std::pair<int, std::string>> v;
    for (const auto& [t, n] : cnt) {
      if (n >= policy.integer("/synthesis/discovered_min_df")) v.emplace_back(-n, t);
    }
    std::sort(v.begin(), v.end());
    for (std::size_t i = 0; i < v.size() && i < static_cast<std::size_t>(policy.integer("/synthesis/discovered_limit")); ++i) out.discovered_terms.push_back(v[i].second);
  }

  auto emit = [&](std::string_view kind, std::string content) {
    const auto name = policy.value("/synthesis_rendering/files").at(std::string(kind)).get<std::string>();
    if (name.empty()) return;  // a disabled projection
    if (!out.files.emplace(name, std::move(content)).second) {
      throw std::invalid_argument("archive artifact projections select the same filename: " + name);
    }
  };
  emit("master", m.str());
  emit("gap", gap_md);
  emit("source_map", sm.str());
  emit("items", items_jsonl);
  emit("graph", json::dump(graph, policy.integer("/synthesis_rendering/json_indent")) + "\n");
  emit("timeline", json::dump(timeline, policy.integer("/synthesis_rendering/json_indent")) + "\n");
  emit("manifest", json::dump(manifest, policy.integer("/synthesis_rendering/json_indent")) + "\n");
  out.gap = std::move(gap);
  return out;
}

}  // namespace loom::archive
