#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>   // for snprintf
#include <string.h>  // for memset
#include <stdlib.h>  // for malloc, free

EMSCRIPTEN_KEEPALIVE
const char* greet(const char* name) {
  // NOTE: returns static storage for simplicity; for real apps, pass a JS buffer in.
  static char buf[128];
  snprintf(buf, sizeof(buf), "hello, %s 👋 (from C/WASM)", name ? name : "world");
  return buf;
}

EMSCRIPTEN_KEEPALIVE
uint32_t count_primes(uint32_t n) {
  if (n < 2) return 0;

  uint32_t size = n + 1;
  uint32_t count = 0;

  bool *is_prime = (bool*)malloc(size * sizeof(bool));
  if (!is_prime) return 0; // simple OOM guard

  memset(is_prime, true, size * sizeof(bool));
  is_prime[0] = false;
  is_prime[1] = false;

  for (uint32_t p = 2; p * p <= n; ++p) {
    if (is_prime[p]) {
      for (uint32_t m = p * p; m <= n; m += p) {
        is_prime[m] = false;
      }
    }
  }

  for (uint32_t i = 2; i <= n; ++i) {
    if (is_prime[i]) ++count;
  }

  free(is_prime);
  return count;
}

