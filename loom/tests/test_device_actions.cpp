// P11 6 (on top of R9, devices as graph nodes): a device is an element by its id (device:<entry or instance>, read from the
// graph_entry profile's /devices with instances expanded); the actions of the devices are declared in the catalogue with `requires` on the
// capability traits of classes.pack, planned with a reason where no executor exists on this platform; the chat names a device by its words.
#include <doctest/doctest.h>

#include "loom/action_runner.h"
#include "loom/chat_route.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

struct Fixture {
  fsutil::TempDir td;
  std::unique_ptr<Runtime> rt;
  std::string conv_id;
  Fixture() {
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    rt = unwrap(Runtime::open(o));
    conv_id = unwrap(rt->db().create_conv("Devices")).id;
    NewMessage m;
    m.conv_id = conv_id;
    m.role = "user";
    m.text = "Hello.";
    unwrap(rt->db().create_msg(m));
  }
  Json for_element(const std::string& element) { return unwrap(rt->actions().invoke("for", Json{{"element", element}})); }
  Json route(const std::string& text) { return unwrap(chat_route::route(*rt, Json{{"text", text}, {"conv_id", conv_id}})); }
  static Json row(const Json& rows, const std::string& action) {
    for (const auto& r : rows.at("actions"))
      if (r.at("action") == action) return r;
    return Json();
  }
};

}  // namespace

TEST_SUITE("device_actions") {
  TEST_CASE("a device is an element by its id: its class, its availability per platform, the actions that fit its capabilities") {
    Fixture f;
    const Json mic = f.for_element("device:microphone");
    CHECK(mic.at("element").at("class") == "microphone");
    CHECK(mic.at("element").at("traits").dump().find("captures_audio") != std::string::npos);
    const Json record = Fixture::row(mic, "device.record_audio");
    REQUIRE_FALSE(record.is_null());
    CHECK(record.at("callable") == false);  // planned: no client is attached to this runtime
    CHECK(record.at("planned_reason").at("en").get<std::string>().find("client") != std::string::npos);
    CHECK(Fixture::row(mic, "device.capture_image").is_null());  // a microphone does not take pictures
    const Json camera = f.for_element("device:camera");
    CHECK_FALSE(Fixture::row(camera, "device.capture_image").is_null());
    CHECK_FALSE(Fixture::row(camera, "device.capture_video").is_null());
    CHECK_FALSE(Fixture::row(camera, "device.request_permission").is_null());  // a camera needs the system's permission
    CHECK(Fixture::row(f.for_element("device:speaker"), "device.play_audio").at("status") != "not_applicable");
    CHECK_FALSE(Fixture::row(f.for_element("device:clipboard"), "device.clipboard_read").is_null());
    CHECK_FALSE(Fixture::row(f.for_element("device:notifications"), "device.notify").is_null());
    CHECK_FALSE(Fixture::row(f.for_element("device:file_access"), "device.pick_file").is_null());
    // an instance: the storages of the storage_security profile, with the availability of that profile mapped to the platforms
    const Json local = f.for_element("device:local_dir");
    CHECK(local.at("element").at("class") == "storage_backend");
    CHECK_FALSE(Fixture::row(local, "storage.status").is_null());  // the storage actions fit it without a line of code
    const Json el = unwrap(f.rt->actions().invoke("explain", Json{{"action", "storage.status"}, {"element", "device:local_dir"}}));
    CHECK(el.at("element").at("attrs").at("instance") == "local_dir");
    CHECK(el.at("element").at("attrs").at("availability").at("cli") == "available");
    CHECK(el.at("element").at("attrs").at("availability").at("web") == "unavailable");
    // not a device: said, not guessed
    CHECK_FALSE(f.rt->actions().invoke("for", Json{{"element", "device:telepathy"}}));
  }

  TEST_CASE("running a device action that has no executor here says why, before anything happens; chains through it are proposals") {
    Fixture f;
    auto run = f.rt->actions().run(Json{{"action", "device.record_audio"}, {"element", "device:microphone"}});
    if (run) CHECK(run->at("status") != "completed");
    else CHECK(run.error().message.find("planned") != std::string::npos);
    const Json chain = unwrap(f.rt->actions().invoke("propose_chain", Json{{"element", "device:microphone"}, {"target_trait", "has_text"}}));
    REQUIRE(chain.at("chains").size() >= 1);
    CHECK(chain.at("chains").at(0).at("steps").at(0).at("action") == "device.record_audio");
    CHECK(chain.at("chains").at(0).at("steps").at(1).at("action") == "transcribe");
    CHECK(chain.at("chains").at(0).at("executable") == false);
  }

  TEST_CASE("from the conversation window: the words of a device name it; what cannot run yet is said with its reason") {
    Fixture f;
    const Json record = f.route("Nagraj dźwięk z mikrofonu");
    REQUIRE(record.at("kind") == "command");
    CHECK(record.at("command").at("action") == "device.record_audio");
    CHECK(record.at("command").at("element").at("class") == "microphone");
    CHECK(record.at("command").at("element").at("id") == "device:microphone");
    CHECK(record.at("command").at("runnable") == false);
    CHECK(record.at("command").at("not_runnable").at(0).at("action") == "device.record_audio");
    CHECK(f.route("Take a picture with the camera").at("command").at("action") == "device.capture_image");
    CHECK(f.route("Zrób zdjęcie aparatem").at("command").at("action") == "device.capture_image");
    CHECK(f.route("Record audio from the microphone").at("command").at("action") == "device.record_audio");
    // a required argument is asked for, a question stays a message, a word without an action too
    const Json notify = f.route("Send a notification");
    CHECK(notify.at("command").at("action") == "device.notify");
    CHECK(notify.at("command").at("reason").at("code") == "missing_args");
    CHECK(f.route("What does the microphone icon mean?").at("kind") == "message");
    CHECK(f.route("My camera is broken").at("kind") == "message");
    // the proposal of the chat says it cannot be done yet and why
    ChatOptions opts;
    opts.conv_id = f.conv_id;
    opts.stream = false;
    const auto out = unwrap(chat_route::handle(*f.rt, "Nagraj dźwięk z mikrofonu", opts, Json{{"message", "Nagraj dźwięk z mikrofonu"}}, nullptr));
    REQUIRE(out.handled);
    const std::string text = unwrap(f.rt->db().get_msg(out.assistant_message_id))->text;
    CHECK(text.find("Nie mogę tego jeszcze wykonać") != std::string::npos);
    CHECK(text.find("klient") != std::string::npos);
  }
}
