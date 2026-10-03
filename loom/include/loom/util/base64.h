// loom/util/base64.h — RFC 4648 base64 (standard alphabet, padded).
#pragma once

#include <string>
#include <string_view>

#include "loom/result.h"

namespace loom::base64 {

std::string encode(std::string_view bytes);

// strict=false mirrors Python base64.b64decode(validate=False): characters
// outside the alphabet are discarded before decoding; padding must still be
// correct ("Incorrect padding" -> Errc::Parse). strict=true rejects them.
Result<std::string> decode(std::string_view text, bool strict = false);

}  // namespace loom::base64
