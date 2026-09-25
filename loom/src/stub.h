// Helpers for wave-2 stubs. Every stubbed function body is marked
// "// STUB: wave2" so `grep -rn "STUB: wave2" loom/src` lists what is left.
#pragma once

#include <string>

#include "loom/result.h"

#define LOOM_NOT_IMPLEMENTED(what) \
  ::loom::Error(::loom::Errc::NotImplemented, std::string(what) + " is not implemented yet (wave 2)")
