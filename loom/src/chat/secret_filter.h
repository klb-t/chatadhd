// secret_filter.h — the check of an outgoing chat message for secrets (R67: the setting secrets_in_chat is read by this).
//
// The recipe is data (runtime/chat_secrets.pack): the patterns (regular expressions with flags and labels in pl and en), the scan of the values in
// the secrets store, the scan of attached text files, the action per value of the setting (refuse | hold | pass) and the words. Native code only
// runs the scan and the action. A hit is reported by rule and place, NEVER by value: the pending document is shown on the screen and may be logged.
#pragma once

#include <string>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::chat::secrets {

// One piece of what leaves: the sentence or the text of one attached file.
struct Source {
  std::string where;  // "text" | "file"
  std::string file;   // the file name, for "file"
  std::string text;
};

// null: the message may go on (no hit, or the action is pass, or the person confirmed a hold). Otherwise the pending document:
//   {"schema":"loom.chat_secret_pending/1","status":"needs_confirmation"|"refused","mode","action","hits":[{rule,label,where,place,file?,key?}],
//    "text":{pl,en},"actions":{"send_anyway":{pl,en},"dont_send":{pl,en}}?,"resend":"..."?}
// `store_values`: {key: value} of the secrets store; `confirmed`: the request carries secret_confirmation {approved:true}.
Result<Json> gate(const Json& recipe, const std::string& mode, const std::vector<Source>& sources, const Json& store_values, bool confirmed);

}  // namespace loom::chat::secrets
