// Element resolution: a bare id becomes an element with its class, own traits and attributes.
// Data: runtime profile element_resolution (id forms, MIME rules, node kind classes).
#include "loom/action_element.h"

#include <algorithm>
#include <cctype>

#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/runtime_profile.h"
#include "loom/settings_fields.h"

namespace loom::actions {

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
  if (!doc) return Json(nullptr);
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

Result<Json> ElementResolver::resolve_form(const ElementSources& sources, const Form& form, std::string_view raw_id) const {
  std::string id(raw_id);
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

Result<std::vector<llm::Part>> ElementResolver::content_parts(const ElementSources& sources, const Json& resolved) const {
  std::vector<llm::Part> parts;
  const std::string source = json::get_string(resolved, "source");
  const std::string id = json::get_string(resolved, "id");
  auto text_part = [&](std::string text, const std::string& node) {
    llm::Part p;
    p.kind = "text";
    p.text = std::move(text);
    p.node_id = node;
    return p;
  };
  if (source.empty()) return parts;
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
    std::string transcript;
    for (const auto& m : messages) {
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
