// OpenAI / ChatGPT data export: conversation objects (mapping tree, content
// types, metadata) and the non-conversation root members.
// Format reference: docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md (2.x).
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "export_internal.h"
#include "loom/log.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::xport {

namespace {
constexpr std::string_view kLog = "loom.import.export";

struct Blk {
  std::string kind, type, text, guess;
  int part = -1;
};

const Json* getp(const Json& o, std::string_view k) { return json::find(o, k); }
std::string gets(const Json& o, std::string_view k) {
  const Json* v = getp(o, k);
  return v && v->is_string() ? v->get<std::string>() : std::string();
}

const std::map<std::string, std::string>& single_kind() {
  static const std::map<std::string, std::string> m = {
      {"code", "code"},
      {"execution_output", "tool_result"},
      {"tether_quote", "quote"},
      {"tether_browsing_display", "browse"},
      {"tether_browsing_code", "browse"},
      {"sonic_webpage", "browse"},
      {"system_error", "error"},
      {"model_editable_context", "context"},
      {"reasoning_recap", "reasoning"}};
  return m;
}

std::string part_kind(const Json& p, std::string* type) {
  if (p.is_string()) {
    *type = "text";
    return "text";
  }
  if (!p.is_object()) {
    *type = "";
    return "unknown";
  }
  const Json* k = getp(p, "content_type");
  if (!k) k = getp(p, "type");
  std::string key = (k && k->is_string()) ? k->get<std::string>() : std::string();
  *type = key;
  if (key == "image_asset_pointer" || key == "audio_asset_pointer" || key == "real_time_user_audio_video_asset_pointer") return "media";
  if (key == "audio_transcription") return "transcript";
  if (key == "search_result_group") return "search_results";
  if (key == "text") return "text";
  return "unknown";
}

std::string guess_kind(const std::string& type, const Json& o) {
  auto has = [&](const char* s) { return type.find(s) != std::string::npos; };
  if (has("asset_pointer")) return "media";
  if (has("thinking") || has("thought") || has("reasoning")) return "reasoning";
  if (o.is_object()) {
    const Json* t = getp(o, "text");
    if (t && t->is_string()) return "text";
  }
  return "";
}

// Plain text a block carries (fallback rendering for hidden/plumbing messages).
std::string block_text(const Blk& b, const Json& content, const Json* part) {
  if (part) {
    if (part->is_string()) return part->get<std::string>();
    if (part->is_object()) return gets(*part, "text");
    return "";
  }
  const std::string& ct = b.type;
  if (ct == "code" || ct == "execution_output" || ct == "tether_quote" || ct == "system_error" || ct == "sonic_webpage") return gets(content, "text");
  if (ct == "tether_browsing_display") return gets(content, "result");
  if (ct == "model_editable_context") return gets(content, "model_set_context");
  if (ct == "reasoning_recap") return gets(content, "content");
  return "";
}

std::vector<Blk> openai_blocks(const Json& content, std::vector<const Json*>* parts_out) {
  std::vector<Blk> out;
  if (!content.is_object()) return out;
  std::string ct = gets(content, "content_type");
  auto push = [&](std::string kind, std::string type, int part, const Json* pj) {
    Blk b;
    b.kind = std::move(kind);
    b.type = std::move(type);
    b.part = part;
    b.text = block_text(b, content, pj);
    out.push_back(std::move(b));
    if (parts_out) parts_out->push_back(pj);
  };
  if (ct == "text" || ct == "multimodal_text") {
    const Json* parts = getp(content, "parts");
    if (parts && parts->is_array()) {
      int i = 0;
      for (const auto& p : *parts) {
        std::string type;
        std::string kind = part_kind(p, &type);
        push(kind, type, i, &p);
        if (kind == "unknown") out.back().guess = guess_kind(type, p);
        ++i;
      }
    }
  } else if (auto it = single_kind().find(ct); it != single_kind().end()) {
    push(it->second, ct, -1, nullptr);
  } else if (ct == "user_editable_context") {
    for (const char* k : {"user_profile", "user_instructions"}) {
      const Json* v = getp(content, k);
      if (v && v->is_string()) {
        push("context", ct, -1, nullptr);
        out.back().text = v->get<std::string>();
      }
    }
  } else if (ct == "thoughts") {
    const Json* th = getp(content, "thoughts");
    if (th && th->is_array()) {
      for (const auto& t : *th) {
        push("reasoning", ct, -1, nullptr);
        std::string txt = t.is_object() ? gets(t, "content") : std::string();
        if (txt.empty() && t.is_object()) txt = gets(t, "summary");
        out.back().text = txt;
      }
    }
  } else {
    push("unknown", ct, -1, nullptr);
    out.back().guess = guess_kind(ct, content);
    if (content.is_object()) {
      // unknown content type: keep any obvious text for search
      std::string t = gets(content, "text");
      if (!t.empty()) out.back().text = t;
    }
  }
  return out;
}

std::string strip_scheme(const std::string& p) {
  for (std::string_view pre : {"file-service://", "sediment://"}) {
    if (p.size() > pre.size() && p.compare(0, pre.size(), pre) == 0) return p.substr(pre.size());
  }
  return p;
}

void collect_pointers(const Json& j, const std::string& path, std::vector<std::pair<std::string, std::string>>& out) {
  if (j.is_string()) {
    std::string s = j.get<std::string>();
    if (s.rfind("file-service://", 0) == 0 || s.rfind("sediment://", 0) == 0) out.emplace_back(strip_scheme(s), path);
    return;
  }
  if (j.is_array()) {
    int i = 0;
    for (const auto& x : j) collect_pointers(x, path + "/" + std::to_string(i++), out);
    return;
  }
  if (!j.is_object()) return;
  if (const Json* ap = getp(j, "asset_pointer"); ap && ap->is_string()) {
    out.emplace_back(strip_scheme(ap->get<std::string>()), path + "/asset_pointer");
  }
  for (const char* k : {"audio_asset_pointer", "video_container_asset_pointer", "frames_asset_pointers"}) {
    if (const Json* v = getp(j, k); v && !v->is_null()) collect_pointers(*v, path + "/" + k, out);
  }
}

struct MsgFacts {
  std::string role, text;
  std::optional<std::string> model;
  double weight = 1.0;
  bool hidden = false;
  std::vector<std::string> hidden_reasons;
  Json meta = Json::object();   // metadata.export
  std::string created;          // "" when absent
};

void add_reason(MsgFacts& f, std::string r) {
  f.hidden = true;
  f.hidden_reasons.push_back(std::move(r));
}

// Everything derivable from one message object.
MsgFacts analyse_message(const Json& m, OpenAiCtx& cx, Counts& counts) {
  MsgFacts f;
  const Json* author = getp(m, "author");
  std::string role = author && author->is_object() ? gets(*author, "role") : std::string();
  f.role = role.empty() ? "unknown" : role;
  std::string author_name = author && author->is_object() ? gets(*author, "name") : std::string();
  const Json* content = getp(m, "content");
  const Json empty = Json::object();
  const Json& md = (getp(m, "metadata") && getp(m, "metadata")->is_object()) ? *getp(m, "metadata") : empty;
  std::string recipient = gets(m, "recipient");
  std::string content_type = content && content->is_object() ? gets(*content, "content_type") : std::string();

  std::vector<const Json*> part_ptrs;
  std::vector<Blk> blocks = content ? openai_blocks(*content, &part_ptrs) : std::vector<Blk>{};

  // text: visible blocks, else every block's plain text (plumbing messages)
  std::string vis, any;
  for (const auto& b : blocks) {
    if ((b.kind == "text" || b.kind == "transcript") && !b.text.empty()) vis += (vis.empty() ? "" : "\n") + b.text;
    if (!b.text.empty()) any += (any.empty() ? "" : "\n") + b.text;
  }
  f.text = !vis.empty() ? vis : any;
  if (f.text.empty() && content && content->is_string()) f.text = content->get<std::string>();

  Json bl = Json::array();
  for (const auto& b : blocks) {
    Json o = Json::object();
    o["kind"] = b.kind;
    o["type"] = b.type;
    if (b.part >= 0) o["part"] = b.part;
    if (!b.guess.empty()) {
      o["guess"] = b.guess;
      o["inferred"] = true;
    }
    if (!b.text.empty()) o["chars"] = static_cast<std::int64_t>(utf8::length(b.text));
    bl.push_back(o);
    counts.block += 1;
    counts.block_kind[b.kind] += 1;
  }

  // attachments / pointers ----------------------------------------------------
  Json atts = Json::array(), ptrs = Json::array(), att_col = Json::array();
  auto resolve_into = [&](Json& rec, const std::string& key, const std::string& name) {
    rec["key"] = key;
    if (!cx.assets) return;
    auto hit = cx.assets->resolve(key, name);
    if (!hit) {
      rec["resolved"] = false;
      return;
    }
    rec["resolved"] = true;
    rec["member"] = hit->member;
    rec["method"] = hit->method;
    std::string h = cx.assets->blob_hash(*cx.env, hit->member);
    if (!h.empty()) rec["blob_hash"] = h;
    std::string loc = cx.assets->readable_path(*cx.env, hit->member);
    bool dup = false;
    for (const auto& x : att_col) dup = dup || (x.is_string() && x.get<std::string>() == loc);
    if (!dup) att_col.push_back(loc);
  };
  if (const Json* a = getp(md, "attachments"); a && a->is_array()) {
    int i = 0;
    for (const auto& x : *a) {
      Json rec = Json::object();
      rec["source"] = "metadata.attachments";
      rec["path"] = "/metadata/attachments/" + std::to_string(i);
      std::string name;
      if (x.is_object()) {
        std::string id = gets(x, "id");
        name = gets(x, "name");
        rec["id"] = id;
        rec["name"] = name;
        if (auto* mt = getp(x, "mime_type")) rec["mime"] = *mt;
        if (auto* sz = getp(x, "size")) rec["size"] = *sz;
        resolve_into(rec, id, name);
      } else {
        rec["resolved"] = false;
      }
      atts.push_back(rec);
      counts.attachment += 1;
      ++i;
    }
  }
  if (const Json* ar = getp(md, "aggregate_result"); ar && ar->is_object()) {
    if (const Json* ms = getp(*ar, "messages"); ms && ms->is_array()) {
      int i = 0;
      for (const auto& x : *ms) {
        Json rec = Json::object();
        rec["source"] = "aggregate_result.messages";
        rec["path"] = "/metadata/aggregate_result/messages/" + std::to_string(i);
        if (x.is_object()) {
          std::string url = gets(x, "image_url");
          if (!url.empty()) resolve_into(rec, strip_scheme(url), "");
          rec["message_type"] = gets(x, "message_type");
        } else {
          rec["resolved"] = false;
        }
        atts.push_back(rec);
        counts.attachment += 1;
        ++i;
      }
    }
  }
  for (std::size_t bi = 0; bi < blocks.size(); ++bi) {
    if (blocks[bi].kind != "media" || !part_ptrs[bi]) continue;
    std::vector<std::pair<std::string, std::string>> keys;
    collect_pointers(*part_ptrs[bi], "/content/parts/" + std::to_string(blocks[bi].part), keys);
    for (auto& [key, path] : keys) {
      Json rec = Json::object();
      rec["source"] = "content.parts";
      rec["path"] = path;
      rec["type"] = blocks[bi].type;
      resolve_into(rec, key, "");
      ptrs.push_back(rec);
    }
  }

  // citations ---------------------------------------------------------------
  Json cits = Json::array(), groups = Json::array();
  auto cite = [&](const char* kind, const std::string& path, const Json& item) {
    Json c = Json::object();
    c["kind"] = kind;
    c["path"] = path;
    if (item.is_object()) {
      std::string url = gets(item, "url"), title = gets(item, "title");
      if (const Json* mm = getp(item, "metadata"); mm && mm->is_object()) {
        if (url.empty()) url = gets(*mm, "url");
        if (title.empty()) title = gets(*mm, "title");
      }
      if (url.empty()) {
        if (const Json* its = getp(item, "items"); its && its->is_array() && !its->empty() && its->front().is_object()) {
          url = gets(its->front(), "url");
          if (title.empty()) title = gets(its->front(), "title");
        }
      }
      if (!url.empty()) c["url"] = url;
      if (!title.empty()) c["title"] = title;
      if (std::string t = gets(item, "type"); !t.empty()) c["type"] = t;
    }
    cits.push_back(c);
    counts.citation += 1;
  };
  if (const Json* c = getp(md, "citations"); c && c->is_array()) {
    int i = 0;
    for (const auto& x : *c) cite("citation", "/metadata/citations/" + std::to_string(i++), x);
  }
  if (const Json* c = getp(md, "content_references"); c && c->is_array()) {
    int i = 0;
    for (const auto& x : *c) cite("content_reference", "/metadata/content_references/" + std::to_string(i++), x);
  }
  if (const Json* g = getp(md, "search_result_groups"); g && g->is_array()) {
    int gi = 0;
    for (const auto& grp : *g) {
      Json gr = Json::object();
      gr["path"] = "/metadata/search_result_groups/" + std::to_string(gi);
      if (grp.is_object()) gr["domain"] = gets(grp, "domain");
      groups.push_back(gr);
      counts.citation_group += 1;
      if (grp.is_object()) {
        if (const Json* es = getp(grp, "entries"); es && es->is_array()) {
          int ei = 0;
          for (const auto& e : *es) cite("search_entry", gr["path"].get<std::string>() + "/entries/" + std::to_string(ei++), e);
        }
      }
      ++gi;
    }
  }
  if ((content_type == "text" || content_type == "multimodal_text") && content) {
    if (const Json* parts = getp(*content, "parts"); parts && parts->is_array()) {
      int pi = 0;
      for (const auto& p : *parts) {
        if (p.is_object() && gets(p, "type") == "search_result_group") {
          if (const Json* es = getp(p, "entries"); es && es->is_array()) {
            int ei = 0;
            for (const auto& e : *es) cite("part_entry", "/content/parts/" + std::to_string(pi) + "/entries/" + std::to_string(ei++), e);
          }
        }
        ++pi;
      }
    }
  }

  // context / memory / artifact -----------------------------------------------
  std::int64_t ci = 0;
  if (content_type == "user_editable_context" && content) {
    for (const char* k : {"user_profile", "user_instructions"}) {
      if (const Json* v = getp(*content, k); v && v->is_string()) ++ci;
    }
  }
  counts.custom_instruction += ci;
  bool memory = content_type == "model_editable_context";
  if (memory) counts.memory += 1;
  Json artifact = nullptr;
  if (recipient == "canmore.create_textdoc") {
    counts.artifact += 1;
    artifact = Json::object();
    artifact["tool"] = recipient;
    std::string payload;
    if (content && content->is_object()) {
      payload = gets(*content, "text");
      if (payload.empty()) {
        if (const Json* parts = getp(*content, "parts"); parts && parts->is_array() && !parts->empty() && parts->front().is_string())
          payload = parts->front().get<std::string>();
      }
    }
    auto pj = json::parse(payload);
    if (pj && pj->is_object()) {
      artifact["parsed"] = true;
      artifact["name"] = gets(*pj, "name");
      artifact["type"] = gets(*pj, "type");
    } else {
      artifact["parsed"] = false;
    }
  }

  // hidden classification (display attribute only; nothing is dropped) --------
  bool user_sys = json::get_bool(md, "is_user_system_message", false);
  if (json::get_bool(md, "is_visually_hidden_from_conversation", false)) add_reason(f, "visually_hidden");
  if (f.role == "system" && !user_sys) add_reason(f, "system");
  if (f.role == "system" && user_sys) add_reason(f, "custom_instructions");
  if (f.role == "tool") add_reason(f, "tool_output");
  if (!recipient.empty() && recipient != "all") add_reason(f, "recipient:" + recipient);
  static const std::set<std::string> plumbing = {"code", "sonic_webpage", "system_error", "tether_browsing_display", "thoughts",
                                                "reasoning_recap", "tether_browsing_code", "execution_output",
                                                "user_editable_context", "model_editable_context"};
  if (plumbing.count(content_type)) add_reason(f, "content_type:" + content_type);
  if (utf8::is_blank(f.text) && blocks.empty()) add_reason(f, "empty");
  double weight = 1.0;
  if (const Json* w = getp(m, "weight"); w && w->is_number()) weight = w->get<double>();
  f.weight = weight;
  if (const Json* w = getp(m, "weight"); w && w->is_number() && weight == 0.0) add_reason(f, "weight_zero");

  if (std::string ms = gets(md, "model_slug"); !ms.empty()) f.model = ms;

  Json ex = Json::object();
  ex["role_raw"] = role;
  ex["author_name"] = author_name.empty() ? Json(nullptr) : Json(author_name);
  ex["recipient"] = recipient.empty() ? Json(nullptr) : Json(recipient);
  ex["channel"] = gets(m, "channel").empty() ? Json(nullptr) : Json(gets(m, "channel"));
  ex["content_type"] = content_type;
  ex["blocks"] = bl;
  ex["attachments"] = atts;
  ex["pointers"] = ptrs;
  ex["citations"] = cits;
  ex["citation_groups"] = groups;
  ex["custom_instruction"] = ci;
  ex["memory"] = memory;
  ex["artifact"] = artifact;
  f.meta = ex;
  f.meta["attachments_col"] = att_col;  // moved to messages.attachments by the caller
  return f;
}

}  // namespace

// ── conversation ────────────────────────────────────────────────────
void parse_openai_conversation(const Json& conv, int index, const std::string& member, OpenAiCtx& cx, ConvModel& out,
                               Counts& counts) {
  counts.conversation += 1;
  out.source = "import:openai";
  out.leaves_total = json_leaves(conv);

  const Json* mapping_p = getp(conv, "mapping");
  const bool has_mapping = mapping_p && mapping_p->is_object() && !mapping_p->empty();
  const Json empty_obj = Json::object();
  const Json& mapping = has_mapping ? *mapping_p : empty_obj;

  struct N {
    std::string id;
    const Json* node = nullptr;
    const Json* msg = nullptr;
    std::optional<std::string> parent;
    std::vector<std::string> children;
  };
  std::vector<N> nodes;
  std::unordered_map<std::string, int> idx;
  for (auto it = mapping.begin(); it != mapping.end(); ++it) {
    N n;
    n.id = it.key();
    n.node = &it.value();
    if (n.node->is_object()) {
      if (const Json* m = getp(*n.node, "message"); m && m->is_object()) n.msg = m;
      if (const Json* p = getp(*n.node, "parent"); p && p->is_string()) n.parent = p->get<std::string>();
      if (const Json* c = getp(*n.node, "children"); c && c->is_array()) {
        for (const auto& x : *c) {
          if (x.is_string()) n.children.push_back(x.get<std::string>());
        }
      }
    }
    idx[n.id] = static_cast<int>(nodes.size());
    nodes.push_back(std::move(n));
  }
  const int N_ = static_cast<int>(nodes.size());

  // graph facts per the provider's `children` lists
  std::vector<int> valid_kids(static_cast<std::size_t>(N_), 0);
  std::int64_t fork_points = 0, leaves = 0;
  for (int i = 0; i < N_; ++i) {
    for (const auto& c : nodes[static_cast<std::size_t>(i)].children) {
      if (idx.count(c)) valid_kids[static_cast<std::size_t>(i)]++;
    }
    if (valid_kids[static_cast<std::size_t>(i)] >= 2) ++fork_points;
    if (valid_kids[static_cast<std::size_t>(i)] == 0 && nodes[static_cast<std::size_t>(i)].msg) ++leaves;
  }
  counts.fork_points += fork_points;
  counts.branch += leaves;

  // tree by parent pointers, ordered by the parent's `children` list
  std::vector<std::vector<int>> kids(static_cast<std::size_t>(N_));
  std::vector<int> roots;
  for (int i = 0; i < N_; ++i) {
    const auto& n = nodes[static_cast<std::size_t>(i)];
    auto pit = n.parent ? idx.find(*n.parent) : idx.end();
    if (pit != idx.end() && pit->second != i) kids[static_cast<std::size_t>(pit->second)].push_back(i);
    else roots.push_back(i);
  }
  for (int p = 0; p < N_; ++p) {
    auto& k = kids[static_cast<std::size_t>(p)];
    if (k.size() < 2) continue;
    std::unordered_map<std::string, int> pos;
    int q = 0;
    for (const auto& c : nodes[static_cast<std::size_t>(p)].children) pos.emplace(c, q++);
    std::stable_sort(k.begin(), k.end(), [&](int a, int b) {
      auto ia = pos.find(nodes[static_cast<std::size_t>(a)].id), ib = pos.find(nodes[static_cast<std::size_t>(b)].id);
      int pa = ia == pos.end() ? 1 << 30 : ia->second, pb = ib == pos.end() ? 1 << 30 : ib->second;
      return pa < pb;
    });
  }
  std::vector<int> order;
  std::vector<char> seen(static_cast<std::size_t>(N_), 0);
  for (int r : roots) {
    std::vector<int> stack{r};
    while (!stack.empty()) {
      int cur = stack.back();
      stack.pop_back();
      if (seen[static_cast<std::size_t>(cur)]) continue;
      seen[static_cast<std::size_t>(cur)] = 1;
      order.push_back(cur);
      const auto& k = kids[static_cast<std::size_t>(cur)];
      for (auto it = k.rbegin(); it != k.rend(); ++it) stack.push_back(*it);
    }
  }
  std::vector<char> reachable = seen;
  for (int i = 0; i < N_; ++i) {
    if (!seen[static_cast<std::size_t>(i)]) order.push_back(i);  // cycles: appended, marked unreachable
  }

  // current path (strict, per current_node) and the inferred fallback
  std::vector<char> on_path(static_cast<std::size_t>(N_), 0), active(static_cast<std::size_t>(N_), 0);
  bool cur_resolved = false, path_inferred = false;
  std::string cur_id = gets(conv, "current_node");
  auto walk = [&](int start, std::vector<char>& mark) {
    std::set<int> vis;
    int cur = start;
    while (cur >= 0 && !vis.count(cur)) {
      vis.insert(cur);
      mark[static_cast<std::size_t>(cur)] = 1;
      const auto& n = nodes[static_cast<std::size_t>(cur)];
      auto pit = n.parent ? idx.find(*n.parent) : idx.end();
      cur = pit == idx.end() ? -1 : pit->second;
    }
  };
  if (!cur_id.empty() && idx.count(cur_id)) {
    cur_resolved = true;
    walk(idx[cur_id], on_path);
    active = on_path;
  } else if (N_ > 0) {
    int best = -1;
    double best_t = -1e300;
    for (int i : order) {
      const auto& n = nodes[static_cast<std::size_t>(i)];
      if (!n.msg || !reachable[static_cast<std::size_t>(i)] || valid_kids[static_cast<std::size_t>(i)] != 0) continue;
      double t = -1e299;
      if (const Json* ct = getp(*n.msg, "create_time"); ct && ct->is_number()) t = ct->get<double>();
      if (best < 0 || t >= best_t) {
        best = i;
        best_t = t;
      }
    }
    if (best >= 0) {
      walk(best, active);
      path_inferred = true;
    }
  }
  std::int64_t path_msgs = 0;
  for (int i = 0; i < N_; ++i) {
    if (on_path[static_cast<std::size_t>(i)] && nodes[static_cast<std::size_t>(i)].msg) ++path_msgs;
  }
  counts.current_path_messages += path_msgs;

  // messages in DFS order
  std::optional<std::string> conv_created = to_iso(json::find(conv, "create_time") ? *json::find(conv, "create_time") : Json(nullptr));
  std::vector<int> msg_index(static_cast<std::size_t>(N_), -1);
  Json null_nodes = Json::array();
  std::int64_t kept = 0;
  std::vector<MsgFacts> facts;
  std::string earliest;
  std::vector<int> node_of;  // message idx -> node idx
  for (int ni : order) {
    const auto& n = nodes[static_cast<std::size_t>(ni)];
    if (!n.msg) {
      Json nn = Json::object();
      nn["i"] = ni;
      nn["id"] = n.id;
      nn["node"] = *n.node;
      null_nodes.push_back(nn);
      kept += json_leaves(*n.node);
      continue;
    }
    counts.message += 1;
    MsgFacts f = analyse_message(*n.msg, cx, counts);
    if (auto t = to_iso(getp(*n.msg, "create_time") ? *getp(*n.msg, "create_time") : Json(nullptr))) {
      f.created = *t;
      if (earliest.empty() || *t < earliest) earliest = *t;
    }
    msg_index[static_cast<std::size_t>(ni)] = static_cast<int>(facts.size());
    facts.push_back(std::move(f));
    node_of.push_back(ni);
  }
  std::string created = conv_created ? *conv_created : (!earliest.empty() ? earliest : timeutil::utc_now_iso());
  std::string updated = created;
  if (const Json* ut = getp(conv, "update_time")) {
    if (auto t = to_iso(*ut)) updated = *t;
  }

  // parent (nearest message ancestor), sibling groups, statuses
  std::vector<int> parent_msg(facts.size(), -1);
  for (std::size_t mi = 0; mi < facts.size(); ++mi) {
    int ni = node_of[mi];
    if (!reachable[static_cast<std::size_t>(ni)]) continue;
    std::set<int> vis{ni};
    const auto& start = nodes[static_cast<std::size_t>(ni)];
    auto pit = start.parent ? idx.find(*start.parent) : idx.end();
    int cur = pit == idx.end() ? -1 : pit->second;
    while (cur >= 0 && !vis.count(cur)) {
      vis.insert(cur);
      if (msg_index[static_cast<std::size_t>(cur)] >= 0) {
        parent_msg[mi] = msg_index[static_cast<std::size_t>(cur)];
        break;
      }
      const auto& c = nodes[static_cast<std::size_t>(cur)];
      auto p2 = c.parent ? idx.find(*c.parent) : idx.end();
      cur = p2 == idx.end() ? -1 : p2->second;
    }
  }
  std::map<int, std::vector<int>> siblings;  // parent msg -> child msgs (DFS order)
  for (std::size_t mi = 0; mi < facts.size(); ++mi) {
    if (parent_msg[mi] >= 0) siblings[parent_msg[mi]].push_back(static_cast<int>(mi));
  }
  std::vector<int> group(facts.size(), -1), vnum(facts.size(), 1), sib_count(facts.size(), 1), sib_index(facts.size(), 0);
  int groups = 0;
  for (auto& [p, v] : siblings) {
    (void)p;
    int g = v.size() >= 2 ? groups++ : -1;
    for (std::size_t k = 0; k < v.size(); ++k) {
      group[static_cast<std::size_t>(v[k])] = g;
      vnum[static_cast<std::size_t>(v[k])] = static_cast<int>(k) + 1;
      sib_count[static_cast<std::size_t>(v[k])] = static_cast<int>(v.size());
      sib_index[static_cast<std::size_t>(v[k])] = static_cast<int>(k);
    }
  }
  out.groups = groups;

  out.msgs.resize(facts.size());
  for (std::size_t mi = 0; mi < facts.size(); ++mi) {
    int ni = node_of[mi];
    const auto& n = nodes[static_cast<std::size_t>(ni)];
    MsgFacts& f = facts[mi];
    MsgModel& m = out.msgs[mi];
    m.key = n.id;
    m.parent = parent_msg[mi];
    m.role = f.role;
    m.text = f.text;
    m.model = f.model;
    m.created = f.created.empty() ? created : f.created;
    m.weight = f.weight;
    m.group = group[mi];
    m.version_num = vnum[mi];
    if (!reachable[static_cast<std::size_t>(ni)]) m.status = "excluded";
    else if (!active[static_cast<std::size_t>(ni)]) m.status = "version";
    else if (f.hidden) m.status = "excluded";
    else m.status = "active";
    m.attachments = f.meta["attachments_col"];
    f.meta.erase("attachments_col");

    Json ex = f.meta;
    ex["provider"] = "openai";
    ex["kind"] = "message";
    ex["key"] = n.id;
    ex["message_id"] = gets(*n.msg, "id");
    Json node_rest = Json::object();
    for (auto it = n.node->begin(); it != n.node->end(); ++it) {
      if (it.key() != "message") node_rest[it.key()] = it.value();
    }
    ex["node"] = node_rest;
    ex["node_index"] = ni;
    ex["raw"] = *n.msg;
    ex["hidden"] = f.hidden;
    ex["hidden_reasons"] = Json(f.hidden_reasons);
    ex["on_current_path"] = static_cast<bool>(on_path[static_cast<std::size_t>(ni)]);
    ex["on_active_path"] = static_cast<bool>(active[static_cast<std::size_t>(ni)]);
    ex["reachable"] = static_cast<bool>(reachable[static_cast<std::size_t>(ni)]);
    ex["leaf"] = valid_kids[static_cast<std::size_t>(ni)] == 0;
    ex["fork"] = valid_kids[static_cast<std::size_t>(ni)] >= 2;
    ex["sibling_index"] = sib_index[mi];
    ex["sibling_count"] = sib_count[mi];
    ex["created_inferred"] = f.created.empty();
    m.export_meta = ex;
    kept += json_leaves(node_rest) + json_leaves(*n.msg);
    if (!f.created.empty() && m.created != f.created) m.created = f.created;
  }

  // conversation-level record
  Json fields = Json::object();
  for (auto it = conv.begin(); it != conv.end(); ++it) {
    if (it.key() == "mapping" && has_mapping) continue;
    fields[it.key()] = it.value();
  }
  kept += json_leaves(fields);
  out.leaves_kept = kept;

  std::string title_raw = gets(conv, "title");
  std::string title = title_raw;
  std::string title_source = "export";
  if (utf8::is_blank(title)) {
    title_source = "generated";
    title.clear();
    for (const auto& m : out.msgs) {
      if (m.role == "user" && m.status == "active" && !utf8::is_blank(m.text)) {
        title = first_line(m.text, 60);
        break;
      }
    }
    if (title.empty()) title = "Untitled conversation";
  }
  std::string ckey = gets(conv, "conversation_id");
  if (ckey.empty()) ckey = gets(conv, "id");
  out.key = ckey;
  out.title = title;
  out.created = created;
  out.updated = updated;

  Json g = Json::object();
  g["nodes"] = N_;
  g["messages"] = static_cast<std::int64_t>(facts.size());
  g["fork_points"] = fork_points;
  g["leaves"] = leaves;
  g["roots"] = static_cast<std::int64_t>(roots.size());
  g["current_path_messages"] = path_msgs;
  g["unreachable_nodes"] = static_cast<std::int64_t>(std::count(reachable.begin(), reachable.end(), 0));
  g["version_groups"] = groups;

  Json ex = Json::object();
  ex["provider"] = "openai";
  ex["schema"] = std::string(kSchema);
  ex["member"] = member;
  ex["index"] = index;
  ex["key"] = ckey;
  ex["title_original"] = getp(conv, "title") ? *getp(conv, "title") : Json(nullptr);
  ex["title_source"] = title_source;
  ex["current_node"] = cur_id.empty() ? Json(nullptr) : Json(cur_id);
  ex["current_node_resolved"] = cur_resolved;
  ex["current_path_inferred"] = path_inferred;
  ex["has_mapping"] = has_mapping;
  ex["fields"] = fields;
  ex["null_nodes"] = null_nodes;
  ex["graph"] = g;
  out.export_meta = ex;
}

// ── root members ────────────────────────────────────────────────────
namespace {
std::string base_of(const std::string& rel) {
  auto p = rel.find_last_of('/');
  return p == std::string::npos ? rel : rel.substr(p + 1);
}

void link_or_note(Env& env, const std::string& src, const std::string& dst, const char* type, Json meta = Json::object()) {
  auto r = env.db.create_link(src, dst, type, 1.0, meta);
  if (!r) log::warn(kLog, "link {} -> {} failed: {}", src, dst, r.error().message);
}
}  // namespace

bool import_openai_member(Env& env, OpenAiCtx& cx, const std::string& rel, const fs::path& abs, Report& rep) {
  const std::string base = base_of(rel);
  const bool in_textdocs = rel.rfind("textdocs/", 0) == 0 && base.size() > 5 && base.substr(base.size() - 5) == ".json";
  const bool known_generic = base == "model_comparisons.json" || base == "group_chats.json" || base == "shopping.json" ||
                             base == "sora.json";
  if (base != "user.json" && base != "message_feedback.json" && base != "shared_conversations.json" && !in_textdocs &&
      !known_generic)
    return false;

  LoadStats st;
  auto doc = load_json_doc(abs, st);
  if (!doc) {
    rep.errors.push_back(Json{{"member", rel}, {"code", st.empty ? "empty" : "invalid_json"}, {"message", st.message}});
    rep.partial = true;
    return true;
  }
  if (Json r = stats_to_json(st); !r.empty()) rep.repairs[rel] = r;

  auto ent = [&](const char* kind, const std::string& label, const std::string& content, const Json& rec) {
    Json md = Json::object();
    md["export"] = Json{{"provider", "openai"}, {"member", rel}, {"record", rec}};
    auto id = write_entity(env, kind, label, content, md);
    return id ? *id : std::string();
  };
  auto conv_id_of = [&](const Json& rec) -> std::string {
    if (!rec.is_object()) return "";
    auto it = cx.conv_db_id.find(gets(rec, "conversation_id"));
    return it == cx.conv_db_id.end() ? "" : it->second;
  };

  if (base == "user.json") {
    if (doc->is_object()) {
      std::string label = gets(*doc, "email").empty() ? gets(*doc, "id") : gets(*doc, "email");
      ent("export:account", label.empty() ? "account" : label, "", *doc);
      rep.counts.account += 1;
    }
  } else if (base == "message_feedback.json") {
    if (doc->is_array()) {
      for (const auto& rec : *doc) {
        std::string label = rec.is_object() ? gets(rec, "rating") + " " + gets(rec, "message_id") : "feedback";
        std::string nid = ent("export:feedback", label, rec.is_object() ? gets(rec, "content") : "", rec);
        rep.counts.feedback += 1;
        if (nid.empty() || !rec.is_object()) continue;
        auto mit = cx.msg_db_id.find(gets(rec, "conversation_id") + '\x1f' + gets(rec, "message_id"));
        if (mit != cx.msg_db_id.end()) {
          link_or_note(env, nid, mit->second, "references");
        } else {
          rep.warnings.push_back("feedback " + gets(rec, "id") + " references message " + gets(rec, "message_id") +
                                 " that is not in the export");
        }
      }
    }
  } else if (base == "shared_conversations.json") {
    if (doc->is_array()) {
      for (const auto& rec : *doc) {
        std::string nid = ent("export:shared_link", rec.is_object() ? gets(rec, "title") : "shared", "", rec);
        rep.counts.shared_link += 1;
        if (nid.empty()) continue;
        if (std::string cid = conv_id_of(rec); !cid.empty()) link_or_note(env, nid, cid, "references");
      }
    }
  } else if (in_textdocs) {
    if (doc->is_object()) {
      std::string nid = ent("export:artifact", gets(*doc, "name").empty() ? base : gets(*doc, "name"), gets(*doc, "content"), *doc);
      rep.counts.artifact += 1;
      if (!nid.empty()) {
        if (std::string cid = conv_id_of(*doc); !cid.empty()) link_or_note(env, nid, cid, "part_of");
      }
    }
  } else {  // known optional files, usually empty
    bool empty = (doc->is_array() && doc->empty()) || (doc->is_object() && doc->empty());
    if (doc->is_object() && doc->size() == 1) {
      auto it = doc->begin();
      if (it.value().is_array() && it.value().empty()) empty = true;
    }
    if (!empty) ent("export:member", rel, "", *doc);
  }
  return true;
}

}  // namespace loom::xport
