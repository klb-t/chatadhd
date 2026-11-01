// Synthetic boundary probe only. No runtime/API registration or source execution.
#include <fstream>
#include <iostream>
#include "loom/tools/python_ast_basic/native_projection.h"
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::ifstream source(argv[1], std::ios::binary), receipt(argv[2]);
  std::string raw{std::istreambuf_iterator<char>(source), {}};
  loom::Json value; receipt >> value;
  auto result = loom::text::sources::python_ast::validate(value, raw,
      value.at("source").at("uri").get<std::string>(),
      value.at("source").at("title").get<std::string>());
  std::cout << (result ? "byte_contract_accepted\n" : "byte_contract_refused\n");
  return result ? 0 : 1;
}
