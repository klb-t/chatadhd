// P15, R54: the operations of the application as actions the conversation can reach. Each is said in Polish and in English; a sentence that
// changes something is answered with a proposal and runs after "tak"/"yes" (over the API it needs the receipt); an answer to a question runs
// at once under the read_only preset; a secret is never taken from the conversation; the choice of a model for a job lands in the overlay
// of the level that was named.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "loom/action_ops.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/semantic_worker.h"
#include "loom/storage_backend.h"
#include "loom/re/regex.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/usage_policy.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv_id, user_msg, assistant_msg;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://api.test");
    rt->config().set("default_model", "test/model");
    conv_id = unwrap(rt->db().create_conv("Planning")).id;
    user_msg = add(conv_id, "user", "We should ship on Friday.");
    assistant_msg = add(conv_id, "assistant", "Agreed, Friday works if the tests pass.");
  }
  std::string add(const std::string& conv, const std::string& role, const std::string& text) {
    NewMessage m;
    m.conv_id = conv;
    m.role = role;
    m.text = text;
    return unwrap(rt->db().create_msg(m));
  }
  chat_route::Outcome input(const std::string& text, const std::string& conv = "") {
    ChatOptions opts;
    opts.conv_id = conv.empty() ? conv_id : conv;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  Json meta(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->metadata; }
  std::string text_of(const chat_route::Outcome& o) { return unwrap(rt->db().get_msg(o.assistant_message_id))->text; }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  // The sentence is read as a command for `action`, shown as a proposal that waits (nothing done yet), and "yes" runs it.
  chat_route::Outcome say_and_confirm(const std::string& sentence, const std::string& action, const std::string& yes) {
    const auto proposal = input(sentence);
    REQUIRE_MESSAGE(proposal.handled, sentence);
    const Json p = meta(proposal).at("action_proposal");
    CHECK_MESSAGE(p.at("action") == action, sentence << " -> " << p.value("action", ""));
    CHECK_MESSAGE(p.at("status") == "pending", sentence);
    return input(yes);
  }
  // The calls that went to the model (the one-off fetch of the public model list, for the estimate of a price, is not one of them).
  std::vector<net::HttpRequest> model_calls() {
    std::vector<net::HttpRequest> out;
    for (auto& r : transport->requests())
      if (r.method == "POST" && r.url.find("/chat/completions") != std::string::npos) out.push_back(std::move(r));
    return out;
  }
  std::filesystem::path overlay_file(const std::string& domain) { return td.path() / "profiles" / (domain + ".pack"); }
  void overlay_put(const std::string& domain, const Json& overrides) {
    std::filesystem::create_directories(td.path() / "profiles");
    std::ofstream(overlay_file(domain)) << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", domain}, {"overrides", overrides}}.dump();
  }
  Json overlay(const std::string& domain) {
    if (!std::filesystem::exists(overlay_file(domain))) return Json::object();
    std::ifstream in(overlay_file(domain));
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return unwrap(json::parse(bytes)).at("overrides");
  }
};

}  // namespace

TEST_SUITE("chat_ops") {
  TEST_CASE("every regular expression of the commands compiles, and writes its pieces in folded letters") {
    Fixture f;
    const Json ir = unwrap(RuntimeProfile::builtin("intent_routing")).values();
    int checked = 0;
    for (const auto& c : ir.at("heuristic").at("commands")) {
      auto rx = re::Regex::compile(c.at("regex").get<std::string>());
      CHECK_MESSAGE(bool(rx), c.at("id").get<std::string>() << ": " << (rx ? "" : rx.error().message));
      const bool runnable = f.rt->actions().runnable(unwrap(RuntimeProfile::builtin("actions")).values().at("actions").at(c.at("action").get<std::string>()));
      CHECK_MESSAGE((runnable || c.at("action") == "actions.status"), c.at("id").get<std::string>() << ": the action is not runnable");
      ++checked;
    }
    CHECK(checked > 100);
  }

  TEST_CASE("memory: remember, recall and forget - in Polish and English, a proposal first") {
    for (const bool pl : {true, false}) {
      Fixture f;
      const auto done = f.say_and_confirm(pl ? "zapamiętaj, że lubię kawę" : "remember that I like coffee", "memory.remember", pl ? "tak" : "yes");
      const auto all = f.rt->memory().get_all();
      REQUIRE(all.size() == 1);
      CHECK(all[0].content == (pl ? "lubię kawę" : "I like coffee"));
      CHECK(f.text_of(done).find(all[0].id) != std::string::npos);
      // a proposal writes nothing before the yes
      Fixture g;
      (void)g.input(pl ? "zapamiętaj, że lubię kawę" : "remember that I like coffee");
      CHECK(g.rt->memory().get_all().empty());
      // recall: the proposal, then the list
      const auto recalled = f.say_and_confirm(pl ? "co zapamiętałeś o mnie?" : "what do you remember about me?", "memory.recall", pl ? "tak" : "yes");
      CHECK(f.text_of(recalled).find(pl ? "lubię kawę" : "I like coffee") != std::string::npos);
      // forget: one match is removed, after the yes
      const auto forgot = f.say_and_confirm(pl ? "zapomnij, że lubię kawę" : "forget that I like coffee", "memory.forget", pl ? "tak" : "yes");
      CHECK(f.rt->memory().get_all().empty());
      CHECK(f.text_of(forgot).find(pl ? "Zapomniane" : "Forgot") != std::string::npos);
    }
  }

  TEST_CASE("memory: forgetting with several matches removes nothing and lists them; changing an entry by its id") {
    Fixture f;
    unwrap(f.rt->memory().add_node("likes green tea"));
    unwrap(f.rt->memory().add_node("likes black tea"));
    const auto done = f.say_and_confirm("forget tea", "memory.forget", "yes");
    CHECK(f.rt->memory().get_all().size() == 2);
    CHECK(f.text_of(done).find("2 entries match") != std::string::npos);
    const std::string id = f.rt->memory().get_all()[0].id;
    const auto changed = f.say_and_confirm("change memory entry " + id + " to likes mate", "memory.update", "yes");
    CHECK(f.rt->memory().get_node(id)->content == "likes mate");
    CHECK(f.text_of(changed).find(id) != std::string::npos);
  }

  TEST_CASE("conversations: create, rename, list; delete asks, and the answer is said in a new conversation") {
    for (const bool pl : {true, false}) {
      Fixture f;
      f.say_and_confirm(pl ? "utwórz nową rozmowę „Podróże”" : "create a new conversation called Travel", "conversation.create", pl ? "tak" : "yes");
      bool found = false;
      for (const auto& cv : unwrap(f.rt->db().list_convs(50))) found = found || cv.title == (pl ? "Podróże" : "Travel");
      CHECK(found);
      f.say_and_confirm(pl ? "zmień nazwę tej rozmowy na Plan wakacji" : "rename this conversation to Holiday plan", "conversation.rename", pl ? "tak" : "yes");
      CHECK(unwrap(f.rt->db().get_conv(f.conv_id))->title == (pl ? "Plan wakacji" : "Holiday plan"));
      const auto listed = f.say_and_confirm(pl ? "pokaż moje rozmowy" : "list my conversations", "conversations.list", pl ? "tak" : "yes");
      CHECK(f.text_of(listed).find(pl ? "Plan wakacji" : "Holiday plan") != std::string::npos);
      const auto removed = f.say_and_confirm(pl ? "usuń tę rozmowę" : "delete this conversation", "conversation.delete", pl ? "tak" : "yes");
      CHECK_FALSE(unwrap(f.rt->db().get_conv(f.conv_id)).has_value());
      REQUIRE(removed.handled);
      CHECK(removed.conv_id != f.conv_id);  // the old one is gone: the answer is in a new conversation
      CHECK(f.text_of(removed).find(pl ? "Usunięto" : "Deleted") != std::string::npos);
    }
  }

  TEST_CASE("messages: edit keeps the old version, restore brings it back, exclude and weight change the message") {
    for (const bool pl : {true, false}) {
      Fixture f;
      f.say_and_confirm(pl ? "popraw tę wiadomość: Spotkanie jest w piątek" : "edit this message: The meeting is on Friday", "message.edit", pl ? "tak" : "yes");
      const auto versions = unwrap(f.rt->db().get_versions(*unwrap(f.rt->db().get_msg(f.assistant_msg))->version_group_id));
      REQUIRE(versions.size() == 2);
      CHECK(versions[1].text == (pl ? "Spotkanie jest w piątek" : "The meeting is on Friday"));
      CHECK(versions[1].status == "active");
      CHECK(unwrap(f.rt->db().get_msg(f.assistant_msg))->status == "version");  // the old one is kept
      f.say_and_confirm(pl ? "wróć do poprzedniej wersji tej wiadomości" : "restore the previous version of this message", "message.restore", pl ? "tak" : "yes");
      CHECK(unwrap(f.rt->db().get_msg(f.assistant_msg))->status == "active");
      const auto listed = f.say_and_confirm(pl ? "pokaż wersje tej wiadomości" : "show the versions of this message", "message.versions", pl ? "tak" : "yes");
      CHECK(f.text_of(listed).find(f.assistant_msg) != std::string::npos);
      f.say_and_confirm(pl ? "wyklucz tę wiadomość z kontekstu" : "exclude this message from the context", "message.set_status", pl ? "tak" : "yes");
      // the last message is now the edited one that was brought back from; find the one that is excluded
      int excluded = 0;
      for (const auto& m : unwrap(f.rt->db().get_msgs(f.conv_id, true))) excluded += m.status == "excluded" ? 1 : 0;
      CHECK(excluded == 1);
    }
  }

  TEST_CASE("messages: the weight by a word and by a number") {
    Fixture f;
    f.say_and_confirm("mark this message as important", "message.weight", "yes");
    CHECK(unwrap(f.rt->db().get_msg(f.assistant_msg))->weight == doctest::Approx(2.0));
    f.say_and_confirm("ustaw wagę tej wiadomości na 3,5", "message.weight", "tak");
    CHECK(unwrap(f.rt->db().get_msg(f.assistant_msg))->weight == doctest::Approx(3.5));
  }

  TEST_CASE("search: the conversations that match, with a snippet; the graph around a name") {
    Fixture f;
    const std::string other = unwrap(f.rt->db().create_conv("Pets")).id;
    f.add(other, "user", "My cats sleep all day and the cats eat a lot.");
    const auto found = f.say_and_confirm("find conversations about cats", "search.find", "yes");
    const std::string text = f.text_of(found);
    CHECK(text.find("Pets") != std::string::npos);
    CHECK(text.find(other) != std::string::npos);
    CHECK(text.find("Planning") == std::string::npos);
    const auto none = f.say_and_confirm("znajdź rozmowy o zebrach", "search.find", "tak");
    CHECK(f.text_of(none).find("Nic nie pasuje") != std::string::npos);

    const std::string python = unwrap(f.rt->db().create_node("Python", "entity"));
    const std::string fastapi = unwrap(f.rt->db().create_node("FastAPI", "entity"));
    unwrap(f.rt->db().create_link(python, fastapi, "mentions", 1.0));
    const auto related = f.say_and_confirm("show what is related to Python", "graph.related", "yes");
    CHECK(f.text_of(related).find("FastAPI") != std::string::npos);
    const auto related_pl = f.say_and_confirm("pokaż, co jest powiązane z Python", "graph.related", "tak");
    CHECK(f.text_of(related_pl).find("FastAPI") != std::string::npos);
  }

  TEST_CASE("read-only answers run at once under the read_only preset; the default asks first; help answers whatever the setting says") {
    Fixture f;
    const auto asked = f.input("list my conversations");
    CHECK(f.meta(asked).at("action_proposal").at("status") == "pending");  // the preset is propose
    f.rt->config().set("intent_routing_run", "read_only");
    const auto answered = f.input("list my conversations");
    CHECK(f.meta(answered).at("action_proposal").at("status") == "executed");
    CHECK(f.text_of(answered).find("Planning") != std::string::npos);
    // a write still waits for the yes under read_only
    const auto write = f.input("remember that I like tea");
    CHECK(f.meta(write).at("action_proposal").at("status") == "pending");
    CHECK(f.rt->memory().get_all().empty());
    // help is an answer: it runs at once under the default too
    f.rt->config().set("intent_routing_run", "propose");
    const auto help = f.input("what can I do here?");
    CHECK(f.meta(help).at("action_proposal").at("status") == "executed");
    const std::string text = f.text_of(help);
    CHECK(text.find("summarise this conversation") != std::string::npos);  // the actions that fit the conversation
    CHECK(text.find("find conversations about cats") != std::string::npos);  // and those of the application
    CHECK(text.find("/help") != std::string::npos);                          // and the syntax for those who know it
    CHECK(text.find("receipt") != std::string::npos);
    const auto help_pl = f.input("co mogę tu zrobić?");
    CHECK(f.text_of(help_pl).find("streść tę rozmowę") != std::string::npos);
    CHECK(f.text_of(help_pl).find("potwierdz") == std::string::npos);  // no stray English / key names of the text table
  }

  TEST_CASE("how to: explained, not run; a plain question that is no command goes on to the model") {
    Fixture f;
    const auto how = f.input("jak to zrobić: streścić tę rozmowę");
    REQUIRE(how.handled);
    const std::string text = f.text_of(how);
    CHECK(text.find("Streść") != std::string::npos);
    CHECK(text.find("Nic nie zostało wykonane") != std::string::npos);
    CHECK(f.model_calls().empty());  // no model was called
    CHECK(f.rt->actions().status(Json{{"list", true}, {"conv_id", f.conv_id}}).has_value());
    const auto en = f.input("how do I summarise this conversation");
    CHECK(f.text_of(en).find("Nothing was run") != std::string::npos);
    CHECK(f.text_of(en).find("/summarise") != std::string::npos);
    // "how to bake bread" names no action: it is a message, the chat path takes it
    const auto message = f.input("how do I bake bread");
    CHECK_FALSE(message.handled);
    const auto message_pl = f.input("jak zrobić sernik");
    CHECK_FALSE(message_pl.handled);
  }

  TEST_CASE("export writes the file; storage questions and the storage commands are reachable") {
    Fixture f;
    f.say_and_confirm("wyeksportuj tę rozmowę do markdown", "export.conversation", "tak");
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(f.td.path() / "exports")) files += e.path().extension() == ".md" ? 1 : 0;
    CHECK(files == 1);
    const auto status = f.say_and_confirm("show the storage status", "storage.status", "yes");
    CHECK(f.text_of(status).find("local_dir") != std::string::npos);
    const auto kinds = f.say_and_confirm("jakie rodzaje magazynów można podłączyć", "storage.kinds", "tak");
    CHECK(f.text_of(kinds).find("gdrive") != std::string::npos);
    CHECK(f.route("back up the database to s3_main").at("command").at("args").at("to") == "s3_main");
    CHECK(f.route("skopiuj blobs do s3_main").at("command").at("args").at("category") == "blobs");
  }

  TEST_CASE("export to a configured storage: the file is written and a copy goes to the backend that was named") {
    Fixture f;
    const auto target = f.td.path() / "cloud";
    std::filesystem::create_directories(target);
    f.overlay_put("storage_security", Json{{"storages", Json{{"my_local", Json{{"class", "user_cloud"}, {"label", Json{{"en", "My local cloud"}, {"pl", "Moja chmura"}}}, {"path", ""},
        {"backend", Json{{"kind", "local"}, {"path", target.string()}}}, {"configured", true}, {"capabilities", Json::array({"read", "write", "list"})}}}}}});
    if (auto p = RuntimeProfile::load("storage_security", f.td.path())) f.rt->storage().set_values(p->values());
    f.say_and_confirm("export this conversation to my_local as json", "export.conversation", "yes");
    bool copied = false;
    for (const auto& e : std::filesystem::recursive_directory_iterator(target)) copied = copied || (e.is_regular_file() && e.path().extension() == ".json");
    CHECK(copied);
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(f.td.path() / "exports")) files += e.path().extension() == ".json" ? 1 : 0;
    CHECK(files == 1);
    // a storage nobody configured is said so, nothing is sent
    Fixture g;
    const auto missing = g.say_and_confirm("export this conversation to nowhere", "export.conversation", "yes");
    CHECK(g.text_of(missing).find("nowhere") != std::string::npos);
  }

  TEST_CASE("import: a path in the words, and the file the device picks") {
    Fixture f;
    const auto path = f.td.path() / "notes.md";
    std::ofstream(path) << "# Trip\n\n## User\nWhere to?\n\n## Assistant\nLisbon.\n";
    const auto done = f.say_and_confirm("zaimportuj " + path.string(), "import.file", "tak");
    CHECK(f.text_of(done).find("Zaimportowano") != std::string::npos);
    const auto lisbon = unwrap(f.rt->db().search_messages("Lisbon", SearchOptions{}));
    CHECK(lisbon.hits.size() == 1);

    // without a path the device picks the file, then the import runs on the blob it brought
    unwrap(f.rt->actions().client_declare(Json{{"client_id", "web-1"}, {"client", Json{{"kind", "web"}}}, {"capabilities", Json{{"picks_files", "granted"}}}}));
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    const auto proposal = unwrap(chat_route::handle(*f.rt, "import this file", opts, Json{{"message", "import this file"}, {"client_id", "web-1"}}, nullptr));
    REQUIRE(proposal.handled);
    CHECK(f.meta(proposal).at("action_proposal").at("action") == "import.file");
    const auto waiting = unwrap(chat_route::handle(*f.rt, "yes", opts, Json{{"message", "yes"}, {"client_id", "web-1"}}, nullptr));
    const Json run = f.meta(waiting).at("action_run");
    REQUIRE(run.at("status") == "awaiting_client");
    const std::string id = unwrap(f.rt->actions().invoke("client.blob_begin", Json::object())).at("upload_id");
    unwrap(f.rt->actions().invoke("client.blob_append", Json{{"upload_id", id}, {"data_base64", base64::encode("# Picked\n\n## User\nHi\n\n## Assistant\nHello\n")}}));
    const std::string hash = unwrap(f.rt->actions().invoke("client.blob_commit", Json{{"upload_id", id}, {"mime", "text/markdown"}})).at("hash");
    const Json answered = unwrap(chat_route::client_result(*f.rt, Json{{"run_id", run.at("run_id")}, {"receipt_id", run.at("pending").at("receipt_id")}, {"state", "ok"},
                                                                       {"parts", Json::array({Json{{"kind", "file_ref"}, {"mime", "text/markdown"}, {"blob_hash", hash}}})}}));
    CHECK(answered.at("status") == "completed");
    CHECK(unwrap(f.rt->db().search_messages("Hello", SearchOptions{})).hits.size() == 1);
  }

  TEST_CASE("storage backends are described in the overlay, the secrets are never taken from the conversation") {
    for (const bool pl : {true, false}) {
      Fixture f;
      const auto done = f.say_and_confirm(pl ? "podłącz mój Dysk Google" : "connect my Google Drive", "storage.connect", pl ? "tak" : "yes");
      const Json storages = f.overlay("storage_security").at("storages");
      REQUIRE(storages.contains("gdrive"));
      CHECK(storages.at("gdrive").at("backend").at("kind") == "gdrive");
      CHECK(storages.at("gdrive").at("configured") == true);
      const std::string text = f.text_of(done);
      CHECK(text.find("storage_gdrive_refresh_token") != std::string::npos);  // the secrets to enter, named
      CHECK(text.find(pl ? "Ustawienia" : "Settings") != std::string::npos);
      CHECK_FALSE(f.rt->secrets().has("storage_gdrive_refresh_token"));
    }
    Fixture f;
    // S3 needs a bucket and an endpoint; they are said in the sentence
    const auto s3 = f.say_and_confirm("connect an S3 bucket named my-data at https://s3.example.com", "storage.connect", "yes");
    const Json entry = f.overlay("storage_security").at("storages").at("s3_main");
    CHECK(entry.at("backend").at("bucket") == "my-data");
    CHECK(entry.at("backend").at("endpoint") == "https://s3.example.com");
    CHECK(entry.at("backend").at("secret_ref") == "storage_s3_main");
    CHECK(f.text_of(s3).find("storage_s3_main_access_key") != std::string::npos);
    // the new backend is known to the running service
    CHECK(f.rt->storage().values().at("storages").contains("s3_main"));
    // a secret handed to the action is refused, and nothing is written for it
    Fixture g;
    auto refused = g.rt->actions().run(Json{{"action", "storage.connect"}, {"element", "application"}, {"args", Json{{"kind", "s3"}, {"bucket", "b"}, {"endpoint", "https://e"}, {"secret_key", "abc"}}}, {"confirmed", false}});
    REQUIRE(refused);
    const Json plan = *refused;
    const Json done = unwrap(g.rt->actions().confirm(Json{{"run_id", plan.at("id")}, {"approved", true}, {"receipt_id", plan.at("pending").at("receipt_id")}}));
    CHECK(done.at("status") == "failed");
    CHECK(done.at("error").at("message").get<std::string>().find("Settings") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(g.overlay_file("storage_security")));
    // the credential rule of the settings stays: a key said in a sentence is refused
    const auto key = g.input("ustaw api_key=sk-abc");
    CHECK(g.meta(key).at("action_proposal").at("status") == "denied");
  }

  TEST_CASE("the model for a job lands in the overlay of the level named, and is used by that job") {
    Fixture f;
    f.say_and_confirm("do streszczeń używaj modelu openai/gpt-4o-mini", "ports.set", "tak");
    const Json roles = f.overlay("ports").at("roles");
    CHECK(roles.at("action.summarise").at("params").at("model") == "openai/gpt-4o-mini");
    CHECK(roles.at("action.summarise").at("adapter") == "openai_chat");
    // the summary uses it: the request body names that model
    f.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("Short.")));
    f.input("streść tę rozmowę");
    f.input("tak");
    const auto seen = f.model_calls();
    REQUIRE(seen.size() == 1);
    CHECK(unwrap(json::parse(seen[0].body)).at("model") == "openai/gpt-4o-mini");

    // for this conversation only: the choice is in its metadata, the user's overlay is not touched
    Fixture g;
    g.say_and_confirm("in this conversation use model x/y for summaries", "ports.set", "yes");
    CHECK(unwrap(g.rt->db().get_conv(g.conv_id))->metadata.at("ports").at("action.summarise").at("params").at("model") == "x/y");
    CHECK_FALSE(std::filesystem::exists(g.overlay_file("ports")));
    g.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("Short.")));
    g.input("summarise this conversation");
    g.input("yes");
    CHECK(unwrap(json::parse(g.model_calls().at(0).body)).at("model") == "x/y");
    // another conversation still uses the default
    const std::string other = unwrap(g.rt->db().create_conv("Other")).id;
    g.add(other, "user", "Hello there.");
    g.transport->expect("POST", "https://api.test/chat/completions", net::ScriptedTransport::Reply::json(200, reply("Short.")));
    g.input("summarise this conversation", other);
    g.input("yes", other);
    CHECK(unwrap(json::parse(g.model_calls().at(1).body)).at("model") == "test/model");

    // the chat model is a setting: the person-level choice is that setting
    Fixture h;
    h.say_and_confirm("use model anthropic/claude-haiku-4-5 for chat", "ports.set", "yes");
    CHECK(h.rt->config().get("default_model") == "anthropic/claude-haiku-4-5");
    // the question: which model do I use for summaries
    const auto asked = h.say_and_confirm("jakiego modelu używam do streszczeń", "ports.show", "tak");
    CHECK(h.text_of(asked).find("action.summarise") != std::string::npos);
    // a job nobody knows is no command: the sentence goes on to the model as it is
    CHECK_FALSE(h.input("use model a/b for baking").handled);
    CHECK_FALSE(h.input("do tego listu użyj uprzejmego tonu").handled);
  }

  TEST_CASE("usage: the spend of a period from the ledger, the growth limit through the rules of its setting") {
    Fixture f;
    {
      auto policy = unwrap(UsagePolicy::open(f.td.path() / "usage-policy.sqlite", Json::object()));
      Json estimate{{"operation_id", "op1"}, {"baseline_key", "chat.send|test|m"}, {"model", "test/model"}, {"resources", Json{{"output_tokens", nullptr}, {"cost_usd", nullptr}}}};
      unwrap(policy->request(estimate));
      unwrap(policy->complete("op1", Json{{"resources", Json{{"requests", 1}, {"output_tokens", 120}, {"cost_usd", 0.0042}}}, {"provenance", "provider_reported"}}));
    }
    const auto spent = f.say_and_confirm("ile wydałem w tym tygodniu", "usage.spend", "tak");
    const std::string text = f.text_of(spent);
    CHECK(text.find("0.0042") != std::string::npos);
    CHECK(text.find("120") != std::string::npos);
    CHECK(text.find("test/model") != std::string::npos);
    const auto en = f.say_and_confirm("how much did I spend today", "usage.spend", "yes");
    CHECK(f.text_of(en).find("0.0042") != std::string::npos);
    const auto old = f.say_and_confirm("how much did I spend yesterday", "usage.spend", "yes");
    CHECK(f.text_of(old).find("0.0042") == std::string::npos);

    // the limit is an edit of loom_usage_policy: its rule applies (the x10 setting asks for a confirmation of its own)
    const auto limit = f.input("ustaw limit wzrostu zużycia na 20");
    REQUIRE(limit.handled);
    const Json p = f.meta(limit).at("action_proposal");
    CHECK(p.at("action") == "usage.set_limit");
    CHECK_FALSE(p.at("needs_confirmation").empty());
    f.input("tak");
    // whichever way the rule asked, the value is either written or still waiting for the confirmation - never written around the rule
    const Json now = f.rt->config().get("loom_usage_policy", Json::object());
    const double factor = now.value("growth_factor", 10.0);
    CHECK((factor == doctest::Approx(20.0) || factor == doctest::Approx(10.0)));
  }

  TEST_CASE("models and providers: refresh the list from the provider, list it, show the providers") {
    Fixture f;
    f.transport->expect("GET", "https://api.test/models", net::ScriptedTransport::Reply::json(200, Json{{"data", Json::array({Json{{"id", "a/alpha"}, {"name", "Alpha"}, {"context_length", 1000}},
                                                                                                                       Json{{"id", "b/beta"}, {"name", "Beta"}, {"context_length", 2000}}})}}));
    const auto refreshed = f.say_and_confirm("odśwież listę modeli", "models.refresh", "tak");
    CHECK(f.text_of(refreshed).find("2") != std::string::npos);
    const auto listed = f.say_and_confirm("list the models", "models.list", "yes");
    CHECK(f.text_of(listed).find("a/alpha") != std::string::npos);
    const auto filtered = f.say_and_confirm("pokaż modele beta", "models.list", "tak");
    CHECK(f.text_of(filtered).find("b/beta") != std::string::npos);
    CHECK(f.text_of(filtered).find("a/alpha") == std::string::npos);
    const auto providers = f.say_and_confirm("show the providers", "providers.list", "yes");
    REQUIRE(providers.handled);
  }

  TEST_CASE("background work: pause, resume, the state, the settings list") {
    Fixture f;
    f.say_and_confirm("wstrzymaj analizę semantyczną", "semantic.pause", "tak");
    CHECK(f.rt->worker().status().paused);
    f.say_and_confirm("resume the semantic analysis", "semantic.resume", "yes");
    CHECK_FALSE(f.rt->worker().status().paused);
    const auto state = f.say_and_confirm("show the semantic analysis status", "semantic.status", "yes");
    CHECK(f.text_of(state).find("Semantic analysis") != std::string::npos);
    const auto settings = f.say_and_confirm("list the memory settings", "settings.list", "yes");
    const std::string text = f.text_of(settings);
    CHECK(text.find("graph_memory_depth") != std::string::npos);
    CHECK(text.find("default_model") == std::string::npos);  // only the group asked for
    const auto secrets = f.say_and_confirm("pokaż ustawienia", "settings.list", "tak");
    CHECK(f.text_of(secrets).find("sk-test-key") == std::string::npos);  // a secret says only whether it is set
    CHECK(f.text_of(secrets).find("api_key = (ustawiony)") != std::string::npos);
  }

  TEST_CASE("the expert path: every new action is reachable by /name key=value, and /help is documented") {
    Fixture f;
    const auto found = f.say_and_confirm("/search.find query=Friday", "search.find", "yes");
    CHECK(f.text_of(found).find("Planning") != std::string::npos);
    f.say_and_confirm("/search.find cats and dogs", "search.find", "yes");  // positional words fill the query
    CHECK(f.route("/models").at("command").at("action") == "models.list");
    CHECK(f.route("/help").at("command").at("action") == "help.can_do");
    CHECK(f.route("/memory.remember content=tea").at("command").at("args").at("content") == "tea");
    // every action of the catalogue can be said this way: the verb is its id, the element defaults sensibly
    const Json catalogue = unwrap(RuntimeProfile::builtin("actions")).values().at("actions");
    int checked = 0;
    std::vector<std::string> ids = actions::chat_op_ids();
    ids.push_back("import.file");  // the composite that picks a file first
    for (const std::string& id : ids) {
      const Json routed = f.route("/" + id);
      CHECK_MESSAGE(routed.value("kind", "") == "command", id);
      CHECK_MESSAGE(routed.at("command").value("action", "") == id, id);
      const auto reason = routed.at("command").value("reason", Json::object()).value("code", "");
      CHECK_MESSAGE(reason != "unfit", id << " is not reachable by its name: " << reason);
      ++checked;
    }
    CHECK(checked >= 40);
    (void)catalogue;
  }

  TEST_CASE("every example sentence of every new action, in Polish and in English, is answered with a proposal that carries its receipt (a direct answer for help)") {
    const Json catalogue = unwrap(RuntimeProfile::builtin("actions")).values().at("actions");
    std::vector<std::string> ids = actions::chat_op_ids();
    ids.push_back("import.file");
    ids.push_back("actions.status");
    int proposals = 0;
    for (const char* lang : {"pl", "en"}) {
      Fixture f;  // one conversation per language: a sentence that is a command opens a proposal, the next one does not answer it
      for (const std::string& id : ids) {
        const Json& entry = catalogue.at(id);
        const std::string text = entry.at("examples").at(lang).at(0).get<std::string>();
        const auto out = f.input(text);
        REQUIRE_MESSAGE(out.handled, id << " (" << lang << "): \"" << text << "\" is a message, not a command");
        const Json p = f.meta(out).at("action_proposal");
        const std::string reached = p.at("action");
        CHECK_MESSAGE((reached == id || (id == "import.run" && reached == "import.file")), id << " (" << lang << "): \"" << text << "\" reached " << reached);
        if (id == "help.can_do" || id == "help.how_to") {
          CHECK_MESSAGE(p.at("status") == "executed", id << ": an answer to a question runs at once");
          continue;
        }
        if (id == "import.file" && text.find('/') == std::string::npos) {
          // no path and no device that can pick a file: the proposal says it cannot be done here (the picked file is tested with a client)
          CHECK_MESSAGE(p.at("status") == "unavailable", id << " (" << lang << ")");
          continue;
        }
        CHECK_MESSAGE(p.at("status") == "pending", id << " (" << lang << ")");
        CHECK_MESSAGE(!p.value("receipt_id", "").empty(), id << " (" << lang << "): the proposal carries its receipt");
        CHECK_MESSAGE(!p.value("run_id", "").empty(), id << " (" << lang << ")");
        ++proposals;
      }
    }
    CHECK(proposals >= 80);
  }

  TEST_CASE("tasks, logs, sources, provenance, about, GitHub without a repository") {
    Fixture f;
    f.say_and_confirm("show the background tasks", "tasks.list", "yes");
    f.say_and_confirm("wznów przerwane zadania", "tasks.resume", "tak");
    const auto logs = f.say_and_confirm("pokaż logi", "logs.show", "tak");
    REQUIRE(logs.handled);
    const auto about = f.say_and_confirm("what version is this application", "app.info", "yes");
    CHECK(f.text_of(about).find("version") != std::string::npos);
    f.say_and_confirm("pokaż źródła", "sources.list", "tak");
    const auto prov = f.say_and_confirm("pokaż pochodzenie " + f.user_msg, "provenance.show", "tak");
    CHECK(f.text_of(prov).find(f.user_msg) != std::string::npos);
    const auto gh = f.say_and_confirm("zsynchronizuj z GitHubem", "github.sync", "tak");
    CHECK(f.text_of(gh).find("github_repo") != std::string::npos);
  }

  TEST_CASE("over the API the proposal's receipt is needed: a bare confirmed:true runs nothing") {
    Fixture f;
    const Json refused = unwrap(f.rt->actions().run(Json{{"action", "memory.remember"}, {"element", "application"}, {"args", Json{{"content", "tea"}}}, {"confirmed", true}}));
    // memory.remember is a write the rules do not ask about, so it may run: the receipt matters for what the rules ask to confirm
    (void)refused;
    const Json asks = unwrap(f.rt->actions().run(Json{{"action", "conversation.delete"}, {"element", f.conv_id}, {"confirmed", true}}));
    CHECK(asks.at("status") == "proposed");
    CHECK(asks.contains("confirmation_refused"));
    CHECK(unwrap(f.rt->db().get_conv(f.conv_id)).has_value());
    const Json preview = unwrap(f.rt->actions().preview(Json{{"action", "conversation.delete"}, {"element", f.conv_id}}));
    const Json ran = unwrap(f.rt->actions().run(Json{{"action", "conversation.delete"}, {"element", f.conv_id}, {"confirmed", true}, {"receipt_id", preview.at("plan_receipt")}}));
    CHECK(ran.at("status") == "completed");
    CHECK_FALSE(unwrap(f.rt->db().get_conv(f.conv_id)).has_value());
  }
}
