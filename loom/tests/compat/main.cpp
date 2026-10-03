// loom_compat_tool: C++ side of the Python differential tests
// (tests/compat/test_*.py). Subcommands self-register (compat_registry.h).
#include <algorithm>
#include <cstdio>
#include <iostream>

#include "compat_registry.h"
#include "loom/log.h"
#include "loom/util/fs.h"

namespace loom::compat {

namespace {
std::vector<Command>& registry() {
  static std::vector<Command> r;
  return r;
}
}  // namespace

void register_command(Command cmd) { registry().push_back(std::move(cmd)); }
const std::vector<Command>& commands() { return registry(); }

void print_json(const Json& j) {
  std::string s = json::dump(j);
  std::fwrite(s.data(), 1, s.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

int fail(std::string_view message, int code) {
  std::fprintf(stderr, "loom_compat_tool: %.*s\n", static_cast<int>(message.size()), message.data());
  return code;
}

Result<std::string> arg_text(const std::string& arg) {
  if (!arg.empty() && arg[0] == '@') return fsutil::read_file(arg.substr(1));
  return arg;
}

Result<Json> arg_json(const std::string& arg) {
  LOOM_TRY_ASSIGN(std::string text, arg_text(arg));
  return json::parse(text);
}

}  // namespace loom::compat

int main(int argc, char** argv) {
  using namespace loom::compat;
  loom::log::set_stderr(std::getenv("LOOM_COMPAT_VERBOSE") != nullptr);
  auto cmds = commands();
  std::sort(cmds.begin(), cmds.end(), [](const Command& a, const Command& b) { return a.name < b.name; });
  if (argc < 2 || std::string(argv[1]) == "list" || std::string(argv[1]) == "--help") {
    for (const auto& c : cmds) std::printf("%-22s %s\n", c.name.c_str(), c.usage.c_str());
    return argc < 2 ? 2 : 0;
  }
  std::string name = argv[1];
  Args args(argv + 2, argv + argc);
  for (const auto& c : cmds) {
    if (c.name == name) {
      try {
        return c.fn(args);
      } catch (const std::exception& e) {
        return fail(std::string("exception: ") + e.what());
      }
    }
  }
  return fail("unknown command: " + name + " (try 'list')");
}
