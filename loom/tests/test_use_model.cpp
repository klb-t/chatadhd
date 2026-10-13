// The one "use model" action: adapters selected from data, variants (sequential
// and first_success), the shared usage guard, provenance, streaming, batches.
#include <doctest/doctest.h>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/net/http.h"
#include "loom/use_model.h"
#include "loom/util/base64.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
using Reply = net::ScriptedTransport::Reply;

namespace {

struct Env {
  fsutil::TempDir td;
  Config cfg{td.path() / "config.json"};
  Secrets secrets{td.path() / "secrets.json"};
  net::ScriptedTransport transport;
  std::vector<Json> provenance;

  Env() {
    cfg.set("base_url", "https://api.test");
    secrets.set("api_key", "sk-secret-test");
  }
  llm::Services services() {
    llm::Services s{cfg, secrets, transport};
    s.on_provenance = [this](const Json& p) { provenance.push_back(p); };
    return s;
  }
};

Json chat_reply(const std::string& text, const std::string& model = "reported/model") {
  return Json{{"model", model},
              {"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", text}}}}})},
              {"usage", Json{{"completion_tokens", 7}}}};
}

llm::ModelRequest simple(const std::string& model, const std::string& prompt = "hello") {
  llm::ModelRequest r;
  r.kind = "test.use";
  r.models = {llm::ModelSpec{model, "", Json::object()}};
  r.prompt = prompt;
  r.parameters = Json{{"temperature", 0.2}, {"max_tokens", 50}};
  r.title = "TestApp";
  r.timeout_ms = 5000;
  return r;
}

}  // namespace

TEST_SUITE("use_model") {
  TEST_CASE("single model: bytes come from settings, the manifest and the adapter; provenance is recorded") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("hi there")));
    auto outcome = unwrap(llm::use_model(env.services(), simple("vendor/m1")));
    REQUIRE(outcome.results.size() == 1);
    const auto& r = outcome.results[0];
    CHECK(r.ok());
    CHECK(r.text == "hi there");
    CHECK(r.reported_model == "reported/model");
    CHECK(r.usage["completion_tokens"] == 7);
    CHECK(r.provider == "openrouter");
    CHECK(r.adapter == "openai_chat");

    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 1);
    const Json expected{
        {"method", "POST"},
        {"url", "https://api.test/chat/completions"},
        {"headers", Json::array({Json::array({"Authorization", "Bearer sk-secret-test"}),
                                 Json::array({"Content-Type", "application/json"}),
                                 Json::array({"HTTP-Referer", "https://github.com/chatadhd"}),
                                 Json::array({"X-Title", "TestApp"})})},
        {"body", R"({"model":"vendor/m1","messages":[{"role":"user","content":"hello"}],"temperature":0.2,"max_tokens":50})"},
        {"timeout_ms", 5000},
        {"stream", false}};
    CHECK(loom::test::request_json(reqs[0]) == expected);

    // Provenance: which model, adapter, provider and parameters produced which output.
    REQUIRE(env.provenance.size() == 1);
    const Json& p = env.provenance[0];
    CHECK(p["schema"] == "loom.model_use/1");
    CHECK(p["requested_model"] == "vendor/m1");
    CHECK(p["reported_model"] == "reported/model");
    CHECK(p["provider"] == "openrouter");
    CHECK(p["adapter"] == "openai_chat");
    CHECK(p["parameters"] == Json{{"temperature", 0.2}, {"max_tokens", 50}});
    CHECK(p["endpoint"] == "https://api.test/chat/completions");
    CHECK(p["status"] == "ok");
    CHECK(p["output_sha256"] == Sha256::hex("hi there"));
    CHECK(p["request_sha256"] == Sha256::hex(reqs[0].body));
    CHECK(r.provenance == p);
    CHECK(p.dump().find("sk-secret-test") == std::string::npos);
  }

  TEST_CASE("the provider is a setting: another manifest changes URL, secret and headers without code") {
    Env env;
    env.cfg.set("llm_provider", "local_llm");
    env.cfg.set("provider_manifests", Json::array({Json{
        {"id", "local_llm"}, {"display_name", "Local"}, {"base_url", "http://localhost:11434/v1/"},
        {"auth_secret", "local_key"}, {"auth_scheme", "token"},
        {"default_headers", Json{{"X-Tenant", "t1"}}}, {"metadata", Json{{"adapter", "openai_chat"}}},
        {"capabilities", Json::array({Json{{"resource", "llm"}, {"name", "chat.completions"}}})}}}));
    env.secrets.set("local_key", "local-secret");
    env.transport.expect("POST", "http://localhost:11434/v1/chat/completions", Reply::json(200, chat_reply("local ok")));
    auto outcome = unwrap(llm::use_model(env.services(), simple("llama")));
    REQUIRE(outcome.results[0].ok());
    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].url == "http://localhost:11434/v1/chat/completions");
    CHECK(net::header_value(reqs[0].headers, "Authorization") == "token local-secret");
    CHECK(net::header_value(reqs[0].headers, "X-Tenant") == "t1");
    CHECK(outcome.results[0].provider == "local_llm");

    // A missing secret is reported per variant, not thrown.
    env.secrets.set("local_key", "");
    auto missing = unwrap(llm::use_model(env.services(), simple("llama")));
    CHECK(missing.results[0].state == llm::State::Unavailable);
    REQUIRE(missing.results[0].error.has_value());
    CHECK(missing.results[0].error->code == Errc::Auth);
    CHECK(env.transport.requests().size() == 1);  // nothing sent
  }

  TEST_CASE("adapter chosen from manifest data: Anthropic Messages") {
    Env env;
    env.cfg.set("llm_provider", "anthropic");
    env.secrets.set("anthropic_batch_key", "ant-key");
    env.transport.expect("POST", "https://api.anthropic.com/v1/messages",
        Reply::json(200, Json{{"model", "claude-x"}, {"content", Json::array({Json{{"type", "text"}, {"text", "Bonjour"}}})},
                              {"usage", Json{{"input_tokens", 4}, {"output_tokens", 2}}}}));
    llm::ModelRequest req = simple("claude-x", "salut");
    req.messages = Json::array({Json{{"role", "system"}, {"content", "Be brief."}}});
    auto outcome = unwrap(llm::use_model(env.services(), req));
    REQUIRE(outcome.results[0].ok());
    CHECK(outcome.results[0].adapter == "anthropic_messages");
    CHECK(outcome.results[0].text == "Bonjour");
    CHECK(outcome.results[0].usage["output_tokens"] == 2);
    auto sent = env.transport.requests().at(0);
    CHECK(net::header_value(sent.headers, "x-api-key") == "ant-key");
    CHECK(net::header_value(sent.headers, "anthropic-version") == "2023-06-01");
    CHECK(net::header_value(sent.headers, "Authorization") == "");
    Json body = unwrap(json::parse(sent.body));
    CHECK(body["system"] == "Be brief.");
    CHECK(body["messages"] == Json::array({Json{{"role", "user"}, {"content", "salut"}}}));
    CHECK(body["max_tokens"] == 50);

    // max_tokens is required by that API: say so instead of inventing a limit.
    llm::ModelRequest bare = simple("claude-x");
    bare.parameters = Json::object();
    auto refused = unwrap(llm::use_model(env.services(), bare));
    CHECK(refused.results[0].state == llm::State::Unavailable);
    CHECK(env.transport.requests().size() == 1);
  }

  TEST_CASE("sequential variants: every model runs in order and every result is returned") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::text(500, "boom"));
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("second")));
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("third")));
    llm::ModelRequest req = simple("a/one");
    req.mode = "sequential";
    req.models = {llm::ModelSpec{"a/one", "", Json::object()},
                  llm::ModelSpec{"b/two", "", Json{{"temperature", 0.9}}},
                  llm::ModelSpec{"c/three", "", Json::object()}};
    auto outcome = unwrap(llm::use_model(env.services(), req));
    REQUIRE(outcome.results.size() == 3);
    CHECK(outcome.results[0].state == llm::State::HttpError);
    CHECK(outcome.results[0].http_status == 500);
    CHECK(outcome.results[0].body == "boom");
    CHECK(outcome.results[1].text == "second");
    CHECK(outcome.results[2].text == "third");
    CHECK(outcome.first_ok() == &outcome.results[1]);
    auto reqs = env.transport.requests();
    REQUIRE(reqs.size() == 3);
    CHECK(unwrap(json::parse(reqs[0].body))["model"] == "a/one");
    CHECK(unwrap(json::parse(reqs[1].body))["model"] == "b/two");
    CHECK(unwrap(json::parse(reqs[1].body))["temperature"] == 0.9);   // per-variant parameter wins
    CHECK(unwrap(json::parse(reqs[2].body))["temperature"] == 0.2);   // shared parameter stays
    CHECK(env.provenance.size() == 3);
    CHECK(env.provenance[1]["variant"] == 1);
    CHECK(env.provenance[1]["requested_model"] == "b/two");
  }

  TEST_CASE("first_success: stops at the first variant that works, falls through failures") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::fail(Errc::Network, "down"));
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("fallback")));
    llm::ModelRequest req = simple("a/one");
    req.mode = "first_success";
    req.models = {llm::ModelSpec{"a/one", "", Json::object()}, llm::ModelSpec{"b/two", "", Json::object()},
                  llm::ModelSpec{"c/three", "", Json::object()}};
    auto outcome = unwrap(llm::use_model(env.services(), req));
    REQUIRE(outcome.results.size() == 2);   // the third was never attempted
    CHECK(outcome.results[0].state == llm::State::TransportError);
    REQUIRE(outcome.results[0].error.has_value());
    CHECK(outcome.results[0].error->code == Errc::Network);
    CHECK(outcome.results[1].ok());
    CHECK(outcome.first_ok()->text == "fallback");
    CHECK(outcome.ok());
    CHECK(env.transport.requests().size() == 2);

    // All variants fail: every failure is returned, nothing is ok.
    Env bad;
    bad.transport.set_fallback(Reply::text(503, "busy"));
    llm::ModelRequest all = req;
    auto none = unwrap(llm::use_model(bad.services(), all));
    CHECK(none.results.size() == 3);
    CHECK_FALSE(none.ok());

    // A variant whose provider has no credential is skipped, the next one runs.
    Env skip;
    skip.cfg.set("provider_manifests", Json::array({Json{{"id", "p2"}, {"base_url", "https://p2.test"},
        {"auth_secret", "p2_key"}, {"auth_scheme", "bearer"}}}));
    skip.transport.expect("POST", "https://api.test/", Reply::json(200, chat_reply("from default")));
    llm::ModelRequest mixed = simple("a/one");
    mixed.mode = "first_success";
    mixed.models = {llm::ModelSpec{"a/one", "p2", Json::object()}, llm::ModelSpec{"b/two", "", Json::object()}};
    auto picked = unwrap(llm::use_model(skip.services(), mixed));
    REQUIRE(picked.results.size() == 2);
    CHECK(picked.results[0].state == llm::State::Unavailable);
    CHECK(picked.results[1].ok());
    CHECK(skip.transport.requests().size() == 1);
  }

  TEST_CASE("invalid requests are errors, not results") {
    Env env;
    llm::ModelRequest none;
    CHECK_FALSE(llm::use_model(env.services(), none));
    llm::ModelRequest two = simple("a");
    two.models.push_back(llm::ModelSpec{"b", "", Json::object()});
    CHECK_FALSE(llm::use_model(env.services(), two));   // mode single, two models
    two.mode = "everything";
    CHECK_FALSE(llm::use_model(env.services(), two));
    CHECK(env.transport.requests().empty());
  }

  TEST_CASE("context reference and prompt become the messages") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("ok")));
    auto services = env.services();
    services.resolve_context = [](const Json& ref) -> Result<Json> {
      return Json::array({Json{{"role", "system"}, {"content", "recipe:" + ref.value("recipe", std::string())}}});
    };
    llm::ModelRequest req = simple("m");
    req.context = Json{{"recipe", "r1"}};
    unwrap(llm::use_model(services, req));
    Json body = unwrap(json::parse(env.transport.requests().at(0).body));
    CHECK(body["messages"] == Json::array({Json{{"role", "system"}, {"content", "recipe:r1"}},
                                           Json{{"role", "user"}, {"content", "hello"}}}));
    llm::ModelRequest unresolved = simple("m");
    unresolved.context = Json{{"recipe", "r1"}};
    Env other;
    auto outcome = unwrap(llm::use_model(other.services(), unresolved));   // no resolver
    CHECK(outcome.results[0].state == llm::State::Unavailable);
  }

  TEST_CASE("streaming: hooks see text and reasoning, the decoder collects usage and model") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions",
        Reply::sse({R"({"model":"s/m","choices":[{"delta":{"reasoning":"think"}}]})",
                    R"({"choices":[{"delta":{"content":"he"}}]})", "not json",
                    R"({"choices":[{"delta":{"content":"llo"}}],"usage":{"completion_tokens":2}})", "[DONE]"}));
    llm::ModelRequest req = simple("m");
    req.stream = true;
    std::string text, reasoning, raw;
    std::vector<std::string> issues;
    req.hooks.on_text = [&](std::string_view t) { text += std::string(t); };
    req.hooks.on_reasoning = [&](std::string_view t) { reasoning += std::string(t); };
    req.hooks.on_raw = [&](std::string_view c) { raw += std::string(c); };
    req.hooks.on_issue = [&](std::string_view i) { issues.emplace_back(i); };
    auto outcome = unwrap(llm::use_model(env.services(), req));
    const auto& r = outcome.results.at(0);
    CHECK(r.ok());
    CHECK(r.text == "hello");
    CHECK(text == "hello");
    CHECK(reasoning == "think");
    CHECK(r.reasoning == "think");
    CHECK(r.reported_model == "s/m");
    CHECK(r.usage["completion_tokens"] == 2);
    CHECK(raw.size() == r.received_bytes);
    CHECK(issues == std::vector<std::string>{"malformed_chunk"});
    auto sent = env.transport.requests().at(0);
    CHECK(sent.stream);
    CHECK(unwrap(json::parse(sent.body))["stream"] == true);

    // Cancellation from a hook stops the stream and keeps what arrived.
    Env cancelled;
    cancelled.transport.expect("POST", "https://api.test/chat/completions",
        Reply::sse({R"({"choices":[{"delta":{"content":"one"}}]})", R"({"choices":[{"delta":{"content":"two"}}]})", "[DONE]"}));
    CancelToken token;
    llm::ModelRequest req2 = simple("m");
    req2.stream = true;
    req2.cancel = &token;
    req2.hooks.on_text = [&](std::string_view) { token.cancel(); };
    auto stopped = unwrap(llm::use_model(cancelled.services(), req2));
    CHECK(stopped.results.at(0).state == llm::State::Cancelled);
    CHECK(stopped.results.at(0).text == "one");
  }

  TEST_CASE("the shared usage guard holds an x10 call: nothing is sent until the owner confirms") {
    Env env;
    for (int i = 0; i < 4; ++i)
      env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("ok")));
    for (int i = 0; i < 3; ++i) REQUIRE(unwrap(llm::use_model(env.services(), simple("g/m", "tiny " + std::to_string(i)))).ok());
    REQUIRE(env.transport.requests().size() == 3);

    const std::string big(60000, 'x');
    auto held = unwrap(llm::use_model(env.services(), simple("g/m", big)));
    REQUIRE(held.results.size() == 1);
    const auto& h = held.results[0];
    CHECK(h.state == llm::State::Held);
    CHECK_FALSE(h.sent);
    REQUIRE(h.hold.is_object());
    CHECK(h.hold["status"] == "requires_confirmation");
    CHECK(env.transport.requests().size() == 3);             // not sent
    CHECK(env.provenance.back()["status"] == "held");

    // Confirmed re-send of the identical request goes out once.
    llm::ModelRequest again = simple("g/m", big);
    again.usage.confirmation = Json{{"approved", true},
                                    {"receipt_id", h.hold["decision"]["receipt_id"]},
                                    {"ref", "test"},
                                    {"operation_id", h.hold["decision"]["operation_id"]}};
    auto sent = unwrap(llm::use_model(env.services(), again));
    CHECK(sent.results.at(0).ok());
    CHECK(env.transport.requests().size() == 4);
  }

  TEST_CASE("a held variant stops a variant chain (no sneaking past the owner)") {
    Env env;
    for (int i = 0; i < 5; ++i)
      env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("ok")));
    for (int i = 0; i < 3; ++i) REQUIRE(unwrap(llm::use_model(env.services(), simple("g/m", "tiny " + std::to_string(i)))).ok());
    llm::ModelRequest req = simple("g/m", std::string(60000, 'x'));
    req.mode = "first_success";
    req.models = {llm::ModelSpec{"g/m", "", Json::object()}, llm::ModelSpec{"other/model", "", Json::object()}};
    auto outcome = unwrap(llm::use_model(env.services(), req));
    REQUIRE(outcome.results.size() == 1);
    CHECK(outcome.results[0].state == llm::State::Held);
    CHECK(env.transport.requests().size() == 3);
  }

  TEST_CASE("Anthropic Message Batches are operations of the same adapter") {
    Env env;
    env.secrets.set("anthropic_batch_key", "ant-key");
    llm::ModelRequest req;
    req.kind = "test.batch";
    req.models = {llm::ModelSpec{}};
    req.route.provider = "anthropic";
    req.operation = llm::Operation::BatchSubmit;
    req.batch_requests = Json::array({Json{{"custom_id", "m_1"}, {"params", Json{{"model", "c"}, {"max_tokens", 5},
        {"messages", Json::array({Json{{"role", "user"}, {"content", "x"}}})}}}}});
    req.timeout_ms = 60000;
    req.usage.guard = false;
    auto prepared = unwrap(llm::prepare_call(env.services(), req));
    CHECK(prepared.request.method == "POST");
    CHECK(prepared.request.url == "https://api.anthropic.com/v1/messages/batches");
    CHECK(net::header_value(prepared.request.headers, "x-api-key") == "ant-key");
    CHECK(unwrap(json::parse(prepared.request.body))["requests"][0]["custom_id"] == "m_1");

    req.operation = llm::Operation::BatchStatus;
    req.target = "batch_9";
    auto status = unwrap(llm::prepare_call(env.services(), req));
    CHECK(status.request.method == "GET");
    CHECK(status.request.url == "https://api.anthropic.com/v1/messages/batches/batch_9");

    env.transport.expect("POST", "https://api.anthropic.com/v1/messages/batches", Reply::json(201, Json{{"id", "batch_9"}}));
    req.operation = llm::Operation::BatchSubmit;
    auto submitted = unwrap(llm::use_model(env.services(), req));
    CHECK(submitted.results.at(0).state == llm::State::Ok);   // 201 is success for a submit
    CHECK(submitted.results.at(0).http_status == 201);
  }

  TEST_CASE("adapters are registered by family; a new family needs no call-site edit") {
    auto families = llm::adapter_families();
    CHECK(std::find(families.begin(), families.end(), "openai_chat") != families.end());
    CHECK(std::find(families.begin(), families.end(), "anthropic_messages") != families.end());
    CHECK(llm::make_adapter("nope") == nullptr);
  }

  TEST_CASE("typed parts: text and json inputs fold into user messages; parts round-trip as JSON") {
    Env env;
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("ok")));
    llm::ModelRequest req = simple("m", "");
    req.prompt.clear();
    llm::Part text;
    text.kind = "text";
    text.text = "describe this";
    llm::Part structured;
    structured.kind = "json";
    structured.value = Json{{"edge", "a->b"}, {"weight", 3}};
    req.inputs = {text, structured};
    auto outcome = unwrap(llm::use_model(env.services(), req));
    REQUIRE(outcome.results[0].ok());
    Json body = unwrap(json::parse(env.transport.requests().at(0).body));
    CHECK(body["messages"] == Json::array({Json{{"role", "user"}, {"content", "describe this"}},
                                           Json{{"role", "user"}, {"content", R"({"edge":"a->b","weight":3})"}}}));
    // The text result is also a typed output part.
    REQUIRE(outcome.results[0].outputs.size() == 1);
    CHECK(outcome.results[0].outputs[0].kind == "text");
    CHECK(outcome.results[0].outputs[0].text == "ok");
    CHECK(outcome.results[0].provenance["input_kinds"] == Json::array({"json", "text"}));

    llm::Part image;
    image.kind = "image";
    image.blob_hash = std::string(64, 'a');
    image.mime = "image/png";
    image.node_id = "n_1";
    auto round = unwrap(llm::Part::from_json(image.to_json()));
    CHECK(round.kind == "image");
    CHECK(round.blob_hash == image.blob_hash);
    CHECK(round.node_id == "n_1");
    CHECK_FALSE(llm::Part::from_json(Json{{"kind", "hologram"}}));
    CHECK(llm::known_part_kind("embedding"));
  }

  TEST_CASE("capabilities as data: a mismatch is rejected before any send, with the reason") {
    Env env;
    llm::ModelRequest audio = simple("m");
    llm::Part clip;
    clip.kind = "audio";
    clip.blob_hash = std::string(64, 'b');
    audio.inputs = {clip};
    auto rejected = unwrap(llm::use_model(env.services(), audio));
    REQUIRE(rejected.results.size() == 1);
    CHECK(rejected.results[0].state == llm::State::Unavailable);
    REQUIRE(rejected.results[0].error.has_value());
    CHECK(rejected.results[0].error->code == Errc::Unsupported);
    CHECK(rejected.results[0].error->message.find("does not accept audio input") != std::string::npos);
    CHECK(rejected.results[0].error->message.find("openai_chat") != std::string::npos);

    llm::ModelRequest wants_image = simple("m");
    wants_image.wants = {"image"};
    auto no_image = unwrap(llm::use_model(env.services(), wants_image));
    CHECK(no_image.results[0].state == llm::State::Unavailable);
    CHECK(no_image.results[0].error->message.find("cannot produce image") != std::string::npos);

    llm::ModelRequest batch = simple("m");
    batch.operation = llm::Operation::BatchSubmit;
    auto no_batch = unwrap(llm::use_model(env.services(), batch));
    CHECK(no_batch.results[0].error->message.find("no batch mode") != std::string::npos);
    CHECK(env.transport.requests().empty());   // nothing was ever sent

    // The model's own declaration (data) narrows what a request may carry.
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("fine")));
    llm::ModelRequest structured = simple("m");
    llm::Part j;
    j.kind = "json";
    j.value = Json{{"a", 1}};
    structured.inputs = {j};
    CHECK(unwrap(llm::use_model(env.services(), structured)).ok());
    env.cfg.set("model_capabilities", Json{{"m", Json{{"accepts", Json::array({"text"})}}}});
    auto narrowed = unwrap(llm::use_model(env.services(), structured));
    CHECK(narrowed.results[0].state == llm::State::Unavailable);
    CHECK(narrowed.results[0].error->message.find("does not accept json input") != std::string::npos);
    // ... and a per-request model declaration wins over the setting.
    structured.models[0].capabilities = Json{{"accepts", Json::array({"text", "json"})}};
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("again")));
    CHECK(unwrap(llm::use_model(env.services(), structured)).ok());

    // A rejected variant is reported and the chain goes on (first_success).
    env.transport.expect("POST", "https://api.test/chat/completions", Reply::json(200, chat_reply("second")));
    llm::ModelRequest chain = simple("m");
    chain.mode = "first_success";
    chain.models = {llm::ModelSpec{"only-text", "", Json::object(), Json{{"produces", Json::array({"image"})}}},
                    llm::ModelSpec{"m", "", Json::object()}};
    auto both = unwrap(llm::use_model(env.services(), chain));
    REQUIRE(both.results.size() == 2);
    CHECK(both.results[0].state == llm::State::Unavailable);
    CHECK(both.results[1].ok());
  }

  TEST_CASE("image generation adapter: bytes land in the blob store with a node and provenance (fake transport)") {
    Env env;
    auto db = loom::test::open_db(env.td.path() / "outputs.db");
    BlobStore blobs(env.td.path() / "blobs", *db);
    ProvenanceStore provenance(*db);
    auto store = llm::make_output_store(blobs, &provenance, db.get());
    env.cfg.set("provider_manifests", Json::array({Json{
        {"id", "img"}, {"base_url", "https://img.test/v1"}, {"auth_secret", "img_key"}, {"auth_scheme", "bearer"},
        {"metadata", Json{{"adapter", "openai_images"}}}}}));
    env.secrets.set("img_key", "img-secret");
    const std::string png = std::string("\x89PNG\r\n\x1a\n") + "FAKEPIXELS";
    env.transport.expect("POST", "https://img.test/v1/images/generations",
        Reply::json(200, Json{{"created", 1}, {"data", Json::array({Json{{"b64_json", base64::encode(png)}}})}}));

    llm::ModelRequest req;
    req.kind = "test.image";
    req.models = {llm::ModelSpec{"image-1", "img", Json::object()}};
    req.prompt = "a red fox";
    req.parameters = Json{{"n", 1}, {"size", "256x256"}, {"response_format", "b64_json"}};
    req.wants = {"image"};
    req.referer = false;
    req.timeout_ms = 5000;
    auto services = env.services();
    services.outputs = store.get();
    auto outcome = unwrap(llm::use_model(services, req));
    REQUIRE(outcome.results.size() == 1);
    const auto& r = outcome.results[0];
    INFO((r.error.has_value() ? r.error->message : std::string("no error")));
    REQUIRE(r.ok());
    CHECK(r.adapter == "openai_images");

    CHECK(loom::test::request_json(env.transport.requests().at(0)) ==
          Json{{"method", "POST"}, {"url", "https://img.test/v1/images/generations"},
               {"headers", Json::array({Json::array({"Authorization", "Bearer img-secret"}),
                                        Json::array({"Content-Type", "application/json"})})},
               {"body", R"({"model":"image-1","prompt":"a red fox","n":1,"size":"256x256","response_format":"b64_json"})"},
               {"timeout_ms", 5000}, {"stream", false}});

    REQUIRE(r.outputs.size() == 1);
    const llm::Part& out = r.outputs[0];
    CHECK(out.kind == "image");
    CHECK(out.mime == "image/png");
    CHECK(out.bytes.empty());                         // bytes live in the store, not in the result
    CHECK(out.blob_hash == Sha256::hex(png));
    CHECK(unwrap(blobs.read(out.blob_hash)) == png);
    REQUIRE_FALSE(out.node_id.empty());
    auto node = unwrap(db->get_node(out.node_id));
    REQUIRE(node.has_value());
    CHECK(node->kind == "blob");
    CHECK(node->metadata["blob_hash"] == out.blob_hash);
    CHECK(node->metadata["produced_by"]["requested_model"] == "image-1");
    CHECK(node->metadata["produced_by"]["adapter"] == "openai_images");
    auto artifacts = unwrap(provenance.list_artifacts(10, "model_output"));
    REQUIRE(artifacts.size() == 1);
    CHECK(artifacts[0].blob_hash == out.blob_hash);
    CHECK(r.provenance["outputs"][0]["blob_hash"] == out.blob_hash);
    CHECK(r.provenance.dump().find("FAKEPIXELS") == std::string::npos);
    CHECK(r.provenance.dump().find("img-secret") == std::string::npos);
  }

  TEST_CASE("image generation: refused before sending without an output store, and for text/stream requests") {
    Env env;
    env.cfg.set("provider_manifests", Json::array({Json{
        {"id", "img"}, {"base_url", "https://img.test/v1"}, {"auth_secret", "img_key"}, {"auth_scheme", "bearer"},
        {"metadata", Json{{"adapter", "openai_images"}}}}}));
    env.secrets.set("img_key", "img-secret");
    llm::ModelRequest req;
    req.models = {llm::ModelSpec{"image-1", "img", Json::object()}};
    req.prompt = "a red fox";
    req.parameters = Json{{"response_format", "b64_json"}};
    req.wants = {"image"};
    auto no_store = unwrap(llm::use_model(env.services(), req));
    CHECK(no_store.results[0].state == llm::State::Unavailable);
    CHECK(no_store.results[0].error->message.find("no output store") != std::string::npos);

    llm::ModelRequest wants_text = req;
    wants_text.wants = {"text"};
    CHECK(unwrap(llm::use_model(env.services(), wants_text)).results[0].error->message.find("cannot produce text") != std::string::npos);
    llm::ModelRequest streamed = req;
    streamed.stream = true;
    CHECK(unwrap(llm::use_model(env.services(), streamed)).results[0].error->message.find("no stream mode") != std::string::npos);
    CHECK(env.transport.requests().empty());

    // A URL reply is a reference: no store needed, nothing is downloaded.
    env.transport.expect("POST", "https://img.test/v1/images/generations",
        Reply::json(200, Json{{"data", Json::array({Json{{"url", "https://cdn.test/fox.png"}}})}}));
    llm::ModelRequest by_url = req;
    by_url.parameters = Json{{"response_format", "url"}};
    auto referenced = unwrap(llm::use_model(env.services(), by_url));
    REQUIRE(referenced.results[0].ok());
    CHECK(referenced.results[0].outputs.at(0).url == "https://cdn.test/fox.png");
    CHECK(referenced.results[0].outputs.at(0).blob_hash.empty());
  }
}
