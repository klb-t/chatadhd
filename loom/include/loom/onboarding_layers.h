#pragma once

#include <string_view>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::onboarding {

// One immutable identity-based layer mechanism for arbitrary graph defaults.
// Policy values and default values belong to the supplied pack, not this code.
class DefaultLayers {
 public:
  // Missing state members are initialised without discarding unknown members.
  // Calling create with a newer pack is the same forward migration as update_pack.
  static Result<DefaultLayers> create(const Json& pack, const Json& state = Json::object());
  const Json& snapshot() const noexcept { return state_; }
  const Json& pack() const noexcept { return pack_; }
  // Effective presentation from the same resolver; null means suppression.
  const Json& presentation() const noexcept { return presentation_; }

  // Returns effective/disabled/excluded/proposal/missing plus source explanation.
  // Suppressed defaults never have an effective `value` in this result.
  Result<Json> resolve(std::string_view key) const;
  // Explanation language comes from the effective presentation catalog unless
  // the caller supplies a locale. Suppressed presentation yields explicit null.
  Result<Json> resolve(std::string_view key, std::string_view locale) const;
  // Returns a replacement persistent state; the original instance is unchanged.
  // Ops: override, clear_override, disable, exclude, reenable, accept_proposal,
  // set_area_mode. An override of an excluded id requires explicit reenable first.
  Result<Json> dispatch(const Json& action) const;
  Result<Json> update_pack(const Json& pack) const;

 private:
  Result<Json> resolve_raw(std::string_view key) const;
  Json pack_;
  Json state_;
  Json presentation_;
};

// Adapt effective graph layers to the W11 RuntimeProfile descriptor. `bindings`
// maps RFC6901 value pointers to layer keys. Validation is delegated entirely to
// RuntimeProfile::from_definition/with_values; disabled/excluded/proposed values
// are removed, never silently resurrected from descriptor defaults. Returns
// Unavailable if W11 has not yet been integrated, never a permissive substitute.
Result<Json> runtime_profile_values(const Json& definition, const DefaultLayers& layers,
                                    const Json& bindings);

}  // namespace loom::onboarding
