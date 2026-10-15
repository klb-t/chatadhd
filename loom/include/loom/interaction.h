// loom/interaction.h — interaction as data: input -> action on a view, per device (P28, step 2).
//
// The effective view `interaction_bindings` (ROM presets + user overlays, with provenance) is a list of BINDINGS, each
//   {label, device, input: {key | button | gesture | phrase | gaze | lock + state, modifiers?: [held], locks?: [lock states that must be on]},
//    action: {kind, ...}}
// Devices (`interaction_devices`) and action kinds (`interaction_actions`) are open lists: a binding for a device, an input field or an action
// the engine does not know is kept in the data and reported in `unsupported` with a visible reason, never dropped and never fatal. A device the
// build only records (xr, voice) gives bindings with status "recorded". Two bindings with the same normalized input are a CONFLICT: reported as a
// warning (the last one wins), never blocked. Presets: held modifiers choose the axis pair the arrows move; lock states switch alternative views;
// everything is remappable by an overlay (e.g. a lock -> zoom with a `modify` of kbd.lock.caps).
//
// Output of resolve(): {schema: "loom.view_interaction/1", device, devices: [{id, label, implemented, inputs}], actions: [{id, label, pairs?, targets?}], bindings: [{id, label, device, input,
// action, key, origin, provenance, status: active | recorded, shadowed?, shadowed_by?, reasons}], conflicts: [{key, input, ids, winner, message}],
// disabled: [ids], unsupported: [{binding, device, reason}], issues}. Nothing in it depends on a renderer.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "loom/effective_view.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::views {

class Interaction {
 public:
  static Result<Interaction> open(const std::filesystem::path& root, const std::vector<std::filesystem::path>& extra_dirs = {});

  // request: {device?: string (empty = every device)}
  Json resolve(const Json& request) const;

 private:
  Effective bindings_, devices_, actions_, rules_, declarations_;
};

}  // namespace loom::views
