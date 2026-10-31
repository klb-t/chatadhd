#pragma once
#include <string>
#include "loom/result.h"
#include "loom/util/json.h"
namespace loom::chat {
struct DocxCommand { std::string python; std::string script; };
// Trusted deployment configuration only. Never searches uploads, HOME, PATH or checkout.
Result<DocxCommand> docx_command(const Json& reader);
}
