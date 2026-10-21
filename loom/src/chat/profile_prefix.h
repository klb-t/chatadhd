// profile_prefix.h — the class "profile" of the request (M1-K1a): what the person told the app about themselves, rendered as one system message.
//
// The recipe is data (chat_context.pack, section profile_prefix); the parts of the node You and their order are data too (user.pack,
// entry profile.subgraphs/v1: order_for_prefix, stability_rank). The rendering is a pure function of (recipe, subgraphs, level, facts): the same
// profile gives the same bytes in every turn (no time stamps, no counters, a fixed order), so a provider can keep the prefix of the request.
// Only values the person set themselves (the layer "user" of the onboarding layers) are facts; presets and proposals are not.
#pragma once

#include <string>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Database;
}

namespace loom::chat::prefix {

struct Fact {
  std::string subgraph;  // id of a part of the node You (identity, preferences, ...)
  std::string key;       // key of the onboarding layers (preference.style)
  Json value;
};

struct Rendered {
  std::string text;                 // "" = nothing to send (no class "profile" in the request)
  std::string level;                // the detail level used (profile_detail_level, or the recipe's default when it names no region)
  Json items = Json::array();       // [{ref, label, subgraph, tokens_est}] in the order they were rendered: references, not values
};

// Pure and deterministic. Subgraphs are taken in the order of their order_for_prefix (null = not part of the prefix), then stability_rank, then id;
// the keys of a subgraph in the order of recipe.subgraph_keys; empty values are skipped.
Rendered render(const Json& recipe, const Json& subgraphs, const std::string& level, const std::vector<Fact>& facts);

struct Gathered {
  std::vector<Fact> facts;
  Json subgraphs = Json::array();   // profile.subgraphs/v1 .subgraphs of the stored pack, else of the shipped one
  std::string unavailable;          // "" or why no profile was read: no_profile | several_profiles | unreadable
};

// The values the person set (layer "user") for the keys named by recipe.subgraph_keys. One stored profile is the person; with several, none is
// guessed (unavailable = several_profiles) because a chat request does not name one.
Gathered gather(Database& db, const Json& recipe);

}  // namespace loom::chat::prefix
