// Unit tests for loom/github_sync.h: fnmatch, GitHubSync (via
// ScriptedTransport) and GitHubSyncManager persistence.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <map>

#include "loom/config.h"
#include "loom/github_sync.h"
#include "loom/net/http.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
void write_file(const std::filesystem::path& p, std::string_view content) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << content;
}
}  // namespace

TEST_SUITE("github_sync") {
  TEST_CASE("profile injection changes REST metadata and commit templates") {
    auto builtin = unwrap(RuntimeProfile::builtin("github"));
    auto profile = unwrap(builtin.with_overrides(Json{{"base_url", "https://offline.invalid"},
      {"headers", {{"User-Agent", "custom-agent"}, {"X-Profile", "test"}}},
      {"authorization_template", "Bearer {{token}}"}, {"connection_timeout_ms", 6},
      {"request_timeout_ms", 8}, {"commit_message_template", "Publish {{path}}"}}));
    fsutil::TempDir td;
    write_file(td.path() / "a.md", "content");
    SyncConfig cfg;
    cfg.repo = "owner/repo";
    cfg.local_path = td.path().string();
    cfg.token = "fake-token";
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json::object()));
    GitHubSync sync(cfg, http, profile);
    CHECK(sync.test_connection());
    auto req = http.requests().back();
    CHECK(req.url == "https://offline.invalid/repos/owner/repo");
    CHECK(req.timeout_ms == 6);
    CHECK(net::header_value(req.headers, "Authorization") == "Bearer fake-token");
    CHECK(net::header_value(req.headers, "User-Agent") == "custom-agent");
    CHECK(net::header_value(req.headers, "X-Profile") == "test");
    GitHubFile file;
    file.path = "a.md";
    file.sha = "existing";
    file.local_path = (td.path() / "a.md").string();
    LOOM_REQUIRE_OK(sync.push_file(file));
    req = http.requests().back();
    CHECK(req.timeout_ms == 8);
    CHECK(req.url == "https://offline.invalid/repos/owner/repo/contents/a.md");
    CHECK(unwrap(json::parse(req.body))["message"] == "Publish a.md");
    CHECK(unwrap(sync.runtime_profile())["hash"] == profile.hash());

    auto removed = unwrap(profile.with_patch(Json::array({
      Json{{"op", "remove"}, {"path", "/headers/User-Agent"}}})));
    GitHubSync without_agent(cfg, http, removed);
    CHECK(without_agent.test_connection());
    CHECK(net::header_value(http.requests().back().headers, "User-Agent").empty());
    Json forged_definition = builtin.definition();
    forged_definition["value_schema"] = Json{{"type", "object"}};
    forged_definition["defaults"]["request_timeout_ms"] = "invalid";
    GitHubSync invalid(cfg, http, unwrap(RuntimeProfile::from_definition(forged_definition)));
    const auto before = http.requests().size();
    CHECK_FALSE(invalid.runtime_profile());
    CHECK_FALSE(invalid.list_remote_files());
    CHECK(http.requests().size() == before);
  }

  TEST_CASE("manager overlay controls defaults and secret references; explicit values win") {
    fsutil::TempDir td;
    Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "github"},
      {"overrides", {{"branch", "work"}, {"sync_direction", "pull_only"}, {"secret_key", "local_github_key"},
                     {"include_patterns", Json::array({"*.cpp"})}, {"exclude_patterns", Json::array()}}}};
    write_file(td.path() / "profiles/github.pack", overlay.dump());
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("local_github_key", "fake-token");
    net::ScriptedTransport http;
    GitHubSyncManager mgr(td.path() / "github_sync.json", secrets, http);
    auto cfg = unwrap(mgr.add_config("custom", "owner/repo", td.path().string()));
    CHECK(cfg.branch == "work");
    CHECK(cfg.sync_direction == "pull_only");
    CHECK(cfg.include_patterns == std::vector<std::string>{"*.cpp"});
    CHECK(cfg.exclude_patterns.empty());
    CHECK(cfg.token == "fake-token");
    CHECK(mgr.to_json().dump().find("fake-token") == std::string::npos);
    auto explicit_cfg = unwrap(mgr.add_config("explicit", "owner/repo", td.path().string(), "", "push_only"));
    CHECK(explicit_cfg.branch.empty());
    CHECK(explicit_cfg.sync_direction == "push_only");
    auto sync = mgr.get_syncer("custom");
    REQUIRE(sync);
    CHECK(sync->should_include("x.cpp"));
    CHECK_FALSE(sync->should_include("x.py"));
    CHECK(unwrap(mgr.runtime_profile())["is_builtin"] == false);
    write_file(td.path() / "profiles/github.pack", "invalid");
    GitHubSyncManager invalid(td.path() / "github_sync.json", secrets, http);
    CHECK_FALSE(invalid.runtime_profile());
    CHECK_FALSE(invalid.add_config("new", "owner/repo", "/tmp"));
    CHECK(http.requests().empty());
  }

  TEST_CASE("fnmatch: literal, star, question mark, char classes") {
    CHECK(fnmatch("readme.md", "*.md"));
    CHECK_FALSE(fnmatch("readme.py", "*.md"));
    CHECK(fnmatch("__pycache__/x.pyc", "__pycache__/*"));
    CHECK(fnmatch(".git/HEAD", ".git/*"));
    CHECK(fnmatch("a/b/c", "*"));  // '*' also matches '/'
    CHECK(fnmatch("abc", "a?c"));
    CHECK_FALSE(fnmatch("ac", "a?c"));
    CHECK(fnmatch("a1", "a[0-9]"));
    CHECK_FALSE(fnmatch("ax", "a[0-9]"));
    CHECK(fnmatch("ax", "a[!0-9]"));
    CHECK_FALSE(fnmatch("a1", "a[!0-9]"));
    CHECK(fnmatch("a.py", "a.py"));
    CHECK_FALSE(fnmatch("axpy", "a.py"));  // '.' is literal, not "any char"
    CHECK(fnmatch("main.cpp", "*.cpp"));
    CHECK_FALSE(fnmatch("main.cpp.bak", "*.cpp"));
  }

  TEST_CASE("SyncConfig JSON round-trip (token never serialised)") {
    SyncConfig cfg;
    cfg.repo = "owner/repo";
    cfg.branch = "dev";
    cfg.local_path = "/tmp/x";
    cfg.token = "secret-token";
    cfg.sync_direction = "push_only";
    cfg.auto_sync = true;
    cfg.include_patterns = {"*.md"};
    cfg.exclude_patterns = {".git/*"};

    Json j = cfg.to_json();
    CHECK_FALSE(j.contains("token"));
    SyncConfig back = unwrap(SyncConfig::from_json(j));
    CHECK(back.repo == "owner/repo");
    CHECK(back.branch == "dev");
    CHECK(back.local_path == "/tmp/x");
    CHECK(back.token.empty());
    CHECK(back.sync_direction == "push_only");
    CHECK(back.auto_sync == true);
    CHECK(back.include_patterns == std::vector<std::string>{"*.md"});
    CHECK(back.exclude_patterns == std::vector<std::string>{".git/*"});
  }

  TEST_CASE("should_include: excludes win, empty include list means include all") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.include_patterns = {};
    cfg.exclude_patterns = {"*.pyc", "__pycache__/*"};
    net::ScriptedTransport http;
    GitHubSync sync(cfg, http);
    CHECK(sync.should_include("readme.md"));
    CHECK_FALSE(sync.should_include("x.pyc"));
    CHECK_FALSE(sync.should_include("__pycache__/x.py"));

    cfg.include_patterns = {"*.py"};
    GitHubSync sync2(cfg, http);
    CHECK(sync2.should_include("a.py"));
    CHECK_FALSE(sync2.should_include("a.md"));
    CHECK_FALSE(sync2.should_include("__pycache__/a.py"));  // excluded wins even if it also matches include
  }

  TEST_CASE("test_connection: 200 -> true, other status or transport failure -> false") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    net::ScriptedTransport http;
    http.expect("GET", "https://api.github.com/repos/o/r", net::ScriptedTransport::Reply::json(200, Json{{"id", 1}}));
    GitHubSync sync(cfg, http);
    CHECK(sync.test_connection());

    auto reqs = http.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(net::header_value(reqs[0].headers, "Accept") == "application/vnd.github.v3+json");
    CHECK(net::header_value(reqs[0].headers, "User-Agent") == "ChatADHD-Sync");
    CHECK(net::header_value(reqs[0].headers, "Authorization").empty());  // no token configured

    net::ScriptedTransport http2;
    http2.set_fallback(net::ScriptedTransport::Reply::json(404, Json{{"message", "not found"}}));
    GitHubSync sync2(cfg, http2);
    CHECK_FALSE(sync2.test_connection());
  }

  TEST_CASE("Authorization header is 'token <token>' when a token is configured") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.token = "ghp_secret";
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(200, Json{{"id", 1}}));
    GitHubSync sync(cfg, http);
    sync.test_connection();
    CHECK(net::header_value(http.requests()[0].headers, "Authorization") == "token ghp_secret");
  }

  TEST_CASE("list_remote_files: recurses into directories, filters by should_include, uses download_url") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.branch = "main";
    cfg.include_patterns = {"*.py", "*.md"};
    cfg.exclude_patterns = {};
    net::ScriptedTransport http;
    http.expect("GET", "https://api.github.com/repos/o/r/contents/",
               net::ScriptedTransport::Reply::json(200, Json::array({
                                                            Json{{"type", "file"}, {"path", "a.py"}, {"sha", "sha1"}, {"size", 10},
                                                                 {"download_url", "https://raw/a.py"}},
                                                            Json{{"type", "file"}, {"path", "b.txt"}, {"sha", "sha2"}, {"size", 5}},
                                                            Json{{"type", "dir"}, {"path", "sub"}},
                                                        })));
    http.expect("GET", "https://api.github.com/repos/o/r/contents/sub",
               net::ScriptedTransport::Reply::json(200, Json::array({
                                                            Json{{"type", "file"}, {"path", "sub/c.md"}, {"sha", "sha3"}, {"size", 3}},
                                                        })));
    GitHubSync sync(cfg, http);
    std::vector<GitHubFile> files = unwrap(sync.list_remote_files());
    REQUIRE(files.size() == 2);
    CHECK(files[0].path == "a.py");
    CHECK(files[0].url == "https://raw/a.py");
    CHECK(files[1].path == "sub/c.md");

    auto reqs = http.requests();
    REQUIRE(reqs.size() == 2);
    CHECK(reqs[0].url == "https://api.github.com/repos/o/r/contents/?ref=main");
    CHECK(reqs[1].url == "https://api.github.com/repos/o/r/contents/sub?ref=main");
  }

  TEST_CASE("list_remote_files: request failure logs and returns partial results, not an error") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "connection reset"));
    GitHubSync sync(cfg, http);
    auto r = sync.list_remote_files();
    LOOM_REQUIRE_OK(r);
    CHECK(r->empty());
  }

  TEST_CASE("list_local_files: recursive walk, relative posix paths, respects should_include") {
    fsutil::TempDir td;
    write_file(td.path() / "a.py", "print(1)");
    write_file(td.path() / "sub" / "b.md", "# hi");
    write_file(td.path() / "sub" / "c.pyc", "junk");
    write_file(td.path() / "__pycache__" / "d.pyc", "junk");

    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.local_path = td.path().string();
    net::ScriptedTransport http;
    GitHubSync sync(cfg, http);
    std::vector<GitHubFile> files = sync.list_local_files();
    std::vector<std::string> paths;
    for (auto& f : files) paths.push_back(f.path);
    std::sort(paths.begin(), paths.end());
    CHECK(paths == std::vector<std::string>{"a.py", "sub/b.md"});
  }

  TEST_CASE("list_local_files: missing directory returns empty, no error") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.local_path = "/nonexistent/does/not/exist";
    net::ScriptedTransport http;
    GitHubSync sync(cfg, http);
    CHECK(sync.list_local_files().empty());
  }

  TEST_CASE("default push omits root and nested secrets; explicit patterns remain authoritative") {
    fsutil::TempDir td;
    const std::string root_secret = R"({"api_key":"fixture-root-only"})";
    const std::string nested_secret = R"({"api_key":"fixture-nested-only"})";
    const std::string notes = R"({"note":"shareable fixture"})";
    write_file(td.path() / "secrets.json", root_secret);
    write_file(td.path() / "nested" / "deeper" / "secrets.json", nested_secret);
    write_file(td.path() / "notes.json", notes);

    for (bool explicit_override : {false, true}) {
      CAPTURE(explicit_override);
      Json settings{{"repo", "o/r"}, {"local_path", td.path().string()}};
      if (explicit_override) settings["exclude_patterns"] = Json::array();
      // Missing patterns use the new preset; persisted explicit patterns do not.
      auto cfg = unwrap(SyncConfig::from_json(settings));
      cfg = unwrap(SyncConfig::from_json(cfg.to_json()));
      net::ScriptedTransport http;
      http.expect("GET", "https://api.github.com/repos/o/r/contents/",
                  net::ScriptedTransport::Reply::json(200, Json::array()));
      http.expect("PUT", "https://api.github.com/repos/o/r/contents/notes.json",
                  net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));
      if (explicit_override) {
        http.expect("PUT", "https://api.github.com/repos/o/r/contents/secrets.json",
                    net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));
        http.expect("PUT", "https://api.github.com/repos/o/r/contents/nested/deeper/secrets.json",
                    net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));
      }
      GitHubSync sync(cfg, http);
      auto outcome = unwrap(sync.push_all());
      CHECK(outcome["success"] == (explicit_override ? 3 : 1));
      CHECK(outcome["failed"] == 0);
      auto requests = http.requests();
      REQUIRE(requests.size() == (explicit_override ? 4 : 2));
      std::map<std::string, std::string> uploaded;
      for (const auto& request : requests) {
        if (request.method != "PUT") continue;
        auto body = unwrap(json::parse(request.body));
        uploaded[request.url] = unwrap(base64::decode(body["content"].get<std::string>()));
      }
      CHECK(uploaded.at("https://api.github.com/repos/o/r/contents/notes.json") == notes);
      if (explicit_override) {
        CHECK(uploaded.at("https://api.github.com/repos/o/r/contents/secrets.json") == root_secret);
        CHECK(uploaded.at("https://api.github.com/repos/o/r/contents/nested/deeper/secrets.json") == nested_secret);
      } else {
        for (const auto& [url, content] : uploaded) {
          CHECK(url.find("secrets.json") == std::string::npos);
          CHECK(content.find("fixture-root-only") == std::string::npos);
          CHECK(content.find("fixture-nested-only") == std::string::npos);
        }
      }
      // Selection never edits the original credential files.
      CHECK(unwrap(fsutil::read_file(td.path() / "secrets.json")) == root_secret);
      CHECK(unwrap(fsutil::read_file(td.path() / "nested" / "deeper" / "secrets.json")) == nested_secret);
    }
  }

  TEST_CASE("get_sync_status: synced / modified / new_remote / new_local") {
    fsutil::TempDir td;
    write_file(td.path() / "same.py", "1234567890");         // 10 bytes, matches remote size
    write_file(td.path() / "changed.py", "short");           // 5 bytes, remote says different size
    write_file(td.path() / "local_only.py", "x");

    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.local_path = td.path().string();
    cfg.include_patterns = {"*.py"};
    cfg.exclude_patterns = {};
    net::ScriptedTransport http;
    http.expect("GET", "https://api.github.com/repos/o/r/contents/",
               net::ScriptedTransport::Reply::json(
                   200, Json::array({Json{{"type", "file"}, {"path", "same.py"}, {"sha", "s1"}, {"size", 10}},
                                     Json{{"type", "file"}, {"path", "changed.py"}, {"sha", "s2"}, {"size", 999}},
                                     Json{{"type", "file"}, {"path", "remote_only.py"}, {"sha", "s3"}, {"size", 1},
                                          {"download_url", "https://raw/remote_only.py"}}})));
    GitHubSync sync(cfg, http);
    std::vector<GitHubFile> status = unwrap(sync.get_sync_status());
    std::map<std::string, std::string> by_path;
    for (auto& f : status) by_path[f.path] = f.status;
    CHECK(by_path["same.py"] == "synced");
    CHECK(by_path["changed.py"] == "modified");
    CHECK(by_path["remote_only.py"] == "new_remote");
    CHECK(by_path["local_only.py"] == "new_local");
    // sorted by path
    std::vector<std::string> order;
    for (auto& f : status) order.push_back(f.path);
    CHECK(std::is_sorted(order.begin(), order.end()));
  }

  TEST_CASE("fetch_file_content: prefers download_url, falls back to contents API with base64 decode") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.branch = "main";
    net::ScriptedTransport http;
    http.expect("GET", "https://raw.example/x.py", net::ScriptedTransport::Reply::text(200, "print('hi')"));
    GitHubFile f;
    f.path = "x.py";
    f.url = "https://raw.example/x.py";
    GitHubSync sync(cfg, http);
    CHECK(unwrap(sync.fetch_file_content(f)) == "print('hi')");

    net::ScriptedTransport http2;
    http2.expect("GET", "https://api.github.com/repos/o/r/contents/y.py",
                net::ScriptedTransport::Reply::json(200, Json{{"encoding", "base64"}, {"content", base64::encode("content here")}}));
    GitHubFile g;
    g.path = "y.py";
    GitHubSync sync2(cfg, http2);
    CHECK(unwrap(sync2.fetch_file_content(g)) == "content here");
  }

  TEST_CASE("fetch_file_content: failure returns empty string, not an error") {
    SyncConfig cfg;
    cfg.repo = "o/r";
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "boom"));
    GitHubFile f;
    f.path = "x.py";
    f.url = "https://raw.example/x.py";
    GitHubSync sync(cfg, http);
    CHECK(unwrap(sync.fetch_file_content(f)).empty());
  }

  TEST_CASE("pull_file: refused in push_only mode; writes local file otherwise") {
    fsutil::TempDir td;
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.local_path = td.path().string();
    cfg.sync_direction = "push_only";
    net::ScriptedTransport http;
    GitHubFile f;
    f.path = "a.md";
    f.url = "https://raw/a.md";
    GitHubSync sync_push_only(cfg, http);
    auto r = sync_push_only.pull_file(f);
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Unsupported);
    CHECK_FALSE(std::filesystem::exists(td.path() / "a.md"));

    cfg.sync_direction = "bidirectional";
    net::ScriptedTransport http2;
    http2.expect("GET", "https://raw/a.md", net::ScriptedTransport::Reply::text(200, "# hello"));
    GitHubSync sync(cfg, http2);
    LOOM_REQUIRE_OK(sync.pull_file(f));
    // fsutil::read_file(), not std::ifstream + istreambuf_iterator: the
    // latter trips a GCC -O3 -Wnull-dereference false positive inside
    // libstdc++'s istreambuf_iterator (-Werror in the `release` preset).
    auto content = fsutil::read_file(td.path() / "a.md");
    REQUIRE(content);
    CHECK(*content == "# hello");
  }

  TEST_CASE("push_file: refused in pull_only mode; PUTs base64 content with sha when updating") {
    fsutil::TempDir td;
    write_file(td.path() / "a.md", "# content");

    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.branch = "main";
    cfg.sync_direction = "pull_only";
    net::ScriptedTransport http;
    GitHubFile f;
    f.path = "a.md";
    f.local_path = (td.path() / "a.md").string();
    f.sha = "oldsha";
    GitHubSync sync_pull_only(cfg, http);
    auto r = sync_pull_only.push_file(f);
    CHECK_FALSE(r);
    CHECK(r.error().code == Errc::Unsupported);

    cfg.sync_direction = "bidirectional";
    net::ScriptedTransport http2;
    http2.expect("PUT", "https://api.github.com/repos/o/r/contents/a.md", net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));
    GitHubSync sync(cfg, http2);
    LOOM_REQUIRE_OK(sync.push_file(f, std::string("custom message")));

    auto reqs = http2.requests();
    REQUIRE(reqs.size() == 1);
    Json body = unwrap(json::parse(reqs[0].body));
    CHECK(body["message"] == "custom message");
    CHECK(body["branch"] == "main");
    CHECK(body["sha"] == "oldsha");
    CHECK(unwrap(base64::decode(body["content"].get<std::string>())) == "# content");
  }

  TEST_CASE("push_file: default commit message matches Python's 'Update {path} via ChatADHD'") {
    fsutil::TempDir td;
    write_file(td.path() / "n.md", "x");
    SyncConfig cfg;
    cfg.repo = "o/r";
    net::ScriptedTransport http;
    http.set_fallback(net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));
    GitHubFile f;
    f.path = "n.md";
    f.local_path = (td.path() / "n.md").string();
    GitHubSync sync(cfg, http);
    LOOM_REQUIRE_OK(sync.push_file(f));
    Json body = unwrap(json::parse(http.requests()[0].body));
    CHECK(body["message"] == "Update n.md via ChatADHD");
    CHECK_FALSE(body.contains("sha"));  // no sha -> new file, no sha field sent
  }

  TEST_CASE("pull_all / push_all: default selection and success/failed tally") {
    fsutil::TempDir td;
    write_file(td.path() / "local_only.py", "x");
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.local_path = td.path().string();
    cfg.include_patterns = {"*.py"};
    cfg.exclude_patterns = {};
    net::ScriptedTransport http;
    // pull_all() and push_all() each compute their own get_sync_status(), so
    // the remote listing is requested twice; after the first pull,
    // remote_only.py also exists locally (same size as remote -> "synced"),
    // which is why it must stay registered for the second listing too.
    Json listing = Json::array({Json{{"type", "file"},
                                     {"path", "remote_only.py"},
                                     {"sha", "s"},
                                     {"size", 2},
                                     {"download_url", "https://raw/remote_only.py"}}});
    http.expect("GET", "https://api.github.com/repos/o/r/contents/", net::ScriptedTransport::Reply::json(200, listing));
    http.expect("GET", "https://api.github.com/repos/o/r/contents/", net::ScriptedTransport::Reply::json(200, listing));
    http.expect("GET", "https://raw/remote_only.py", net::ScriptedTransport::Reply::text(200, "hi"));
    http.expect("PUT", "https://api.github.com/repos/o/r/contents/local_only.py", net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));

    GitHubSync sync(cfg, http);
    Json pulled = unwrap(sync.pull_all());
    CHECK(pulled["success"] == 1);
    CHECK(pulled["failed"] == 0);
    CHECK(std::filesystem::exists(td.path() / "remote_only.py"));

    Json pushed = unwrap(sync.push_all());
    CHECK(pushed["success"] == 1);
    CHECK(pushed["failed"] == 0);
  }

  TEST_CASE("sync: pulls new_remote, pushes new_local, reports modified as conflicts") {
    fsutil::TempDir td;
    write_file(td.path() / "same.py", "1234567890");
    write_file(td.path() / "changed.py", "short");
    write_file(td.path() / "local_only.py", "x");
    SyncConfig cfg;
    cfg.repo = "o/r";
    cfg.local_path = td.path().string();
    cfg.include_patterns = {"*.py"};
    cfg.exclude_patterns = {};
    net::ScriptedTransport http;
    http.expect("GET", "https://api.github.com/repos/o/r/contents/",
               net::ScriptedTransport::Reply::json(
                   200, Json::array({Json{{"type", "file"}, {"path", "same.py"}, {"sha", "s1"}, {"size", 10}},
                                     Json{{"type", "file"}, {"path", "changed.py"}, {"sha", "s2"}, {"size", 999}},
                                     Json{{"type", "file"}, {"path", "remote_only.py"}, {"sha", "s3"}, {"size", 2},
                                          {"download_url", "https://raw/remote_only.py"}}})));
    http.expect("GET", "https://raw/remote_only.py", net::ScriptedTransport::Reply::text(200, "hi"));
    http.expect("PUT", "https://api.github.com/repos/o/r/contents/local_only.py", net::ScriptedTransport::Reply::json(201, Json{{"ok", true}}));

    GitHubSync sync(cfg, http);
    Json result = unwrap(sync.sync());
    CHECK(result["pulled"]["success"] == 1);
    CHECK(result["pushed"]["success"] == 1);
    CHECK(result["conflicts"] == Json::array({"changed.py"}));
  }

  TEST_CASE("GitHubSyncManager: add, persist, reload, get_syncer fills token from secrets, remove") {
    fsutil::TempDir td;
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("github_token", "ghp_from_secrets");
    net::ScriptedTransport http;
    std::filesystem::path cfg_path = td.path() / "github_sync.json";

    {
      GitHubSyncManager mgr(cfg_path, secrets, http);
      CHECK(mgr.list_configs().empty());
      SyncConfig added = unwrap(mgr.add_config("main-repo", "owner/repo", "/local/path", "dev", "pull_only"));
      CHECK(added.repo == "owner/repo");
      CHECK(added.token == "ghp_from_secrets");
      CHECK(std::filesystem::exists(cfg_path));
      // Token must never be persisted to disk.
      std::string raw = unwrap(fsutil::read_file(cfg_path));
      CHECK(raw.find("ghp_from_secrets") == std::string::npos);
    }

    GitHubSyncManager mgr2(cfg_path, secrets, http);
    CHECK(mgr2.list_configs() == std::vector<std::string>{"main-repo"});
    auto syncer = mgr2.get_syncer("main-repo");
    REQUIRE(syncer != nullptr);
    CHECK(syncer->config().repo == "owner/repo");
    CHECK(syncer->config().branch == "dev");
    CHECK(syncer->config().token == "ghp_from_secrets");
    CHECK(syncer->config().sync_direction == "pull_only");

    CHECK(mgr2.get_syncer("does-not-exist") == nullptr);

    LOOM_REQUIRE_OK(mgr2.remove_config("main-repo"));
    CHECK(mgr2.list_configs().empty());
    GitHubSyncManager mgr3(cfg_path, secrets, http);
    CHECK(mgr3.list_configs().empty());
  }
}
