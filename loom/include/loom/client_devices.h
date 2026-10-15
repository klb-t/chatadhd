// loom/client_devices.h — the devices a CLIENT can drive (P13, GRAF_HUB_KOD_SILNIK §4 pt 3, R54).
//
// The runtime does not know its platform: the microphone, the camera, the clipboard belong to the client that
// holds them (the web page, the Android shell). A client therefore DECLARES, per session, which device
// capabilities it can carry out right now - captures_audio, captures_image, plays_audio, shows_notifications,
// clipboard, picks_files ... - each with its permission state (granted | prompt | denied). The kernel keeps the
// declaration in memory for that client only (never in shipped data, never in the event log) and, with it:
//
//   * actions.for / explain / propose_chain / preview / run and the chat router show a device action that the
//     client declared as RUNNABLE FOR THIS CLIENT (status implemented, `runs_on: client`); for a client that
//     declared nothing - and for the server side - it stays `planned`, with its reason, exactly as before;
//   * a plan step that is such an action pauses the run with a `client_step` receipt (durable like a rule
//     confirmation): the client carries it out and answers with actions.client_result, which continues the chain.
//
// Mechanism vs data: which action needs which capability is the `client` member of the action's catalogue entry
// (runtime/actions.pack); the permission states, aliases, time to live and the texts are the profile
// `client_devices`. A new device action, or a client with a capability of its own, is data.
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::actions {

inline constexpr std::string_view kClientSchema = "loom.client_declaration/1";
inline constexpr std::string_view kClientStepSchema = "loom.client_step/1";

// What the kernel holds of the clients that declared themselves. Thread-safe.
class ClientRegistry {
 public:
  // {client_id?, client?: {kind, platform, label}, capabilities: {name: "granted" | "prompt" | "denied" |
  // {permission, via?, detail?}} | [names]}. The declaration replaces the client's previous one. The answer is the
  // stored declaration:
  //   {schema, client_id, client{kind,platform?,label?}, declared, expires?, capabilities{name:{permission,via?,
  //    detail?,runnable,actions[...]}}, runnable_actions[...], refused{name: permission}}
  // `profile` = values of the runtime profile client_devices; `catalogue` = the action catalogue (id -> entry).
  Result<Json> declare(const Json& request, const Json& profile, const Json& catalogue);
  // The declaration of a client, or null (never declared, or older than the time to live it was declared with).
  Json find(std::string_view client_id) const;
  // [{client_id, kind, declared, expires?}] of the clients that are still valid.
  Json list() const;
  // Replaces the clock (milliseconds since the epoch): tests of expiry.
  void set_clock(std::function<std::int64_t()> now_ms);

 private:
  std::int64_t now_ms() const;
  mutable std::mutex mu_;
  struct Entry {
    Json declaration;
    std::int64_t expires_ms = -1;  // -1 = never
  };
  std::map<std::string, Entry, std::less<>> clients_;
  std::function<std::int64_t()> clock_;
};

// What a declaration changes in the catalogue, per action: {action id: {status, runs_on, client{...}}} for the
// device actions the client can run, {action id: {planned_reason}} for the ones it declared as denied. Empty
// object for a null declaration. Engine::apply_client applies it.
Json client_patches(const Json& catalogue, const Json& declaration, const Json& profile);

// The capability the catalogue entry needs from a client ("" = the action is not carried out by a client).
std::string client_capability(const Json& entry);

// The part of the catalogue entry that tells a client what to do: {schema, run_id, step, action, capability,
// permission?, with, inputs[part json], expects[kinds], prompt{en,pl}, title}.
Json client_step_spec(const Json& entry, std::string_view run_id, std::string_view step_id, const Json& with, const Json& inputs);

}  // namespace loom::actions
