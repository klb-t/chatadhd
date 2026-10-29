// R103 B1b: the working-copy bundle (include/loom/working_copy.h). Exact round trip without analysis, integrity, the
// allow-list on import, and a generative proof that no credential value ever ends up in the bundle bytes.
#include <doctest/doctest.h>

#include <chrono>
#include <random>
#include <thread>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/memory_engine.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/working_copy.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

std::string random_secret(std::mt19937_64& rng, std::size_t len) {
  static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~+/=";
  std::string s;
  for (std::size_t i = 0; i < len; ++i) s += alphabet[rng() % alphabet.size()];
  return s;
}

struct Rt {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  explicit Rt(std::map<std::string, std::string> overlay = {}, bool workers = false) {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = workers;
    o.http = transport;
    o.credential_overlay = std::move(overlay);
    rt = unwrap(Runtime::open(o));
    rt->config().set("model_registry_autorefresh", "off");
  }
};

// A working copy with every kind of thing the bundle has to carry.
void seed(Runtime& rt) {
  auto& db = rt.db();
  const Conversation c = unwrap(db.create_conv("Wakacje w Tatrach"));
  NewMessage m;
  m.conv_id = c.id;
  m.role = "user";
  m.text = "Jadę w Tatry z Anną w lipcu.";
  const std::string m1 = unwrap(db.create_msg(m));
  m.role = "assistant";
  m.text = "Świetnie, Anna zna szlaki.";
  m.parent_id = m1;
  const std::string m2 = unwrap(db.create_msg(m));
  REQUIRE(db.mark_analysed(m1, Json{{"source", "regex"}, {"entities", Json::array()}}));
  REQUIRE(db.mark_analysed(m2, Json{{"source", "regex"}, {"entities", Json::array()}}));
  NodeOptions no;
  no.metadata = Json{{"analysis", Json{{"confidence", 0.75}, {"source_text_sha256", "abc"}}}};
  const std::string anna = unwrap(db.create_node("Anna", "person", no));
  const std::string tatry = unwrap(db.create_node("Tatry", "topic"));
  (void)unwrap(db.create_link(anna, tatry, "mentions", 0.5, Json{{"by", "analysis"}}));
  (void)unwrap(db.create_link(c.id, "desktop:main", "pinned_on", 1.0, Json{{"x", 3}, {"y", 4}}));
  const Conversation c2 = unwrap(db.create_conv("Do kosza"));
  ConvPatch trash;
  trash.metadata = Json{{"trash", Json{{"at", "2026-10-29T10:00:00Z"}, {"from", "desktop"}}}};
  REQUIRE(db.update_conv(c2.id, trash));
  (void)unwrap(rt.memory().add_node("Lubię kawę bez cukru."));
  rt.config().set("window_layouts", Json{{"mine", Json{{"windows", Json::array({Json{{"kind", "chat"}, {"x", 0.25}}})}}}});
  rt.config().set("journal", Json::array({Json{{"op", "open"}, {"kind", "graph"}}}));
  rt.config().set("max_tokens", 2048);
  REQUIRE(rt.config().save());
}

}  // namespace

TEST_CASE("working copy bundle: exact round trip, no analysis, no model call") {
  Rt a;
  seed(*a.rt);
  const Json bundle = unwrap(working_copy::export_bundle(*a.rt));
  CHECK(bundle.at("schema") == "loom.guest_bundle/2");
  CHECK(bundle.at("integrity").at("content_sha256") == working_copy::content_sha256(bundle.at("content")));
  for (const char* t : {"conversations", "messages", "nodes", "links"}) CHECK(bundle.at("content").at("tables").contains(t));
  CHECK_FALSE(bundle.at("content").at("tables").contains("_meta"));

  Rt b({}, /*workers=*/true);
  const Json r = unwrap(working_copy::import_bundle(*b.rt, bundle));
  CHECK(r.at("analysis_runs") == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));  // a worker that wanted to analyse would have started
  CHECK(b.transport->requests().empty());

  const Json again = unwrap(working_copy::export_bundle(*b.rt));
  CHECK(again.at("content").at("tables") == bundle.at("content").at("tables"));  // same ids, statuses, metadata
  CHECK(again.at("content").at("memory") == bundle.at("content").at("memory"));
  CHECK(again.at("content").at("config").at("window_layouts") == bundle.at("content").at("config").at("window_layouts"));
  CHECK(again.at("content").at("config").at("journal") == bundle.at("content").at("config").at("journal"));
  const auto pending = unwrap(b.rt->db().count_pending_semantic());
  CHECK(pending == 0);
  CHECK(b.rt->memory().get_active_context().find("kawę") != std::string::npos);
  // the pin and the trash mark came back as rows
  CHECK(unwrap(b.rt->db().get_links(std::nullopt, std::string_view("pinned_on"))).size() == 1);
}

TEST_CASE("working copy bundle: integrity, modes, allow-list on import") {
  Rt a;
  seed(*a.rt);
  Json bundle = unwrap(working_copy::export_bundle(*a.rt));
  Rt b;
  Json tampered = bundle;
  tampered["content"]["tables"]["conversations"]["rows"][0][1] = "zmienione";
  CHECK_FALSE(working_copy::import_bundle(*b.rt, tampered));
  Json old = bundle;
  old["schema"] = "loom.guest_bundle/1";
  CHECK_FALSE(working_copy::import_bundle(*b.rt, old));
  // a crafted bundle naming tables outside the allow-list (and a credential-named setting) writes none of them
  Json crafted = bundle;
  crafted["content"]["tables"]["_meta"] = Json{{"columns", Json::array({"key", "value"})}, {"rows", Json::array({Json::array({"schema_version", "99"})})}};
  crafted["content"]["config"]["api_key"] = "planted-key-123456";
  crafted["integrity"]["content_sha256"] = working_copy::content_sha256(crafted["content"]);
  REQUIRE(working_copy::import_bundle(*b.rt, crafted));
  CHECK(unwrap(b.rt->db().conn().query_text("SELECT value FROM _meta WHERE key='schema_version'")).value_or("") == "4");
  CHECK_FALSE(b.rt->config().contains("api_key"));
  // merge keeps what is there; replace makes it exactly the bundle
  const Conversation extra = unwrap(b.rt->db().create_conv("Tylko tutaj"));
  REQUIRE(working_copy::import_bundle(*b.rt, bundle, Json{{"mode", "merge"}}));
  CHECK(unwrap(b.rt->db().get_conv(extra.id)).has_value());
  REQUIRE(working_copy::import_bundle(*b.rt, bundle, Json{{"mode", "replace"}, {"keep_config_keys", Json::array({"model_registry_autorefresh"})}}));
  CHECK_FALSE(unwrap(b.rt->db().get_conv(extra.id)).has_value());
  CHECK(unwrap(working_copy::export_bundle(*b.rt)).at("content").at("tables") == bundle.at("content").at("tables"));
}

TEST_CASE("working copy bundle: generative - no credential value is ever in the bundle bytes") {
  std::mt19937_64 rng(0xB1B);
  for (int round = 0; round < 25; ++round) {
    std::vector<std::string> planted;
    auto next = [&] { planted.push_back(random_secret(rng, 12 + rng() % 60)); return planted.back(); };
    std::map<std::string, std::string> overlay = {{"OPENROUTER_API_KEY", next()}, {"vertex_access_token", next()}, {"GROQ_API_KEY", next()}};
    Rt a(overlay);
    seed(*a.rt);
    for (const char* k : {"api_key", "anthropic_batch_key", "github_token", "groq_api_key"}) {
      a.rt->secrets().set(k, next());
    }
    REQUIRE(a.rt->secrets().save());
    // settings carrying credentials by mistake: credential-named keys, a credential value under an ordinary key, nested
    a.rt->config().set("api_key", next());
    a.rt->config().set("sessionToken", next());
    a.rt->config().set("note_for_me", "mój klucz to " + overlay["GROQ_API_KEY"]);
    a.rt->config().set("provider_extra", Json{{"headers", Json{{"Authorization", "Bearer " + overlay["vertex_access_token"]}}}, {"label", "ok"}});
    a.rt->config().set("cookie", next());
    REQUIRE(a.rt->config().save());
    const Json bundle = unwrap(working_copy::export_bundle(*a.rt));
    const std::string bytes = bundle.dump();
    for (const auto& s : planted) CHECK_MESSAGE(bytes.find(s) == std::string::npos, "round " << round << " leaked a planted value");
    CHECK(bundle.at("content").at("config").at("provider_extra").at("label") == "ok");  // only the carrier is left out
    CHECK(bundle.at("content").at("config").contains("max_tokens"));                     // "tokens" is not "token"
    CHECK_FALSE(bundle.at("excluded").at("config_keys").empty());
  }
}
