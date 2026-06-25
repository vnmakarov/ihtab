// profile_lookup.cpp — focused profiling harness for ihtab vs absl IntLookup
// Usage: ./profile_lookup --absl | --ihtab | --both
// Designed for: perf stat ./profile_lookup --ihtab
//               valgrind --tool=cachegrind ./profile_lookup --both

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Woverflow"
#include "absl/container/internal/raw_hash_set.cc"
#include "absl/base/internal/raw_logging.cc"
#include "absl/hash/internal/hash.cc"
#include "absl/hash/internal/city.cc"
#include "absl/hash/internal/low_level_hash.cc"
#include "absl/container/flat_hash_map.h"
#pragma GCC diagnostic pop

extern "C" {
#include "vmum.h"
}
#undef static_assert

#ifdef USE_V0
#include "ihtab-v0.hpp"
#else
#include "ihtab.hpp"
#endif

static volatile size_t do_not_optimize;

struct int_hash {
  using is_avalanching = void;
  size_t operator() (uint64_t k) const { return vmum_hash64 (k, 0); }
};

struct ihtab_int_entry {
  uint64_t key;
  int value;
};
struct ihtab_int_hash {
  size_t operator() (const ihtab_int_entry &e) const { return vmum_hash64 (e.key, 0); }
};
struct ihtab_int_eq {
  bool operator() (const ihtab_int_entry &a, const ihtab_int_entry &b) const { return a.key == b.key; }
};
using ihtab_int_t = iht::ihtab<ihtab_int_entry, ihtab_int_hash, ihtab_int_eq>;

#ifndef PROFILE_N
#define PROFILE_N 20000000
#endif
static const int N = PROFILE_N;
static const int ITERS = 10;

static std::vector<uint64_t> gen_keys () {
  std::vector<uint64_t> keys (N);
  std::mt19937_64 rng (42);
  for (int i = 0; i < N; i++) keys[i] = rng () % (N * 10ULL);
  return keys;
}

__attribute__ ((noinline)) static void profile_absl (const std::vector<uint64_t> &keys) {
  absl::flat_hash_map<uint64_t, int, int_hash> m;
  for (int i = 0; i < N; i++) m[keys[i]] = i;

  int total = 0;
  for (int iter = 0; iter < ITERS; iter++) {
    for (int i = 0; i < N; i++) {
      auto it = m.find (keys[i]);
      if (it != m.end ()) total += it->second;
    }
  }
  do_not_optimize = total;
}

__attribute__ ((noinline)) static void profile_ihtab (const std::vector<uint64_t> &keys) {
  ihtab_int_t m (8);
  for (int i = 0; i < N; i++) {
    ihtab_int_entry e{keys[i], i};
    ihtab_int_entry *r;
    if (!m.perform (e, iht::INSERT, &r)) *r = e;
  }

  int total = 0;
  for (int iter = 0; iter < ITERS; iter++) {
    for (int i = 0; i < N; i++) {
      ihtab_int_entry e{keys[i], 0};
      ihtab_int_entry *r;
      if (m.perform (e, iht::FIND, &r)) total += r->value;
    }
  }
  do_not_optimize = total;
}

int main (int argc, char *argv[]) {
  bool run_absl = false, run_ihtab = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp (argv[i], "--absl") == 0) run_absl = true;
    else if (strcmp (argv[i], "--ihtab") == 0) run_ihtab = true;
    else if (strcmp (argv[i], "--both") == 0) run_absl = run_ihtab = true;
    else {
      fprintf (stderr, "Usage: %s --absl | --ihtab | --both\n", argv[0]);
      return 1;
    }
  }
  if (!run_absl && !run_ihtab) {
    fprintf (stderr, "Usage: %s --absl | --ihtab | --both\n", argv[0]);
    return 1;
  }

  auto keys = gen_keys ();
  printf ("N=%d, ITERS=%d, total ops=%d\n", N, ITERS, N * ITERS);

  if (run_absl) {
    printf ("Profiling absl::flat_hash_map IntLookup...\n");
    profile_absl (keys);
    printf ("  done (sink=%zu)\n", (size_t) do_not_optimize);
  }
  if (run_ihtab) {
    printf ("Profiling ihtab IntLookup...\n");
    profile_ihtab (keys);
    printf ("  done (sink=%zu)\n", (size_t) do_not_optimize);
  }
  return 0;
}
