// S2/R9 (GRAF_HUB_KOD_SILNIK §4.3): input/output devices as graph nodes. Classes and traits live in
// data/graph/classes.pack, instances and per-platform availability in data/graph/devices.pack; actions fit a
// device by the traits of its class, exactly as for every other element.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "loom/action_rules.h"
#include "loom/graph_entry.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

const std::filesystem::path kLoomDir = std::filesystem::weakly_canonical(std::filesystem::path(LOOM_TEST_FIXTURES) / ".." / "..");

Json read_json(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  REQUIRE(in.good());
  std::ostringstream ss;
  ss << in.rdbuf();
  return Json::parse(ss.str());
}

std::set<std::string> action_ids(const Json& result) {
  std::set<std::string> ids;
  for (const auto& r : result.at("actions")) ids.insert(r.at("action").get<std::string>());
  return ids;
}

Json load_classes() { return read_json(kLoomDir / "data/graph/classes.pack"); }
Json load_devices() { return read_json(kLoomDir / "data/graph/devices.pack"); }

}  // namespace

TEST_SUITE("graph_devices") {

  TEST_CASE("every device has a class that is a device, with traits, and availability for every platform") {
    const Json classes = load_classes(), devices = load_devices();
    const Json& platforms = devices.at("platforms");
    const Json& values = devices.at("availability_values");
    REQUIRE(platforms.size() == 4);
    for (const char* p : {"web", "android", "server", "cli"}) CHECK(platforms.contains(p));
    for (const char* d : {"microphone", "camera", "screen", "speaker", "file_access", "clipboard", "notifications",
                          "storage_backend", "model_provider", "server_endpoint"})
      CHECK(devices.at("devices").contains(d));
    for (const auto& [id, dev] : devices.at("devices").items()) {
      INFO("device " << id);
      const std::string cls = dev.at("class").get<std::string>();
      REQUIRE(classes.at("classes").contains(cls));
      bool is_device = false, has_trait = false;
      std::set<std::string> seen;
      for (std::string c = cls; !c.empty() && seen.insert(c).second;) {
        const Json& k = classes.at("classes").at(c);
        if (c == "device") is_device = true;
        for (const auto& t : k.value("traits", Json::array())) if (t == "device") is_device = true;
        if (k.contains("traits") && !k.at("traits").empty()) has_trait = true;
        for (const auto& t : k.value("traits", Json::array())) CHECK(classes.at("traits").contains(t.get<std::string>()));
        c = k.value("parent", std::string());
      }
      CHECK(is_device);
      CHECK(has_trait);
      for (const auto& [p, _] : platforms.items()) {
        REQUIRE(dev.at("availability").contains(p));
        CHECK(values.contains(dev.at("availability").at(p).get<std::string>()));
      }
      CHECK(dev.at("availability").size() == platforms.size());
    }
  }

  TEST_CASE("availability per platform is as declared") {
    const Json devices = load_devices();
    const Json& d = devices.at("devices");
    CHECK(d.at("microphone").at("availability").at("web") == "permission");
    CHECK(d.at("microphone").at("availability").at("server") == "unavailable");
    CHECK(d.at("file_access").at("availability").at("cli") == "available");
    CHECK(d.at("screen").at("availability").at("android") == "available");
  }

  TEST_CASE("the graph entry lists the devices with class and availability") {
    const Json devices = load_devices();
    fsutil::TempDir td;
    graph_entry::Env env;
    env.data_dir = td.path();
    const Json view = unwrap(graph_entry::compute(env));
    const Json* node = nullptr;
    for (const auto& s : view["seed"]) if (s["id"] == "seed:devices") node = &s;
    REQUIRE(node);
    std::set<std::string> ids;
    for (const auto& dv : (*node)["devices"]) {
      ids.insert(dv["id"].get<std::string>());
      CHECK(dv.contains("class"));
      CHECK(dv["availability"].size() == 4);
    }
    CHECK(ids.size() == devices.at("devices").size());
    CHECK((*node)["platforms"].size() == 4);
    const Json& count = (*node)["counts"]["devices"];
    REQUIRE(count["known"] == true);
    // storages expand to one node per configured storage of the storage_security profile
    CHECK(count["by"]["storage_backend"].get<int>() >= 5);
    CHECK(count["by"]["microphone"] == 1);
  }

  TEST_CASE("actions.for on a device id matches by class traits only") {
    fsutil::TempDir td;
    auto ids_for = [&](const char* element) {
      return action_ids(unwrap(actions::invoke(td.path(), "for", Json{{"element", element}})));
    };
    // universal actions (no requirements) fit every device
    for (const char* d : {"microphone", "camera", "screen", "speaker", "file_access", "clipboard", "notifications",
                          "storage_backend", "model_provider", "server_endpoint"}) {
      INFO("device " << d);
      const auto ids = ids_for(d);
      CHECK(ids.count("use_model") == 1);
      CHECK(ids.count("storage.status") == (std::string(d) == "storage_backend" ? 1u : 0u));
      CHECK(ids.count("transcribe") == 0);  // a microphone is not a recording: it has no has_audio
    }
    // a storage backend is a storage: the existing storage.* catalogue entries fit it through the class lineage
    const auto st = ids_for("storage_backend");
    for (const char* a : {"storage.status", "storage.probe", "storage.copy", "storage.backup"}) CHECK(st.count(a) == 1);
    const Json sj = unwrap(actions::invoke(td.path(), "for", Json{{"element", "storage_backend"}}));
    CHECK(sj["element"]["traits"].dump().find("device") != std::string::npos);
  }
}
