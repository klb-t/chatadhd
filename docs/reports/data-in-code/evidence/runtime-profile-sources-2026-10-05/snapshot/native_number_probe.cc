#include <iostream>
#include "loom/util/json.h"
int main() {
  auto integers = loom::json::parse(R"({"wide":18446744073709551616,"quantity":36893488147419103232})");
  auto exponents = loom::json::parse(R"({"wide":1.8446744073709552e19,"quantity":3.6893488147419103e19})");
  if (!integers || !exponents || !integers->at("wide").is_number_float() ||
      !integers->at("quantity").is_number_float() || *integers != *exponents) return 1;
  std::cout << loom::json::canonical(*integers) << '\n';
  return 0;
}
