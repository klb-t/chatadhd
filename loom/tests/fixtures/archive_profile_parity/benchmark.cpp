// Offline helper benchmark. No Runtime, providers or user data.
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include "archive/archive_internal.h"

using namespace loom::archive;
namespace {
std::uint64_t fixture() {
  static constexpr std::array<std::string_view, 6> texts = {
      "We decided to use SQLite for structured retrieval.",
      "The importer must preserve branch context and source evidence.",
      "Should we enable vectors or BM25 for graph retrieval?",
      "FIXME: The archive file parser loses attachments.",
      "Implementacje pamięci muszą zachować pochodzenie informacji.",
      "Review should include tests and measurements."};
  std::uint64_t checksum = 0;
  for (const auto text : texts) {
    const auto words = content_tokens(text);
    checksum += words.size() + candidate_terms(text).size() + split_sentences(text).size();
    const auto category = classify_sentence(text, "");
    checksum += category.type.size() + category.cues.size();
    for (const auto& word : words) checksum += stem(word).size() + gloss(word).size() + is_stopword(word);
  }
  return checksum;
}
}
int main() {
  using Clock = std::chrono::steady_clock;
  const auto cold_begin = Clock::now();
  const auto cold_checksum = fixture();
  const auto cold_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - cold_begin).count();
  constexpr int repetitions = 500;
  std::uint64_t checksum = 0;
  const auto steady_begin = Clock::now();
  for (int i = 0; i < repetitions; ++i) checksum += fixture();
  const auto steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - steady_begin).count();
  std::cout << "{\"cold_ns\":" << cold_ns << ",\"steady_ns\":" << steady_ns
            << ",\"repetitions\":" << repetitions << ",\"cold_checksum\":" << cold_checksum
            << ",\"steady_checksum\":" << checksum << "}\n";
}
