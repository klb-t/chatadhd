// R54 / GRAF_HUB_KOD_SILNIK §8: a bare id is enough to ask which actions fit an element.
#include <doctest/doctest.h>

#include "loom/action_element.h"
#include "loom/action_rules.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::actions::ElementResolver;
using loom::actions::ElementSources;
using loom::test::unwrap;

namespace {

struct Stores {
  fsutil::TempDir td;
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "test.db");
  BlobStore blobs{td.path() / "blobs", *db};
  ElementSources sources() { return ElementSources{db.get(), &blobs, td.path()}; }
  Json ask(std::string_view action, const Json& request) {
    const auto s = sources();
    return unwrap(actions::invoke(td.path(), action, request, &s));
  }
};

Json row_of(const Json& result, const std::string& action) {
  for (const auto& r : result.at("actions"))
    if (r.at("action") == action) return r;
  return Json();
}

}  // namespace

TEST_SUITE("action_element") {
  TEST_CASE("a message id resolves to the class message with its attributes, and actions.for works with just the id") {
    Stores st;
    auto conv = unwrap(st.db->create_conv("t"));
    NewMessage nm;
    nm.conv_id = conv.id;
    nm.role = "user";
    nm.text = "Remember the milk";
    const std::string mid = unwrap(st.db->create_msg(nm));
    const Json by_id = st.ask("for", Json{{"element", mid}});
    CHECK(by_id.at("element").at("class") == "message");
    CHECK(by_id.at("element").at("id") == mid);
    CHECK(by_id.at("element").at("source") == "message");
    CHECK(by_id.at("element").at("attrs").at("role") == "user");
    CHECK(by_id.at("element").at("attrs").at("conv_id") == conv.id);
    // The same answer as naming the class by hand (traits come from the class, not from the caller).
    const Json by_class = unwrap(actions::invoke(st.td.path(), "for", Json{{"element", Json{{"class", "message"}, {"id", mid}}}}));
    CHECK(by_id.at("actions") == by_class.at("actions"));
    CHECK_FALSE(row_of(by_id, "summarise").is_null());
    CHECK(by_id.at("element").at("traits").is_array());
  }

  TEST_CASE("a conversation id resolves; the default action of the class is found") {
    Stores st;
    auto conv = unwrap(st.db->create_conv("A talk"));
    const Json r = st.ask("for", Json{{"element", conv.id}});
    CHECK(r.at("element").at("class") == "conversation");
    CHECK(r.at("element").at("label") == "A talk");
    CHECK(r.at("default_action") == "chat.send");
    // The object form with only an id is the same as the bare string.
    const Json obj = st.ask("for", Json{{"element", Json{{"id", conv.id}}}});
    CHECK(obj.at("actions") == r.at("actions"));
  }

  TEST_CASE("a version group id resolves to its active message") {
    Stores st;
    auto conv = unwrap(st.db->create_conv("t"));
    NewMessage nm;
    nm.conv_id = conv.id;
    nm.role = "user";
    nm.text = "first";
    const std::string first = unwrap(st.db->create_msg(nm));
    const auto edited = unwrap(st.db->edit_msg(first, "second"));
    REQUIRE(edited.has_value());
    const auto msg = unwrap(st.db->get_msg(*edited));
    REQUIRE(msg.has_value());
    REQUIRE(msg->version_group_id.has_value());
    const Json r = st.ask("for", Json{{"element", *msg->version_group_id}});
    CHECK(r.at("element").at("class") == "message");
    CHECK(r.at("element").at("id") == *edited);
    CHECK(r.at("element").at("attrs").at("version_group_id") == *msg->version_group_id);
  }

  TEST_CASE("a blob hash takes its class and traits from the stored MIME type") {
    Stores st;
    const auto audio = unwrap(st.blobs.put("RIFF....WAVE", "audio/wav"));
    const auto image = unwrap(st.blobs.put("\x89PNG....", "image/png"));
    const auto other = unwrap(st.blobs.put("opaque bytes", "application/octet-stream"));
    const Json a = st.ask("for", Json{{"element", audio.hash}});
    CHECK(a.at("element").at("class") == "audio_form");
    CHECK(a.at("default_action") == "transcribe");  // the default of the class, found from the hash alone
    CHECK(a.at("element").at("attrs").at("mime") == "audio/wav");
    const Json i = st.ask("for", Json{{"element", image.hash}});
    CHECK(i.at("element").at("class") == "image_form");
    CHECK(i.at("default_action") == "describe_image");
    const Json o = st.ask("for", Json{{"element", other.hash}});
    CHECK(o.at("element").at("class") == "file");
    CHECK(row_of(o, "storage.locate").at("status") != "not_applicable");  // has_hash is a trait of a file
    // A blob that is not stored is an error, never a guess.
    const std::string missing(64, 'a');
    const auto s = st.sources();
    auto none = actions::invoke(st.td.path(), "for", Json{{"element", missing}}, &s);
    REQUIRE_FALSE(none);
    CHECK(none.error().code == Errc::NotFound);
  }

  TEST_CASE("the node of a stored output is the blob; other nodes take the class of their kind") {
    Stores st;
    const auto png = unwrap(st.blobs.put("\x89PNG....", "image/png"));
    NodeOptions opts;
    const std::string blob_node = unwrap(st.db->get_or_create_node("blob:" + png.hash, "blob", opts));
    const Json b = st.ask("for", Json{{"element", blob_node}});
    CHECK(b.at("element").at("class") == "image_form");
    CHECK(b.at("element").at("id") == blob_node);
    CHECK(b.at("element").at("attrs").at("node_id") == blob_node);
    const std::string entity = unwrap(st.db->create_node("Alice", "entity"));
    const Json e = st.ask("for", Json{{"element", entity}});
    CHECK(e.at("element").at("class") == "");  // no class for this kind: only the own traits count
    CHECK(e.at("element").at("attrs").at("kind") == "entity");
  }

  TEST_CASE("a link id resolves to an edge") {
    Stores st;
    const std::string a = unwrap(st.db->create_node("A", "entity"));
    const std::string b = unwrap(st.db->create_node("B", "entity"));
    const std::string link = unwrap(st.db->create_link(a, b, "mentions", 0.5));
    const Json r = st.ask("for", Json{{"element", link}});
    CHECK(r.at("element").at("class") == "edge");
    CHECK(r.at("element").at("attrs").at("link_type") == "mentions");
    CHECK(r.at("element").at("attrs").at("src") == a);
  }

  TEST_CASE("a settings key is a settings field; a class id and an unknown string keep their old meaning") {
    Stores st;
    const Json field = st.ask("for", Json{{"element", "semantic_retry_mode"}});
    CHECK(field.at("element").at("class") == "settings_field");
    CHECK(field.at("element").at("attrs").at("field_scope") == "config");
    CHECK(field.at("element").at("attrs").at("key") == "semantic_retry_mode");
    const Json cls = st.ask("for", Json{{"element", "message"}});
    CHECK(cls.at("element").at("class") == "message");
    CHECK_FALSE(cls.at("element").contains("source"));
    const Json unknown = st.ask("for", Json{{"element", "nonsense"}});
    CHECK(unknown.at("issues").at(0).at("code") == "class_unknown");
    // An id of a known form that is not stored is an error.
    const auto s = st.sources();
    auto none = actions::invoke(st.td.path(), "for", Json{{"element", "m_000000000000"}}, &s);
    REQUIRE_FALSE(none);
    CHECK(none.error().code == Errc::NotFound);
  }

  TEST_CASE("explain and propose_chain accept a bare id too") {
    Stores st;
    const auto audio = unwrap(st.blobs.put("RIFF....WAVE", "audio/wav"));
    const Json chain = st.ask("propose_chain", Json{{"element", audio.hash}, {"target_trait", "summary"}});
    REQUIRE(chain.at("chains").size() >= 1);
    CHECK(chain.at("chains").at(0).at("steps").at(0).at("action") == "transcribe");
    const Json why = st.ask("explain", Json{{"element", audio.hash}, {"action", "summarise"}});
    CHECK(why.at("fits") == false);
    CHECK(why.at("element").at("class") == "audio_form");
  }

  TEST_CASE("content parts: message text, conversation transcript, text blob and a media reference") {
    Stores st;
    auto conv = unwrap(st.db->create_conv("t"));
    NewMessage u;
    u.conv_id = conv.id;
    u.role = "user";
    u.text = "hello";
    unwrap(st.db->create_msg(u));
    NewMessage a = u;
    a.role = "assistant";
    a.text = "hi there";
    const std::string aid = unwrap(st.db->create_msg(a));
    auto resolver = unwrap(ElementResolver::load(st.td.path()));
    const auto known = [](std::string_view c) { return c == "message" || c == "conversation"; };
    const auto s = st.sources();
    const Json conv_el = unwrap(resolver.resolve(s, Json(conv.id), known));
    const auto transcript = unwrap(resolver.content_parts(s, conv_el));
    REQUIRE(transcript.size() == 1);
    CHECK(transcript[0].kind == "text");
    CHECK(transcript[0].text == "User: hello\n\nAssistant: hi there");
    const auto one = unwrap(resolver.content_parts(s, unwrap(resolver.resolve(s, Json(aid), known))));
    REQUIRE(one.size() == 1);
    CHECK(one[0].text == "hi there");
    const auto text_blob = unwrap(st.blobs.put("plain words", "text/plain"));
    const auto words = unwrap(resolver.content_parts(s, unwrap(resolver.resolve(s, Json(text_blob.hash), known))));
    REQUIRE(words.size() == 1);
    CHECK(words[0].text == "plain words");
    const auto image = unwrap(st.blobs.put("\x89PNG....", "image/png"));
    const auto pic = unwrap(resolver.content_parts(s, unwrap(resolver.resolve(s, Json(image.hash), known))));
    REQUIRE(pic.size() == 1);
    CHECK(pic[0].kind == "image");
    CHECK(pic[0].blob_hash == image.hash);
  }
}
