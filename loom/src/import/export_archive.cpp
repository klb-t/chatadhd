// Driver of the lossless provider-export path: ZIP archives (also nested
// containers) and bare JSON files. Interprets OpenAI/Anthropic exports, reports
// everything it could not interpret, never throws on malformed input.
#include <algorithm>
#include <cstring>

#include "export_internal.h"
#include "importer_internal.h"
#include "loom/log.h"
#include "loom/sqlite.h"
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

struct Member {
  std::string rel;
  fs::path abs;
  std::int64_t size = 0;
  std::int64_t archive_index = 0;
  Json materialization = Json::object();
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

std::string pointer_token(std::string_view key) {
  std::string escaped;
  for (char character : key) {
    if (character == '~') escaped += "~0";
    else if (character == '/') escaped += "~1";
    else escaped += character;
  }
  return escaped;
}

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
    rep.include_result_metadata = env.opts.include_result_metadata;
  }

  // Returns false to stop (cancelled).
  bool element(Json&& el, const std::string& member, std::int64_t idx, const std::string& provider_default,
               const Loader& loader) {
    if (env.cancelled()) return false;
    if (!el.is_object()) {
      rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "unrecognized_element"},
                                {"message", "conversation element is not a JSON object"}});
      rep.partial = true;
      return true;
    }
    const bool is_oa = json::find(el, "mapping") != nullptr;
    const bool is_cl = json::find(el, "chat_messages") != nullptr;
    if (!is_oa && !is_cl) {
      rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "unrecognized_element"},
                               {"message", "conversation element has no supported provider structure"}});
      rep.partial = true;
      return true;
    }
    std::string prov = is_oa ? "openai" : (is_cl ? "anthropic" : provider_default);
    ConvModel cm;
    Counts c;
    if (prov == "openai") parse_openai_conversation(el, conv_index, member, oa, cm, c);
    else parse_anthropic_conversation(el, conv_index, member, env, cm, c);

    // Global import/traversal order is not an address in the source member.
    const std::string conversation_pointer = loader.wrapper ? "/conversations/" + std::to_string(idx)
        : loader.top_is_array ? "/" + std::to_string(idx) : "";
    cm.export_meta["source_index"] = idx;
    cm.export_meta["source_container"] = loader.wrapper ? "conversations_wrapper" : loader.top_is_array ? "array" : "object";
    cm.export_meta["json_pointer"] = conversation_pointer;
    if (loader.archive_index) cm.export_meta["archive_index"] = *loader.archive_index;
    if (loader.wrapper) cm.export_meta["wrapper_fields"] = loader.wrapper_fields;
    for (std::size_t message_index = 0; message_index < cm.msgs.size(); ++message_index) {
      auto& message = cm.msgs[message_index];
      auto& metadata = message.export_meta;
      metadata["member"] = member;
      metadata["source_conversation_index"] = idx;
      if (prov == "openai") {
        metadata["source_key"] = message.key;
        metadata["traversal_index"] = message_index;
        metadata["json_pointer"] = conversation_pointer + "/mapping/" + pointer_token(message.key) + "/message";
      } else {
        metadata["json_pointer"] = conversation_pointer + "/chat_messages/" +
            std::to_string(json::get_int(metadata, "source_index", -1));
      }
    }

    auto lock = env.db.lock();
    sql::Txn transaction(env.db.conn());
    auto begin = transaction.begin_status();
    if (!begin) { rep.partial = true; rep.errors.push_back(Json{{"code", "write_failed"}, {"message", begin.error().message}}); return true; }
    auto checkpoint = read_checkpoint(env, member, loader.archive_index.value_or(-1), idx);
    if (!checkpoint) {
      rep.partial = true; rep.errors.push_back(Json{{"code", "checkpoint_failed"}, {"message", checkpoint.error().message}}); return true;
    }
    std::optional<Conversation> prior;
    if (*checkpoint && !(**checkpoint).conversation_id.empty()) {
      auto conversation = env.db.get_conv((**checkpoint).conversation_id);
      if (!conversation) { rep.partial = true; rep.errors.push_back(Json{{"code", "checkpoint_read_failed"}, {"message", conversation.error().message}}); return true; }
      if (*conversation) prior = **conversation;
    }
    Result<Conversation> w = prior ? Result<Conversation>(*prior) : write_conversation(env, cm);
    if (!w) {
      rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "write_failed"}, {"message", w.error().message}});
      rep.partial = true;
      return true;
    }
    if (prior) {
      ++rep.resumed_conversations;
    } else {
      if (env.on_conv) {
        auto recorded = env.on_conv(*w, member, conv_index);
        if (!recorded) { rep.partial = true; rep.errors.push_back(Json{{"code", "provenance_failed"}, {"message", recorded.error().message}}); return true; }
      }
      auto saved = write_checkpoint(env, member, loader.archive_index.value_or(-1), idx, w->id, Json{{"kind", "conversation"}});
      if (!saved) { rep.partial = true; rep.errors.push_back(Json{{"code", "checkpoint_failed"}, {"message", saved.error().message}}); return true; }
    }
    auto committed = transaction.commit();
    if (!committed) { rep.partial = true; rep.errors.push_back(Json{{"code", "commit_failed"}, {"message", committed.error().message}}); return true; }
    lock.unlock();
    rep.counts.add(c);
    rep.json_leaves += cm.leaves_total;
    rep.leaves_preserved += cm.leaves_kept;
    if (!cm.key.empty()) oa.conv_db_id[cm.key] = w->id;
    Conversation returned = *w;
    if (!env.opts.include_result_metadata) returned.metadata = Json::object();
    convs.push_back(std::move(returned));
    if (env.opts.progress) env.opts.progress(static_cast<std::int64_t>(convs.size()), -1, "export");
    ++conv_index;
    return true;
  }

  // Provider auxiliary files (accounts, feedback, projects, memories, etc.)
  // commit together with their member journal. Report deltas and project IDs
  // are replayed so later auxiliary records retain the same bindings.
  Result<bool> checkpoint_member(const Member& member, const std::function<Result<bool>()>& handle) {
    auto lock = env.db.lock();
    sql::Txn transaction(env.db.conn());
    LOOM_TRY(transaction.begin_status());
    LOOM_TRY_ASSIGN(auto prior, read_checkpoint(env, member.rel, member.archive_index, -1));
    if (prior) {
      const Json& delta = prior->metadata["report"];
      rep.counts.add(Counts::from_json(delta["counts"]));
      for (const auto& error : delta["errors"]) rep.errors.push_back(error);
      for (const auto& warning : delta["warnings"]) rep.warnings.push_back(warning.get<std::string>());
      for (const auto& unknown : delta["unknown_members"]) rep.unknown_members.push_back(unknown.get<std::string>());
      rep.repairs.update(delta["repairs"]);
      rep.partial = rep.partial || delta.value("partial", false);
      if (const Json* projects = json::find(prior->metadata, "projects"); projects && projects->is_object())
        for (auto project = projects->begin(); project != projects->end(); ++project)
          an.project_db_id[project.key()] = project.value().get<std::string>();
      ++rep.resumed_members;
      LOOM_TRY(transaction.commit());
      return true;
    }
    const auto old_counts = rep.counts.to_json();
    const auto old_errors = rep.errors.size(), old_warnings = rep.warnings.size(), old_unknown = rep.unknown_members.size();
    const auto old_repairs = rep.repairs;
    const auto old_projects = an.project_db_id;
    env.write_error.reset();
    auto handled = handle();
    if (!handled || env.write_error || rep.errors.size() != old_errors) {
      rep.counts = Counts::from_json(old_counts); an.project_db_id = old_projects;
      return !handled ? handled.error() : env.write_error ? *env.write_error : Error(Errc::Parse, "member interpretation incomplete");
    }
    if (!*handled) return false;
    auto counts = rep.counts.to_json();
    for (auto count = counts.begin(); count != counts.end(); ++count) {
      if (count.value().is_object()) {
        for (auto kind = count.value().begin(); kind != count.value().end(); ++kind)
          kind.value() = kind.value().get<std::int64_t>() - json::get_int(old_counts[count.key()], kind.key());
      } else count.value() = count.value().get<std::int64_t>() - old_counts[count.key()].get<std::int64_t>();
    }
    Json errors = Json::array(), warnings = Json::array(), unknown = Json::array(), repairs = Json::object();
    for (auto i = old_errors; i < rep.errors.size(); ++i) errors.push_back(rep.errors[i]);
    for (auto i = old_warnings; i < rep.warnings.size(); ++i) warnings.push_back(rep.warnings[i]);
    for (auto i = old_unknown; i < rep.unknown_members.size(); ++i) unknown.push_back(rep.unknown_members[i]);
    for (auto repair = rep.repairs.begin(); repair != rep.repairs.end(); ++repair)
      if (!old_repairs.contains(repair.key()) || old_repairs[repair.key()] != repair.value()) repairs[repair.key()] = repair.value();
    Json delta{{"counts", counts}, {"errors", errors}, {"warnings", warnings}, {"unknown_members", unknown},
               {"repairs", repairs}, {"partial", !errors.empty()}};
    LOOM_TRY(write_checkpoint(env, member.rel, member.archive_index, -1, "",
                             Json{{"kind", "member"}, {"report", delta}, {"projects", an.project_db_id}}));
    LOOM_TRY(transaction.commit());
    lock.unlock();
    if (env.opts.progress) env.opts.progress(static_cast<std::int64_t>(rep.members.size()), -1, "export.member");
    return true;
  }

  void note_stats(const std::string& member, const LoadStats& st) {
    rep.largest_json_value_bytes = std::max(rep.largest_json_value_bytes, st.largest_value_bytes);
    rep.scanner_buffer_bytes = std::max(rep.scanner_buffer_bytes, st.scanner_buffer_bytes);
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

  Status unknown_member(const Member& m) {
    rep.unknown_members.push_back(m.rel);
    Json md = Json::object();
    Json ex = Json::object();
    ex["member"] = m.rel;
    ex["size"] = m.size;
    LOOM_TRY_ASSIGN(auto hash, sha256_file_hex(m.abs));
    ex["sha256"] = hash;
    if (env.blobs) {
      LOOM_TRY_ASSIGN(auto blob, env.blobs->put_file(m.abs, ""));
      ex["blob_hash"] = blob.hash;
    }
    std::string content;
    if (m.size <= env.opts.json_inline_threshold_bytes) {
      if (auto raw = fsutil::read_file(m.abs); raw && utf8::is_valid(*raw)) content = *raw;
    }
    md["export"] = ex;
    LOOM_TRY(write_entity(env, "export:member", m.rel, content, md));
    return {};
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

  Env env{db_, blobs_, opts, {}, current_source_ ? current_source_->source_id : ""};
  env.on_conv = [this](const Conversation& c, const std::string& member, int index) {
    if (!current_source_) return Status{};
    current_source_->conv_index = index;
    auto saved = current_source_->zip_member;
    current_source_->zip_member = saved ? (*saved + "!" + member) : member;
    auto result = record_provenance(c, "export");
    current_source_->zip_member = saved;
    return result;
  };
  Run run(env);
  Report& rep = run.rep;

  std::vector<Member> entries;
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    if (run.env.cancelled()) { rep.partial = true; break; }
    if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st)) {
      rep.errors.push_back(Json{{"archive_index", i}, {"code", "member_stat_failed"}, {"message", "could not read archive entry metadata"}});
      rep.partial = true;
      continue;
    }
    std::string name = st.m_filename;
    fs::path relp(name);
    if (!safe_relpath(relp)) {
      log::warn(kLog, "zip: skipping unsafe entry path {}", name);
      rep.members.push_back(member_json(name, static_cast<std::int64_t>(st.m_uncomp_size), "skipped_unsafe_path"));
      rep.warnings.push_back("skipped unsafe archive path: " + name);
      rep.partial = true;
      continue;
    }
    // Each archive entry owns its extracted bytes, including duplicate paths.
    fs::path dest = td.path() / std::to_string(i) / relp;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (!mz_zip_reader_extract_to_file(&zip, i, dest.string().c_str(), 0)) {
      rep.members.push_back(member_json(name, static_cast<std::int64_t>(st.m_uncomp_size), "extract_failed"));
      rep.errors.push_back(Json{{"member", name}, {"code", "extract_failed"}, {"message", "could not extract (corrupt entry?)"}});
      rep.partial = true;
      continue;
    }
    auto materialized = materialize_zip_member(dest, opts, name, i);
    Json identity = Json::object();
    if (materialized) identity = *materialized;
    else {
      rep.errors.push_back(Json{{"member", name}, {"archive_index", i}, {"code", "materialization_failed"},
                                {"message", materialized.error().message}});
      rep.partial = true;
      identity["materialization_error"] = materialized.error().message;
    }
    entries.push_back(Member{name, dest, static_cast<std::int64_t>(st.m_uncomp_size), i, std::move(identity)});
    if (opts.progress) opts.progress(i + 1, n, "zip.materialize");
  }
  mz_zip_reader_end(&zip);
  std::stable_sort(entries.begin(), entries.end(), [](const Member& a, const Member& b) { return a.rel < b.rel; });

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
    extra.update(m.materialization);
    extra["archive_index"] = m.archive_index;
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
      L.read_chunk_bytes = opts.json_read_chunk_bytes; L.max_depth = opts.json_max_depth;
      L.cancelled = [&] { return run.env.cancelled(); };
      L.element = [&](Json&& el, std::int64_t) {
        if (el.is_object()) {
          if (json::find(el, "mapping")) provider = "openai";
          else if (json::find(el, "chat_messages")) provider = "anthropic";
        }
        return provider == "unknown";
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
      L.read_chunk_bytes = opts.json_read_chunk_bytes; L.max_depth = opts.json_max_depth;
      L.cancelled = [&] { return run.env.cancelled(); };
      L.archive_index = e.archive_index;
      const std::string member = e.rel;
      L.element = [&](Json&& el, std::int64_t idx) { return run.element(std::move(el), member, idx, provider, L); };
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
      if (run.env.cancelled()) break;
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
        auto interpreted = run.checkpoint_member(*m, [&]() -> Result<bool> {
          return provider == "openai" ? import_openai_member(run.env, run.oa, sub, m->abs, run.rep)
                                       : import_anthropic_member(run.env, run.an, sub, m->abs, run.rep);
        });
        if (!interpreted) {
          rep.errors.push_back(Json{{"member", m->rel}, {"code", "member_checkpoint_failed"}, {"message", interpreted.error().message}});
          rep.partial = true;
        } else handled = *interpreted;
      }
      if (handled) {
        disp(*m, "record");
        continue;
      }
      if (detect_format(m->abs) == "zip" && ends_with(m->rel, ".zip")) {
        ImportOptions mo = opts;
        mo.title = std::nullopt;
        mo.progress = nullptr;
        auto r = import_file_as(m->abs, mo, "zip_member", m->rel, m->archive_index);
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
      auto kept = run.checkpoint_member(*m, [&]() -> Result<bool> { LOOM_TRY(run.unknown_member(*m)); return true; });
      if (!kept) { rep.partial = true; rep.errors.push_back(Json{{"code", "member_checkpoint_failed"}, {"message", kept.error().message}}); }
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
      if (run.env.cancelled()) break;
      if (ends_with(e.rel, ".zip") && detect_format(e.abs) == "zip") {
        ImportOptions mo = opts;
        mo.title = opts.title.has_value() ? opts.title : std::optional<std::string>(e.rel);
        mo.progress = nullptr;
        auto r = import_file_as(e.abs, mo, "zip_member", e.rel, e.archive_index);
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
        if (fmt != "json" || (opts.generic_inference_max_bytes && m->size > opts.generic_inference_max_bytes)) continue;
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
        auto r = import_file_as(m->abs, mo, "zip_member", m->rel, m->archive_index);
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
          if (fmt == "json" || fmt == "jsonl") { auto kept = run.unknown_member(*m); if (!kept) rep.partial = true; }
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
    if (r.value("partial", false)) rep.partial = true;
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
  if (run.env.cancelled()) rep.partial = true;
  std::set<std::int64_t> reported_indices;
  for (const auto& member : rep.members) reported_indices.insert(json::get_int(member, "archive_index", -1));
  for (const auto& entry : entries) {
    if (!reported_indices.count(entry.archive_index))
      disp(entry, run.env.cancelled() ? "not_interpreted_cancelled" : "not_interpreted");
  }
  report_out = rep.to_json();
  return std::move(run.convs);
}

// ── bare JSON ───────────────────────────────────────────────────────
Result<std::optional<std::vector<Conversation>>> ConversationImporter::export_json_body(const fs::path& path,
                                                                                         const ImportOptions& opts,
                                                                                         Json& report_out) {
  Env env{db_, blobs_, opts, {}, current_source_ ? current_source_->source_id : ""};
  env.on_conv = [this](const Conversation& c, const std::string&, int index) {
    if (current_source_) { current_source_->conv_index = index; return record_provenance(c, "export"); }
    return Status{};
  };
  Run run(env);
  std::string provider;
  bool recognized = true;
  bool bad_element_parse = false;
  const std::string member = current_source_ && !current_source_->source_filename.empty()
      ? current_source_->source_filename : path.filename().string();

  Loader L;
  L.read_chunk_bytes = opts.json_read_chunk_bytes; L.max_depth = opts.json_max_depth;
  L.cancelled = [&] { return run.env.cancelled(); };
  L.element = [&](Json&& el, std::int64_t idx) {
    if (provider.empty()) {
      if (el.is_object() && json::find(el, "mapping")) provider = "openai";
      else if (el.is_object() && json::find(el, "chat_messages")) provider = "anthropic";
      else {
        if (L.top_is_array) {
          run.rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "unrecognized_element"},
                                       {"message", "element before provider recognition"}});
          run.rep.partial = true;
          return true;
        }
        recognized = false;
        return false;
      }
      run.rep.provider = provider;
    }
    return run.element(std::move(el), member, idx, provider, L);
  };
  L.bad_element = [&](std::int64_t idx, const std::string& why) {
    // Recognition may happen after a malformed array element. Retain earlier
    // errors if a later element establishes this as a provider export; the
    // report is still discarded when the whole path falls back to legacy.
    // A syntactically valid value beyond the caller's depth preset remains a
    // capability diagnostic; malformed JSON must not enter legacy fallback.
    if (why == "invalid JSON") bad_element_parse = true;
    run.rep.errors.push_back(Json{{"member", member}, {"index", idx}, {"code", "invalid_element"}, {"message", why}});
    run.rep.partial = true;
  };
  LoadStats st = load_json_file(path, L);
  if (st.cancelled) {
    run.rep.partial = true;
    report_out = run.rep.to_json();
    return std::optional<std::vector<Conversation>>(std::move(run.convs));
  }
  if (!recognized || provider.empty()) {
    if (provider.empty() && (bad_element_parse || st.invalid || st.truncated)) {
      run.note_stats(member, st);
      run.rep.partial = true; report_out = run.rep.to_json();
      return Error(Errc::Parse, st.message.empty() ? "invalid or truncated JSON" : st.message);
    }
    if (st.invalid || st.too_deep || st.truncated) {
      run.note_stats(member, st);
      run.rep.partial = true; report_out = run.rep.to_json();
      // A malformed standalone value has no stream of committed elements to
      // retain. Preserve the structured parse error of the bare-file API.
      if (!L.top_is_array && !L.wrapper)
        return Error(Errc::Parse, st.message.empty() ? "invalid or truncated JSON" : st.message);
      return std::optional<std::vector<Conversation>>(std::move(run.convs));
    }
    // No provider evidence: retain generic legacy interpretation for valid input.
    return std::optional<std::vector<Conversation>>{};
  }
  run.note_stats(member, st);
  run.rep.members.push_back(member_json(member, 0, "conversations", Json{{"wrapper", L.wrapper}}));
  (void)db_.fts_sync();
  report_out = run.rep.to_json();
  return std::optional<std::vector<Conversation>>(std::move(run.convs));
}

}  // namespace loom
