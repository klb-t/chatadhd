// loom_compat_tool subcommand registry.
//
// Each area adds commands in its own tests/compat/tool_<area>.cpp without
// touching main.cpp:
//
//   LOOM_COMPAT_COMMAND(cmd_dump, "db-dump", "<db> : canonical API dump") {
//     ... args[0] ...
//     loom::compat::print_json(result);
//     return 0;
//   }
//
// Conventions: results go to stdout as one JSON document; diagnostics go to
// stderr; exit code 0 = success, 1 = check failed, 2 = usage / runtime error.
// Arguments that start with '@' are file paths whose content is the value
// (use loom::compat::arg_json / arg_text).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::compat {

using Args = std::vector<std::string>;
using CommandFn = int (*)(const Args& args);

struct Command {
  std::string name;
  std::string usage;
  CommandFn fn;
};

void register_command(Command cmd);
const std::vector<Command>& commands();

struct Registrar {
  Registrar(const char* name, const char* usage, CommandFn fn) { register_command(Command{name, usage, fn}); }
};

// stdout helpers
void print_json(const Json& j);
int fail(std::string_view message, int code = 2);

// Argument helpers ('@path' -> file content).
Result<std::string> arg_text(const std::string& arg);
Result<Json> arg_json(const std::string& arg);

}  // namespace loom::compat

#define LOOM_COMPAT_COMMAND(ident, name, usage)                                              \
  static int ident(const ::loom::compat::Args& args);                                       \
  static const ::loom::compat::Registrar ident##_registrar_(name, usage, &ident);           \
  static int ident(const ::loom::compat::Args& args)
