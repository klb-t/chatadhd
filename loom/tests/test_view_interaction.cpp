// P28 step 2: interaction as data - effective input bindings per device, overrides, conflicts, unsupported.
#include <doctest/doctest.h>

#include "loom/effective_view.h"
#include "loom/interaction.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

const Json* binding(const Json& r, const std::string& id) {
  for (const auto& b : r["bindings"])
    if (b["id"] == id) return &b;
  return nullptr;
}

Json resolve(const std::filesystem::path& root, const std::string& device) {
  auto i = unwrap(views::Interaction::open(root));
  return i.resolve(Json{{"device", device}});
}

bool has_unsupported(const Json& r, const std::string& id, const std::string& code) {
  for (const auto& u : r["unsupported"])
    if (u["binding"] == id && u["reason"]["code"] == code && u["reason"]["text"]["pl"].is_string() && !u["reason"]["text"]["pl"].get<std::string>().empty()) return true;
  return false;
}

}  // namespace

TEST_SUITE("view_interaction") {
  TEST_CASE("the keyboard preset: held modifiers choose the axis pair of the arrows, locks switch alternative views") {
    fsutil::TempDir td;
    const Json r = resolve(td.path(), "keyboard");
    CHECK(r["schema"] == "loom.view_interaction/1");
    CHECK(r["conflicts"].empty());
    CHECK(r["unsupported"].empty());
    const Json* plane = binding(r, "kbd.plane.left");
    REQUIRE(plane);
    CHECK((*plane)["action"]["kind"] == "move");
    CHECK((*plane)["action"]["pair"] == "plane");
    CHECK((*plane)["input"]["modifiers"].empty());
    CHECK((*plane)["status"] == "active");
    CHECK((*plane)["origin"] == "rom");
    const Json* shift = binding(r, "kbd.depth_time.down");
    REQUIRE(shift);
    CHECK((*shift)["input"]["modifiers"][0] == "shift");
    CHECK((*shift)["action"]["pair"] == "depth_time");
    CHECK((*shift)["action"]["axis"] == "vertical");
    CHECK((*binding(r, "kbd.branches.right"))["input"]["modifiers"][0] == "ctrl");
    CHECK((*binding(r, "kbd.branches.right"))["action"]["pair"] == "branches");
    CHECK((*binding(r, "kbd.rotate.up"))["input"]["modifiers"][0] == "alt");
    CHECK((*binding(r, "kbd.rotate.up"))["action"]["kind"] == "rotate");
    const Json* caps = binding(r, "kbd.lock.caps");
    REQUIRE(caps);
    CHECK((*caps)["action"]["kind"] == "switch_view");
    CHECK((*caps)["action"]["declaration"] == "age_lanes");
    CHECK((*caps)["reasons"].empty());  // age_lanes is a shipped declaration
    for (const auto& b : r["bindings"]) CHECK(b["device"] == "keyboard");
  }

  TEST_CASE("touch: on-screen held-modifier buttons and gestures; xr and voice are recorded as data only") {
    fsutil::TempDir td;
    const Json touch = resolve(td.path(), "touch");
    const Json* hold = binding(touch, "touch.hold.shift");
    REQUIRE(hold);
    CHECK((*hold)["action"]["kind"] == "hold_modifier");
    CHECK((*hold)["status"] == "active");
    CHECK(binding(touch, "touch.plane.left"));
    const Json xr = resolve(td.path(), "xr");
    const Json* gaze = binding(xr, "xr.focus.gaze");
    REQUIRE(gaze);
    CHECK((*gaze)["status"] == "recorded");
    CHECK((*gaze)["reasons"][0]["code"] == "device_not_implemented");
    CHECK((*binding(xr, "xr.coarse.left"))["action"]["coarse"] == true);
    CHECK((*binding(resolve(td.path(), "voice"), "voice.zoom.in"))["status"] == "recorded");
    // without a device: every device
    auto all = unwrap(views::Interaction::open(td.path())).resolve(Json::object());
    CHECK(binding(all, "kbd.plane.left"));
    CHECK(binding(all, "xr.focus.gaze"));
  }

  TEST_CASE("everything is remappable by an overlay: a lock becomes zoom, with provenance; nothing in the ROM changes") {
    fsutil::TempDir td;
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", Json{{"view", "interaction_bindings"}, {"op", "modify"}, {"id", "kbd.lock.caps"},
                                                       {"patch", Json{{"action", Json{{"kind", "zoom"}, {"factor", 1.5}, {"declaration", nullptr}}}}}}));
    const Json r = resolve(td.path(), "keyboard");
    const Json* caps = binding(r, "kbd.lock.caps");
    REQUIRE(caps);
    CHECK((*caps)["action"]["kind"] == "zoom");
    CHECK((*caps)["action"]["factor"] == 1.5);
    CHECK(!(*caps)["action"].contains("declaration"));
    CHECK((*caps)["origin"] == "rom");
    CHECK((*caps)["provenance"].size() == 2);
    CHECK((*caps)["provenance"][1]["layer"] == "overlay:u");
    CHECK(r["conflicts"].empty());
    // undo: the preset is back
    unwrap(views::remove_ops(td.path(), "u", "interaction_bindings", "kbd.lock.caps"));
    CHECK((*binding(resolve(td.path(), "keyboard"), "kbd.lock.caps"))["action"]["kind"] == "switch_view");
  }

  TEST_CASE("a binding with the same input as another is a visible warning, never blocked; the last one wins") {
    fsutil::TempDir td;
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", Json{{"view", "interaction_bindings"}, {"op", "add"}, {"id", "mine.caps"},
                                                       {"entry", Json{{"label", Json{{"pl", "Moje"}, {"en", "Mine"}}}, {"device", "keyboard"}, {"input", Json{{"state", "on"}, {"lock", "caps"}}},
                                                                      {"action", Json{{"kind", "zoom"}, {"factor", 2}}}}}}));
    const Json r = resolve(td.path(), "keyboard");
    REQUIRE(r["conflicts"].size() == 1);
    CHECK(r["conflicts"][0]["winner"] == "mine.caps");
    CHECK(r["conflicts"][0]["ids"].size() == 2);
    CHECK(r["conflicts"][0]["message"]["pl"].get<std::string>().find("Konflikt") == 0);
    const Json* old = binding(r, "kbd.lock.caps");
    REQUIRE(old);
    CHECK((*old)["shadowed"] == true);
    CHECK((*old)["shadowed_by"] == "mine.caps");
    CHECK((*old)["reasons"][0]["code"] == "conflict");
    CHECK((*old)["reasons"][0]["level"] == "warning");
    CHECK(binding(r, "mine.caps"));  // both are listed
    CHECK(!(*binding(r, "mine.caps")).contains("shadowed"));
    // a disabled binding is not a conflict and is listed as disabled
    LOOM_REQUIRE_OK(views::put_op(td.path(), "u", Json{{"view", "interaction_bindings"}, {"op", "disable"}, {"id", "kbd.lock.caps"}}));
    const Json r2 = resolve(td.path(), "keyboard");
    CHECK(r2["conflicts"].empty());
    CHECK(!binding(r2, "kbd.lock.caps"));
    CHECK(r2["disabled"][0] == "kbd.lock.caps");
  }

  TEST_CASE("open lists: an unknown device, input field or action is kept in the data and reported as unsupported with a reason") {
    fsutil::TempDir td;
    auto add = [&](const char* id, Json entry) {
      LOOM_REQUIRE_OK(views::put_op(td.path(), "u", Json{{"view", "interaction_bindings"}, {"op", "add"}, {"id", id}, {"entry", std::move(entry)}}));
    };
    add("x.device", Json{{"device", "brainwave"}, {"input", Json{{"thought", "up"}}}, {"action", Json{{"kind", "zoom"}}}});
    add("x.field", Json{{"device", "keyboard"}, {"input", Json{{"foot_pedal", "left"}}}, {"action", Json{{"kind", "zoom"}}}});
    add("x.action", Json{{"device", "keyboard"}, {"input", Json{{"key", "F9"}}}, {"action", Json{{"kind", "teleport"}}}});
    add("x.noinput", Json{{"device", "keyboard"}, {"action", Json{{"kind", "zoom"}}}});
    add("x.view", Json{{"device", "keyboard"}, {"input", Json{{"key", "F8"}}}, {"action", Json{{"kind", "switch_view"}, {"declaration", "nope"}}}});
    const Json r = resolve(td.path(), "");
    CHECK(has_unsupported(r, "x.device", "unsupported_device"));
    CHECK(has_unsupported(r, "x.field", "unsupported_input_field"));
    CHECK(has_unsupported(r, "x.action", "unsupported_action_kind"));
    CHECK(has_unsupported(r, "x.noinput", "binding_without_input"));
    CHECK(!binding(r, "x.action"));
    CHECK(binding(r, "kbd.plane.left"));  // the rest is resolved
    const Json* sw = binding(r, "x.view");  // a warning, not a refusal
    REQUIRE(sw);
    CHECK((*sw)["reasons"][0]["code"] == "switch_view_unknown_declaration");
    CHECK((*sw)["status"] == "active");
    // an unknown device asked for is an issue, not an error
    const Json none = resolve(td.path(), "brainwave");
    CHECK(none["bindings"].empty());
    CHECK(none["issues"][0]["code"] == "unknown_device_requested");
  }
}
