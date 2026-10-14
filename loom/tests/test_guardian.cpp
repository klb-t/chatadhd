// P21: the context guardian - a secretary that builds the representation of a conversation beside the raw messages
// (loom/guardian.h; docs/architecture/STRAZNIK_I_SRODOWISKA_2026-10-14.md).
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>

#include "loom/action_overlay.h"
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
    CHECK(row.at("levels").at("L1").at("text") == "I want, a list of tasks for Friday");
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
    CHECK(undone.at("levels").at("L1").at("text") == "yyy I want, a list of tasks for Friday");
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
    CHECK(proposal.at("text").get<std::string>().find("zrób listę") != std::string::npos);
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
    CHECK(h.at("scope") == "person");
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
    CHECK(f.hypothesis("course_correction").at("scope") == "person");
    CHECK(f.hypothesis("course_correction").at("status") == "observing");  // one occurrence, whatever the scope
  }
}
