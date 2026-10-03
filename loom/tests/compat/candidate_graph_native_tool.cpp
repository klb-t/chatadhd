// Pure stdin/stdout transport for the independent native candidate-validator gate.
// No Runtime, database, provider, graph projection, or canonical persistence.
#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "loom/knowledge_candidate_graph.h"
#include "loom/util/json.h"

namespace {
constexpr std::size_t kMaxInputBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaxJsonDepth = 128;

bool exceeds_json_depth(const std::string& input) {
  std::size_t depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (const char ch : input) {
    if (quoted) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        quoted = false;
      }
    } else if (ch == '"') {
      quoted = true;
    } else if (ch == '{' || ch == '[') {
      if (++depth > kMaxJsonDepth) return true;
    } else if ((ch == '}' || ch == ']') && depth > 0) {
      --depth;
    }
  }
  return false;
}

int fail(const char* reason) {
  std::cerr << "candidate_graph_native_tool: " << reason << '\n';
  return 2;
}
}  // namespace

int main() {
  std::string input;
  std::array<char, 8192> buffer{};
  while (std::cin) {
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(std::cin.gcount());
    if (count > kMaxInputBytes - input.size()) return fail("input_exceeds_4MiB");
    input.append(buffer.data(), count);
  }
  if (!std::cin.eof()) return fail("stdin_read_failed");
  if (exceeds_json_depth(input)) return fail("input_exceeds_depth128");
  auto parsed = loom::json::parse(input);
  if (!parsed || !parsed->is_object()) return fail("expected_JSON_object");
  if (parsed->size() != 3 || !parsed->contains("packet") ||
      !parsed->contains("bundle") || !parsed->contains("vocabulary")) {
    return fail("expected_packet_bundle_vocabulary");
  }
  try {
    const auto report = loom::extract::validate_candidate_graph_bundle(
        (*parsed)["bundle"], (*parsed)["packet"], (*parsed)["vocabulary"]);
    std::cout << loom::json::dump(report) << '\n';
    return std::cout ? 0 : fail("stdout_write_failed");
  } catch (const std::exception&) {
    return fail("validator_exception");
  }
}
