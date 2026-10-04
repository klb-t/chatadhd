#pragma once

#include "loom/runtime_profile.h"

namespace loom::model::detail {

inline Result<RuntimeProfile> checked_model_profile(const RuntimeProfile& profile) {
  if (profile.domain() != "model") return Error(Errc::InvalidArgument, "expected model profile");
  LOOM_TRY_ASSIGN(auto builtin, RuntimeProfile::builtin("model"));
  return builtin.with_values(profile.values());
}

}  // namespace loom::model::detail
