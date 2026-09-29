// Anthropic / Claude data export: conversation objects (chat_messages, content
// blocks, attachments, artifacts, branches via parent_message_uuid) and the
// non-conversation members (projects.json, users.json, memories.json).
// Format reference: docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md (3.x).
#include <algorithm>
#include <unordered_map>

#include "export_internal.h"
#include "loom/log.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::xport {

namespace {
constexpr std::string_view kLog = "loom.import.export";

const Json* getp(const Json& o, std::string_view k) { return json::find(o, k); }
std::string gets(const Json& o, std::string_view k) {
  const Json* v = getp(o, k);
  return v && v->is_string() ? v->get<std::string>() : std::string();
}

std::string block_kind(const Json& b) {
  if (!b.is_object()) return "unknown";
  std::string t = gets(b, "type");
  if (t == "text") return "text";
  if (t == "thinking") return "reasoning";
  if (t == "voice_note") return "transcript";
  if (t == "tool_use") return "tool_call";
  if (t == "tool_result") return "tool_result";
  if (t == "image") return "media";
  if (t == "document") return "document";
  return "unknown";
}

// <antArtifact identifier="x" type="y" title="z"> tags (older exports).
Json artifact_tags(const std::string& text) {
  Json out = Json::array();
  std::size_t pos = 0;
  while ((pos = text.find("<antArtifact", pos)) != std::string::npos) {
    std::size_t end = text.find('>', pos);
    if (end == std::string::npos) break;
    std::string tag = text.substr(pos, end - pos);
    auto attr = [&](const char* name) -> Json {
      std::string key = std::string(name) + "=\"";
      auto p = tag.find(key);
      if (p == std::string::npos) return nullptr;
      p += key.size();
      auto q = tag.find('"', p);
      if (q == std::string::npos) return nullptr;
      return tag.substr(p, q - p);
    };
    out.push_back(Json{{"identifier", attr("identifier")}, {"type", attr("type")}, {"title", attr("title")}});
    pos = end;
  }
  return out;
}

}  // namespace

void parse_anthropic_conversation(const Json& conv, int index, const std::string& member, Env& env, ConvModel& out,
                                  Counts& counts) {
  (void)env;
  counts.conversation += 1;
  out.source = "import:anthropic";
  out.leaves_total = json_leaves(conv);

  const Json* cm = getp(conv, "chat_messages");
  const bool has_msgs = cm && cm->is_array() && !cm->empty();
  std::vector<const Json*> raws;
  if (has_msgs) {
    for (const auto& m : *cm) raws.push_back(&m);
  }
  const int N = static_cast<int>(raws.size());

  // keys
  std::vector<std::string> keys(static_cast<std::size_t>(N));
  std::unordered_map<std::string, int> idx;
  for (int i = 0; i < N; ++i) {
    std::string k = raws[static_cast<std::size_t>(i)]->is_object() ? gets(*raws[static_cast<std::size_t>(i)], "uuid") : "";
    if (k.empty() || idx.count(k)) k = (k.empty() ? "#" : k + "#dup") + std::to_string(i);
    keys[static_cast<std::size_t>(i)] = k;
    idx[k] = i;
  }
  // parents / children
  std::vector<int> parent(static_cast<std::size_t>(N), -1);
  std::vector<std::vector<int>> kids(static_cast<std::size_t>(N));
  bool has_parents = false;
  for (int i = 0; i < N; ++i) {
    const Json& m = *raws[static_cast<std::size_t>(i)];
    if (!m.is_object()) continue;
    std::string p = gets(m, "parent_message_uuid");
    auto it = p.empty() ? idx.end() : idx.find(p);
    if (it != idx.end() && it->second != i) {
      parent[static_cast<std::size_t>(i)] = it->second;
      kids[static_cast<std::size_t>(it->second)].push_back(i);
      has_parents = true;
    }
  }
  std::int64_t fork_points = 0, leaves = 0;
  if (has_parents) {
    for (int i = 0; i < N; ++i) {
      if (kids[static_cast<std::size_t>(i)].size() >= 2) ++fork_points;
      if (kids[static_cast<std::size_t>(i)].empty()) ++leaves;
    }
  } else if (N > 0) {
    leaves = 1;
  }
  counts.fork_points += fork_points;
  counts.branch += leaves;

  // order: DFS from roots (file order); cycles appended
  std::vector<int> order, roots;
  std::vector<char> seen(static_cast<std::size_t>(N), 0);
  if (has_parents) {
    for (int i = 0; i < N; ++i) {
      if (parent[static_cast<std::size_t>(i)] < 0) roots.push_back(i);
    }
    for (int r : roots) {
      std::vector<int> st{r};
      while (!st.empty()) {
        int c = st.back();
        st.pop_back();
        if (seen[static_cast<std::size_t>(c)]) continue;
        seen[static_cast<std::size_t>(c)] = 1;
        order.push_back(c);
        const auto& k = kids[static_cast<std::size_t>(c)];
        for (auto it = k.rbegin(); it != k.rend(); ++it) st.push_back(*it);
      }
    }
    for (int i = 0; i < N; ++i) {
      if (!seen[static_cast<std::size_t>(i)]) order.push_back(i);
    }
  } else {
    for (int i = 0; i < N; ++i) {
      order.push_back(i);
      seen[static_cast<std::size_t>(i)] = 1;
    }
  }
  std::vector<char> reachable = seen;

  // paths
  std::vector<char> on_path(static_cast<std::size_t>(N), 0), active(static_cast<std::size_t>(N), 0);
  std::string leaf_key = gets(conv, "current_leaf_message_uuid");
  bool resolved = false, inferred = false;
  auto walk = [&](int start, std::vector<char>& mark) {
    std::set<int> vis;
    int cur = start;
    while (cur >= 0 && !vis.count(cur)) {
      vis.insert(cur);
      mark[static_cast<std::size_t>(cur)] = 1;
      cur = parent[static_cast<std::size_t>(cur)];
    }
  };
  if (!has_parents) {
    std::fill(on_path.begin(), on_path.end(), 1);
    active = on_path;
    resolved = true;
  } else if (!leaf_key.empty() && idx.count(leaf_key)) {
    resolved = true;
    walk(idx[leaf_key], on_path);
    active = on_path;
  } else {
    int best = -1;
    std::string best_t;
    for (int i : order) {
      if (!kids[static_cast<std::size_t>(i)].empty() || !reachable[static_cast<std::size_t>(i)]) continue;
      std::string t = raws[static_cast<std::size_t>(i)]->is_object() ? to_iso(getp(*raws[static_cast<std::size_t>(i)], "created_at") ? *getp(*raws[static_cast<std::size_t>(i)], "created_at") : Json(nullptr)).value_or("") : "";
      if (best < 0 || t >= best_t) {
        best = i;
        best_t = t;
      }
    }
    if (best >= 0) {
      walk(best, active);
      inferred = true;
    }
  }
  std::int64_t path_msgs = 0;
  for (int i = 0; i < N; ++i) path_msgs += on_path[static_cast<std::size_t>(i)] ? 1 : 0;
  counts.current_path_messages += path_msgs;

  // per-message model, in insertion (DFS) order
  std::optional<std::string> conv_model;
  if (std::string cmod = gets(conv, "model"); !cmod.empty()) conv_model = cmod;
  std::optional<std::string> conv_created = to_iso(getp(conv, "created_at") ? *getp(conv, "created_at") : Json(nullptr));
  std::string earliest;
  std::vector<std::string> mtime(static_cast<std::size_t>(N));
  for (int i = 0; i < N; ++i) {
    const Json& m = *raws[static_cast<std::size_t>(i)];
    if (!m.is_object()) continue;
    if (auto t = to_iso(getp(m, "created_at") ? *getp(m, "created_at") : Json(nullptr))) {
      mtime[static_cast<std::size_t>(i)] = *t;
      if (earliest.empty() || *t < earliest) earliest = *t;
    }
  }
  std::string created = conv_created ? *conv_created : (!earliest.empty() ? earliest : timeutil::utc_now_iso());
  std::string updated = created;
  if (const Json* u = getp(conv, "updated_at")) {
    if (auto t = to_iso(*u)) updated = *t;
  }

  std::vector<int> mindex(static_cast<std::size_t>(N), -1);
  for (std::size_t p = 0; p < order.size(); ++p) mindex[static_cast<std::size_t>(order[p])] = static_cast<int>(p);

  // sibling groups
  std::vector<int> group(static_cast<std::size_t>(N), -1), vnum(static_cast<std::size_t>(N), 1),
      sib_i(static_cast<std::size_t>(N), 0), sib_n(static_cast<std::size_t>(N), 1);
  int groups = 0;
  if (has_parents) {
    for (int p = 0; p < N; ++p) {
      const auto& k = kids[static_cast<std::size_t>(p)];
      if (k.empty()) continue;
      int g = k.size() >= 2 ? groups++ : -1;
      for (std::size_t q = 0; q < k.size(); ++q) {
        group[static_cast<std::size_t>(k[q])] = g;
        vnum[static_cast<std::size_t>(k[q])] = static_cast<int>(q) + 1;
        sib_i[static_cast<std::size_t>(k[q])] = static_cast<int>(q);
        sib_n[static_cast<std::size_t>(k[q])] = static_cast<int>(k.size());
      }
    }
  }
  out.groups = groups;

  std::int64_t kept = 0;
  out.msgs.resize(order.size());
  for (std::size_t p = 0; p < order.size(); ++p) {
    int i = order[p];
    const Json& m = *raws[static_cast<std::size_t>(i)];
    MsgModel& mm = out.msgs[p];
    counts.message += 1;
    mm.key = keys[static_cast<std::size_t>(i)];
    std::string sender = m.is_object() ? gets(m, "sender") : "";
    mm.role = sender == "human" ? "user" : (sender.empty() ? "assistant" : sender);
    mm.created = mtime[static_cast<std::size_t>(i)].empty() ? created : mtime[static_cast<std::size_t>(i)];
    if (has_parents) {
      int pi = parent[static_cast<std::size_t>(i)];
      mm.parent = (pi >= 0 && reachable[static_cast<std::size_t>(i)]) ? mindex[static_cast<std::size_t>(pi)] : -1;
    } else {
      mm.parent = p == 0 ? -1 : static_cast<int>(p) - 1;  // linear export: implied chain
    }
    mm.group = group[static_cast<std::size_t>(i)];
    mm.version_num = vnum[static_cast<std::size_t>(i)];
    if (!reachable[static_cast<std::size_t>(i)]) mm.status = "excluded";
    else if (!active[static_cast<std::size_t>(i)]) mm.status = "version";
    else mm.status = "active";
    if (conv_model && mm.role == "assistant") mm.model = conv_model;

    // blocks
    Json bl = Json::array();
    std::string vis;
    bool used_legacy_text = false;
    const Json* content = m.is_object() ? getp(m, "content") : nullptr;
    if (content && content->is_array() && !content->empty()) {
      int bi = 0;
      for (const auto& b : *content) {
        std::string kind = block_kind(b);
        Json o = Json::object();
        o["kind"] = kind;
        o["type"] = b.is_object() ? gets(b, "type") : "";
        o["index"] = bi;
        std::string text;
        if (kind == "text") text = gets(b, "text");
        if (kind == "transcript") text = gets(b, "text");
        if (kind == "unknown" && b.is_object()) {
          std::string t = gets(b, "type");
          if (t.find("thinking") != std::string::npos || t.find("reasoning") != std::string::npos) o["guess"] = "reasoning";
          else if (t.find("image") != std::string::npos) o["guess"] = "media";
          else if (getp(b, "text") && getp(b, "text")->is_string()) o["guess"] = "text";
          if (o.contains("guess")) o["inferred"] = true;
        }
        if (!text.empty()) {
          o["chars"] = static_cast<std::int64_t>(utf8::length(text));
          vis += (vis.empty() ? "" : "\n") + text;
        }
        bl.push_back(o);
        counts.block += 1;
        counts.block_kind[kind] += 1;
        ++bi;
      }
    } else if (m.is_object() && getp(m, "text") && getp(m, "text")->is_string()) {
      used_legacy_text = true;
      Json o = Json::object();
      o["kind"] = "text";
      o["type"] = "text";
      o["index"] = 0;
      o["inferred"] = true;
      o["source"] = "text";
      bl.push_back(o);
      counts.block += 1;
      counts.block_kind["text"] += 1;
    }
    mm.text = vis;
    if (mm.text.empty() && m.is_object()) mm.text = gets(m, "text");
    (void)used_legacy_text;

    // attachments (metadata only: the export carries no file bytes)
    Json atts = Json::array();
    if (m.is_object()) {
      for (const char* src : {"attachments", "files", "files_v2"}) {
        const Json* a = getp(m, src);
        if (!a || !a->is_array()) continue;
        int ai = 0;
        for (const auto& x : *a) {
          Json rec = Json::object();
          rec["source"] = src;
          rec["path"] = std::string("/") + src + "/" + std::to_string(ai++);
          if (x.is_object()) {
            std::string id = gets(x, "id");
            if (id.empty()) id = gets(x, "file_uuid");
            rec["id"] = id;
            rec["name"] = gets(x, "file_name");
            std::string mime = gets(x, "file_type");
            if (mime.empty()) mime = gets(x, "file_kind");
            rec["mime"] = mime;
            if (const Json* sz = getp(x, "file_size")) rec["size"] = *sz;
            rec["has_extracted_content"] = getp(x, "extracted_content") && getp(x, "extracted_content")->is_string();
          }
          rec["resolved"] = false;
          atts.push_back(rec);
          counts.attachment += 1;
        }
      }
    }
    // citations + artifacts
    Json cits = Json::array(), arts = Json::array();
    if (content && content->is_array()) {
      int bi = 0;
      for (const auto& b : *content) {
        if (b.is_object()) {
          std::string t = gets(b, "type");
          if (t == "text") {
            if (const Json* c = getp(b, "citations"); c && c->is_array()) {
              int ci = 0;
              for (const auto& x : *c) {
                Json rec = Json::object();
                rec["kind"] = "citation";
                rec["path"] = "/content/" + std::to_string(bi) + "/citations/" + std::to_string(ci++);
                if (x.is_object()) {
                  if (const Json* d = getp(x, "details"); d && d->is_object() && !gets(*d, "url").empty()) rec["url"] = gets(*d, "url");
                }
                cits.push_back(rec);
                counts.citation += 1;
              }
            }
          }
          if (t == "tool_use") {
            const Json* in = getp(b, "input");
            if (in && in->is_object() && getp(*in, "content") && getp(*in, "content")->is_string() && getp(*in, "command")) {
              Json a = Json::object();
              a["path"] = "/content/" + std::to_string(bi) + "/input";
              a["id"] = gets(*in, "id");
              a["title"] = gets(*in, "title");
              a["type"] = gets(*in, "type");
              a["command"] = gets(*in, "command");
              a["version_uuid"] = gets(*in, "version_uuid");
              arts.push_back(a);
              counts.artifact += 1;
            }
          }
        }
        ++bi;
      }
    }

    Json ex = Json::object();
    ex["provider"] = "anthropic";
    ex["kind"] = "message";
    ex["key"] = mm.key;
    ex["raw"] = m;
    ex["role_raw"] = sender.empty() ? Json(nullptr) : Json(sender);
    ex["blocks"] = bl;
    ex["attachments"] = atts;
    ex["pointers"] = Json::array();
    ex["citations"] = cits;
    ex["citation_groups"] = Json::array();
    ex["artifacts"] = arts;
    ex["artifact_tags"] = artifact_tags(m.is_object() ? gets(m, "text") : "");
    ex["custom_instruction"] = 0;
    ex["memory"] = false;
    ex["hidden"] = false;
    ex["hidden_reasons"] = Json::array();
    ex["parent_key"] = (has_parents && parent[static_cast<std::size_t>(i)] >= 0) ? Json(keys[static_cast<std::size_t>(parent[static_cast<std::size_t>(i)])]) : Json(nullptr);
    ex["parent_inferred"] = !has_parents && p > 0;
    ex["on_current_path"] = static_cast<bool>(on_path[static_cast<std::size_t>(i)]);
    ex["on_active_path"] = static_cast<bool>(active[static_cast<std::size_t>(i)]);
    ex["reachable"] = static_cast<bool>(reachable[static_cast<std::size_t>(i)]);
    ex["leaf"] = has_parents ? kids[static_cast<std::size_t>(i)].empty() : (i == N - 1);
    ex["fork"] = has_parents && kids[static_cast<std::size_t>(i)].size() >= 2;
    ex["sibling_index"] = sib_i[static_cast<std::size_t>(i)];
    ex["sibling_count"] = sib_n[static_cast<std::size_t>(i)];
    ex["created_inferred"] = mtime[static_cast<std::size_t>(i)].empty();
    ex["model_inferred"] = mm.model.has_value();
    mm.export_meta = ex;
    kept += json_leaves(m);
  }

  Json fields = Json::object();
  for (auto it = conv.begin(); it != conv.end(); ++it) {
    if (it.key() == "chat_messages" && has_msgs) continue;
    fields[it.key()] = it.value();
  }
  kept += json_leaves(fields);
  out.leaves_kept = kept;

  std::string name = gets(conv, "name");
  std::string title = name, title_source = "export";
  if (utf8::is_blank(title)) {
    title_source = "generated";
    title.clear();
    for (const auto& m : out.msgs) {
      if (m.role == "user" && !utf8::is_blank(m.text)) {
        title = first_line(m.text, 60);
        break;
      }
    }
    if (title.empty()) title = "Untitled conversation";
  }
  out.key = gets(conv, "uuid");
  out.title = title;
  out.created = created;
  out.updated = updated;

  Json g = Json::object();
  g["messages"] = N;
  g["fork_points"] = fork_points;
  g["leaves"] = leaves;
  g["roots"] = static_cast<std::int64_t>(roots.size());
  g["current_path_messages"] = path_msgs;
  g["has_parent_links"] = has_parents;
  g["version_groups"] = groups;

  Json ex = Json::object();
  ex["provider"] = "anthropic";
  ex["schema"] = std::string(kSchema);
  ex["member"] = member;
  ex["index"] = index;
  ex["key"] = out.key;
  ex["title_original"] = getp(conv, "name") ? *getp(conv, "name") : Json(nullptr);
  ex["title_source"] = title_source;
  ex["current_node"] = leaf_key.empty() ? Json(nullptr) : Json(leaf_key);
  ex["current_node_resolved"] = resolved;
  ex["current_path_inferred"] = inferred;
  ex["has_mapping"] = has_msgs;
  ex["fields"] = fields;
  ex["null_nodes"] = Json::array();
  ex["graph"] = g;
  out.export_meta = ex;
}

// ── root members ────────────────────────────────────────────────────
namespace {
std::string base_of(const std::string& rel) {
  auto p = rel.find_last_of('/');
  return p == std::string::npos ? rel : rel.substr(p + 1);
}
}  // namespace

bool import_anthropic_member(Env& env, AnthropicCtx& cx, const std::string& rel, const fs::path& abs, Report& rep) {
  const std::string base = base_of(rel);
  if (base != "projects.json" && base != "users.json" && base != "memories.json") return false;

  LoadStats st;
  auto doc = load_json_doc(abs, st);
  if (!doc) {
    rep.errors.push_back(Json{{"member", rel}, {"code", st.empty ? "empty" : "invalid_json"}, {"message", st.message}});
    rep.partial = true;
    return true;
  }
  if (Json r = stats_to_json(st); !r.empty()) rep.repairs[rel] = r;

  auto ent = [&](const char* kind, const std::string& label, const std::string& content, const Json& rec) -> std::string {
    Json md = Json::object();
    md["export"] = Json{{"provider", "anthropic"}, {"member", rel}, {"record", rec}};
    auto id = write_entity(env, kind, label, content, md);
    return id ? *id : std::string();
  };
  auto link = [&](const std::string& a, const std::string& b, const char* t) {
    if (a.empty() || b.empty()) return;
    if (auto r = env.db.create_link(a, b, t, 1.0, Json::object()); !r) log::warn(kLog, "link failed: {}", r.error().message);
  };
  auto items_of = [&](const char* wrapper) -> std::vector<const Json*> {
    std::vector<const Json*> v;
    const Json* arr = nullptr;
    if (doc->is_array()) arr = &*doc;
    else if (doc->is_object()) {
      if (const Json* w = getp(*doc, wrapper); w && w->is_array()) arr = w;
    }
    if (arr) {
      for (const auto& x : *arr) v.push_back(&x);
    } else if (doc->is_object() && !doc->empty()) {
      v.push_back(&*doc);
    }
    return v;
  };

  if (base == "users.json") {
    for (const Json* u : items_of("users")) {
      std::string label = u->is_object() ? gets(*u, "email_address") : "";
      if (label.empty() && u->is_object()) label = gets(*u, "full_name");
      if (label.empty() && u->is_object()) label = gets(*u, "uuid");
      ent("export:account", label.empty() ? "account" : label, "", *u);
      rep.counts.account += 1;
    }
  } else if (base == "projects.json") {
    for (const Json* p : items_of("projects")) {
      if (!p->is_object()) continue;
      Json rest = Json::object();
      const Json* docs = getp(*p, "docs");
      for (auto it = p->begin(); it != p->end(); ++it) {
        if (it.key() != "docs") rest[it.key()] = it.value();
      }
      if (docs && !docs->is_array()) rest["docs"] = *docs;
      std::string pid = ent("export:project", gets(*p, "name").empty() ? gets(*p, "uuid") : gets(*p, "name"), gets(*p, "prompt_template"), rest);
      rep.counts.project += 1;
      cx.project_db_id[gets(*p, "uuid")] = pid;
      if (docs && docs->is_array()) {
        for (const auto& d : *docs) {
          std::string label = d.is_object() ? gets(d, "filename") : "doc";
          std::string did = ent("export:project_doc", label, d.is_object() ? gets(d, "content") : "", d);
          rep.counts.project_doc += 1;
          link(did, pid, "part_of");
        }
      }
    }
  } else {  // memories.json
    if (doc->is_object()) {
      Json rest = Json::object();
      bool matched = false;
      for (auto it = doc->begin(); it != doc->end(); ++it) {
        if (it.key() != "conversations_memory" && it.key() != "project_memories") rest[it.key()] = it.value();
      }
      if (const Json* cmem = getp(*doc, "conversations_memory"); cmem && cmem->is_string()) {
        matched = true;
        Json rec = Json{{"scope", "conversations"}, {"container_fields", rest}};
        ent("export:memory", "conversations memory", cmem->get<std::string>(), rec);
        rep.counts.memory += 1;
      }
      if (const Json* pm = getp(*doc, "project_memories"); pm && pm->is_object()) {
        matched = true;
        for (auto it = pm->begin(); it != pm->end(); ++it) {
          Json rec = Json{{"scope", "project"}, {"project_uuid", it.key()}, {"container_fields", rest}};
          std::string mid = ent("export:memory", "project memory " + it.key(),
                                it.value().is_string() ? it.value().get<std::string>() : "", rec);
          rep.counts.memory += 1;
          if (auto pit = cx.project_db_id.find(it.key()); pit != cx.project_db_id.end()) link(mid, pit->second, "part_of");
        }
      }
      if (!matched && !doc->empty()) {
        ent("export:memory", "memory (unrecognised shape)", "", *doc);
        rep.counts.memory += 1;
        rep.warnings.push_back(rel + ": memories shape not recognised; kept verbatim as one record");
      }
    }
  }
  return true;
}

}  // namespace loom::xport
