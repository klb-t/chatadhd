// Pre-existing public API only; deterministic summaries of random paths/time.
#include <iostream>

#include "loom/log.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"

using namespace loom;

int main() {
  Json result = Json::object();
  result["initial_level"] = static_cast<int>(log::level());
  Json records = Json::array();
  for (const auto lvl : {log::Level::Debug, log::Level::Info, log::Level::Warning, log::Level::Error,
                         static_cast<log::Level>(123)}) {
    const log::Record r{lvl, "synthetic.logger", "unicode Ą {{message}}\nsecond line", "12:34:56"};
    records.push_back(Json{{"level_name", std::string(log::level_name(lvl))}, {"formatted", r.formatted()}});
  }
  result["records"] = records;
  log::set_stderr(false);
  log::set_level(log::Level::Info);
  log::clear_recent();
  for (int i = 0; i < 503; ++i) log::write(log::Level::Info, "synthetic.logger", "initial" + std::to_string(i));
  const auto initial_ring = log::recent(1000);
  result["initial_ring_size"] = initial_ring.size();
  result["initial_ring_first"] = initial_ring.front().substr(8);
  result["initial_ring_last"] = initial_ring.back().substr(8);
  log::set_ring_capacity(5);
  log::clear_recent();
  Json seen = Json::array();
  const auto sink = log::add_sink([&](const log::Record& r) { seen.push_back(Json{{"level", static_cast<int>(r.level)}, {"logger", r.logger}, {"message", r.message}}); });
  const auto broken = log::add_sink([](const log::Record&) { throw 42; });
  for (int i = 0; i < 7; ++i) log::write(log::Level::Info, "synthetic.logger", "line" + std::to_string(i));
  log::write(log::Level::Debug, "synthetic.logger", "suppressed");
  log::remove_sink(sink);
  log::remove_sink(broken);
  result["sink_records"] = seen;
  Json ring_rows = Json::array();
  const auto summarize = [](std::vector<std::string> lines) {
    Json out = Json::array();
    for (auto& line : lines) { line.replace(0, 8, "12:34:56"); out.push_back(line); }
    return out;
  };
  for (std::size_t count : {0, 1, 3, 10}) ring_rows.push_back(Json{{"limit", count}, {"lines", summarize(log::recent(count))}});
  result["ring_rows"] = ring_rows;
  result["ring_default"] = summarize(log::recent());
  log::set_ring_capacity(0);
  log::write(log::Level::Error, "synthetic.logger", "capacity-zero-legacy");
  result["ring_zero_capacity"] = summarize(log::recent());
  log::set_ring_capacity(500);
  log::clear_recent();

  Json paths = Json::array();
  std::filesystem::path created;
  {
    fsutil::TempDir dir;
    created = dir.path();
    paths.push_back(Json{{"valid", dir.valid()}, {"is_directory", std::filesystem::is_directory(dir.path())},
                        {"parent", dir.path().parent_path().string()},
                        {"prefix", dir.path().filename().string().substr(0, 5)}, {"name_length", dir.path().filename().string().size()}});
  }
  result["default_cleanup"] = !std::filesystem::exists(created);
  {
    fsutil::TempDir dir("custom_");
    paths.push_back(Json{{"valid", dir.valid()}, {"is_directory", std::filesystem::is_directory(dir.path())},
                        {"prefix", dir.path().filename().string().substr(0, 7)}, {"name_length", dir.path().filename().string().size()}});
    fsutil::TempDir moved(std::move(dir));
    result["move"] = Json{{"from_valid", dir.valid()}, {"to_valid", moved.valid()}};
  }
  {
    fsutil::TempDir dir("__loom_nonexistent_fixture__/child_");
    result["failed_temp_valid"] = dir.valid();
  }
  result["paths"] = paths;
  std::cout << json::dump(result) << '\n';
}
