// Heuristic extraction for exports no provider rule recognises: find
// conversation-like objects (an array of message-like objects next to
// title/time fields) and import them, flagged `inferred`, with the verbatim
// source objects kept. Nothing here claims to be a format definition.
#include <algorithm>

#include "export_internal.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::xport {

namespace {

const Json* getp(const Json& o, std::string_view k) { return json::find(o, k); }

const Json* first_string(const Json& o, std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    const Json* v = getp(o, k);
    if (v && v->is_string() && !v->get_ref<const std::string&>().empty()) return v;
  }
  return nullptr;
}

bool message_like(const Json& m) {
  if (!m.is_object()) return false;
  return first_string(m, {"role", "author", "sender", "from", "speaker"}) && first_string(m, {"text", "body", "content", "message", "msg"});
}

const Json* message_array(const Json& o) {
  if (!o.is_object()) return nullptr;
  for (auto it = o.begin(); it != o.end(); ++it) {
    const Json& v = it.value();
    if (!v.is_array() || v.empty()) continue;
    std::size_t ok = 0;
    for (const auto& m : v) ok += message_like(m) ? 1 : 0;
    if (ok * 2 >= v.size()) return &v;  // at least half look like messages
  }
  return nullptr;
}

std::string message_array_key(const Json& o, const Json* arr) {
  for (auto it = o.begin(); it != o.end(); ++it) {
    if (&it.value() == arr) return it.key();
  }
  return "";
}

void find_convs(const Json& j, int depth, std::vector<const Json*>& acc) {
  if (depth > 4) return;
  if (j.is_object()) {
    if (message_array(j)) {
      acc.push_back(&j);
      return;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (it.value().is_array() || it.value().is_object()) find_convs(it.value(), depth + 1, acc);
    }
  } else if (j.is_array()) {
    for (const auto& x : j) {
      if (x.is_object() || x.is_array()) find_convs(x, depth + 1, acc);
    }
  }
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

bool generic_structure(const Json& doc) {
  std::vector<const Json*> convs;
  find_convs(doc, 0, convs);
  return !convs.empty();
}

void infer_generic(Env& env, const Json& doc, const std::string& member, const std::set<std::string>& file_members,
                   InferOutcome& out, Report& rep) {
  std::vector<const Json*> convs;
  find_convs(doc, 0, convs);
  int index = 0;
  for (const Json* cj : convs) {
    const Json& c = *cj;
    const Json* arr = message_array(c);
    std::string akey = message_array_key(c, arr);

    ConvModel cm;
    Counts counts;
    counts.conversation += 1;
    cm.source = "import:unknown";
    cm.leaves_total = json_leaves(c);
    std::string title;
    if (const Json* t = first_string(c, {"name", "title", "subject", "topic"})) title = t->get<std::string>();
    std::string created = timeutil::utc_now_iso();
    bool created_known = false;
    for (const char* k : {"started", "created", "created_at", "start_time", "date", "timestamp"}) {
      if (const Json* v = getp(c, k)) {
        if (auto t = to_iso(*v)) {
          created = *t;
          created_known = true;
          break;
        }
      }
    }
    std::int64_t kept = 0;
    cm.msgs.resize(arr->size());
    std::size_t i = 0;
    for (const auto& m : *arr) {
      MsgModel& mm = cm.msgs[i];
      counts.message += 1;
      mm.key = "#" + std::to_string(i);
      mm.parent = i == 0 ? -1 : static_cast<int>(i) - 1;
      std::string role = message_like(m) ? lower(first_string(m, {"role", "author", "sender", "from", "speaker"})->get<std::string>()) : "assistant";
      if (role == "user" || role == "human" || role == "me" || role == "you" || role == "customer" || role == "client") mm.role = "user";
      else if (role == "system") mm.role = "system";
      else mm.role = "assistant";
      if (message_like(m)) {
        const Json* t = first_string(m, {"text", "body", "content", "message", "msg"});
        mm.text = t->get<std::string>();
      }
      mm.created = created;
      for (const char* k : {"ts", "time", "timestamp", "created", "created_at", "date", "at"}) {
        if (const Json* v = m.is_object() ? getp(m, k) : nullptr) {
          if (auto t = to_iso(*v)) {
            mm.created = *t;
            break;
          }
        }
      }
      if (!created_known && i == 0 && mm.created != created) {
        created = mm.created;
      }
      Json bl = Json::array();
      bl.push_back(Json{{"kind", "text"}, {"type", "text"}, {"inferred", true}});
      counts.block += 1;
      counts.block_kind["text"] += 1;

      Json atts = Json::array(), col = Json::array();
      if (m.is_object()) {
        for (const char* src : {"attachments", "files"}) {
          const Json* a = getp(m, src);
          if (!a || !a->is_array()) continue;
          int ai = 0;
          for (const auto& x : *a) {
            Json rec = Json::object();
            rec["source"] = src;
            rec["path"] = std::string("/") + src + "/" + std::to_string(ai++);
            std::string name;
            if (x.is_string()) name = x.get<std::string>();
            else if (x.is_object()) {
              if (const Json* n = first_string(x, {"file", "name", "filename", "file_name", "path"})) name = n->get<std::string>();
            }
            rec["name"] = name;
            rec["resolved"] = false;
            for (const auto& fm : file_members) {
              if (!name.empty() && (fm == name || (fm.size() > name.size() && fm.compare(fm.size() - name.size(), name.size(), name) == 0 &&
                                                   fm[fm.size() - name.size() - 1] == '/'))) {
                rec["resolved"] = true;
                rec["member"] = fm;
                rec["method"] = "name";
                col.push_back(fm);
                break;
              }
            }
            atts.push_back(rec);
            counts.attachment += 1;
          }
        }
      }
      mm.attachments = col;
      Json ex = Json::object();
      ex["provider"] = "unknown";
      ex["kind"] = "message";
      ex["key"] = mm.key;
      ex["raw"] = m;
      ex["blocks"] = bl;
      ex["attachments"] = atts;
      ex["pointers"] = Json::array();
      ex["citations"] = Json::array();
      ex["citation_groups"] = Json::array();
      ex["custom_instruction"] = 0;
      ex["memory"] = false;
      ex["hidden"] = false;
      ex["hidden_reasons"] = Json::array();
      ex["on_current_path"] = true;
      ex["on_active_path"] = true;
      ex["reachable"] = true;
      ex["leaf"] = (i + 1 == arr->size());
      ex["fork"] = false;
      ex["inferred"] = true;
      mm.export_meta = ex;
      kept += json_leaves(m);
      ++i;
    }
    counts.branch += 1;
    counts.current_path_messages += static_cast<std::int64_t>(arr->size());

    Json fields = Json::object();
    for (auto it = c.begin(); it != c.end(); ++it) {
      if (it.key() != akey) fields[it.key()] = it.value();
    }
    kept += json_leaves(fields);
    cm.leaves_kept = kept;
    if (utf8::is_blank(title)) {
      for (const auto& m : cm.msgs) {
        if (m.role == "user" && !utf8::is_blank(m.text)) {
          title = first_line(m.text, 60);
          break;
        }
      }
      if (title.empty()) title = "Untitled conversation";
    }
    cm.title = title;
    cm.created = created;
    cm.updated = created;
    cm.key = first_string(c, {"id", "uuid", "key"}) ? first_string(c, {"id", "uuid", "key"})->get<std::string>() : "";
    for (auto& m : cm.msgs) {
      if (m.created.empty()) m.created = created;
    }
    Json ex = Json::object();
    ex["provider"] = "unknown";
    ex["schema"] = std::string(kSchema);
    ex["member"] = member;
    ex["index"] = index;
    ex["key"] = cm.key;
    ex["inferred"] = true;
    ex["message_array"] = akey;
    ex["title_source"] = "export";
    ex["fields"] = fields;
    ex["null_nodes"] = Json::array();
    ex["graph"] = Json{{"messages", static_cast<std::int64_t>(arr->size())}, {"fork_points", 0}, {"leaves", 1}};
    cm.export_meta = ex;

    auto w = write_conversation(env, cm);
    if (!w) {
      rep.errors.push_back(Json{{"member", member}, {"index", index}, {"code", "write_failed"}, {"message", w.error().message}});
      rep.partial = true;
      ++index;
      continue;
    }
    out.counts.add(counts);
    rep.json_leaves += cm.leaves_total;
    rep.leaves_preserved += cm.leaves_kept;
    out.conversations.push_back(*w);
    if (env.on_conv) env.on_conv(*w, member, index);
    ++index;
  }
}

}  // namespace loom::xport
