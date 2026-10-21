// loom/turn_carrier.h - the values that hold for ONE request only (M1-K1b): "szerzej", "weź profil lean" said for the next turn.
//
// The action context.modify puts a value here for a conversation; the next ChatEngine::send of that conversation takes everything the
// carrier holds (the carrier is empty afterwards), reads it as the `turn` level of the scope chain (scopes::Binding.turn) and says so in the
// receipt. Nothing here is written to the data directory: a restart forgets it, as a turn that never happened. The preview of a request
// (preview_usage) only looks (peek) and consumes nothing.
//
// Values are keyed like the scoped values (action_rules.pack scoped_values.keys with default_level turn): context.breadth_delta and
// context.depth_delta hold a LIST of step ids (said twice = applied twice), context.profile holds one profile id (the last one said wins).
#pragma once

#include <map>
#include <mutex>
#include <string>

#include "loom/util/json.h"

namespace loom {

class TurnCarrier {
 public:
  // `append`: the value is a step id that joins the list of its key; otherwise it replaces the key's value.
  // `conv_id` "" stands for the conversation that is not open yet: the next send that starts one takes it.
  void add(const std::string& conv_id, const std::string& key, const Json& value, bool append);
  // The values now (object {key: value}); an empty object when nothing is waiting. Consumes nothing.
  Json peek(const std::string& conv_id) const;
  // The values now, and the carrier is empty for this conversation afterwards.
  Json take(const std::string& conv_id);
  // A request that took the values and did not go out gives them back; a value said in the meantime wins over the returned one.
  void give_back(const std::string& conv_id, const Json& values);
  // Drops what waits for the conversation. Returns whether something was there.
  bool clear(const std::string& conv_id);
  // How many conversations have something waiting (tests, status).
  std::size_t waiting() const;

 private:
  mutable std::mutex mu_;
  std::map<std::string, Json> by_conv_;
};

}  // namespace loom
