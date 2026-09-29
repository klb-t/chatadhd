// Driver of the lossless provider-export path: ZIP archives (also nested
// containers) and bare JSON files. Interprets OpenAI/Anthropic exports, reports
// everything it could not interpret, never throws on malformed input.
#include <algorithm>
#include <cstring>

#include "export_internal.h"
#include "importer_internal.h"
#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include <miniz.h>

namespace loom {

namespace fs = std::filesystem;
namespace idt = importer_detail;
using namespace xport;

namespace {
constexpr std::string_view kLog = "loom.import.export";
constexpr std::int64_t kInlineMemberLimit = 1'000'000;

struct Member {
  std::string rel;
  fs::path abs;
  std::int64_t size = 0;
};

bool safe_relpath(const fs::path& rel) {
  if (rel.empty() || rel.is_absolute()) return false;
  for (const auto& part : rel) {
    if (part == "..") return false;
  }
  return true;
}

std::string base_of(const std::string& rel) {
  auto p = rel.find_last_of('/');
  return p == std::string::npos ? rel : rel.substr(p + 1);
}
std::string dir_of(const std::string& rel) {
  auto p = rel.find_last_of('/');
  return p == std::string::npos ? std::string() : rel.substr(0, p + 1);
}
int depth_of(const std::string& rel) { return static_cast<int>(std::count(rel.begin(), rel.end(), '/')); }
bool ends_with(std::string_view s, std::string_view suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }

Json member_json(const std::string& name, std::int64_t size, std::string_view disposition, Json extra = Json::object()) {
  extra["name"] = name;
  extra["size"] = size;
  extra["disposition"] = disposition;
  return extra;
}

// Approximation of what the legacy JSON router turns into conversations.
bool legacy_recognizable(const Json& doc) {
  auto has = [](const Json& o, std::initializer_list<const char*> keys) {
    if (!o.is_object()) return false;
    for (const char* k : keys) {
      if (json::find(o, k)) return true;
    }
    return false;
  };
  if (doc.is_array()) return !doc.empty() && has(doc.front(), {"mapping", "role", "content", "messages", "chat_messages"});
  if (doc.is_object()) {
    if (has(doc, {"mapping", "chat_messages", "messages", "role"})) return true;
    for (const char* k : {"conversations", "data"}) {
      const Json* v = json::find(doc, k);
      if (v && v->is_array()) return true;
    }
  }
  return false;
}

// One provider-export run (shared by the zip and the bare-json entry points).
struct Run {
  Env env;
  Report rep;
  OpenAiCtx oa;
  AnthropicCtx an;
  AssetIndex assets;
  std::vector<Conversation> convs;
  int conv_index = 0;
  std::int64_t seen_conversations = 0;

  Run(Env e) : env(std::move(e)) {
    oa.env = &env;
  }

  // Returns false to stop (cancelled).
  bool element(Json&& el, const std::string& member, std::int64_t idx, const std::string& provider_default) {
    if (env.cancelled()) return false;
    if (!el.is_object()) {
      rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "unrecognized_element"},
                                {"message", "conversation element is not a JSON object"}});
      rep.partial = true;
      return true;
    }
    const bool is_oa = json::find(el, "mapping") != nullptr;
    const bool is_cl = json::find(el, "chat_messages") != nullptr;
    std::string prov = is_oa ? "openai" : (is_cl ? "anthropic" : provider_default);
    ConvModel cm;
    Counts c;
    if (prov == "openai") parse_openai_conversation(el, conv_index, member, oa, cm, c);
    else parse_anthropic_conversation(el, conv_index, member, env, cm, c);

    std::map<std::string, std::string> key_to_id;
    auto w = write_conversation(env, cm, &key_to_id);
    if (!w) {
      rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "write_failed"}, {"message", w.error().message}});
      rep.partial = true;
      return true;
    }
    rep.counts.add(c);
    rep.json_leaves += cm.leaves_total;
    rep.leaves_preserved += cm.leaves_kept;
    if (!cm.key.empty()) oa.conv_db_id[cm.key] = w->id;
    for (const auto& [k, id] : key_to_id) oa.msg_db_id[cm.key + '\x1f' + k] = id;
    for (std::size_t i = 0; i < cm.msgs.size(); ++i) {
      if (const Json* mid = json::find(cm.msgs[i].export_meta, "message_id"); mid && mid->is_string()) {
        auto it = key_to_id.find(cm.msgs[i].key);
        if (it != key_to_id.end()) oa.msg_db_id[cm.key + '\x1f' + mid->get<std::string>()] = it->second;
      }
    }
    convs.push_back(*w);
    if (env.on_conv) env.on_conv(*w, member, conv_index);
    if (env.opts.progress) env.opts.progress(static_cast<std::int64_t>(convs.size()), -1, "export");
    ++conv_index;
    return true;
  }

  void note_stats(const std::string& member, const LoadStats& st) {
    if (Json r = stats_to_json(st); !r.empty()) rep.repairs[member] = r;
    if (st.truncated) {
      rep.errors.push_back(Json{{"member", member}, {"code", "truncated"}, {"message", st.message},
                                {"discarded_bytes", st.truncated_bytes}});
      rep.partial = true;
    }
    if (st.empty || st.invalid || st.too_deep) {
      rep.errors.push_back(Json{{"member", member}, {"code", st.empty ? "empty" : (st.too_deep ? "too_deep" : "invalid_json")},
                                {"message", st.message}});
      rep.partial = true;
    }
  }

  void unknown_member(const Member& m) {
    rep.unknown_members.push_back(m.rel);
    Json md = Json::object();
    Json ex = Json::object();
    ex["member"] = m.rel;
    ex["size"] = m.size;
    if (auto h = sha256_file_hex(m.abs); h) ex["sha256"] = *h;
    if (env.blobs) {
      if (auto b = env.blobs->put_file(m.abs, ""); b) ex["blob_hash"] = b->hash;
    }
    std::string content;
    if (m.size <= kInlineMemberLimit) {
      if (auto raw = fsutil::read_file(m.abs); raw && utf8::is_valid(*raw)) content = *raw;
    }
    md["export"] = ex;
    (void)write_entity(env, "export:member", m.rel, content, md);
  }
};

}  // namespace

// ── ZIP ─────────────────────────────────────────────────────────────
Result<std::vector<Conversation>> ConversationImporter::export_zip_body(const fs::path& path, const ImportOptions& opts,
                                                                        Json& report_out) {
  fsutil::TempDir td("loom_xport_");
  if (!td.valid()) return Error(Errc::Io, "could not create a temp dir for zip extraction");

  mz_zip_archive zip;
  std::memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_file(&zip, path.string().c_str(), 0)) {
    return Error(Errc::Parse, "invalid zip archive: " + path.string());
  }

  Env env{db_, blobs_, opts, {}};
  env.on_conv = [this](const Conversation& c, const std::string& member, int) {
    if (!current_source_) return;
    auto saved = current_source_->zip_member;
    current_source_->zip_member = saved ? (*saved + "!" + member) : member;
    record_provenance(c, "export");
    current_source_->zip_member = saved;
  };
  Run run(env);
  Report& rep = run.rep;

  std::vector<Member> entries;
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
    std::string name = st.m_filename;
    fs::path relp(name);
    if (!safe_relpath(relp)) {
      log::warn(kLog, "zip: skipping unsafe entry path {}", name);
      rep.members.push_back(member_json(name, static_cast<std::int64_t>(st.m_uncomp_size), "skipped_unsafe_path"));
      rep.warnings.push_back("skipped unsafe archive path: " + name);
      continue;
    }
    fs::path dest = td.path() / relp;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (!mz_zip_reader_extract_to_file(&zip, i, dest.string().c_str(), 0)) {
      rep.members.push_back(member_json(name, static_cast<std::int64_t>(st.m_uncomp_size), "extract_failed"));
      rep.errors.push_back(Json{{"member", name}, {"code", "extract_failed"}, {"message", "could not extract (corrupt entry?)"}});
      rep.partial = true;
      continue;
    }
    entries.push_back(Member{relp.generic_string(), dest, static_cast<std::int64_t>(st.m_uncomp_size)});
  }
  mz_zip_reader_end(&zip);
  std::sort(entries.begin(), entries.end(), [](const Member& a, const Member& b) { return a.rel < b.rel; });

  // conversation files: shallowest directory that has any
  std::string prefix;
  bool have_conv_files = false;
  {
    int best_depth = 1 << 20;
    for (const auto& e : entries) {
      if (!is_conversation_file(base_of(e.rel))) continue;
      int d = depth_of(e.rel);
      if (d < best_depth) {
        best_depth = d;
        prefix = dir_of(e.rel);
        have_conv_files = true;
      }
    }
  }

  auto disp = [&](const Member& m, std::string_view d, Json extra = Json::object()) {
    rep.members.push_back(member_json(m.rel, m.size, d, std::move(extra)));
  };

  // ---------------------------------------------------------------- provider export
  std::string provider = "unknown";
  if (have_conv_files) {
    std::vector<const Member*> conv_files;
    for (const auto& e : entries) {
      if (dir_of(e.rel) == prefix && is_conversation_file(base_of(e.rel))) conv_files.push_back(&e);
    }
    // peek at the first element of the first readable file to pick the provider
    for (const Member* f : conv_files) {
      Loader L;
      L.element = [&](Json&& el, std::int64_t) {
        if (el.is_object()) {
          if (json::find(el, "mapping")) provider = "openai";
          else if (json::find(el, "chat_messages")) provider = "anthropic";
        }
        return false;
      };
      (void)load_json_file(f->abs, L);
      if (provider != "unknown") break;
    }
    if (provider == "unknown") {
      for (const Member* f : conv_files) {
        std::string b = base_of(f->rel);
        if (b.rfind("conversations-", 0) == 0) provider = "openai";  // shard naming is ChatGPT-specific
      }
    }
  }

  if (provider == "openai" || provider == "anthropic") {
    rep.provider = provider;
    if (provider == "openai") {
      for (const auto& e : entries) {
        if (e.rel.rfind(prefix, 0) != 0) continue;
        std::string sub = e.rel.substr(prefix.size());
        if (AssetIndex::is_asset(sub)) run.assets.add(sub, e.abs, e.size);
      }
      run.oa.assets = &run.assets;
    }
    // 1. conversations
    for (const auto& e : entries) {
      if (dir_of(e.rel) != prefix || !is_conversation_file(base_of(e.rel))) continue;
      if (run.env.cancelled()) break;
      Loader L;
      const std::string member = e.rel;
      L.element = [&](Json&& el, std::int64_t idx) { return run.element(std::move(el), member, idx, provider); };
      L.bad_element = [&](std::int64_t idx, const std::string& why) {
        run.rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "invalid_element"}, {"message", why}});
        run.rep.partial = true;
      };
      LoadStats st = load_json_file(e.abs, L);
      run.note_stats(member, st);
      disp(e, "conversations", Json{{"wrapper", L.wrapper}});
    }
    // 2. other members
    std::vector<const Member*> rest;
    for (const auto& e : entries) {
      if (dir_of(e.rel) == prefix && is_conversation_file(base_of(e.rel))) continue;
      rest.push_back(&e);
    }
    if (provider == "anthropic") {
      std::stable_sort(rest.begin(), rest.end(), [](const Member* a, const Member* b) {
        auto rank = [](const std::string& r) { return base_of(r) == "projects.json" ? 0 : 1; };
        return rank(a->rel) < rank(b->rel);
      });
    }
    std::vector<Json> nested_parts;
    for (const Member* m : rest) {
      const std::string sub = m->rel.rfind(prefix, 0) == 0 ? m->rel.substr(prefix.size()) : m->rel;
      if (provider == "openai" && m->rel.rfind(prefix, 0) == 0 && AssetIndex::is_asset(sub)) {
        disp(*m, "asset");
        continue;
      }
      if (provider == "openai" && base_of(m->rel) == "chat.html") {
        disp(*m, "offline_viewer_skipped", Json{{"reason", "duplicate rendering of conversations.json; bytes kept in the source archive"}});
        continue;
      }
      bool handled = false;
      if (m->rel.rfind(prefix, 0) == 0) {
        handled = provider == "openai" ? import_openai_member(run.env, run.oa, sub, m->abs, run.rep)
                                       : import_anthropic_member(run.env, run.an, sub, m->abs, run.rep);
      }
      if (handled) {
        disp(*m, "record");
        continue;
      }
      if (detect_format(m->abs) == "zip" && ends_with(m->rel, ".zip")) {
        ImportOptions mo = opts;
        mo.title = std::nullopt;
        mo.progress = nullptr;
        auto r = import_file_as(m->abs, mo, "zip_member", m->rel);
        if (r) {
          for (auto& c : r->conversations) run.convs.push_back(std::move(c));
          run.rep.parts.push_back(Json{{"member", m->rel}, {"report", r->export_report}});
          disp(*m, "nested_archive");
        } else {
          disp(*m, "nested_archive_failed");
          run.rep.errors.push_back(Json{{"member", m->rel}, {"code", "nested_failed"}, {"message", r.error().message}});
          run.rep.partial = true;
        }
        continue;
      }
      disp(*m, "unknown");
      run.unknown_member(*m);
    }
    if (provider == "openai") run.assets.finish(run.env, rep);
    for (const auto& s : run.rep.unresolved_keys) {
      run.rep.warnings.push_back("asset key not found in the archive: " + s);
    }
  } else {
    // ------------------------------------------------------------ not a known provider export
    rep.provider = have_conv_files ? "unknown" : "generic";
    std::set<std::string> file_members;
    for (const auto& e : entries) file_members.insert(e.rel);

    // pass 0: nested archives (containers of exports)
    bool any_provider_part = false;
    std::vector<const Member*> others;
    for (const auto& e : entries) {
      if (ends_with(e.rel, ".zip") && detect_format(e.abs) == "zip") {
        ImportOptions mo = opts;
        mo.title = opts.title.has_value() ? opts.title : std::optional<std::string>(e.rel);
        mo.progress = nullptr;
        auto r = import_file_as(e.abs, mo, "zip_member", e.rel);
        if (!r) {
          log::warn(kLog, "Failed to import {} from ZIP: {}", e.rel, r.error().message);
          disp(e, "nested_archive_failed");
          rep.errors.push_back(Json{{"member", e.rel}, {"code", "nested_failed"}, {"message", r.error().message}});
          rep.partial = true;
          continue;
        }
        if (!r->export_report.is_null()) {
          const Json* pv = json::find(r->export_report, "provider");
          if (pv && pv->is_string() && (pv->get<std::string>() == "openai" || pv->get<std::string>() == "anthropic")) any_provider_part = true;
          rep.parts.push_back(Json{{"member", e.rel}, {"report", r->export_report}});
        }
        for (auto& c : r->conversations) run.convs.push_back(std::move(c));
        disp(e, "nested_archive");
      } else {
        others.push_back(&e);
      }
    }
    if (any_provider_part) {
      for (const Member* m : others) disp(*m, "container_note", Json{{"reason", "member of an export container, not a conversation source"}});
    } else {
      // Which JSON members carry a conversation structure that the legacy
      // handlers do not know (=> an export of an unknown provider)? Decided up
      // front so members are still processed in sorted order.
      std::set<std::string> inference_members;
      for (const Member* m : others) {
        std::string fmt = detect_format(m->abs);
        if (fmt != "json" || m->size > 64'000'000) continue;
        LoadStats st;
        auto doc = load_json_doc(m->abs, st);
        if (doc && !legacy_recognizable(*doc) && generic_structure(*doc)) inference_members.insert(m->rel);
      }
      const bool inference_archive = !inference_members.empty();
      bool inferred_any = false;
      for (const Member* m : others) {
        if (run.env.cancelled()) break;
        std::string fmt = detect_format(m->abs);
        if (fmt == "unknown") {
          disp(*m, "unrecognized");
          continue;
        }
        if (inference_members.count(m->rel)) {
          LoadStats st;
          auto doc = load_json_doc(m->abs, st);
          InferOutcome io;
          if (doc) infer_generic(run.env, *doc, m->rel, file_members, io, rep);
          if (!io.conversations.empty()) {
            inferred_any = true;
            rep.inferred = true;
            rep.counts.add(io.counts);
            for (auto& c : io.conversations) run.convs.push_back(std::move(c));
            disp(*m, "inferred", Json{{"conversations", static_cast<std::int64_t>(io.conversations.size())}});
            continue;
          }
        }
        if (inference_archive && fmt != "json" && fmt != "jsonl") {
          disp(*m, "container_note", Json{{"reason", "member of an inferred export, not a conversation source"}});
          continue;
        }
        ImportOptions mo = opts;
        mo.title = opts.title.has_value() ? opts.title : std::optional<std::string>(m->rel);
        mo.progress = nullptr;
        auto r = import_file_as(m->abs, mo, "zip_member", m->rel);
        if (!r) {
          log::warn(kLog, "Failed to import {} from ZIP: {}", m->rel, r.error().message);
          disp(*m, "import_failed", Json{{"message", r.error().message}});
          rep.errors.push_back(Json{{"member", m->rel}, {"code", "import_failed"}, {"message", r.error().message}});
          rep.partial = true;
          continue;
        }
        for (auto& c : r->conversations) run.convs.push_back(std::move(c));
        if (r->conversations.empty()) {
          disp(*m, "unrecognized");
          if (fmt == "json" || fmt == "jsonl") run.unknown_member(*m);
        } else {
          disp(*m, "imported_legacy", Json{{"format", fmt}, {"conversations", static_cast<std::int64_t>(r->conversations.size())}});
        }
      }
      (void)inferred_any;
      if (rep.inferred) {
        rep.warnings.push_back("provider not recognised: conversations were inferred heuristically from the JSON structure");
      }
    }
    if (rep.inferred || (rep.provider == "generic" && run.convs.empty())) rep.provider = "unknown";
    if (have_conv_files && rep.provider == "unknown" && run.convs.empty()) {
      rep.warnings.push_back("conversation files present but no ChatGPT/Claude structure was recognised");
    }
    if (!any_provider_part && rep.provider == "unknown" && !rep.inferred && run.convs.empty() && !have_conv_files) {
      rep.warnings.push_back("no conversations recognised in the archive");
    }
    if (any_provider_part) {
      // aggregate the parts' counters and choose a common provider label
      std::set<std::string> provs;
      for (const auto& p : rep.parts) {
        const Json& r = p["report"];
        if (r.is_object()) {
          if (const Json* pv = json::find(r, "provider"); pv && pv->is_string()) provs.insert(pv->get<std::string>());
        }
      }
      rep.provider = provs.size() == 1 ? *provs.begin() : "mixed";
      rep.warnings.push_back("container archive: see parts[] for per-archive reports");
    }
  }

  // Aggregate nested parts' counters into this report so totals are complete.
  for (const auto& p : rep.parts) {
    const Json& r = p["report"];
    if (!r.is_object()) continue;
    if (const Json* c = json::find(r, "counts"); c && c->is_object()) {
      Counts pc;
      auto gi = [&](const char* k) { return json::get_int(*c, k, 0); };
      pc.conversation = gi("conversation");
      pc.message = gi("message");
      pc.block = gi("block");
      pc.attachment = gi("attachment");
      pc.citation = gi("citation");
      pc.citation_group = gi("citation_group");
      pc.branch = gi("branch");
      pc.fork_points = gi("fork_points");
      pc.custom_instruction = gi("custom_instruction");
      pc.memory = gi("memory");
      pc.artifact = gi("artifact");
      pc.current_path_messages = gi("current_path_messages");
      pc.account = gi("account");
      pc.feedback = gi("feedback");
      pc.shared_link = gi("shared_link");
      pc.project = gi("project");
      pc.project_doc = gi("project_doc");
      pc.record = gi("record");
      if (const Json* bk = json::find(*c, "block_kind"); bk && bk->is_object()) {
        for (auto it = bk->begin(); it != bk->end(); ++it) pc.block_kind[it.key()] = it.value().get<std::int64_t>();
      }
      rep.counts.add(pc);
    }
  }

  (void)db_.fts_sync();
  report_out = rep.to_json();
  return std::move(run.convs);
}

// ── bare JSON ───────────────────────────────────────────────────────
Result<std::optional<std::vector<Conversation>>> ConversationImporter::export_json_body(const fs::path& path,
                                                                                         const ImportOptions& opts,
                                                                                         Json& report_out) {
  Env env{db_, blobs_, opts, {}};
  env.on_conv = [this](const Conversation& c, const std::string&, int) {
    if (current_source_) record_provenance(c, "export");
  };
  Run run(env);
  std::string provider;
  bool recognized = true;
  const std::string member = path.filename().string();

  Loader L;
  L.element = [&](Json&& el, std::int64_t idx) {
    if (provider.empty()) {
      if (el.is_object() && json::find(el, "mapping")) provider = "openai";
      else if (el.is_object() && json::find(el, "chat_messages")) provider = "anthropic";
      else {
        recognized = false;
        return false;
      }
      run.rep.provider = provider;
    }
    return run.element(std::move(el), member, idx, provider);
  };
  L.bad_element = [&](std::int64_t idx, const std::string& why) {
    if (provider.empty()) return;
    run.rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "invalid_element"}, {"message", why}});
    run.rep.partial = true;
  };
  LoadStats st = load_json_file(path, L);
  if (!recognized || provider.empty()) {
    // Not a provider export: undo nothing (nothing was written before recognition).
    return std::optional<std::vector<Conversation>>{};
  }
  run.note_stats(member, st);
  run.rep.members.push_back(member_json(member, 0, "conversations", Json{{"wrapper", L.wrapper}}));
  (void)db_.fts_sync();
  report_out = run.rep.to_json();
  return std::optional<std::vector<Conversation>>(std::move(run.convs));
}

}  // namespace loom
