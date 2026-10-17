// Element resolution: a bare id becomes an element with its class, own traits and attributes.
// Data: runtime profile element_resolution (id forms, MIME rules, node kind classes).
#include "loom/action_element.h"

#include <algorithm>
#include <cctype>
#include <iterator>

#include "loom/db.h"
#include "loom/log.h"
#include "loom/provenance.h"
#include "loom/runtime_profile.h"
#include "loom/settings_fields.h"

namespace loom::actions {

namespace {
std::vector<std::string> strings_of_json(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}
}  // namespace

namespace {

bool is_lower_hex(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
}

std::string clip(std::string_view text, std::size_t codepoints) {
  std::string out;
  std::size_t n = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && n++ == codepoints) return out + "...";
    out += text[i];
  }
  return out;
}

Json element_object(const std::string& id, const std::string& cls, const Json& traits, const Json& attrs,
                    const std::string& source, const std::string& label) {
  Json o = Json::object();
  o["id"] = id;
  o["class"] = cls;
  o["traits"] = traits;
  o["attrs"] = attrs;
  o["source"] = source;
  if (!label.empty()) o["label"] = label;
  return o;
}

}  // namespace

Result<ElementResolver> ElementResolver::load(const std::filesystem::path& data_dir) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("element_resolution", data_dir));
  return from_values(profile.values());
}

Result<ElementResolver> ElementResolver::from_values(const Json& values) {
  ElementResolver r;
  r.values_ = values;
  const Json* forms = json::find(values, "id_forms");
  if (!forms || !forms->is_array()) return Error(Errc::InvalidArgument, "element_resolution needs id_forms");
  for (const auto& f : *forms) {
    Form form;
    form.id = json::get_string(f, "id");
    form.prefix = json::get_string(f, "prefix");
    form.source = json::get_string(f, "source");
    form.cls = json::get_string(f, "class");
    form.same_as_prefix = json::get_string(f, "same_as_prefix");
    form.hex_length = static_cast<std::size_t>(std::max<std::int64_t>(0, json::get_int(f, "hex_length", 0)));
    if (form.prefix.empty() && form.hex_length == 0) return Error(Errc::InvalidArgument, "id form '" + form.id + "' needs a prefix or hex_length");
    r.forms_.push_back(std::move(form));
  }
  return r;
}

const ElementResolver::Form* ElementResolver::form_of(std::string_view text) const {
  for (const auto& f : forms_) {
    if (!f.prefix.empty()) {
      if (text.size() > f.prefix.size() && text.substr(0, f.prefix.size()) == f.prefix) return &f;
    } else if (f.hex_length > 0 && text.size() == f.hex_length && is_lower_hex(text)) {
      return &f;
    }
  }
  return nullptr;
}

bool ElementResolver::is_id(std::string_view text) const { return form_of(text) != nullptr; }

Json ElementResolver::mime_element(std::string_view mime) const {
  const Json* rules = json::find(values_, "mime_rules");
  if (rules && rules->is_array()) {
    for (const auto& rule : *rules) {
      const std::string exact = json::get_string(rule, "exact");
      const std::string prefix = json::get_string(rule, "prefix");
      const bool match = !exact.empty() ? mime == exact : mime.substr(0, prefix.size()) == prefix;
      if (match) return rule;
    }
  }
  return Json::object();
}

Result<Json> ElementResolver::resolve_blob(const ElementSources& sources, std::string_view hash) const {
  if (!sources.db) return Error(Errc::Unavailable, "blob " + std::string(hash) + " cannot be resolved without a database");
  auto lock = sources.db->lock();
  LOOM_TRY_ASSIGN(auto st, sources.db->conn().prepare("SELECT size, mime FROM loom_blobs WHERE hash = ?"));
  st.bind(1, hash);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return Error(Errc::NotFound, "no stored blob '" + std::string(hash) + "'");
  const std::int64_t size = st.get_int(0);
  const std::string mime = st.get_text(1);
  const Json rule = mime_element(mime);
  Json traits = Json::array();
  if (const Json* t = json::find(rule, "traits"); t && t->is_array()) traits = *t;
  return element_object(std::string(hash), json::get_string(rule, "class"), traits,
                        Json{{"hash", std::string(hash)}, {"mime", mime}, {"size", size}}, "blob",
                        mime.empty() ? std::string() : mime);
}

Result<Json> ElementResolver::resolve_setting(const ElementSources& sources, std::string_view key) const {
  const Json* setting = json::find(values_, "setting");
  if (!setting) return Json(nullptr);
  auto doc = settings_fields(sources.data_dir);
  if (!doc) {
    // Any bare id is tried as a setting key, so an unreadable settings document must not fail every resolution; the reason is
    // logged (the runner's settings.set/settings.get report it themselves when they read the document).
    log::debug("loom.actions.element", "settings fields unreadable, '{}' not resolved as a setting: {}", key, doc.error().message);
    return Json(nullptr);
  }
  const Json* fields = json::find(*doc, "fields");
  if (!fields || !fields->is_array()) return Json(nullptr);
  for (const auto& f : *fields) {
    if (json::get_string(f, "key") != key) continue;
    Json attrs{{"field_scope", json::get_string(f, "scope")}, {"key", std::string(key)}, {"type", json::get_string(f, "type")}};
    if (json::get_bool(f, "sensitive", false)) attrs["sensitive"] = true;
    return element_object("settings:" + std::string(key), json::get_string(*setting, "class"), Json::array(), attrs,
                          json::get_string(*setting, "source", "setting"), std::string(key));
  }
  return Json(nullptr);
}

// A device by its id (device:<entry or instance>): the entries of graph_entry /devices, with `instances` expanded from the profile they
// name (the storages of storage_security ...). The element is its class (the traits come from classes.pack) with the availability per platform
// as attributes; nothing is touched, descriptions only.
Result<Json> ElementResolver::resolve_device(const ElementSources& sources, const Form& form, std::string_view raw_id) const {
  const std::string key = std::string(raw_id).substr(form.prefix.size());
  LOOM_TRY_ASSIGN(auto entry, RuntimeProfile::load("graph_entry", sources.data_dir));
  const Json* devices = json::find(entry.values(), "devices");
  if (!devices || !devices->is_object()) return Error(Errc::NotFound, "this installation describes no devices");
  auto label_of = [](const Json& def, const std::string& fallback) {
    const Json* l = json::find(def, "label");
    return l && l->is_object() ? json::get_string(*l, "en", fallback) : fallback;
  };
  for (auto it = devices->begin(); it != devices->end(); ++it) {
    const Json& def = it.value();
    if (!def.is_object() || json::get_bool(def, "hidden", false)) continue;
    const std::string cls = json::get_string(def, "class", it.key());
    if (it.key() == key && !json::find(def, "instances")) {
      Json attrs{{"device", it.key()}, {"availability", def.value("availability", Json::object())}};
      return element_object(std::string(raw_id), cls, Json::array(), attrs, form.source, label_of(def, it.key()));
    }
    const Json* inst = json::find(def, "instances");
    if (!inst || !inst->is_object()) continue;
    const Json* from = json::find(*inst, "from");
    if (!from || !from->is_object()) continue;
    auto source = RuntimeProfile::load(json::get_string(*from, "domain"), sources.data_dir);
    if (!source) continue;
    const Json* members = &source->values();
    std::string pointer = json::get_string(*from, "pointer");
    for (std::size_t pos = 0; members && pos < pointer.size();) {  // an RFC 6901 pointer into the profile
      const std::size_t next = pointer.find('/', pos + 1);
      std::string token = pointer.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1);
      members = members->is_object() ? json::find(*members, token) : nullptr;
      pos = next == std::string::npos ? pointer.size() : next;
    }
    const Json* one = members && members->is_object() ? json::find(*members, key) : nullptr;
    if (!one) continue;
    // availability: the instance's own list of systems, mapped to the platforms by the entry's map
    Json availability = Json::object();
    const Json map = inst->value("platform_map", Json::object());
    std::vector<std::string> systems;
    if (const Json* have = json::find(*one, json::get_string(*inst, "availability_key")); have && have->is_array())
      for (const auto& x : *have)
        if (x.is_string()) systems.push_back(x.get<std::string>());
    for (auto p = map.begin(); p != map.end(); ++p) {
      bool any = false;
      if (p.value().is_array())
        for (const auto& os : p.value())
          if (os.is_string() && std::find(systems.begin(), systems.end(), os.get<std::string>()) != systems.end()) any = true;
      availability[p.key()] = any ? "available" : "unavailable";
    }
    Json attrs{{"device", it.key()}, {"instance", key}, {"availability", availability}};
    for (const char* k : {"capabilities", "path"})
      if (const Json* v = json::find(*one, k)) attrs[k] = *v;
    return element_object(std::string(raw_id), json::get_string(*inst, "class", cls), Json::array(), attrs, form.source, label_of(*one, key));
  }
  return Error(Errc::NotFound, "no device '" + key + "'");
}

Result<Json> ElementResolver::resolve_form(const ElementSources& sources, const Form& form, std::string_view raw_id) const {
  std::string id(raw_id);
  if (form.source == "device") return resolve_device(sources, form, raw_id);
  if (form.source == "projection") {
    // An address of the graph projection (runtime/graph_sources.pack). One that is the same node as a database id (conv:<id> = c_<id>,
    // msg:<id> = m_<id>) is read as that id; any other is an element of its class, read from no store (the projection computes it).
    const std::string rest = id.substr(form.prefix.size());
    if (!form.same_as_prefix.empty()) {
      const std::string db_id = form.same_as_prefix + rest;
      const Form* same = form_of(db_id);
      if (!same || same->source == "projection") return Error(Errc::InvalidArgument, "id form '" + form.id + "': no id form reads '" + form.same_as_prefix + "'");
      LOOM_TRY_ASSIGN(auto resolved, resolve_form(sources, *same, db_id));
      resolved["attrs"]["address"] = id;
      return resolved;
    }
    // setting:<key> is the settings field <key> when the settings document describes it (a profile value such as setting:worker/batch_threshold
    // is not a config key and stays a projected element).
    if (const Json* setting = json::find(values_, "setting"); setting && json::get_string(*setting, "class") == form.cls) {
      LOOM_TRY_ASSIGN(auto field, resolve_setting(sources, rest));
      if (!field.is_null()) {
        field["attrs"]["address"] = id;
        return field;
      }
    }
    return element_object(id, form.cls, Json::array(), Json{{"address", id}}, form.source, rest);
  }
  if (form.source == "blob") {
    std::string hash = id;
    if (!form.prefix.empty()) hash = id.substr(form.prefix.size());
    return resolve_blob(sources, hash);
  }
  if (!sources.db) return Error(Errc::Unavailable, "'" + id + "' cannot be resolved without a database");
  Database& db = *sources.db;
  if (form.source == "message") {
    LOOM_TRY_ASSIGN(auto m, db.get_msg(id));
    if (!m) return Error(Errc::NotFound, "no message '" + id + "'");
    Json attrs{{"role", m->role}, {"conv_id", m->conv_id}, {"status", m->status}};
    if (m->model) attrs["model"] = *m->model;
    if (m->version_group_id) attrs["version_group_id"] = *m->version_group_id;
    return element_object(id, form.cls, Json::array(), attrs, form.source, clip(m->text, 60));
  }
  if (form.source == "conversation") {
    LOOM_TRY_ASSIGN(auto c, db.get_conv(id));
    if (!c) return Error(Errc::NotFound, "no conversation '" + id + "'");
    return element_object(id, form.cls, Json::array(), Json{{"source", c->source}, {"title", c->title}}, form.source, c->title);
  }
  if (form.source == "version_group") {
    LOOM_TRY_ASSIGN(auto versions, db.get_versions(id));
    if (versions.empty()) return Error(Errc::NotFound, "no version group '" + id + "'");
    const Message* pick = &versions.back();
    for (const auto& v : versions)
      if (v.status == "active") pick = &v;
    Json attrs{{"role", pick->role}, {"conv_id", pick->conv_id}, {"status", pick->status}, {"version_group_id", id},
               {"version_num", pick->version_num}};
    return element_object(pick->id, form.cls, Json::array(), attrs, form.source, clip(pick->text, 60));
  }
  if (form.source == "node") {
    LOOM_TRY_ASSIGN(auto n, db.get_node(id));
    if (!n) return Error(Errc::NotFound, "no node '" + id + "'");
    constexpr std::string_view blob_label = "blob:";
    if (n->kind == "blob" && n->label.rfind(blob_label, 0) == 0) {  // the node of a stored output: the blob is the element
      auto blob = resolve_blob(sources, n->label.substr(blob_label.size()));
      if (blob) {
        (*blob)["attrs"]["node_id"] = id;
        (*blob)["attrs"]["kind"] = n->kind;
        (*blob)["id"] = id;
        return *blob;
      }
    }
    std::string cls;
    if (const Json* map = json::find(values_, "node_kind_classes")) cls = json::get_string(*map, n->kind);
    return element_object(id, cls, Json::array(), Json{{"kind", n->kind}, {"label", n->label}}, form.source, n->label);
  }
  if (form.source == "link") {
    auto lock = db.lock();
    LOOM_TRY_ASSIGN(auto st, db.conn().prepare("SELECT src, dst, link_type, weight FROM links WHERE id = ?"));
    st.bind(1, id);
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) return Error(Errc::NotFound, "no link '" + id + "'");
    return element_object(id, form.cls, Json::array(),
                          Json{{"src", st.get_text(0)}, {"dst", st.get_text(1)}, {"link_type", st.get_text(2)}, {"weight", st.get_double(3)}},
                          form.source, st.get_text(2));
  }
  return Error(Errc::InvalidArgument, "id form '" + form.id + "' names an unknown source '" + form.source + "'");
}

Result<Json> ElementResolver::resolve(const ElementSources& sources, const Json& element,
                                      const std::function<bool(std::string_view)>& known_class) const {
  auto merge_caller = [&](Json resolved, const Json& caller) {
    // What the caller said in addition (traits, attrs) is kept on top of what the stores say.
    if (const Json* t = json::find(caller, "traits"); t && t->is_array())
      for (const auto& x : *t)
        if (std::find(resolved["traits"].begin(), resolved["traits"].end(), x) == resolved["traits"].end()) resolved["traits"].push_back(x);
    if (const Json* a = json::find(caller, "attrs"); a && a->is_object())
      for (auto it = a->begin(); it != a->end(); ++it) resolved["attrs"][it.key()] = it.value();
    return resolved;
  };
  if (element.is_string()) {
    const std::string text = element.get<std::string>();
    if (known_class && known_class(text)) return element;
    if (const Form* form = form_of(text)) return resolve_form(sources, *form, text);
    LOOM_TRY_ASSIGN(Json field, resolve_setting(sources, text));
    if (!field.is_null()) return field;
    return element;
  }
  if (element.is_object()) {
    const std::string cls = !json::get_string(element, "class").empty() ? json::get_string(element, "class") : json::get_string(element, "kind");
    const std::string id = json::get_string(element, "id");
    if (!cls.empty() || id.empty()) return element;
    if (const Form* form = form_of(id)) {
      LOOM_TRY_ASSIGN(Json resolved, resolve_form(sources, *form, id));
      return merge_caller(std::move(resolved), element);
    }
    LOOM_TRY_ASSIGN(Json field, resolve_setting(sources, id));
    if (!field.is_null()) return merge_caller(std::move(field), element);
  }
  return element;
}

Result<Json> ElementResolver::group(const ElementSources& sources, const Json& members, const std::function<bool(std::string_view)>& known_class,
                                    const std::function<std::set<std::string>(std::string_view)>& traits_of) const {
  if (!members.is_array() || members.empty()) return Error(Errc::InvalidArgument, "a group of elements needs at least one member");
  std::vector<Json> resolved;
  for (const auto& m : members) {
    LOOM_TRY_ASSIGN(Json r, resolve(sources, m, known_class));
    if (r.is_string()) r = Json{{"class", r}};
    if (!r.is_object()) return Error(Errc::InvalidArgument, "a member of a group of elements is not something I can act on");
    resolved.push_back(std::move(r));
  }
  if (resolved.size() == 1) return resolved[0];
  std::string common = json::get_string(resolved[0], "class");
  for (const auto& r : resolved)
    if (json::get_string(r, "class") != common) common.clear();
  std::set<std::string> shared;
  if (common.empty() && traits_of) {
    bool first = true;
    for (const auto& r : resolved) {
      std::set<std::string> own = traits_of(json::get_string(r, "class"));
      for (const auto& t : strings_of_json(json::find(r, "traits"))) own.insert(t);
      if (first) shared = std::move(own);
      else {
        std::set<std::string> both;
        std::set_intersection(shared.begin(), shared.end(), own.begin(), own.end(), std::inserter(both, both.begin()));
        shared = std::move(both);
      }
      first = false;
    }
  }
  Json out = Json::object();
  out["id"] = "";
  out["class"] = common;
  out["traits"] = Json::array();
  for (const auto& t : shared) out["traits"].push_back(t);
  out["source"] = "group";
  out["attrs"] = Json{{"count", resolved.size()}};
  out["members"] = Json::array();
  for (auto& r : resolved) out["members"].push_back(std::move(r));
  return out;
}

Result<std::vector<llm::Part>> ElementResolver::content_parts(const ElementSources& sources, const Json& resolved) const {
  std::vector<llm::Part> parts;
  const std::string source = json::get_string(resolved, "source");
  const std::string id = json::get_string(resolved, "id");
  if (source == "group") {  // the members' contents, one after another
    if (const Json* members = json::find(resolved, "members"); members && members->is_array())
      for (const auto& m : *members) {
        LOOM_TRY_ASSIGN(auto one, content_parts(sources, m));
        for (auto& p : one) parts.push_back(std::move(p));
      }
    return parts;
  }
  auto text_part = [&](std::string text, const std::string& node) {
    llm::Part p;
    p.kind = "text";
    p.text = std::move(text);
    p.node_id = node;
    return p;
  };
  if (source.empty() || source == "device") return parts;  // a device has no content of its own
  if (!sources.db && source != "blob") return Error(Errc::Unavailable, "content of '" + id + "' needs a database");
  if (source == "message" || source == "version_group") {
    LOOM_TRY_ASSIGN(auto m, sources.db->get_msg(id));
    if (!m) return Error(Errc::NotFound, "no message '" + id + "'");
    parts.push_back(text_part(m->text, id));
    return parts;
  }
  if (source == "conversation") {
    LOOM_TRY_ASSIGN(auto messages, sources.db->get_msgs(id));
    const Json* content = json::find(values_, "content");
    const std::string line = content ? json::get_string(*content, "conversation_line", "{{role}}: {{text}}") : "{{role}}: {{text}}";
    const std::string separator = content ? json::get_string(*content, "conversation_separator", "\n\n") : "\n\n";
    const Json* roles = content ? json::find(*content, "conversation_roles") : nullptr;
    std::vector<std::string> skipped;
    if (content)
      if (const Json* keys = json::find(*content, "exclude_metadata_keys"); keys && keys->is_array())
        for (const auto& k : *keys)
          if (k.is_string()) skipped.push_back(k.get<std::string>());
    std::string transcript;
    for (const auto& m : messages) {
      if (std::any_of(skipped.begin(), skipped.end(), [&](const std::string& k) { return m.metadata.is_object() && m.metadata.contains(k); })) continue;
      const std::string role = roles ? json::get_string(*roles, m.role, m.role) : m.role;
      LOOM_TRY_ASSIGN(std::string rendered, render_profile_template(line, Json{{"role", role}, {"text", m.text}}));
      if (!transcript.empty()) transcript += separator;
      transcript += rendered;
    }
    parts.push_back(text_part(std::move(transcript), id));
    return parts;
  }
  if (source == "node") {
    LOOM_TRY_ASSIGN(auto n, sources.db->get_node(id));
    if (!n) return Error(Errc::NotFound, "no node '" + id + "'");
    // A result of an action keeps its bytes (or a long text) in the blob store and the reference in its metadata.
    const std::string hash = json::get_string(n->metadata, "blob_hash");
    if (n->content.empty() && !hash.empty()) {
      const std::string mime = json::get_string(n->metadata, "mime");
      const std::string kind = json::get_string(mime_element(mime), "part", "file_ref");
      if (kind == "text") {
        if (!sources.blobs) return Error(Errc::Unavailable, "the blob store is not available to read '" + hash + "'");
        LOOM_TRY_ASSIGN(std::string bytes, sources.blobs->read(hash));
        parts.push_back(text_part(std::move(bytes), id));
        return parts;
      }
      llm::Part p;
      p.kind = kind;
      p.blob_hash = hash;
      p.mime = mime;
      p.node_id = id;
      parts.push_back(std::move(p));
      return parts;
    }
    parts.push_back(text_part(n->content.empty() ? n->label : n->content, id));
    return parts;
  }
  if (source == "blob") {
    const Json* attrs = json::find(resolved, "attrs");
    const std::string hash = attrs ? json::get_string(*attrs, "hash") : std::string();
    const std::string mime = attrs ? json::get_string(*attrs, "mime") : std::string();
    const Json rule = mime_element(mime);
    const std::string kind = json::get_string(rule, "part", "file_ref");
    const std::string node_id = attrs ? json::get_string(*attrs, "node_id") : std::string();
    if (kind == "text") {
      if (!sources.blobs) return Error(Errc::Unavailable, "the blob store is not available to read '" + hash + "'");
      LOOM_TRY_ASSIGN(std::string bytes, sources.blobs->read(hash));
      parts.push_back(text_part(std::move(bytes), node_id));
      return parts;
    }
    llm::Part p;
    p.kind = kind;
    p.blob_hash = hash;
    p.mime = mime;
    p.node_id = node_id;
    parts.push_back(std::move(p));
    return parts;
  }
  return parts;
}

}  // namespace loom::actions
