// P21: the context guardian - a secretary that builds the representation of a conversation beside the raw messages
// (loom/guardian.h; docs/architecture/STRAZNIK_I_SRODOWISKA_2026-10-14.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>

#include "loom/action_overlay.h"
#include "loom/annotation_layer.h"
#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/guardian.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

Json chat_reply(const std::string& content) {
  return Json{{"choices", Json::array({Json{{"message", Json{{"role", "assistant"}, {"content", content}}}}})}, {"usage", Json{{"total_tokens", 7}}}};
}

struct Fixture {
  fsutil::TempDir td;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> rt;
  std::string conv;

  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    o.http = transport;
    rt = unwrap(Runtime::open(o));
    rt->secrets().set("api_key", "sk-test-key");
    rt->config().set("base_url", "https://guardian.test");
    rt->config().set("default_model", "test/model");
    rt->config().set("system_prompt", "System instructions.");
    rt->config().set("semantic_analysis", false);
    rt->config().set("model_registry_autorefresh", "off");
    conv = unwrap(rt->db().create_conv("Guardian")).id;
  }

  // A message as the chat engine stores it, announced on the bus like any other.
  std::string say(const std::string& role, const std::string& text, bool announce = true) {
    NewMessage m;
    m.conv_id = conv;
    m.role = role;
    m.text = text;
    const std::string id = unwrap(rt->db().create_msg(m));
    if (announce) rt->bus().emit(events::kMsgCreated, Json{{"id", id}, {"text", text}, {"conv_id", conv}, {"role", role}});
    return id;
  }
  std::string user(const std::string& text) { return say("user", text); }
  std::string assistant(const std::string& text = "Understood.") { return say("assistant", text); }

  Json invoke(const std::string& action, const Json& request) { return unwrap(rt->guardian().invoke(action, request)); }
  Json status() { return invoke("guardian.status", Json{{"conv_id", conv}}); }
  Json hypothesis(const std::string& signal = "") {
    const Json st = status();
    for (const auto& h : st.at("hypotheses"))
      if (signal.empty() || h.at("signal") == signal) return h;
    return Json(nullptr);
  }
  Json last(const std::string& role = "assistant") {
    auto msgs = unwrap(rt->db().get_msgs(conv));
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it)
      if (role.empty() || it->role == role) return it->to_json();
    return Json();
  }
  chat_route::Outcome input(const std::string& text) {
    ChatOptions opts;
    opts.conv_id = conv;
    opts.stream = false;
    return unwrap(chat_route::handle(*rt, text, opts, Json{{"message", text}}, nullptr));
  }
  std::filesystem::path overlay() const { return td.path() / "profiles" / "guardian.pack"; }
  Json overlay_doc() const {
    std::ifstream in(overlay());
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return body.empty() ? Json::object() : unwrap(json::parse(body));
  }
  Json sent(const std::string& text, const std::string& reply = "A reply.") {
    transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(reply)));
    ChatOptions o;
    o.conv_id = conv;
    o.stream = false;
    o.include_memory = false;
    o.include_graph_memory = false;
    (void)unwrap(rt->chat().send(text, o));
    const auto reqs = transport->requests();
    return unwrap(json::parse(reqs.back().body))["messages"];
  }
  std::size_t links(const std::string& node, const std::string& type) {
    return unwrap(rt->db().get_links(node, type)).size();
  }
};

bool has_text(const Json& messages, const std::string& needle) {
  for (const auto& m : messages)
    if (m["content"].is_string() && m["content"].get<std::string>().find(needle) != std::string::npos) return true;
  return false;
}
int exact_user(const Json& messages, const std::string& text) {
  int n = 0;
  for (const auto& m : messages)
    if (m["role"] == "user" && m["content"] == text) ++n;
  return n;
}

}  // namespace

TEST_SUITE("guardian") {
  TEST_CASE("noise: the cheap categories are annotations, L1 is a derived node, the raw text is never touched, a drop can be taken back") {
    Fixture f;
    const std::string raw = "yyy I want, I want a list of of tasks um for Friday";
    const std::string mid = f.user(raw);
    const Json lv = f.invoke("guardian.levels", Json{{"message_id", mid}});
    const Json row = lv.at("messages")[0];
    CHECK(row.at("raw") == raw);
    REQUIRE(row.at("levels").contains("L1"));
    CHECK(row.at("levels").at("L1").at("text") == "I want, a list of of tasks for Friday");  // a doubled single word may be emphasis: confidence 0.3, kept
    std::set<std::string> categories;
    for (const auto& d : row.at("levels").at("L1").at("dropped")) categories.insert(d.at("category"));
    CHECK(categories == std::set<std::string>{"filler", "repetition"});
    // The annotations are the shared layer: message -> span -> category node, produced once.
    CHECK(row.at("annotations").size() == 4);
    CHECK(unwrap(f.rt->db().get_msg(mid))->text == raw);
    CHECK(f.invoke("guardian.observe", Json{{"message_id", mid}, {"post", false}}).at("annotations_written") == 0);
    // Take one drop back: the level is rebuilt without it.
    std::string filler_annotation;
    for (const auto& a : row.at("annotations"))
      if (a.at("category") == "filler" && a.at("excerpt") == "yyy") filler_annotation = a.at("id");
    REQUIRE_FALSE(filler_annotation.empty());
    const Json undone = f.invoke("guardian.undo", Json{{"annotation", filler_annotation}});
    CHECK(undone.at("levels").at("L1").at("text") == "yyy I want, a list of of tasks for Friday");
    CHECK(unwrap(f.rt->db().get_msg(mid))->text == raw);
    // A message with nothing to drop has no level at all.
    const std::string clean = f.user("Write the report for Friday");
    CHECK(f.invoke("guardian.levels", Json{{"message_id", clean}}).at("messages")[0].at("levels").empty());
  }

  TEST_CASE("correction: occurrence, hypothesis with escalation, evidence edges, a stated exception, then a proposal after the reply") {
    Fixture f;
    f.user("Write a summary of the quarterly report");
    f.assistant("Here is a summary with a table.");
    const std::string m1 = f.user("nie chodziło mi o tabelę, zrób listę, poza nagłówkami");
    Json h = f.hypothesis("course_correction");
    REQUIRE_FALSE(h.is_null());
    CHECK(h.at("count") == 1);
    CHECK(h.at("weight") == 1.0);
    CHECK(h.at("status") == "observing");
    CHECK(h.at("scope") == "conversation");
    REQUIRE(h.at("exceptions").size() == 1);
    CHECK(h.at("exceptions")[0].at("text") == "nagłówkami");
    CHECK(h.at("exceptions")[0].at("explicit") == true);
    // Nodes and edges: occurrence -> message (derived_from), occurrence -> hypothesis (supports), exception -> hypothesis (except).
    const std::string occ = h.at("occurrences")[0].at("occurrence");
    CHECK(f.links(occ, "derived_from") == 1);
    CHECK(f.links(occ, "supports") == 1);
    CHECK(f.links(h.at("exceptions")[0].at("node"), "except") == 1);
    CHECK(unwrap(f.rt->db().get_node(occ))->kind == "guardian_occurrence");
    f.assistant("Here is a list.");
    CHECK(f.last().at("metadata").contains("guardian") == false);  // nothing yet: one calm correction is below the threshold
    // The second time, sharper: the curve and the repetition marker raise the weight; the hypothesis is ready and is proposed after the reply.
    f.user("mówiłem już: nie chodziło mi o tabelę, zrób listę!!");
    h = f.hypothesis("course_correction");
    CHECK(h.at("count") == 2);
    CHECK(h.at("weight").get<double>() > 3.0);
    CHECK(h.at("status") == "ready");
    f.assistant("Sorry, here is the list again.");
    h = f.hypothesis("course_correction");
    CHECK(h.at("status") == "proposed");
    const Json proposal = f.last();
    CHECK(proposal.at("metadata").at("action_proposal").at("action") == "guardian.promote");
    CHECK(proposal.at("metadata").at("action_proposal").at("status") == "pending");
    CHECK(proposal.at("metadata").at("guardian").at("traffic") == true);
    // The proposal states a rule, it does not repeat the quote: the concepts the corrections agree on (lists yes, tables no) name it.
    CHECK(proposal.at("text").get<std::string>().find("Używaj list") != std::string::npos);
    CHECK(proposal.at("text").get<std::string>().find("Nie używaj tabel") != std::string::npos);
    CHECK(proposal.at("text").get<std::string>().find("nagłówkami") != std::string::npos);
    (void)m1;
    // The raw list is untouched and the proposal exchange is not part of what the model reads.
    const Json built = unwrap(f.rt->chat().build_messages(f.conv, "next", {}));
    CHECK_FALSE(has_text(built, "Zauważyłem"));
    CHECK(has_text(built, "nie chodziło mi o tabelę, zrób listę, poza nagłówkami"));
  }

  TEST_CASE("the user's yes writes an overlay rule and the conversation's choice; undo takes both back; nothing is deleted") {
    Fixture f;
    f.user("Write a summary of the quarterly report");
    f.assistant();
    f.user("nie chodziło mi o tabelę, zrób listę, poza nagłówkami");
    f.assistant();
    f.user("mówiłem już: nie chodziło mi o tabelę, zrób listę!!");
    f.assistant();
    const auto before = unwrap(f.rt->db().get_msgs(f.conv)).size();
    const auto outcome = f.input("tak");
    CHECK(outcome.handled);
    const Json h = f.hypothesis("course_correction");
    CHECK(h.at("status") == "promoted");
    const std::string rule = h.at("rule");
    REQUIRE_FALSE(rule.empty());
    const Json doc = f.overlay_doc();
    CHECK(doc.at("overrides").at("rules").contains(rule));
    CHECK(doc.at("overrides").at("rules").at(rule).at("exceptions")[0].at("text") == "nagłówkami");
    CHECK(doc.at("overrides").at("rules").at(rule).at("scope") == "conversation");
    // The selector's choice for this conversation is a rule of the user's overlay, not a change of the global preset.
    const auto sel = unwrap(guardian::select(f.rt->db(), f.rt->config(), f.conv));
    CHECK(sel.preset == "guardian");
    CHECK(sel.spec.has_value());
    CHECK(f.rt->config().get("chat_representation").is_null());
    CHECK(unwrap(guardian::select(f.rt->db(), f.rt->config(), unwrap(f.rt->db().create_conv("Other")).id)).preset == "raw");
    // The next send carries the rule as a compiled instruction; the proposal exchange is not in it.
    const Json messages = f.sent("Now write it.");
    CHECK(has_text(messages, "[goal] Write a summary of the quarterly report"));
    CHECK(has_text(messages, "Exception: nagłówkami"));
    CHECK_FALSE(has_text(messages, "Zauważyłem"));
    // Undo: the rule leaves the overlay, the conversation returns to raw, the hypothesis and its occurrences stay.
    const Json undone = f.invoke("guardian.undo", Json{{"rule", rule}});
    CHECK(undone.at("rule_removed") == true);
    CHECK_FALSE(f.overlay_doc().value("overrides", Json::object()).value("rules", Json::object()).contains(rule));
    CHECK(unwrap(guardian::select(f.rt->db(), f.rt->config(), f.conv)).preset == "raw");
    CHECK(f.hypothesis("course_correction").at("status") == "undone");
    CHECK(f.hypothesis("course_correction").at("occurrences").size() == 2);
    CHECK(unwrap(f.rt->db().get_msgs(f.conv)).size() >= before);
  }

  TEST_CASE("a no is remembered: the hypothesis is not asked again until its weight doubles") {
    Fixture f;
    f.user("Write a summary of the quarterly report");
    f.assistant();
    f.user("nie chodziło mi o tabelę, zrób listę");
    f.assistant();
    f.user("mówiłem już: nie chodziło mi o tabelę, zrób listę!!");
    f.assistant();
    REQUIRE(f.hypothesis("course_correction").at("status") == "proposed");
    f.input("nie");
    Json h = f.hypothesis("course_correction");
    CHECK(h.at("status") == "rejected");
    CHECK(f.overlay_doc().value("overrides", Json::object()).empty());
    f.assistant();
    CHECK(f.hypothesis("course_correction").at("status") == "rejected");
  }

  TEST_CASE("a run of clarifications: three refinements become one compiled instruction through the existing task specification") {
    Fixture f;
    const std::string goal = f.user("Write a short report about the quarterly sales numbers");
    f.assistant();
    const std::string r1 = f.user("Also add a table of the regions");
    f.assistant();
    const std::string r2 = f.user("Additionally make sure the totals are in euros");
    f.assistant();
    CHECK(f.hypothesis("refinement").at("count") == 2);
    CHECK(f.hypothesis("refinement").at("status") == "observing");
    const std::string r3 = f.user("Plus include a short summary at the end");
    CHECK(f.hypothesis("refinement").at("status") == "ready");
    CHECK(f.hypothesis("refinement").at("goal_message_id") == goal);
    // Before anything is accepted the request is the raw one (the golden shape).
    f.assistant();
    const Json proposal = f.last();
    CHECK(proposal.at("metadata").at("guardian").at("promotes") == "transform");
    const std::string instruction = proposal.at("metadata").at("guardian").at("instruction");
    CHECK(instruction.find("[goal] Write a short report about the quarterly sales numbers") != std::string::npos);
    CHECK(instruction.find("[format] Also add a table of the regions") != std::string::npos);
    CHECK(instruction.find("[style] Plus include a short summary at the end") != std::string::npos);
    CHECK(f.input("yes").handled);
    const Json h = f.hypothesis("refinement");
    CHECK(h.at("status") == "promoted");
    const auto rep = unwrap(f.rt->db().find_node("rep:" + f.conv, "guardian_representation"));
    REQUIRE(rep);
    CHECK(rep->content == instruction);
    CHECK(f.links(rep->id, "derived_from") == 4);  // the goal and the three clarifications
    // From now on a send uses the representation: one instruction in place of the run, everything else as it was.
    const Json messages = f.sent("Now produce the report.");
    CHECK(has_text(messages, instruction));
    CHECK(exact_user(messages, "Also add a table of the regions") == 0);
    CHECK(exact_user(messages, "Plus include a short summary at the end") == 0);
    CHECK(exact_user(messages, "Write a short report about the quarterly sales numbers") == 0);
    CHECK(messages.back().at("content") == "Now produce the report.");
    // The raw messages are all still there.
    for (const auto& id : {goal, r1, r2, r3}) CHECK(unwrap(f.rt->db().get_msg(id))->status == "active");
    // Switched off by the user's own setting for the conversation, the request is raw again.
    (void)f.invoke("guardian.undo", Json{{"hypothesis", h.at("id")}});
    const Json raw = f.sent("And again.");
    CHECK(exact_user(raw, "Also add a table of the regions") == 1);
    CHECK_FALSE(has_text(raw, "[goal]"));
  }

  TEST_CASE("golden: guardian off, or nothing proposed, or nothing accepted - the request bytes are those of a conversation without a guardian") {
    auto run = [](const char* method) {
      Fixture f;
      if (std::string(method) != "default") f.rt->config().set("guardian_method", method);
      f.user("Write a short report about the quarterly sales numbers");
      f.assistant();
      f.user("Also add a table of the regions");
      f.assistant();
      f.user("yyy um I want the totals in euros");
      f.assistant();
      f.sent("Now produce the report.");
      return f.transport->requests().back().body;
    };
    const std::string off = run("off");
    CHECK(run("default") == off);  // observed, nothing proposed: the bytes do not change
    CHECK(run("heuristic") == off);
  }

  TEST_CASE("the selector's presets: denoised sends L1, mixed appends the instruction, raw is the preset") {
    Fixture f;
    f.user("Write a short report about the quarterly sales numbers");
    f.assistant();
    const std::string noisy = "yyy um I want the totals in euros";
    f.user(noisy);
    f.assistant();
    const Json raw = f.sent("Go on.");
    CHECK(exact_user(raw, noisy) == 1);
    f.rt->config().set("chat_representation", "denoised");
    const Json lean = f.sent("Go on.");
    CHECK(exact_user(lean, noisy) == 0);
    CHECK(exact_user(lean, "I want the totals in euros") == 1);
    // A preset of the user's own, as data in the overlay profile.
    f.rt->config().set("chat_representation", "no-such-preset");
    CHECK(exact_user(f.sent("Go on."), noisy) == 1);  // an unknown name is the profile's `unknown` preset: raw
    CHECK(f.rt->guardian().invoke("guardian.status", Json{{"conv_id", f.conv}}));
  }

  TEST_CASE("promotion modes: ask asks at the first occurrence, apply writes the rule and says so") {
    {
      Fixture f;
      f.rt->config().set("guardian_promotion", "ask");
      f.user("Write a summary of the quarterly report");
      f.assistant();
      f.user("nie chodziło mi o tabelę, zrób listę");
      CHECK(f.hypothesis("course_correction").at("status") == "ready");
      f.assistant();
      const Json p = f.last();
      CHECK(p.at("metadata").at("guardian").at("mode") == "ask");
      CHECK(p.at("text").get<std::string>().find("Czy zapamiętać") != std::string::npos);
    }
    {
      Fixture f;
      f.rt->config().set("guardian_promotion", "apply");
      f.user("Write a summary of the quarterly report");
      f.assistant();
      f.user("nie chodziło mi o tabelę, zrób listę");
      f.assistant();
      CHECK(f.hypothesis("course_correction").at("status") == "observing");  // the apply threshold is higher
      f.user("mówiłem już: nie chodziło mi o tabelę, zrób listę!!");
      CHECK(f.hypothesis("course_correction").at("status") == "promoted");
      f.assistant();
      const Json note = f.last();
      CHECK(note.at("metadata").at("guardian").at("notice") == "applied");
      CHECK(note.at("metadata").contains("action_proposal") == false);
      CHECK(f.overlay_doc().at("overrides").at("rules").size() == 1);
      CHECK(unwrap(guardian::select(f.rt->db(), f.rt->config(), f.conv)).preset == "guardian");
    }
  }

  TEST_CASE("contradiction: opposite claims are linked, a narrower one is an inferred exception of the wider one") {
    Fixture f;
    const std::string a = f.user("Use tables for the data in every answer");
    const std::string b = f.user("Do not use tables for the data in this answer");
    (void)f.invoke("guardian.observe", Json{{"message_id", a}, {"kind", "course_correction"}, {"subject", "use tables for the data"}, {"scope", "person"}, {"post", false}});
    (void)f.invoke("guardian.observe", Json{{"message_id", b}, {"kind", "course_correction"}, {"subject", "do not use tables for the data"}, {"scope", "message"}, {"post", false}});
    const Json st = f.status();
    const Json hyps = st.at("hypotheses");
    REQUIRE(hyps.size() == 2);
    Json wide, narrow;
    for (const auto& h : hyps) (h.at("scope") == "person" ? wide : narrow) = h;
    REQUIRE_FALSE(wide.is_null());
    REQUIRE_FALSE(narrow.is_null());
    CHECK(f.links(narrow.at("id"), "contradicts") == 2);  // both directions: the edge is mutual
    CHECK(f.links(wide.at("id"), "contradicts") == 2);
    REQUIRE(wide.at("exceptions").size() == 1);
    CHECK(wide.at("exceptions")[0].at("explicit") == false);
    CHECK(f.links(wide.at("exceptions")[0].at("node"), "except") == 1);
    CHECK(wide.at("weight").get<double>() < 1.0);  // the older claim is weighed against the newer
  }

  TEST_CASE("rejection of a proposal is a signal; the guardian's own proposal is not counted twice") {
    Fixture f;
    f.user("We should ship on Friday.");
    f.assistant();
    const auto outcome = f.input("Summarise this conversation");
    REQUIRE(outcome.handled);
    f.input("no");
    const Json h = f.hypothesis("proposal_rejection");
    REQUIRE_FALSE(h.is_null());
    CHECK(h.at("count") == 1);
    CHECK(h.at("weight") == 0.5);
    CHECK(h.at("claim").at("text") == "Summarise");  // the title of the proposal that was declined
    CHECK(f.hypothesis("proposal_rejection").at("status") == "observing");  // recorded, never proposed: no consumer of the rule yet
  }

  TEST_CASE("model method: one guarded reading; an annotation of the analyzer is reused without a second call") {
    Fixture f;
    f.rt->config().set("guardian_method", "model");
    f.rt->config().set("guardian_model", "test/guard");
    f.say("user", "Write a summary of the quarterly report", false);
    f.say("assistant", "ok", false);
    const std::string m = f.say("user", "hmm, actually let's keep it to bullet points", false);
    f.transport->expect("POST", "https://guardian.test/chat/completions",
                        net::ScriptedTransport::Reply::json(200, chat_reply(R"({"kind":"course_correction","confidence":0.9,"subject":"Prefer bullet points","tone":"sharp","exceptions":["the appendix"],"scope":"person"})")));
    const Json r = f.invoke("guardian.annotate", Json{{"message_id", m}, {"post", false}});
    INFO(r.dump());
    CHECK(r.at("issues").empty());
    const Json h = f.hypothesis("course_correction");
    REQUIRE_FALSE(h.is_null());
    CHECK(h.at("claim").at("text") == "Prefer bullet points");
    CHECK(h.at("scope") == "conversation");  // the model's "person" is a request: a question, not a decision
    CHECK(h.at("scope_request") == "person");
    CHECK(h.at("exceptions")[0].at("text") == "the appendix");
    CHECK(h.at("occurrences")[0].at("factor").get<double>() == doctest::Approx(1.5));  // tone: sharp
    CHECK(f.transport->requests().size() == 1);
    // The analyzer annotated another message: the guardian reads that annotation and sends nothing.
    const std::string n = f.say("user", "no no, put the numbers in a table instead", false);
    NodeOptions no;
    no.metadata = Json{{"category", "correction"}};
    const std::string node = unwrap(f.rt->db().get_or_create_node("annotation:correction", "annotation_category", no));
    NewMessageAnnotation ann;
    ann.message_id = n;
    ann.node_id = node;
    ann.start_char = 0;
    ann.end_char = 10;
    ann.origin = "model";
    ann.metadata = Json{{"category", "correction"}, {"producer", "semantic_llm"}, {"confidence", 0.8}};
    (void)unwrap(f.rt->db().create_message_annotation(ann));
    const Json again = f.invoke("guardian.observe", Json{{"message_id", n}, {"post", false}, {"method", "model"}});
    CHECK(again.at("signals")[0].at("detected_by") == "annotation");
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("held or failed model readings never stop anything and are reported") {
    Fixture f;
    f.rt->config().set("guardian_method", "model");
    f.rt->config().set("guardian_model", "test/guard");
    f.say("user", "Write a summary of the quarterly report", false);
    const std::string m = f.say("user", "make it shorter please", false);
    f.transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(500, Json{{"error", "boom"}}));
    const Json r = f.invoke("guardian.annotate", Json{{"message_id", m}, {"post", false}});
    CHECK(r.at("signals").empty());
    CHECK_FALSE(r.at("issues").empty());
    CHECK(unwrap(f.rt->db().get_msg(m))->text == "make it shorter please");
  }

  TEST_CASE("over the API: promote is a run with a receipt, like rules.set; reading is direct") {
    Fixture f;
    f.user("Write a summary of the quarterly report");
    f.assistant();
    const std::string m = f.user("Use bullet points");
    (void)f.invoke("guardian.observe", Json{{"message_id", m}, {"kind", "course_correction"}, {"post", false}});
    const std::string hyp = f.hypothesis("course_correction").at("id");
    const Json run = unwrap(f.rt->actions().invoke("guardian.promote", Json{{"hypothesis", hyp}}));
    CHECK(run.at("status") == "awaiting_confirmation");
    const std::string receipt = run.at("pending").at("receipt_id");
    CHECK_FALSE(receipt.empty());
    CHECK(f.hypothesis("course_correction").at("status") != "promoted");
    const Json done = unwrap(f.rt->actions().confirm(Json{{"run_id", run.at("id")}, {"approved", true}, {"receipt_id", receipt}}));
    CHECK(done.at("status") == "completed");
    CHECK(f.hypothesis("course_correction").at("status") == "promoted");
    const Json read = unwrap(f.rt->actions().invoke("guardian.status", Json{{"conv_id", f.conv}}));
    CHECK(read.at("schema") == "loom.guardian_status/1");
  }
  TEST_CASE("from the conversation window: ask what the guardian noticed, see the proposal, say yes, read the answer (R54)") {
    Fixture f;
    f.user("Write a summary of the quarterly report");
    f.assistant();
    f.user("nie chodziło mi o tabelę, zrób listę");
    const auto out = f.input("what did the guardian notice in this conversation");
    REQUIRE(out.handled);
    const Json proposal = f.last();
    CHECK(proposal.at("metadata").at("action_proposal").at("action") == "guardian.status");
    CHECK(proposal.at("metadata").at("action_proposal").at("status") == "pending");
    f.input("yes");
    const Json answer = f.last();
    INFO(answer.dump());
    CHECK(answer.at("text").get<std::string>().find("What the guardian noticed") != std::string::npos);
    CHECK(answer.at("metadata").at("action_run").at("status") == "completed");
    // The same in Polish, and the element is the conversation the words named.
    const auto pl = f.input("co zauważył strażnik w tej rozmowie");
    REQUIRE(pl.handled);
    CHECK(f.last().at("metadata").at("action_proposal").at("action") == "guardian.status");
    CHECK(f.last().at("metadata").at("action_proposal").at("element").at("id") == f.conv);
  }
  TEST_CASE("hypotheses of different kinds about the same thing support each other") {
    Fixture f;
    const std::string a = f.user("Keep the bullet points for the tables section please");
    const std::string b = f.user("No");
    (void)f.invoke("guardian.observe", Json{{"message_id", a}, {"kind", "course_correction"}, {"subject", "keep the bullet points for the tables section"}, {"post", false}});
    (void)f.invoke("guardian.observe", Json{{"message_id", b}, {"kind", "proposal_rejection"}, {"subject", "bullet points for the tables section"}, {"post", false}});
    const Json st = f.status();
    const Json rejection = f.hypothesis("proposal_rejection");
    REQUIRE_FALSE(rejection.is_null());
    REQUIRE(rejection.at("supports").size() == 1);
    CHECK(f.links(rejection.at("id"), "supports") >= 1);
    CHECK(rejection.at("supports")[0] == f.hypothesis("course_correction").at("id"));
    (void)st;
  }
  TEST_CASE("everything is data: an overlay changes the thresholds, a setting changes the categories of a level") {
    Fixture f;
    std::filesystem::create_directories(f.td.path() / "profiles");
    std::ofstream(f.td.path() / "profiles" / "guardian.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "guardian"}, {"overrides", Json{{"thresholds", Json{{"propose", 0.5}}}}}}.dump();
    f.user("Write a summary of the quarterly report");
    f.assistant();
    f.user("nie chodziło mi o tabelę, zrób listę");
    CHECK(f.hypothesis("course_correction").at("status") == "ready");  // one calm correction is enough at the user's own threshold
    // A category of the user's own in a level: jokes are dropped at L1 when an annotation says what a joke is.
    f.rt->config().set("guardian_levels", Json{{"L1", Json{{"drop_categories", Json::array({"filler", "joke"})}}}});
    const std::string raw = "Use bullet points. Ha, that is funny. Keep it short.";
    const std::string mid = f.say("user", raw, false);
    NodeOptions no;
    no.metadata = Json{{"category", "joke"}};
    NewMessageAnnotation ann;
    ann.message_id = mid;
    ann.node_id = unwrap(f.rt->db().get_or_create_node("annotation:joke", "annotation_category", no));
    ann.start_char = static_cast<std::int64_t>(raw.find("Ha, that"));
    ann.end_char = static_cast<std::int64_t>(raw.find(" Keep"));
    ann.origin = "model";
    ann.metadata = Json{{"category", "joke"}, {"producer", "semantic_llm"}, {"confidence", 0.9}};
    (void)unwrap(f.rt->db().create_message_annotation(ann));
    const Json row = f.invoke("guardian.levels", Json{{"message_id", mid}, {"refresh", true}}).at("messages")[0];
    CHECK(row.at("levels").at("L1").at("text") == "Use bullet points. Keep it short.");
    CHECK(row.at("levels").at("L1").at("dropped")[0].at("producer") == "semantic_llm");
    CHECK(row.at("raw") == raw);
  }

  TEST_CASE("L2: digressions the analyzer marked are dropped when the selector asks for them, and only then") {
    Fixture f;
    f.user("Write the quarterly report");
    f.assistant();
    const std::string raw = "Use bullet points. By the way, my cat is hilarious today. Keep it short.";
    const std::string mid = f.say("user", raw, false);
    NodeOptions no;
    no.metadata = Json{{"category", "digression"}};
    NewMessageAnnotation ann;
    ann.message_id = mid;
    ann.node_id = unwrap(f.rt->db().get_or_create_node("annotation:digression", "annotation_category", no));
    ann.start_char = static_cast<std::int64_t>(raw.find("By the way"));
    ann.end_char = static_cast<std::int64_t>(raw.find(" Keep"));
    ann.origin = "model";
    ann.metadata = Json{{"category", "digression"}, {"producer", "semantic_llm"}, {"confidence", 0.9}};
    (void)unwrap(f.rt->db().create_message_annotation(ann));
    (void)f.invoke("guardian.levels", Json{{"message_id", mid}, {"refresh", true}});
    f.assistant();
    f.rt->config().set("chat_representation", "denoised");  // L1 does not drop digressions
    CHECK(exact_user(f.sent("Go on."), raw) == 1);
    f.rt->config().set("chat_representation", "lean");
    const Json lean = f.sent("Go on.");
    CHECK(exact_user(lean, raw) == 0);
    CHECK(exact_user(lean, "Use bullet points. Keep it short.") == 1);
    CHECK(unwrap(f.rt->db().get_msg(mid))->text == raw);
  }

  TEST_CASE("mixed: the raw messages stay and the compiled instruction follows them") {
    Fixture f;
    f.user("Write a short report about the quarterly sales numbers");
    f.assistant();
    f.user("Also add a table of the regions");
    f.assistant();
    f.user("Additionally make sure the totals are in euros");
    f.assistant();
    f.user("Plus include a short summary at the end");
    f.assistant();
    f.input("yes");
    const std::string instruction = unwrap(f.rt->db().find_node("rep:" + f.conv, "guardian_representation"))->content;
    REQUIRE_FALSE(instruction.empty());
    Json set{{"scope", "conversation"}, {"scope_id", f.conv}, {"effect", "set_value"}, {"key", "chat_representation"}, {"value", "mixed"}};
    (void)unwrap(actions::rules_set(f.td.path(), set));  // the user's own rule for this conversation replaces the guardian's
    const Json messages = f.sent("Now produce the report.");
    CHECK(exact_user(messages, "Also add a table of the regions") == 1);
    CHECK(has_text(messages, instruction));
  }

  TEST_CASE("an explicit scope in the user's words widens the hypothesis") {
    Fixture f;
    f.user("Write a summary of the quarterly report");
    f.assistant();
    f.user("nie chodziło mi o tabelę, zrób listę, zawsze");
    CHECK(f.hypothesis("preference_statement").at("scope_request") == "person");  // the marker stands in the clause of the stated preference
    CHECK(f.hypothesis("preference_statement").at("scope") == "conversation");     // asking is not deciding
    CHECK(f.hypothesis("course_correction").at("scope") == "conversation");
    CHECK(f.hypothesis("preference_statement").at("status") == "observing");  // one occurrence, whatever the scope
  }

  TEST_CASE("P23.1: one message holds a correction, a stated preference and an exception - three annotations, three things in status, and L1 keeps \"nie, nie\"") {
    Fixture f;
    f.user("Napisz podsumowanie raportu");
    f.assistant();
    const std::string raw = "nie, nie tak \xE2\x80\x94 kr\xC3\xB3\x63zej, ale zawsze po polsku, opr\xC3\xB3\x63z nazw plik\xC3\xB3w";
    const std::string mid = f.user(raw);
    const Json st = f.status();
    std::map<std::string, Json> by;
    for (const auto& seg : st.at("segments")) by[seg.at("category")] = seg;
    REQUIRE(by.count("correction"));
    REQUIRE(by.count("preference"));
    REQUIRE(by.count("exception"));
    CHECK(by["correction"].at("excerpt") == "nie, nie tak");
    CHECK(by["preference"].at("excerpt") == "ale zawsze po polsku");
    CHECK(by["exception"].at("excerpt") == "opr\xC3\xB3\x63z nazw plik\xC3\xB3w");
    CHECK(by["preference"].at("polarity") == "positive");
    CHECK(by["correction"].at("producer") == "guardian_heuristic");
    const double conf = by["preference"].at("confidence");
    CHECK(conf > 0.0);
    CHECK(conf < 1.0);  // computed from where the marker stands, not a constant 1.0
    // Two hypotheses (the correction, the preference of the person) and the exception on the preference it follows.
    const Json correction = f.hypothesis("course_correction"), preference = f.hypothesis("preference_statement");
    REQUIRE_FALSE(correction.is_null());
    REQUIRE_FALSE(preference.is_null());
    CHECK(correction.at("exceptions").empty());
    REQUIRE(preference.at("exceptions").size() == 1);
    CHECK(preference.at("exceptions")[0].at("text") == "nazw plik\xC3\xB3w");
    // The noise layer does not cut the emphasis: a doubled "nie" is a single-word repetition of low confidence ...
    Json row = f.invoke("guardian.levels", Json{{"message_id", mid}, {"refresh", true}}).at("messages")[0];
    CHECK(row.at("levels").empty());
    // ... and even where the user accepts low-confidence drops, the correction (a signal) protects the segment it covers.
    std::filesystem::create_directories(f.td.path() / "profiles");
    std::ofstream(f.td.path() / "profiles" / "guardian.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "guardian"}, {"overrides", Json{{"levels", Json{{"min_confidence", 0.2}}}}}}.dump();
    row = f.invoke("guardian.levels", Json{{"message_id", mid}, {"refresh", true}}).at("messages")[0];
    CHECK(row.at("levels").empty());
    bool repetition_listed = false;
    for (const auto& a : row.at("annotations")) repetition_listed = repetition_listed || a.at("category") == "repetition";
    CHECK_FALSE(repetition_listed);  // in the contract's reading the covered noise is not in force
  }

  TEST_CASE("P23.2: the analyzer's one prompt yields entities, topics AND segment categories; the guardian reuses them - one model call per message") {
    Fixture f;
    f.rt->config().set("semantic_analysis", true);
    f.rt->config().set("semantic_model", "test/frontier-opus");
    f.rt->config().set("guardian_method", "model");
    f.rt->config().set("guardian_model", "test/guard");
    f.say("user", "Write a summary of the quarterly report", false);
    f.say("assistant", "ok", false);
    const std::string text = "Nie chodzi\xC5\x82o mi o tabel\xC4\x99, zr\xC3\xB3\x62 list\xC4\x99. Ha, that is funny. Also add the date.";
    f.transport->expect("POST", "https://guardian.test/chat/completions",
                        net::ScriptedTransport::Reply::json(200, chat_reply(R"({"entities":[{"name":"report","kind":"concept","relevance":0.8}],"topics":[{"label":"tech","confidence":0.8}],"relations":[],
          "segments":[{"category":"correction","quote":"Nie chodziło mi o tabelę","confidence":0.9},{"category":"digression","quote":"Ha, that is funny.","confidence":0.8},{"category":"made_up","quote":"the date","confidence":0.9},{"category":"joke","quote":"not in the message","confidence":0.9}],
          "thread":"report layout"})")));
    const std::string mid = f.user(text);  // announced: the graph engine analyses it (one call), the guardian reads the layer
    const auto reqs = f.transport->requests();
    REQUIRE(reqs.size() == 1);  // one prompt contract, one call, for both readers
    CHECK(reqs[0].body.find("Segment categories") != std::string::npos);
    CHECK(reqs[0].body.find("Extract ALL named entities") != std::string::npos);  // the classic part is still there
    const Json st = f.status();
    std::map<std::string, Json> by;
    for (const auto& seg : st.at("segments")) by[seg.at("category")] = seg;
    REQUIRE(by.count("correction"));
    CHECK(by["correction"].at("producer") == "semantic_llm");
    CHECK(by["correction"].at("confidence").get<double>() == doctest::Approx(0.9));
    CHECK(by["correction"].at("cost").at("calls") == 1);  // the receipt of the one call
    REQUIRE(by.count("thread"));
    CHECK(by["thread"].at("producer") == "semantic_llm");
    const Json row = f.invoke("guardian.levels", Json{{"message_id", mid}, {"refresh", true}}).at("messages")[0];
    std::set<std::string> categories;
    for (const auto& a : row.at("annotations")) categories.insert(a.at("category"));
    CHECK(categories.count("digression") == 1);
    CHECK(categories.count("made_up") == 0);  // a category the data does not know is not written
    CHECK(categories.count("joke") == 0);     // a quote that is not in the message is not a segment
    // L2 differs from L1 on a conversation with a digression.
    REQUIRE(row.at("levels").contains("L2"));
    CHECK(row.at("levels").at("L2").at("text").get<std::string>().find("Ha, that is funny.") == std::string::npos);
    CHECK_FALSE(row.at("levels").contains("L1"));
    // The guardian recorded the correction from the layer; no model call of its own followed.
    const Json h = f.hypothesis("course_correction");
    REQUIRE_FALSE(h.is_null());
    CHECK(f.transport->requests().size() == 1);
  }

  TEST_CASE("P23.2: with nobody reading segments the analyzer's prompt is the classic one, byte for byte") {
    Fixture f;
    f.rt->config().set("semantic_analysis", true);
    f.rt->config().set("semantic_model", "test/sem");
    f.transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(R"({"entities":[],"topics":[],"relations":[]})")));
    f.user("Write a summary of the quarterly report for Friday");
    const auto reqs = f.transport->requests();
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].body.find("Segment categories") == std::string::npos);
    CHECK(reqs[0].body.find("\"max_tokens\":800") != std::string::npos);
  }

  TEST_CASE("P23.2: a regex analysis writes the thread of a message from its topic - when somebody reads threads - and the selector can leave another thread out") {
    Fixture f;
    f.rt->config().set("chat_thread_filter", "current");
    const std::string legal = f.say("user", "Draft the contract clause about liability and the court jurisdiction for the lawsuit", true);
    f.assistant("Here is the clause.");
    const std::string tech = f.say("user", "Now debug the python server code: the api returns a bug in the database query", true);
    const std::string reply = f.assistant("Looking at the code.");
    (void)reply;
    const Json a = f.invoke("guardian.levels", Json{{"message_id", legal}, {"refresh", true}}).at("messages")[0];
    std::string t1, t2;
    for (const auto& x : a.at("annotations")) if (x.at("category") == "thread") t1 = x.at("data").at("thread");
    const Json b = f.invoke("guardian.levels", Json{{"message_id", tech}, {"refresh", true}}).at("messages")[0];
    for (const auto& x : b.at("annotations")) if (x.at("category") == "thread") t2 = x.at("data").at("thread");
    INFO(t1 << " / " << t2);
    REQUIRE_FALSE(t1.empty());
    REQUIRE_FALSE(t2.empty());
    CHECK(t1 != t2);
    const auto sel = unwrap(guardian::select(f.rt->db(), f.rt->config(), f.conv));
    CHECK(std::find(sel.skip.begin(), sel.skip.end(), legal) != sel.skip.end());
    CHECK(std::find(sel.skip.begin(), sel.skip.end(), tech) == sel.skip.end());
    const Json messages = f.sent("And the stack trace?");
    CHECK_FALSE(has_text(messages, "contract clause"));
    CHECK(has_text(messages, "debug the python server"));
    // The messages stay in the conversation; only the request is lighter. Switching the filter off sends them again.
    CHECK(unwrap(f.rt->db().get_msg(legal))->text.find("contract clause") != std::string::npos);
    f.rt->config().set("chat_thread_filter", "off");
    CHECK(has_text(f.sent("One more thing"), "contract clause"));
  }

  TEST_CASE("P23.3: three corrections worded differently are one hypothesis and one proposal that reads as a rule; a no separates a doubtful join") {
    Fixture f;
    f.user("Napisz podsumowanie raportu");
    f.assistant();
    f.user("nie tak, krócej proszę");
    f.user("to nie to \xE2\x80\x94 za długie");
    f.user("nie chodziło mi o tyle tekstu, skróć to");
    f.assistant();
    const Json st = f.status();
    int corrections = 0;
    for (const auto& h : st.at("hypotheses"))
      if (h.at("signal") == "course_correction") ++corrections;
    CHECK(corrections == 1);
    const Json h = f.hypothesis("course_correction");
    CHECK(h.at("count") == 3);
    CHECK(h.at("claim").at("named") == true);
    CHECK(h.at("claim").at("source") == "concept");
    CHECK(h.at("claim").at("text") == "Pisz krótko i zwięźle.");
    CHECK(h.at("claim").at("quotes").size() == 3);  // the evidence stays apart from the content
    REQUIRE_FALSE(h.at("merged").empty());          // a join by shared concept, not by the same words, is recorded with its confidence and method
    CHECK(h.at("merged")[0].at("merge").at("method") == "overlap");
    int proposals = 0;
    Json proposal;
    for (const auto& m : unwrap(f.rt->db().get_msgs(f.conv)))
      if (m.metadata.is_object() && m.metadata.contains("action_proposal")) {
        ++proposals;
        proposal = m.to_json();
      }
    CHECK(proposals == 1);
    const std::string text = proposal.at("text");
    CHECK(text.find("Pisz krótko i zwięźle.") != std::string::npos);  // it reads as a rule ...
    CHECK(text.find("krócej proszę") == std::string::npos);            // ... not as a quotation
    // One "no": the proposal is refused and the doubtful join (the one the method was not sure of) is separated, nothing is lost.
    f.input("nie");
    INFO(f.status().dump());
    int total = 0, rejected = 0;
    const Json after = f.status();
    for (const auto& x : after.at("hypotheses"))
      if (x.at("signal") == "course_correction") {
        ++total;
        rejected += x.at("status") == "rejected" ? 1 : 0;
      }
    CHECK(total == 2);
    CHECK(rejected == 1);
  }

  TEST_CASE("P23.3: the match method is swappable - the model method asks one question and records the join; unmerge separates it") {
    Fixture f;
    f.rt->config().set("guardian_match_method", "model");
    f.rt->config().set("guardian_model", "test/guard");
    const std::string a = f.say("user", "make it terse", false);
    const std::string b = f.say("user", "fewer words please", false);
    (void)f.invoke("guardian.observe", Json{{"message_id", a}, {"kind", "course_correction"}, {"subject", "make it terse"}, {"post", false}});
    f.transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(R"({"same":true,"confidence":0.9})")));
    (void)f.invoke("guardian.observe", Json{{"message_id", b}, {"kind", "course_correction"}, {"subject", "fewer words please"}, {"post", false}});
    CHECK(f.transport->requests().size() == 1);
    const Json h = f.hypothesis("course_correction");
    CHECK(h.at("count") == 2);
    REQUIRE(h.at("merged").size() == 1);
    CHECK(h.at("merged")[0].at("merge").at("method") == "model");
    CHECK(h.at("merged")[0].at("merge").at("confidence").get<double>() == doctest::Approx(0.9));
    CHECK(f.links(h.at("merged")[0].at("occurrence"), "same_as") == 1);
    const Json out = f.invoke("guardian.unmerge", Json{{"hypothesis", h.at("id")}});
    CHECK(out.at("split") == 1);
    INFO(f.status().dump());
    int n = 0;
    const Json after = f.status();
    for (const auto& x : after.at("hypotheses")) n += x.at("signal") == "course_correction" ? 1 : 0;
    CHECK(n == 2);
  }

  TEST_CASE("P23.3: the user can state a rule himself - it is named, counts as one strong occurrence and is proposed as he said it") {
    Fixture f;
    f.user("Napisz podsumowanie raportu");
    f.assistant();
    f.user("Zapamiętaj, że odpowiedzi mają być po polsku.");
    const Json h = f.hypothesis("rule_statement");
    REQUIRE_FALSE(h.is_null());
    CHECK(h.at("claim").at("text") == "odpowiedzi mają być po polsku");
    CHECK(h.at("claim").at("source") == "stated");
    CHECK(h.at("status") == "ready");
    f.assistant();
    CHECK(f.last().at("text").get<std::string>().find("odpowiedzi mają być po polsku") != std::string::npos);
  }

  TEST_CASE("P23.4: six corrections in one conversation are not a rule about the person; two conversations are - as a question, with the reason") {
    Fixture f;
    f.user("Napisz podsumowanie raportu");
    for (int i = 0; i < 6; ++i) {
      f.assistant();
      f.user("nie tak, krócej proszę");
    }
    f.assistant();
    Json h = f.hypothesis("course_correction");
    CHECK(h.at("scope_candidate") == "conversation");
    Json asked;
    for (const auto& m : unwrap(f.rt->db().get_msgs(f.conv)))
      if (m.metadata.is_object() && m.metadata.contains("guardian") && m.metadata["guardian"].contains("hypothesis") && m.metadata.contains("action_proposal")) asked = m.metadata["guardian"];
    REQUIRE_FALSE(asked.is_null());
    CHECK(asked.at("scope") == "conversation");
    // The same thing in another conversation: evidence of diversity.
    const std::string first = f.conv;
    f.conv = unwrap(f.rt->db().create_conv("Other")).id;
    f.user("Przygotuj plan podróży");
    f.assistant();
    f.user("krócej, nie tak");
    h = f.hypothesis("course_correction");
    CHECK(h.at("scope_candidate") == "person");
    CHECK(h.at("siblings").size() == 1);
    CHECK(h.at("scope") == "conversation");  // nothing is decided by counting
    (void)first;
  }

  TEST_CASE("P23.4: a rule about the person is saved as normalized text only, is sent in other conversations unless the scope chain says no, and is remembered on request") {
    Fixture f;
    f.rt->config().set("guardian_promotion", "propose");
    f.user("Napisz podsumowanie raportu");
    f.assistant();
    f.user("nie tak, krócej proszę");
    f.assistant();
    const std::string a = f.conv;
    f.conv = unwrap(f.rt->db().create_conv("Other")).id;
    f.user("Przygotuj plan podróży");
    f.assistant();
    f.user("krócej, nie tak");
    f.assistant();
    const Json proposal = f.last();
    REQUIRE(proposal.at("metadata").contains("guardian"));
    CHECK(proposal.at("metadata").at("guardian").at("scope") == "person");
    CHECK(proposal.at("text").get<std::string>().find("2") != std::string::npos);
    f.input("tak");
    const Json overlay = f.overlay_doc();
    const Json rules = overlay.at("overrides").at("rules");
    REQUIRE(rules.size() == 1);
    const Json rule = rules.begin().value();
    CHECK(rule.at("scope") == "person");
    CHECK(rule.at("text") == "Pisz krótko i zwięźle.");
    CHECK_FALSE(rule.contains("evidence"));             // the conversations it was heard in are edges of the graph, not part of the rule
    CHECK(rule.at("evidence_count") == 2);
    CHECK(rule.dump().find("proszę") == std::string::npos);
    CHECK(rule.contains("review_after"));
    // The sibling in the first conversation is covered: it does not ask again.
    f.conv = a;
    CHECK(f.hypothesis("course_correction").at("status") == "covered");
    // A third conversation: the request carries the rule's text, and nothing of the quotes.
    f.conv = unwrap(f.rt->db().create_conv("Third")).id;
    Json messages = f.sent("Opisz pogodę");
    CHECK(has_text(messages, "Pisz krótko i zwięźle."));
    CHECK_FALSE(has_text(messages, "krócej proszę"));
    // The scope chain (R47) decides per conversation: off there, and the request is the plain one.
    f.rt->config().set("guardian_person_rules", "off");
    CHECK_FALSE(has_text(f.sent("Opisz jeszcze pogodę"), "Pisz krótko i zwięźle."));
    f.rt->config().set("guardian_person_rules", "send");
    // "What do you remember about me?"
    const Json mem = f.invoke("guardian.remembered", Json{{"lang", "pl"}});
    REQUIRE(mem.at("rules").size() == 1);
    CHECK(mem.at("text").get<std::string>().find("Pisz krótko i zwięźle.") != std::string::npos);
    // Undo takes it back from the overlay; the request is plain again.
    (void)f.invoke("guardian.undo", Json{{"rule", mem.at("rules")[0].at("id")}});
    CHECK_FALSE(has_text(f.sent("A teraz?"), "Pisz krótko i zwięźle."));
  }

  TEST_CASE("P23.4: a rule beyond the conversation must have a name of its own; an unnamed one keeps its quotes in the conversation") {
    Fixture f;
    const std::string m = f.say("user", "do it differently", false);
    (void)f.invoke("guardian.observe", Json{{"message_id", m}, {"kind", "course_correction"}, {"post", false}});
    const Json h = f.hypothesis("course_correction");
    CHECK(h.at("claim").at("named") == false);
    auto bad = f.rt->guardian().invoke("guardian.promote", Json{{"hypothesis", h.at("id")}, {"scope", "person"}});
    CHECK_FALSE(bad.has_value());
    auto ok = f.rt->guardian().invoke("guardian.promote", Json{{"hypothesis", h.at("id")}, {"scope", "person"}, {"text", "Vary the structure."}});
    REQUIRE(ok.has_value());
    const Json rule = f.overlay_doc().at("overrides").at("rules").begin().value();
    CHECK(rule.at("text") == "Vary the structure.");
    CHECK_FALSE(rule.contains("evidence"));
  }

  TEST_CASE("P23.4: a rule lives - a correction against it proposes switching it off; a review asks after a while; an expired rule stops applying; nothing is deleted") {
    Fixture f;
    const std::string a = f.say("user", "Use tables for the data in every answer", false);
    (void)f.invoke("guardian.observe", Json{{"message_id", a}, {"kind", "course_correction"}, {"subject", "use tables for the data"}, {"post", false}});
    const Json h = f.hypothesis("course_correction");
    (void)f.invoke("guardian.promote", Json{{"hypothesis", h.at("id")}});
    const std::string b = f.say("user", "Do not use tables for the data", false);
    (void)f.invoke("guardian.observe", Json{{"message_id", b}, {"kind", "course_correction"}, {"subject", "do not use tables for the data"}, {"post", false}});
    f.assistant();
    const Json p = f.last();
    INFO(unwrap(f.rt->db().get_node(std::string(h.at("id").get<std::string>())))->metadata.dump());
    REQUIRE(p.at("metadata").contains("action_proposal"));
    CHECK(p.at("metadata").at("action_proposal").at("action") == "guardian.undo");
    f.input("tak");
    CHECK(f.overlay_doc().value("overrides", Json::object()).value("rules", Json::object()).empty());
    // The hypothesis is still there, as history.
    CHECK(f.hypothesis("course_correction").at("status") == "undone");
  }

  TEST_CASE("P23.4: a rule is reviewed after a while without confirmation (no keeps it) and an expired rule stops applying but stays in the overlay") {
    Fixture f;
    std::filesystem::create_directories(f.td.path() / "profiles");
    std::ofstream(f.td.path() / "profiles" / "guardian.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "guardian"}, {"overrides", Json{{"lifecycle", Json{{"review_after_days", 1e-9}}}}}}.dump();
    const std::string a = f.say("user", "do it differently", false);
    (void)f.invoke("guardian.observe", Json{{"message_id", a}, {"kind", "course_correction"}, {"post", false}});
    const Json h = f.hypothesis("course_correction");
    (void)f.invoke("guardian.promote", Json{{"hypothesis", h.at("id")}, {"scope", "person"}, {"text", "Vary the structure."}});
    f.assistant();
    const Json p = f.last();
    REQUIRE(p.at("metadata").contains("action_proposal"));
    CHECK(p.at("metadata").at("action_proposal").at("action") == "guardian.undo");
    CHECK(p.at("metadata").at("guardian").at("lifecycle") == "review");
    f.input("nie");  // keep it
    f.assistant();
    CHECK(f.last().at("metadata").contains("action_proposal") == false);  // not asked again within the review period
    CHECK(f.overlay_doc().at("overrides").at("rules").size() == 1);
    // Expiry: a rule whose date has passed is not sent; it is still there.
    Json doc = f.overlay_doc();
    for (auto& r : doc["overrides"]["rules"]) r["expires"] = "2000-01-01T00:00:00Z";
    std::ofstream(f.overlay()) << doc.dump();
    f.conv = unwrap(f.rt->db().create_conv("Other")).id;
    CHECK_FALSE(has_text(f.sent("Hello there"), "Vary the structure."));
    CHECK(f.overlay_doc().at("overrides").at("rules").size() == 1);
  }

  TEST_CASE("P23 owner decision: the conversation's model is never inherited - setting, role, analysis, else no model and it says so") {
    Fixture f;
    f.rt->config().set("guardian_method", "model");
    f.say("user", "Write a summary", false);
    f.say("assistant", "ok", false);  // the chat model: test/model (default_model) is NOT a guardian model
    const std::string m = f.say("user", "make it shorter now", false);
    const Json none = f.invoke("guardian.annotate", Json{{"message_id", m}, {"post", false}});
    CHECK(f.transport->requests().empty());  // no call
    CHECK_FALSE(none.at("issues").empty());   // and it reports why
    CHECK(none.at("issues")[0].get<std::string>().find("No analysis model is set") != std::string::npos);
    const Json st = f.status();
    CHECK(st.at("model").at("model") == "");
    CHECK(st.contains("model_note"));
    auto model_of_call = [&](const std::string& text) {
      const std::string id = f.say("user", text, false);
      f.transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(R"({"kind":"none","confidence":0})")));
      (void)f.invoke("guardian.annotate", Json{{"message_id", id}, {"post", false}});
      return unwrap(json::parse(f.transport->requests().back().body)).value("model", "");
    };
    f.rt->config().set("semantic_model", "test/analysis");  // the analysis model: via the ports role guardian (preset = analysis.fast)
    CHECK(model_of_call("make it shorter again") == "test/analysis");
    f.rt->config().set("guardian_model", "test/model");     // the chat model, but by hand
    CHECK(model_of_call("shorter once more") == "test/model");
  }

  TEST_CASE("P23 owner decision: a policy the user wrote may derive the model from the conversation's (relative_to, one tier down); the flow follows the class of the model") {
    Fixture f;
    f.rt->config().set("guardian_method", "model");
    std::filesystem::create_directories(f.td.path() / "profiles");
    std::ofstream(f.td.path() / "profiles" / "guardian.pack")
        << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "guardian"},
                {"overrides", Json{{"models", Json{{"policies", Json{{"guardian", Json{{"chain", Json::array({Json{{"type", "relative_to"}, {"base", "conversation"}, {"tier_offset", -1}}})}}}}},
                                                   {"tier_models", Json{{"mid", "test/mid-model"}, {"cheap", "test/cheap-model"}}}}}}}}.dump();
    f.say("user", "Write a summary", false);
    NewMessage a;
    a.conv_id = f.conv;
    a.role = "assistant";
    a.text = "ok";
    a.model = "anthropic/claude-opus-4";
    (void)unwrap(f.rt->db().create_msg(a));
    const Json st = f.status();
    CHECK(st.at("model").at("model") == "test/mid-model");   // frontier -> one tier cheaper
    CHECK(st.at("model").at("via") == "relative_to:mid");
    CHECK(st.at("model").at("flow") == "narrow_questions");
    CHECK(annotations::model_tier(unwrap(annotations::Policy::load(f.td.path())).models, "anthropic/claude-opus-4") == "frontier");
    CHECK(annotations::model_tier(unwrap(annotations::Policy::load(f.td.path())).models, "x/claude-haiku-4") == "cheap");
  }

  TEST_CASE("P23 flows: a frontier-class analysis model gets one call for everything, a cheap one the classic call and a narrow question") {
    auto run = [](const std::string& model) {
      Fixture f;
      f.rt->config().set("semantic_analysis", true);
      f.rt->config().set("semantic_model", model);
      f.rt->config().set("semantic_segments", "on");
      f.transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(R"({"entities":[],"topics":[],"relations":[],"segments":[],"thread":"t"})")));
      f.transport->expect("POST", "https://guardian.test/chat/completions", net::ScriptedTransport::Reply::json(200, chat_reply(R"({"segments":[],"thread":"narrow thread"})")));
      f.user("Write a summary of the quarterly report for Friday");
      return f.transport->requests().size();
    };
    CHECK(run("test/frontier-opus") == 1);
    CHECK(run("test/cheap-haiku") == 2);
  }

  TEST_CASE("M1-D1: resolve_model works for every policy of the data - each explicit step, every role step alone, else none") {
    Fixture f;
    const auto profile = unwrap(RuntimeProfile::builtin("guardian"));
    const Json& policies = profile.values().at("models").at("policies");
    const auto ports = unwrap(RuntimeProfile::builtin("ports"));
    for (const char* name : {"guardian", "analyzer", "chat", "intent", "image", "speech", "embed", "asr", "ocr", "action"}) CHECK_MESSAGE(policies.contains(name), name);
    std::vector<std::string> settings;  // every setting an explicit step names, cleared before each case
    for (auto it = policies.begin(); it != policies.end(); ++it)
      for (const auto& step : it.value().at("chain"))
        if (json::get_string(step, "type") == "explicit") settings.push_back(json::get_string(step, "setting"));
    // the role presets that would supply a model through their `setting` (semantic_model for guardian / analysis.fast ...)
    for (auto it = ports.values().at("roles").begin(); it != ports.values().at("roles").end(); ++it)
      if (const std::string s = json::get_string(it.value(), "setting"); !s.empty()) settings.push_back(s);
    const auto overlay = f.td.path() / "profiles" / "ports.pack";
    auto clear = [&] {
      for (const auto& s : settings) f.rt->config().set(s, "");
      std::filesystem::remove(overlay);
    };
    for (auto it = policies.begin(); it != policies.end(); ++it) {
      const std::string name = it.key();
      INFO(name);
      const Json& chain = it.value().at("chain");
      REQUIRE_FALSE(chain.empty());
      CHECK(json::get_string(chain.back(), "type") == "none");
      // every step says what it means, in pl and en
      for (const auto& step : chain) {
        CHECK_FALSE(json::get_string(step.value("meaning", Json::object()), "pl").empty());
        CHECK_FALSE(json::get_string(step.value("meaning", Json::object()), "en").empty());
      }
      // a policy names the ports role it chooses for; one that nothing reads yet says what happens today
      const std::string role = json::get_string(it.value(), "role");
      CHECK(ports.values().at("roles").contains(role));
      if (name != "guardian" && name != "analyzer") {
        CHECK(it.value().at("status").at("state") == "planned");
        CHECK_FALSE(json::get_string(it.value().at("status").at("today"), "pl").empty());
      }
      // nothing set: no model, and the reader is told why
      clear();
      auto none = annotations::resolve_model(f.rt->config(), f.td.path(), name, f.conv, "", -1, &f.rt->db());
      CHECK(none.model.empty());
      CHECK(none.via == "none");
      CHECK_MESSAGE(none.reason.find("'" + name + "'") != std::string::npos, none.reason);  // the reason names the policy
      bool explicit_seen = false;
      for (const auto& step : chain) {
        const std::string type = json::get_string(step, "type");
        if (type == "explicit") {
          // an explicit setting decides first
          clear();
          const std::string setting = json::get_string(step, "setting");
          f.rt->config().set(setting, "explicit/" + name);
          auto c = annotations::resolve_model(f.rt->config(), f.td.path(), name, f.conv, "", -1, &f.rt->db());
          if (!explicit_seen) {
            CHECK(c.model == "explicit/" + name);
            CHECK(c.via == "explicit:" + setting);
            CHECK_FALSE(c.tier.empty());
            CHECK_FALSE(c.flow.empty());
          }
          explicit_seen = true;
        } else if (type == "role") {
          // with the explicit settings empty and only THIS role chosen in the ports profile, this step decides: every earlier
          // role step yields nothing (its preset reads a cleared setting), so a later step is reached (intent: analysis.fast, in
          // place of today's fallback to default_model; guardian: analysis.fast)
          clear();
          const std::string r = json::get_string(step, "role");
          std::filesystem::create_directories(overlay.parent_path());
          std::ofstream(overlay) << Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "ports"},
                                         {"overrides", Json{{"roles", Json{{r, Json{{"adapter", "openai_chat"}, {"params", Json{{"model", "role/" + name + "/" + r}}}}}}}}}}.dump();
          auto c = annotations::resolve_model(f.rt->config(), f.td.path(), name, f.conv, "", -1, &f.rt->db());
          INFO(r);
          CHECK(c.model == "role/" + name + "/" + r);
          CHECK(c.via == "role:" + r);
        }
      }
      clear();
    }
  }

  TEST_CASE("M1-D1: the class of a model as guardian.pack says it today - the name decides, a price above the mid threshold too") {
    Fixture f;
    const Json models = unwrap(annotations::Policy::load(f.td.path())).models;
    // no reader passes a price today (models.price_path planned): the name patterns decide, cheap first ("mini" is in "gemini")
    CHECK(annotations::model_tier(models, "google/gemini-2.5-pro") == "cheap");
    CHECK(annotations::model_tier(models, "vendor/brand-x") == "unknown");
    // with a price above the mid threshold (frontier has no threshold) the patterns of all classes decide, in order: cheap, mid
    // or frontier, else unknown (tiers[frontier].reason.threshold, unknown_tier.reason)
    CHECK(annotations::model_tier(models, "google/gemini-2.5-pro", 10) == "cheap");
    CHECK(annotations::model_tier(models, "anthropic/claude-sonnet-4", 10) == "mid");
    CHECK(annotations::model_tier(models, "anthropic/claude-opus-4", 10) == "frontier");
    CHECK(annotations::model_tier(models, "vendor/brand-x", 10) == "unknown");
    // a price within a threshold decides before the name
    CHECK(annotations::model_tier(models, "anthropic/claude-opus-4", 0.5) == "cheap");
    // the texts say what the code does: no price path today, every threshold reason marked planned
    CHECK(models.at("price_path").at("state") == "planned");
    for (const auto& t : models.at("tiers")) CHECK(t.at("reason").at("threshold_state") == "planned");
    // every class says why, in pl and en, for the pattern and for the threshold; the unknown class says why too
    const Json builtin = unwrap(RuntimeProfile::builtin("guardian")).values().at("models");
    REQUIRE_FALSE(builtin.at("tiers").empty());
    for (const auto& t : builtin.at("tiers")) {
      INFO(json::get_string(t, "id"));
      for (const char* kind : {"pattern", "threshold"})
        for (const char* lang : {"pl", "en"}) {
          INFO(kind << " " << lang);
          const Json reason = t.at("reason").value(kind, Json::object());
          REQUIRE(reason.contains(lang));
          REQUIRE(reason.at(lang).is_string());
          CHECK_FALSE(reason.at(lang).get<std::string>().empty());
        }
    }
    for (const char* lang : {"pl", "en"}) CHECK_FALSE(json::get_string(builtin.at("unknown_tier").at("reason"), lang).empty());
    // the preset assigns no model to a class by hand: tier_models is the person's overlay, every value empty in the preset
    REQUIRE(builtin.at("tier_models").is_object());
    for (auto it = builtin.at("tier_models").begin(); it != builtin.at("tier_models").end(); ++it) {
      INFO(it.key());
      CHECK(it.value() == "");
    }
  }
}
