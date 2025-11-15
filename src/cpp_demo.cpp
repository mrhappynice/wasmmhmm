#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <emscripten/bind.h>

std::string greet_cpp(std::string name) {
  return "hello, " + (name.empty() ? std::string("world") : name) + " 👋 (from C++/WASM)";
}

uint32_t count_primes_cpp(uint32_t n) {
  if (n < 2) return 0;
  std::vector<bool> is_prime(n + 1, true);
  is_prime[0] = false;
  is_prime[1] = false;

  for (uint32_t p = 2; p * p <= n; ++p) {
    if (is_prime[p]) {
      for (uint32_t m = p * p; m <= n; m += p) {
        is_prime[m] = false;
      }
    }
  }

  return std::count(is_prime.begin(), is_prime.end(), true);
}

EMSCRIPTEN_BINDINGS(my_module) {
  emscripten::function("greet_cpp", &greet_cpp);
  emscripten::function("count_primes_cpp", &count_primes_cpp);
}

