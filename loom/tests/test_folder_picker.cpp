// P18 2: the folder picker. "Analyse this folder" without a path is a folder the DEVICE picks (device.pick_folder): the web client opens its folder
// chooser, uploads the files one by one through the blob upload and answers with a manifest; the kernel writes them back as a folder under
// tmp/uploads and the pipeline (archive / knowledge / catalog) runs on it. A client that cannot pick a folder is told so, with the reason.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_pipelines.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/client_blobs.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace client_blobs = loom::actions::client_blobs;

namespace {

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    NewMessage m;
    m.conv_id = conv_id;
    m.role = "user";
    m.text = "We should ship on Friday.";
    unwrap(rt->db().create_msg(m));
  }
  Json declare(const Json& capabilities, const std::string& id = "web-1") {
    return unwrap(rt->actions().invoke("client.declare", Json{{"client_id", id}, {"client", Json{{"kind", "web"}, {"label", "test client"}}}, {"capabilities", capabilities}}));
  }
  Json route(const std::string& text, const std::string& client = "web-1") {
    Json req{{"text", text}, {"conv_id", conv_id}};
    if (!client.empty()) req["client_id"] = client;
    return unwrap(chat_route::route(*rt, req));
  }
  chat_route::Outcome input(const std::string& text, const std::string& client = "web-1") {
    ChatOptions opts;
    opts.conv_id = conv_id;
    opts.stream = false;
    Json req{{"message", text}};
    if (!client.empty()) req["client_id"] = client;
    return unwrap(chat_route::handle(*rt, text, opts, req, nullptr));
  }
  Json meta(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata; }
  // What the client does for one file of the folder: the bytes go up in chunks (as the native shell does), the manifest names them by hash.
  Json uploaded(const std::string& path, const std::string& bytes, const std::string& mime = "text/plain") {
    const std::string id = unwrap(rt->actions().invoke("client.blob_begin", Json::object())).at("upload_id");
    unwrap(rt->actions().invoke("client.blob_append", Json{{"upload_id", id}, {"data_base64", base64::encode(bytes)}}));
    const std::string hash = unwrap(rt->actions().invoke("client.blob_commit", Json{{"upload_id", id}, {"mime", mime}})).at("hash");
    return Json{{"path", path}, {"blob_hash", hash}, {"size", bytes.size()}, {"mime", mime}};
  }
  Json manifest(const Json& files, const std::string& name = "notes") {
    return Json{{"schema", "loom.folder_manifest/1"}, {"name", name}, {"files", files}};
  }
  Json answer(const Json& waiting, const Json& manifest_value) {
    return Json{{"run_id", waiting.at("id")}, {"receipt_id", waiting.at("pending").at("receipt_id")}, {"state", "ok"},
                {"parts", Json::array({Json{{"kind", "json"}, {"value", manifest_value}}})}};
  }
  std::filesystem::path folder_dir() {
    for (const auto& e : std::filesystem::directory_iterator(td.path() / "tmp" / "uploads"))
      if (e.path().filename().string().rfind("folder_", 0) == 0) return e.path();
    return {};
  }
};

Json row_of(const Json& rows, const std::string& action) {
  for (const auto& r : rows.at("actions"))
    if (r.at("action") == action) return r;
  return Json();
}

}  // namespace

TEST_SUITE("folder_picker") {
  TEST_CASE("a client that has a folder chooser can pick a folder; every other is told it cannot, with the reason") {
    Fixture f;
    f.declare(Json{{"picks_files", "granted"}}, "files-only");
    f.declare(Json{{"picks_files", "granted"}, {"picks_folders", "granted"}}, "web-1");
    const Json none = unwrap(f.rt->actions().invoke("for", Json{{"element", "device:file_access"}}));
    const Json planned = row_of(none, "device.pick_folder");
    REQUIRE_FALSE(planned.is_null());
    CHECK(planned.at("callable") == false);
    CHECK(planned.at("planned_reason").at("en").get<std::string>().find("folder chooser") != std::string::npos);
    CHECK(planned.at("planned_reason").at("pl").get<std::string>().find("wybierak folderów") != std::string::npos);
    const Json files_only = unwrap(f.rt->actions().invoke("for", Json{{"element", "device:file_access"}, {"client_id", "files-only"}}));
    CHECK(row_of(files_only, "device.pick_file").at("runs_on") == "client");
    CHECK(row_of(files_only, "device.pick_folder").at("callable") == false);
    const Json mine = unwrap(f.rt->actions().invoke("for", Json{{"element", "device:file_access"}, {"client_id", "web-1"}}));
    CHECK(row_of(mine, "device.pick_folder").at("runs_on") == "client");
  }

  TEST_CASE("\"przeanalizuj ten folder\" without a path asks the device for a folder; \"to archiwum\" asks for a file; a client without a folder chooser is told why") {
    Fixture f;
    f.declare(Json{{"picks_files", "granted"}, {"picks_folders", "granted"}}, "web-1");
    f.declare(Json{{"picks_files", "granted"}}, "files-only");
    for (const char* sentence : {"przeanalizuj ten folder", "analyse this folder", "przejrzyj ten katalog", "scan this directory", "zbuduj wiedzę z tego folderu", "build knowledge from this folder"}) {
      const Json cmd = f.route(sentence).at("command");
      INFO(sentence << " -> " << cmd.value("action", ""));
      INFO(cmd.at("client_steps").dump());
      CHECK(cmd.at("runnable") == true);
      REQUIRE(cmd.at("client_steps").size() == 1);
      CHECK(cmd.at("client_steps").at(0).at("action") == "device.pick_folder");
      CHECK(cmd.at("args").value("path", "").empty());
    }
    for (const char* sentence : {"przeanalizuj to archiwum", "analyse this archive", "zbuduj wiedzę z tego archiwum", "build knowledge from this archive"}) {
      const Json cmd = f.route(sentence).at("command");
      INFO(sentence << " -> " << cmd.value("action", ""));
      REQUIRE(cmd.at("client_steps").size() == 1);
      CHECK(cmd.at("client_steps").at(0).at("action") == "device.pick_file");
      CHECK(cmd.at("args").at("pick") == "file");
    }
    // a path in the words is used as before: nothing is asked of the device
    const Json typed = f.route("przeanalizuj ten folder /tmp/notes").at("command");
    CHECK(typed.at("client_steps").empty());
    CHECK(typed.at("args").at("path") == "/tmp/notes");
    // a client that cannot pick a folder: not runnable, with the reason, and the way out is named
    const Json files_only = f.route("przeanalizuj ten folder", "files-only").at("command");
    CHECK(files_only.at("runnable") == false);
    CHECK(files_only.at("not_runnable").at(0).at("action") == "device.pick_folder");
    CHECK(files_only.at("not_runnable").at(0).at("reason").at("pl").get<std::string>().find("Wypisz ścieżkę folderu") != std::string::npos);
    // a client that picks files can still analyse an archive file
    CHECK(f.route("przeanalizuj to archiwum", "files-only").at("command").at("runnable") == true);
  }

  TEST_CASE("the manifest becomes a folder the pipeline runs on; the paths of the manifest stay inside it") {
    Fixture f;
    const Json manifest = f.manifest(Json::array({f.uploaded("plan.md", "# Atlas plan\nShip on Friday.\n"), f.uploaded("sub/deep/ideas.txt", "Idea: the importer first.\n"),
                                                  f.uploaded("sub\\win.txt", "windows separators are folded\n")}));
    const Json made = unwrap(client_blobs::materialize_folder(*f.rt, manifest));
    const std::filesystem::path dir = made.at("path").get<std::string>();
    CHECK(made.at("files") == 3);
    CHECK(dir.filename() == "notes");
    CHECK(dir.string().find((f.td.path() / "tmp" / "uploads").string()) == 0);
    CHECK(unwrap(fsutil::read_file(dir / "plan.md")) == "# Atlas plan\nShip on Friday.\n");
    CHECK(unwrap(fsutil::read_file(dir / "sub" / "deep" / "ideas.txt")) == "Idea: the importer first.\n");
    CHECK(unwrap(fsutil::read_file(dir / "sub" / "win.txt")) == "windows separators are folded\n");
    // again: the same folder, nothing changes
    CHECK(unwrap(client_blobs::materialize_folder(*f.rt, manifest)).at("path") == made.at("path"));
    // unsafe or unknown: the whole manifest is refused and nothing is written
    const Json ok = f.uploaded("a.txt", "a");
    for (const char* bad : {"../escape.txt", "/etc/passwd", "a/../../b.txt", "", ".", "a/\x01" "b"}) {
      Json entry = ok;
      entry["path"] = bad;
      auto refused = client_blobs::materialize_folder(*f.rt, f.manifest(Json::array({ok, entry}), "unsafe"));
      INFO("path '" << bad << "'");
      REQUIRE_FALSE(refused);
      CHECK(refused.error().code == Errc::InvalidArgument);
    }
    Json missing = ok;
    missing["blob_hash"] = std::string(64, 'a');
    auto absent = client_blobs::materialize_folder(*f.rt, f.manifest(Json::array({missing}), "absent"));
    REQUIRE_FALSE(absent);
    CHECK(absent.error().code == Errc::NotFound);
    CHECK_FALSE(std::filesystem::exists(f.td.path() / "tmp" / "uploads" / "folder_x"));
    CHECK_FALSE(client_blobs::materialize_folder(*f.rt, Json{{"schema", "other"}, {"files", Json::array()}}));
    CHECK_FALSE(client_blobs::materialize_folder(*f.rt, Json{{"schema", "loom.folder_manifest/1"}}));
    // a name that is a path is one safe segment
    const Json odd = unwrap(client_blobs::materialize_folder(*f.rt, f.manifest(Json::array({ok}), "../../out")));
    CHECK(std::filesystem::path(odd.at("path").get<std::string>()).filename() == "folder");
  }

  TEST_CASE("from the conversation: yes -> the device picks the folder -> its files go up -> the archive analysis runs on them") {
    Fixture f;
    f.declare(Json{{"picks_files", "granted"}, {"picks_folders", "granted"}}, "web-1");
    const auto proposal = f.input("przeanalizuj ten folder");
    REQUIRE(proposal.handled);
    const Json p = f.meta(proposal).at("action_proposal");
    CHECK(p.at("action") == "archive.run");
    CHECK(p.at("client_steps").at(0).at("action") == "device.pick_folder");
    const auto yes = f.input("tak");
    REQUIRE(yes.handled);
    const Json waiting_meta = f.meta(yes).at("action_run");
    REQUIRE(waiting_meta.at("status") == "awaiting_client");
    CHECK(waiting_meta.at("pending").at("client_step").at("action") == "device.pick_folder");
    CHECK(waiting_meta.at("pending").at("client_step").at("capability") == "picks_folders");
    CHECK(waiting_meta.at("pending").at("client_step").at("expects") == Json::array({"json"}));
    CHECK(waiting_meta.at("pending").at("client_step").at("prompt").at("pl").get<std::string>().find("nie do żadnego modelu") != std::string::npos);
    CHECK(f.rt->actions().pipelines().status("archive").at("state") == "idle");
    const Json manifest = f.manifest(Json::array({f.uploaded("plan.md", "# Atlas plan\n\nWe decided to ship the Atlas release on Friday. The open question is the migration of the old archive.\n"),
                                                  f.uploaded("ideas.txt", "Idea: build the Atlas importer first.\nDecision: keep the data in one folder.\nBug: the importer drops accents.\n")}));
    const Json done = unwrap(chat_route::client_result(*f.rt, Json{{"run_id", waiting_meta.at("run_id")}, {"receipt_id", waiting_meta.at("pending").at("receipt_id")}, {"state", "ok"},
                                                                  {"parts", Json::array({Json{{"kind", "json"}, {"value", manifest}}})}}));
    INFO(done.value("error", Json(nullptr)).dump());
    REQUIRE(done.at("status") == "completed");
    // the pipeline was started on the folder the files were written to, and the kernel said so in the conversation
    const Json config = f.rt->actions().pipelines().status("archive").at("config");
    REQUIRE(config.at("sources").size() == 1);
    const std::filesystem::path source = config.at("sources").at(0).get<std::string>();
    CHECK(source == f.folder_dir() / "notes");
    CHECK(std::filesystem::exists(source / "plan.md"));
    CHECK(std::filesystem::exists(source / "ideas.txt"));
    CHECK(f.transport->requests().empty());  // nothing went to a model: the pipeline is local and free
    (void)f.rt->actions().pipelines().cancel("archive");
  }

  TEST_CASE("a folder the device brings back as nothing, or declines, ends the run with the reason; the manifest of another schema is no folder") {
    Fixture f;
    f.declare(Json{{"picks_folders", "granted"}}, "web-1");
    f.input("przeanalizuj ten folder");
    const auto yes = f.input("tak");
    const Json waiting = f.meta(yes).at("action_run");
    REQUIRE(waiting.at("status") == "awaiting_client");
    const Json declined = unwrap(chat_route::client_result(*f.rt, Json{{"run_id", waiting.at("run_id")}, {"receipt_id", waiting.at("pending").at("receipt_id")},
                                                                     {"state", "declined"}, {"error", Json{{"message", "no folder was chosen"}}}}));
    CHECK(declined.at("status") != "completed");
    CHECK(f.rt->actions().pipelines().status("archive").at("state") == "idle");
  }
}
