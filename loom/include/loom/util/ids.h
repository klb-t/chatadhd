// loom/util/ids.h — ID generation compatible with the Python engine.
//
// Python: prefix + uuid.uuid4().hex[:12]. The first 12 hex digits of a UUID4
// are fully random (the version nibble is digit 13), so this is exactly
// 48 random bits rendered as 12 lowercase hex characters.
//
// Prefixes in use: c_ m_ vg_ n_ l_ (Python core), src_ pv_ ev_ t_ a_ (Loom).
// MemoryEngine node IDs have no prefix (Python memory_engine._gen_id).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace loom {

std::string random_hex(std::size_t n_chars);
std::string gen_id(std::string_view prefix = "");
// True if `id` == prefix + 12 lowercase hex chars.
bool is_generated_id(std::string_view id, std::string_view prefix) noexcept;

namespace id_prefix {
inline constexpr std::string_view kConversation = "c_";
inline constexpr std::string_view kMessage = "m_";
inline constexpr std::string_view kVersionGroup = "vg_";
inline constexpr std::string_view kNode = "n_";
inline constexpr std::string_view kLink = "l_";
inline constexpr std::string_view kSource = "src_";
inline constexpr std::string_view kProvenance = "pv_";
inline constexpr std::string_view kEvent = "ev_";
inline constexpr std::string_view kTask = "t_";
inline constexpr std::string_view kArtifact = "a_";
}  // namespace id_prefix

}  // namespace loom
