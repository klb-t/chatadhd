// Import as graph nodes (P1): see import_graph.h.
#include "import/import_graph.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/sqlite.h"
#include "loom/util/sha256.h"

namespace loom::import_graph {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kLog = "loom.import.graph";

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

bool truthy(const Json& v) {
  if (v.is_boolean()) return v.get<bool>();
  if (v.is_number_integer()) return v.get<std::int64_t>() != 0;
  if (v.is_number()) return v.get<double>() != 0.0;
  if (v.is_string()) return !v.get<std::string>().empty();
  return !v.is_null();
}

// Words of one language from a {lang: text} object (fallback: en, then any).
std::string pick(const Json& v, const std::string& lang) {
  if (v.is_string()) return v.get<std::string>();
  if (!v.is_object()) return std::string();
  if (const Json* x = json::find(v, lang); x && x->is_string()) return x->get<std::string>();
  if (const Json* x = json::find(v, "en"); x && x->is_string()) return x->get<std::string>();
  for (const auto& it : v.items())
    if (it.value().is_string()) return it.value().get<std::string>();
  return std::string();
}

// Union of two string arrays (order kept, first seen first).
Json union_strings(const Json& a, const Json& b) {
  Json out = Json::array();
  std::set<std::string> seen;
  for (const Json* arr : {&a, &b}) {
    if (!arr->is_array()) continue;
    for (const auto& x : *arr)
      if (x.is_string() && seen.insert(x.get<std::string>()).second) out.push_back(x);
  }
  return out;
}

struct Writer {
  explicit Writer(Database& d) : db(d) {}
  Database& db;
  std::string prefix;  // kind prefix
  std::string mkey;    // metadata key
  std::string blob;
  Json by_class = Json::object();
  std::int64_t created = 0, reused = 0;
  std::set<std::string> touched;

  static std::string id_for(const std::string& key) { return "n_" + Sha256::hex(key).substr(0, 12); }

  void count(const std::string& id, const std::string& cls) {
    if (!touched.insert(id).second) return;
    by_class[cls] = json::get_int(by_class, cls, 0) + 1;
  }

  // Writes the node of `key` or finds it again. `block` is the import description (class, facets, nav_parent,
  // attrs ...); arrays attrs.conv_ids / attrs.source_ids are united with what the node already had.
  Result<std::string> node(const std::string& cls, const std::string& key, const std::string& label, Json block,
                           const std::string& content = std::string()) {
    const std::string id = id_for(key);
    block["class"] = cls;
    block["key"] = key;
    block["blob_hash"] = blob;
    if (!block.contains("facets")) block["facets"] = Json::array();
    std::optional<std::string> prior;
    {
      auto lock = db.lock();
      LOOM_TRY_ASSIGN(auto got, db.conn().query_text("SELECT metadata FROM nodes WHERE id=?", id));
      prior = std::move(got);
    }
    if (prior) {
      Json meta = json::parse_or(*prior, Json::object());
      if (!meta.is_object()) meta = Json::object();
      const Json old = meta.value(mkey, Json::object());
      if (old.is_object() && block.contains("attrs") && block["attrs"].is_object()) {
        const Json old_attrs = old.value("attrs", Json::object());
        for (const char* k : {"conv_ids", "source_ids"}) {
          if (old_attrs.is_object() && old_attrs.contains(k)) {
            const Json mine = block["attrs"].value(k, Json::array());
            block["attrs"][k] = union_strings(old_attrs[k], mine);
          }
        }
      }
      if (old != block) {
        meta[mkey] = block;
        NodePatch patch;
        patch.metadata = meta;
        LOOM_TRY(db.update_node(id, patch));
      }
      ++reused;
    } else {
      NodeOptions no;
      no.content = content;
      no.metadata = Json{{mkey, block}};
      no.node_id = id;
      LOOM_TRY(db.create_node(label, prefix + cls, no));
      ++created;
    }
    count(id, cls);
    return id;
  }

  Status link(const std::string& src, const std::string& dst, const std::string& type) {
    if (src.empty() || dst.empty() || type.empty() || src == dst) return {};
    LOOM_TRY(db.create_link(src, dst, type, 1.0, Json{{"by", "import_graph"}}));
    return {};
  }
};

}  // namespace

Result<Json> project(Database& db, const fs::path& path, const ImportResult& result, const std::string& display_name) {
  auto profile = RuntimeProfile::load("import_graph", db.path().parent_path());
  if (!profile) {
    log::warn(kLog, "import_graph profile not readable from the data directory ({}); the builtin is used",
              profile.error().message);
    profile = RuntimeProfile::builtin("import_graph");
  }
  if (!profile) return profile.error();
  const Json preset = profile->values();
  return project_with(db, path, result, preset, display_name);
}

Result<Json> project_with(Database& db, const fs::path& path, const ImportResult& result, const Json& preset,
                          const std::string& display_name) {
  if (!json::get_bool(preset, "enabled", true)) return Json(nullptr);
  const std::string lang = json::get_string(preset, "language", "pl");
  const Json links = preset.value("links", Json::object());
  const std::string contains = json::get_string(links, "contains", "part_of");
  const std::string read_from = json::get_string(links, "read_from", "derived_from");
  const Json words = preset.value("words", Json::object());

  Writer w(db);
  w.prefix = json::get_string(preset, "kind_prefix", "import_graph:");
  w.mkey = json::get_string(preset, "metadata_key", "import_graph");
  w.blob = result.blob_hash;
  if (w.blob.empty()) {
    auto hashed = sha256_file_hex(path);
    w.blob = hashed ? *hashed : "path:" + path.string();
  }
  const std::string file_name = !display_name.empty() ? fs::path(display_name).filename().string() : path.filename().string();
  const Json& report = result.export_report;
  const bool is_export = report.is_object();
  const std::string provider = is_export ? json::get_string(report, "provider", "unknown") : std::string();
  const Json report_counts = is_export ? report.value("counts", Json::object()) : Json::object();

  // ── file node ──
  const Json by_format = preset.value("container_class_by_format", Json::object());
  std::string file_class = json::get_string(by_format, result.format);
  if (file_class.empty()) file_class = json::get_string(preset, "default_container_class", "file");
  std::int64_t file_size = -1;
  {
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (!ec) file_size = static_cast<std::int64_t>(sz);
  }
  Json source_ids = Json::array();
  if (!result.source_id.empty()) source_ids.push_back(result.source_id);

  // The root shown under the area: the export (when the provider-export path ran), else the file itself.
  std::string export_id;
  const std::string what = is_export ? pick(words.value("provider", Json::object()).value(provider, Json(provider)), lang)
                                     : pick(words.value("plain_file", Json("File")), lang);
  if (is_export) {
    Json facets = Json::array();
    const Json by_provider = preset.value("export_facets_by_provider", Json::object());
    if (const Json* f = json::find(by_provider, provider); f && f->is_array()) facets = *f;
    std::string label = what + ": " + file_name;
    if (auto rendered = render_profile_template(pick(words.value("export_label", Json()), lang),
                                                Json{{"what", what}, {"file", file_name}});
        rendered && !rendered->empty())
      label = *rendered;
    Json attrs{{"provider", provider}, {"file", file_name}, {"format", result.format}, {"source_ids", source_ids},
               {"counts", report_counts}, {"member_count", json::get_int(report, "member_count", 0)},
               {"partial", json::get_bool(report, "partial", false)}};
    LOOM_TRY_ASSIGN(export_id, w.node("export", w.blob + "/export", label,
                                      Json{{"facets", facets}, {"nav_parent", ""}, {"attrs", attrs}}));
  }
  Json file_attrs{{"name", file_name}, {"format", result.format}, {"sha256", w.blob}, {"source_ids", source_ids},
                  {"path", path.string()}};
  if (file_size >= 0) file_attrs["size"] = file_size;
  LOOM_TRY_ASSIGN(auto file_id, w.node(file_class, w.blob + "/file", file_name,
                                       Json{{"facets", Json::array()}, {"nav_parent", export_id}, {"attrs", file_attrs}}));
  if (!export_id.empty()) LOOM_TRY(w.link(export_id, file_id, read_from));
  const std::string root_id = export_id.empty() ? file_id : export_id;

  // ── archive entries ──
  std::map<std::string, std::string> member_node;  // member name -> node id
  std::int64_t members = 0;
  Json skipped = Json::array();  // {name, disposition, reason}
  std::set<std::string> read_ok;
  const Json read_dispositions = preset.value("read_dispositions", Json::array());
  for (const auto& d : read_dispositions)
    if (d.is_string()) read_ok.insert(d.get<std::string>());
  const Json by_ext = preset.value("member_class_by_extension", Json::object());
  bool archive = false;
  const Json archive_formats = preset.value("archive_formats", Json::array());
  for (const auto& f : archive_formats)
    if (f.is_string() && f.get<std::string>() == result.format) archive = true;
  if (is_export && archive) {
    const Json member_list = report.value("members", Json::array());
    for (const auto& m : member_list) {
      if (!m.is_object()) continue;
      const std::string name = json::get_string(m, "name");
      const std::int64_t index = json::get_int(m, "archive_index", members);
      Json facets = Json::array();
      if (const std::string ext_class = json::get_string(by_ext, extension_of(name)); !ext_class.empty())
        facets.push_back(ext_class);
      Json attrs = Json::object();
      for (const char* k : {"name", "size", "disposition", "reason", "blob_hash", "source_id", "archive_index", "wrapper"})
        if (m.contains(k)) attrs[k] = m[k];
      LOOM_TRY_ASSIGN(auto mid, w.node("archive_member", w.blob + "/member/" + std::to_string(index) + "/" + name,
                                       name.empty() ? "#" + std::to_string(index) : name,
                                       Json{{"facets", facets}, {"nav_parent", file_id}, {"attrs", attrs}}));
      LOOM_TRY(w.link(mid, file_id, contains));
      if (!name.empty() && !member_node.count(name)) member_node[name] = mid;
      ++members;
      const std::string disposition = json::get_string(m, "disposition");
      if (!disposition.empty() && !read_ok.count(disposition))
        skipped.push_back(Json{{"name", name}, {"disposition", disposition}});
    }
  }

  // ── conversations and their attachments ──
  std::int64_t attachments = 0;
  std::int64_t position = 0;
  for (const auto& listed : result.conversations) {
    const std::int64_t at = position++;
    LOOM_TRY_ASSIGN(auto conv, db.get_conv(listed.id));
    if (!conv) continue;  // removed since; nothing to describe
    const Json cmeta = conv->metadata.is_object() ? conv->metadata : Json::object();
    const Json xmeta = cmeta.value("export", Json::object());
    const std::string member = json::get_string(xmeta, "member");
    const std::string ckey = json::get_string(xmeta, "key");
    std::string where = member.empty() ? "#" + std::to_string(at)
                                       : member + "#" + std::to_string(json::get_int(xmeta, "index", at));
    if (!ckey.empty()) where += "/" + ckey;
    std::int64_t message_count = 0;
    {
      auto lock = db.lock();
      LOOM_TRY_ASSIGN(auto n, db.conn().query_int("SELECT COUNT(*) FROM messages WHERE conv_id=?", conv->id));
      message_count = n.value_or(0);
    }
    Json attrs{{"conv_ids", Json::array({conv->id})}, {"title", conv->title}, {"source", conv->source},
               {"created", conv->created}, {"updated", conv->updated}, {"messages", message_count}};
    if (!member.empty()) attrs["member"] = member;
    if (!ckey.empty()) attrs["key"] = ckey;
    if (xmeta.contains("title_original")) attrs["title_original"] = xmeta["title_original"];
    LOOM_TRY_ASSIGN(auto cid, w.node("conversation", w.blob + "/conversation/" + where,
                                     conv->title.empty() ? conv->id : conv->title,
                                     Json{{"facets", Json::array()}, {"nav_parent", root_id}, {"attrs", attrs}}));
    LOOM_TRY(w.link(cid, root_id, contains));
    if (auto it = member_node.find(member); it != member_node.end()) LOOM_TRY(w.link(cid, it->second, read_from));

    // Attachment records of every message of the conversation (active, versions and excluded alike).
    struct Row { std::string id, metadata; };
    std::vector<Row> rows;
    {
      auto lock = db.lock();
      LOOM_TRY_ASSIGN(auto st, db.conn().prepare("SELECT id, metadata FROM messages WHERE conv_id=? ORDER BY rowid"));
      st.bind(1, conv->id);
      while (true) {
        LOOM_TRY_ASSIGN(bool more, st.step());
        if (!more) break;
        rows.push_back(Row{st.get_text(0), st.get_text(1)});
      }
    }
    for (const auto& row : rows) {
      const Json mmeta = json::parse_or(row.metadata, Json::object());
      if (!mmeta.is_object()) continue;
      const Json mx = mmeta.value("export", Json::object());
      if (!mx.is_object()) continue;
      const Json records = mx.value("attachments", Json::array());
      if (!records.is_array()) continue;
      const std::string mkey_msg = json::get_string(mx, "key", row.id);
      std::int64_t k = 0;
      for (const auto& a : records) {
        const std::int64_t ai = k++;
        if (!a.is_object()) continue;
        const std::string apath = json::get_string(a, "path", "#" + std::to_string(ai));
        std::string label = json::get_string(a, "name");
        if (label.empty()) label = json::get_string(a, "id", apath);
        Json aattrs = Json::object();
        for (const char* f : {"id", "name", "mime", "size", "source", "path", "resolved", "member", "reason",
                              "has_extracted_content"})
          if (a.contains(f) && !a[f].is_object() && !a[f].is_array()) aattrs[f] = a[f];
        aattrs["message_id"] = row.id;
        LOOM_TRY_ASSIGN(auto aid, w.node("attachment", w.blob + "/attachment/" + where + "/" + mkey_msg + apath, label,
                                         Json{{"facets", Json::array()}, {"nav_parent", cid}, {"attrs", aattrs}}));
        LOOM_TRY(w.link(aid, cid, contains));
        std::string amember = json::get_string(a, "member");
        if (amember.empty()) {
          if (const Json* loc = json::find(a, "locator"); loc && loc->is_object()) amember = json::get_string(*loc, "member");
        }
        if (auto it = member_node.find(amember); !amember.empty() && it != member_node.end())
          LOOM_TRY(w.link(aid, it->second, read_from));
        ++attachments;
      }
    }
  }

  // ── entities the export described (account, projects, memories ...): linked to the export and shown under it ──
  Json entities = Json::object();  // class -> n
  std::int64_t entity_total = 0;
  if (is_export && !result.source_id.empty()) {
    const Json by_kind = preset.value("entity_class_by_kind", Json::object());
    struct Ent { std::string id, kind, metadata; };
    std::vector<Ent> ents;
    {
      auto lock = db.lock();
      LOOM_TRY_ASSIGN(auto st, db.conn().prepare(
          "SELECT id, kind, metadata FROM nodes WHERE kind LIKE 'export:%' AND json_valid(metadata) "
          "AND json_extract(metadata,'$.export.source_id')=? ORDER BY rowid"));
      st.bind(1, result.source_id);
      while (true) {
        LOOM_TRY_ASSIGN(bool more, st.step());
        if (!more) break;
        ents.push_back(Ent{st.get_text(0), st.get_text(1), st.get_text(2)});
      }
    }
    for (const auto& e : ents) {
      const std::string cls = json::get_string(by_kind, e.kind);
      if (cls.empty()) continue;  // kinds without a class (export:member: the entries above describe them)
      Json meta = json::parse_or(e.metadata, Json::object());
      if (!meta.is_object()) continue;
      const Json block{{"class", cls}, {"facets", Json::array()}, {"key", w.blob + "/entity/" + e.id},
                       {"blob_hash", w.blob}, {"nav_parent", export_id}, {"attrs", Json{{"kind", e.kind}}}};
      if (meta.value(w.mkey, Json()) != block) {
        meta[w.mkey] = block;
        NodePatch patch;
        patch.metadata = meta;
        LOOM_TRY(db.update_node(e.id, patch));
      }
      LOOM_TRY(w.link(e.id, export_id, contains));
      entities[cls] = json::get_int(entities, cls, 0) + 1;
      ++entity_total;
    }
  }

  // ── summary sentence (templates of the preset; scalars only, never raw JSON) ──
  const Json summary = preset.value("summary", Json::object());
  const Json parts = summary.value("parts", Json::array());
  std::set<std::string> langs;
  for (const auto& p : parts)
    if (const Json* t = json::find(p, "text"); t && t->is_object())
      for (const auto& it : t->items()) langs.insert(it.key());
  if (langs.empty()) langs.insert(lang);
  const std::string sep = json::get_string(summary, "separator", " ");
  const std::string list_sep = json::get_string(summary, "list_separator", ", ");
  Json texts = Json::object();
  for (const auto& L : langs) {
    std::string entities_text;
    for (const auto& it : entities.items()) {
      const std::string label = pick(words.value("class", Json::object()).value(it.key(), Json(it.key())), L);
      auto item = render_profile_template(pick(summary.value("entity_item", Json("{{label}}: {{count}}")), L),
                                          Json{{"label", label}, {"count", it.value()}});
      if (!item) return item.error();
      entities_text += (entities_text.empty() ? "" : list_sep) + *item;
    }
    std::string skipped_text;
    for (const auto& s : skipped) {
      const std::string disposition = json::get_string(s, "disposition");
      std::string why = pick(words.value("disposition", Json::object()).value(disposition, Json()), L);
      if (why.empty()) why = disposition;
      auto item = render_profile_template(pick(summary.value("skipped_item", Json("{{name}} ({{why}})")), L),
                                          Json{{"name", json::get_string(s, "name")}, {"why", why}});
      if (!item) return item.error();
      skipped_text += (skipped_text.empty() ? "" : list_sep) + *item;
    }
    const std::string what_l = is_export ? pick(words.value("provider", Json::object()).value(provider, Json(provider)), L)
                                         : pick(words.value("plain_file", Json("File")), L);
    const Json vars{{"what", what_l},
                    {"file", file_name},
                    {"conversations", json::get_int(w.by_class, "conversation", 0)},
                    {"messages", result.messages},
                    {"branches", json::get_int(report_counts, "branch", 0)},
                    {"attachments", attachments},
                    {"attachments_linked", is_export ? json::get_int(report, "attachment_linked", 0) : 0},
                    {"entities", entity_total},
                    {"entities_text", entities_text},
                    {"members", members},
                    {"skipped", static_cast<std::int64_t>(skipped.size())},
                    {"skipped_text", skipped_text},
                    {"already_imported", result.already_imported},
                    {"nodes_created", w.created},
                    {"nodes_reused", w.reused}};
    std::string text;
    for (const auto& p : parts) {
      if (const std::string when = json::get_string(p, "when"); !when.empty()) {
        const Json* v = json::find(vars, when);
        if (!v || !truthy(*v)) continue;
      }
      const std::string tmpl = pick(p.value("text", Json()), L);
      if (tmpl.empty()) continue;
      auto piece = render_profile_template(tmpl, vars);
      if (!piece) return piece.error();
      text += (text.empty() ? "" : sep) + *piece;
    }
    texts[L] = text;
  }
  const std::string chosen = texts.contains(lang) ? lang : texts.contains("en") ? std::string("en") : *langs.begin();

  Json out{{"nodes", w.by_class},
           {"created", w.created},
           {"reused", w.reused},
           {"file_node", file_id},
           {"export_node", export_id},
           {"root_node", root_id},
           {"entities", entities},
           {"summary", Json{{"lang", chosen}, {"text", texts.value(chosen, std::string())}, {"texts", texts}}}};
  return out;
}

}  // namespace loom::import_graph
