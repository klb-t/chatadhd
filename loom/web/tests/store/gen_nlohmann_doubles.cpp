// Regenerate: g++ -std=c++20 -I ../../../third_party/nlohmann gen_nlohmann_doubles.cpp -o /tmp/nl && /tmp/nl > nlohmann-doubles.json
// PH29 P1 fixture generator: how nlohmann::json prints doubles (compared with the browser's formatDouble).
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <cstdint>
int main() {
  std::mt19937_64 rng(20261029);
  nlohmann::ordered_json out = nlohmann::ordered_json::array();
  auto add = [&](double d) { std::uint64_t b; std::memcpy(&b, &d, 8); char hex[17]; std::snprintf(hex, 17, "%016llx", (unsigned long long)b);
    out.push_back({hex, nlohmann::ordered_json(d).dump()}); };
  double fixed[] = {0.0, -0.0, 1.0, -1.0, 0.5, 0.1, 0.2, 0.30000000000000004, 1e15, 1e14, 123456789012345.0, 1234567890123456.0, 1e-4, 1e-5, 0.00012345,
                    1.5e300, 5e-324, 1.7976931348623157e308, 1e21, 1e20, 3.14e-10, 2.5, 100.0, 1e16, 9007199254740993.0, 0.7, 1.1, 2.0/3.0};
  for (double d : fixed) add(d);
  std::uniform_real_distribution<double> u(-1000, 1000);
  std::uniform_int_distribution<int> ex(-30, 30);
  for (int i = 0; i < 3000; ++i) add(u(rng) * std::pow(10.0, ex(rng)));
  for (int i = 0; i < 2000; ++i) { std::uint64_t b = rng(); double d; std::memcpy(&d, &b, 8); if (std::isfinite(d)) add(d); }
  for (int i = 0; i < 1000; ++i) add(double(int(u(rng) * 1000)) / 100.0);
  std::puts(out.dump().c_str());
}
