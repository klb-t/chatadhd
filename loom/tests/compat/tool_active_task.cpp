// Native ActiveTaskSpec compiler entry point for Python/reference differential tests.
#include "compat_registry.h"

#include "chat/active_task_spec.h"

using namespace loom;
using namespace loom::compat;

LOOM_COMPAT_COMMAND(cmd_active_task_compile, "active-task-compile",
                    "@spec.json : validate and deterministically compile an ActiveTaskSpec") {
  if (args.size() != 1) return fail("usage: active-task-compile @spec.json");
  auto spec = arg_json(args[0]);
  if (!spec) return fail(spec.error().to_string());
  auto result = chat::compile_active_task_spec(*spec);
  if (!result) {
    print_json(Json{{"ok", false},
                    {"error", {{"code", std::string(errc_name(result.error().code))},
                               {"message", result.error().message}}}});
    return 0;
  }
  print_json(Json{{"ok", true}, {"compiled", std::move(*result)}});
  return 0;
}
