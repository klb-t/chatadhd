// loom/util/random.h — OS cryptographic randomness.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace loom {

// Fills `out` with OS-provided random bytes (getrandom / arc4random /
// /dev/urandom). Aborts only if the OS has no entropy source at all.
void random_bytes(void* out, std::size_t len) noexcept;
std::string random_bytes(std::size_t len);
std::uint64_t random_u64() noexcept;

}  // namespace loom
