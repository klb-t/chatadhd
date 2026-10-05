#include <fstream>
#include <iostream>
#include <sstream>
#include "loom/util/json.h"
using loom::Json;
#include "getter.inc"
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 3;
  std::ostringstream text; text << input.rdbuf();
  auto expected = loom::json::parse(text.str());
  if (!expected) return 4;
  const auto& actual = builtin_generator_codec_fixture();
  if (actual != *expected || loom::json::canonical(actual) != loom::json::canonical(*expected)) return 5;
  std::cout << loom::json::dump(Json{{"passed", true}, {"cases", 1},
      {"strings_round_tripped", actual.at("strings").size()}, {"full_value_equal", true},
      {"canonical_value_equal", true}, {"provider_calls", 0}, {"paid_calls", 0}}) << "\n";
}
