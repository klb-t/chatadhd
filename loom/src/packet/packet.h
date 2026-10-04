#pragma once
#include "loom/util/json.h"
namespace loom::packet {
// Projection operations. No database writes or provider calls.
Result<Json> execute(const Json& request);
Json capture(std::string_view raw);
std::string digest(const Json& value);
Json parse_strict(std::string_view raw);
void validate(const Json& packet, const Json& limits = Json::object());
Json empty_diff(const Json& packet, const Json& proposal, const Json& origin, const Json& known_at);
Json preview(const Json& packet, const Json& diff, const Json& limits = Json::object());
Json compile_reply(const Json& packet, std::string_view raw, const Json& host, const Json& limits);
Json reply_fragment(const Json& compilation, const Json& address);
void shape(const Json& value, std::initializer_list<const char*> fields);
void text(const Json& value, bool empty = false);
void origin(const Json& value);
void timestamp(const Json& value);
void resources(const Json& value, const Json& limits);
}  // namespace loom::packet
