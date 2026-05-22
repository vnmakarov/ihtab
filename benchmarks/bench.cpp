// bench.cpp — standalone benchmark comparing absl, ihtab, ixhtab
// Uses vmum.h for hashing. Sizes and benchmarks match Go map benchmarks.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// Shim: absl::flat_hash_map
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

#include "ihtab.hpp"
#include "ixhtab.hpp"
#include "bench_c.h"




static volatile size_t do_not_optimize;
static bool include_string = false;

static const int WARMUP = 2;
static const int REPEATS = 5;

// ===== Key generators =====

static std::vector<uint64_t> gen_int_keys(int n) {
  std::vector<uint64_t> keys(n);
  std::mt19937_64 rng(42);
  for (int i = 0; i < n; i++) keys[i] = rng() % (n * 10ULL);
  return keys;
}

static std::vector<char> str_backing;
static constexpr int STR_LEN = 32;

static std::vector<char *> gen_str_keys(int n) {
  str_backing.resize((size_t)n * STR_LEN);
  std::vector<char *> keys(n);
  for (int i = 0; i < n; i++) {
    keys[i] = str_backing.data() + (size_t)i * STR_LEN;
    snprintf(keys[i], STR_LEN, "key_%d_%.*s", i, i % 10, "xxxxxxxxxx");
  }
  return keys;
}

static std::vector<char> rand_str_backing;

static std::vector<char *> gen_random_str_keys(int n) {
  rand_str_backing.resize((size_t)n * STR_LEN);
  std::vector<char *> keys(n);
  std::mt19937 rng(42);
  for (int i = 0; i < n; i++) {
    keys[i] = rand_str_backing.data() + (size_t)i * STR_LEN;
    snprintf(keys[i], STR_LEN, "key_%06d", (int)(rng() % (n * 10)));
  }
  return keys;
}

static std::vector<char> coll_str_backing;

static std::vector<char *> gen_collision_keys(int n) {
  int mod = n <= 10 ? 3 : (n <= 100 ? 10 : 100);
  coll_str_backing.resize((size_t)n * STR_LEN);
  std::vector<char *> keys(n);
  for (int i = 0; i < n; i++) {
    keys[i] = coll_str_backing.data() + (size_t)i * STR_LEN;
    snprintf(keys[i], STR_LEN, "collision_%d%d", i % mod, i);
  }
  return keys;
}

// Large key generator: 200-400 char strings (matches Go's generateVeryLongStringKeys).
static constexpr int LKEY_LEN = 400;
static std::vector<char> lkey_backing;

static std::vector<char *> gen_large_keys(int n) {
  lkey_backing.resize((size_t)n * LKEY_LEN);
  std::vector<char *> keys(n);
  std::mt19937 rng(42);
  const char *components[] = {
    "component", "element", "attribute", "property", "parameter",
    "configuration", "specification", "description", "identifier", "reference",
    "authentication", "authorization", "transaction", "processing", "management",
  };
  for (int i = 0; i < n; i++) {
    keys[i] = lkey_backing.data() + (size_t)i * LKEY_LEN;
    int pos = snprintf(keys[i], LKEY_LEN, "very_long_key_%06d_", i);
    int target = 200 + (int)(rng() % 200);
    while (pos < target && pos < LKEY_LEN - 20) {
      pos += snprintf(keys[i] + pos, LKEY_LEN - pos, "_%s_%d",
                      components[rng() % 15], (int)(rng() % 10000));
    }
  }
  return keys;
}

// Large value type (~96 bytes, matches Go's LargeStruct).
struct large_value_t {
  char data[64];
  int value;
  char name[24];
  bool active;
};

static large_value_t make_large_value(int i) {
  large_value_t v{};
  memset(v.data, (char)(i % 256), sizeof(v.data));
  v.value = i;
  snprintf(v.name, sizeof(v.name), "item_%d", i);
  v.active = (i % 2 == 0);
  return v;
}

// Hash wrappers using vmum.
struct int_hash {
  using is_avalanching = void;
  size_t operator()(uint64_t k) const { return vmum_hash64(k, 0); }
};

struct cstr_hash {
  using is_avalanching = void;
  size_t operator()(const char *s) const { return vmum_hash(s, strlen(s), 0); }
};
struct cstr_eq {
  bool operator()(const char *a, const char *b) const { return strcmp(a, b) == 0; }
};

// Timing helper: returns nanoseconds per operation.
template<typename F>
double bench_ns_op(F func, int ops) {
  for (int w = 0; w < WARMUP; w++) func();
  double best = 1e18;
  for (int r = 0; r < REPEATS; r++) {
    auto t0 = std::chrono::high_resolution_clock::now();
    func();
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    if (ns < best) best = ns;
  }
  return best / ops;
}

struct result {
  const char *impl;
  const char *benchmark;
  const char *size_name;
  int size;
  double ns_op;
};

static std::vector<result> results;

static void record(const char *impl, const char *bench, const char *sz, int n, double ns) {
  results.push_back({impl, bench, sz, n, ns});
  printf("  %-11s %-22s %-6s %10.1f ns/op\n", impl, bench, sz, ns);
}

// ===== ihtab / ixhtab entry types =====

struct ihtab_int_entry { uint64_t key; int value; };
struct ihtab_int_hash { size_t operator()(const ihtab_int_entry &e) const { return vmum_hash64(e.key, 0); } };
struct ihtab_int_eq { bool operator()(const ihtab_int_entry &a, const ihtab_int_entry &b) const { return a.key == b.key; } };
using ihtab_int_t = iht::ihtab<ihtab_int_entry, ihtab_int_hash, ihtab_int_eq>;

struct ihtab_str_entry { const char *key; int value; };
struct ihtab_str_hash { size_t operator()(const ihtab_str_entry &e) const { return vmum_hash(e.key, strlen(e.key), 0); } };
struct ihtab_str_eq { bool operator()(const ihtab_str_entry &a, const ihtab_str_entry &b) const { return strcmp(a.key, b.key) == 0; } };
using ihtab_str_t = iht::ihtab<ihtab_str_entry, ihtab_str_hash, ihtab_str_eq>;

struct ixhtab_int_entry { uint64_t key; int value; };
struct ixhtab_int_hash { size_t operator()(const ixhtab_int_entry &e) const { return vmum_hash64(e.key, 0); } };
struct ixhtab_int_eq { bool operator()(const ixhtab_int_entry &a, const ixhtab_int_entry &b) const { return a.key == b.key; } };
using ixhtab_int_t = ixht::ixhtab<ixhtab_int_entry, ixhtab_int_hash, ixhtab_int_eq>;

struct ixhtab_str_entry { const char *key; int value; };
struct ixhtab_str_hash { size_t operator()(const ixhtab_str_entry &e) const { return vmum_hash(e.key, strlen(e.key), 0); } };
struct ixhtab_str_eq { bool operator()(const ixhtab_str_entry &a, const ixhtab_str_entry &b) const { return strcmp(a.key, b.key) == 0; } };
using ixhtab_str_t = ixht::ixhtab<ixhtab_str_entry, ixhtab_str_hash, ixhtab_str_eq>;

// Large value entry types.
struct ihtab_lv_entry { uint64_t key; large_value_t value; };
struct ihtab_lv_hash { size_t operator()(const ihtab_lv_entry &e) const { return vmum_hash64(e.key, 0); } };
struct ihtab_lv_eq { bool operator()(const ihtab_lv_entry &a, const ihtab_lv_entry &b) const { return a.key == b.key; } };
using ihtab_lv_t = iht::ihtab<ihtab_lv_entry, ihtab_lv_hash, ihtab_lv_eq>;

struct ixhtab_lv_entry { uint64_t key; large_value_t value; };
struct ixhtab_lv_hash { size_t operator()(const ixhtab_lv_entry &e) const { return vmum_hash64(e.key, 0); } };
struct ixhtab_lv_eq { bool operator()(const ixhtab_lv_entry &a, const ixhtab_lv_entry &b) const { return a.key == b.key; } };
using ixhtab_lv_t = ixht::ixhtab<ixhtab_lv_entry, ixhtab_lv_hash, ixhtab_lv_eq>;

// ===== Core benchmarks (Int/Str Insert/Lookup/Delete/Iteration) =====

static void bench_core(int n, const char *sz) {
  auto ikeys = gen_int_keys(n);

  // --- absl int ---
  record("absl", "IntInsert", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<uint64_t, int, int_hash> m;
    for (int i = 0; i < n; i++) m[ikeys[i]] = i;
    do_not_optimize = m.size();
  }, n));

  absl::flat_hash_map<uint64_t, int, int_hash> am_i;
  for (int i = 0; i < n; i++) am_i[ikeys[i]] = i;

  record("absl", "IntLookup", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { auto it = am_i.find(ikeys[i]); if (it != am_i.end()) s += it->second; }
    do_not_optimize = s;
  }, n));

  record("absl", "IntDelete", sz, n, bench_ns_op([&]{
    auto mc = am_i;
    for (int i = 0; i < n; i++) mc.erase(ikeys[i]);
    do_not_optimize = mc.size();
  }, n));

  record("absl", "IntIteration", sz, n, bench_ns_op([&]{
    int s = 0; for (auto &[k, v] : am_i) s += v; do_not_optimize = s;
  }, n));

  // --- ixhtab int ---
  record("C++ ixhtab", "IntInsert", sz, n, bench_ns_op([&]{
    ixhtab_int_t m(8);
    for (int i = 0; i < n; i++) { ixhtab_int_entry e{ikeys[i], i}; ixhtab_int_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ixhtab_int_t xm_i(8);
  for (int i = 0; i < n; i++) { ixhtab_int_entry e{ikeys[i], i}; ixhtab_int_entry *r; if (!xm_i.perform(e, ixht::INSERT, &r)) *r = e; }

  record("C++ ixhtab", "IntLookup", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_int_entry e{ikeys[i], 0}; ixhtab_int_entry *r; if (xm_i.perform(e, ixht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C++ ixhtab", "IntDelete", sz, n, bench_ns_op([&]{
    ixhtab_int_t mc(8);
    for (int i = 0; i < n; i++) { ixhtab_int_entry e{ikeys[i], i}; ixhtab_int_entry *r; if (!mc.perform(e, ixht::INSERT, &r)) *r = e; }
    for (int i = 0; i < n; i++) { ixhtab_int_entry e{ikeys[i], 0}; ixhtab_int_entry *r; mc.perform(e, ixht::DELETE, &r); }
    do_not_optimize = mc.els_count();
  }, n));

  record("C++ ixhtab", "IntIteration", sz, n, bench_ns_op([&]{
    int s = 0; for (auto &e : xm_i) s += e.value;
    do_not_optimize = s;
  }, n));

  // --- ihtab int ---
  record("C++ ihtab", "IntInsert", sz, n, bench_ns_op([&]{
    ihtab_int_t m(8);
    for (int i = 0; i < n; i++) { ihtab_int_entry e{ikeys[i], i}; ihtab_int_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ihtab_int_t im_i(8);
  for (int i = 0; i < n; i++) { ihtab_int_entry e{ikeys[i], i}; ihtab_int_entry *r; if (!im_i.perform(e, iht::INSERT, &r)) *r = e; }

  record("C++ ihtab", "IntLookup", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_int_entry e{ikeys[i], 0}; ihtab_int_entry *r; if (im_i.perform(e, iht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C++ ihtab", "IntDelete", sz, n, bench_ns_op([&]{
    ihtab_int_t mc(8);
    for (int i = 0; i < n; i++) { ihtab_int_entry e{ikeys[i], i}; ihtab_int_entry *r; if (!mc.perform(e, iht::INSERT, &r)) *r = e; }
    for (int i = 0; i < n; i++) { ihtab_int_entry e{ikeys[i], 0}; ihtab_int_entry *r; mc.perform(e, iht::DELETE, &r); }
    do_not_optimize = mc.els_count();
  }, n));

  record("C++ ihtab", "IntIteration", sz, n, bench_ns_op([&]{
    int s = 0; for (auto &e : im_i) s += e.value;
    do_not_optimize = s;
  }, n));

  // --- C ixhtab int ---
  record("C ixhtab", "IntInsert", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_int_insert(ikeys.data(), n);
  }, n));

  void *cxi_i = c_ixhtab_int_build(ikeys.data(), n);

  record("C ixhtab", "IntLookup", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_int_lookup(cxi_i, ikeys.data(), n);
  }, n));

  record("C ixhtab", "IntDelete", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_int_delete(ikeys.data(), n);
  }, n));

  record("C ixhtab", "IntIteration", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_int_iterate(cxi_i);
  }, n));

  c_ixhtab_int_free(cxi_i);

  // --- C ihtab int ---
  record("C ihtab", "IntInsert", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_int_insert(ikeys.data(), n);
  }, n));

  void *ci_i = c_ihtab_int_build(ikeys.data(), n);

  record("C ihtab", "IntLookup", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_int_lookup(ci_i, ikeys.data(), n);
  }, n));

  record("C ihtab", "IntDelete", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_int_delete(ikeys.data(), n);
  }, n));

  record("C ihtab", "IntIteration", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_int_iterate(ci_i);
  }, n));

  c_ihtab_int_free(ci_i);

  if (!include_string) return;
  auto skeys = gen_str_keys(n);

  // --- absl str ---
  record("absl", "StrInsert", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> m;
    for (int i = 0; i < n; i++) m[skeys[i]] = i;
    do_not_optimize = m.size();
  }, n));

  absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> am_s;
  for (int i = 0; i < n; i++) am_s[skeys[i]] = i;

  record("absl", "StrLookup", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { auto it = am_s.find(skeys[i]); if (it != am_s.end()) s += it->second; }
    do_not_optimize = s;
  }, n));

  record("absl", "StrDelete", sz, n, bench_ns_op([&]{
    auto mc = am_s;
    for (int i = 0; i < n; i++) mc.erase(skeys[i]);
    do_not_optimize = mc.size();
  }, n));

  record("absl", "StrIteration", sz, n, bench_ns_op([&]{
    int s = 0; for (auto &[k, v] : am_s) s += v; do_not_optimize = s;
  }, n));

  // --- ixhtab str ---
  record("C++ ixhtab", "StrInsert", sz, n, bench_ns_op([&]{
    ixhtab_str_t m(8);
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{skeys[i], i}; ixhtab_str_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ixhtab_str_t xm_s(8);
  for (int i = 0; i < n; i++) { ixhtab_str_entry e{skeys[i], i}; ixhtab_str_entry *r; if (!xm_s.perform(e, ixht::INSERT, &r)) *r = e; }

  record("C++ ixhtab", "StrLookup", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{skeys[i], 0}; ixhtab_str_entry *r; if (xm_s.perform(e, ixht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C++ ixhtab", "StrDelete", sz, n, bench_ns_op([&]{
    ixhtab_str_t mc(8);
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{skeys[i], i}; ixhtab_str_entry *r; if (!mc.perform(e, ixht::INSERT, &r)) *r = e; }
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{skeys[i], 0}; ixhtab_str_entry *r; mc.perform(e, ixht::DELETE, &r); }
    do_not_optimize = mc.els_count();
  }, n));

  record("C++ ixhtab", "StrIteration", sz, n, bench_ns_op([&]{
    int s = 0; for (auto &e : xm_s) s += e.value;
    do_not_optimize = s;
  }, n));

  // --- ihtab str ---
  record("C++ ihtab", "StrInsert", sz, n, bench_ns_op([&]{
    ihtab_str_t m(8);
    for (int i = 0; i < n; i++) { ihtab_str_entry e{skeys[i], i}; ihtab_str_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ihtab_str_t im_s(8);
  for (int i = 0; i < n; i++) { ihtab_str_entry e{skeys[i], i}; ihtab_str_entry *r; if (!im_s.perform(e, iht::INSERT, &r)) *r = e; }

  record("C++ ihtab", "StrLookup", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_str_entry e{skeys[i], 0}; ihtab_str_entry *r; if (im_s.perform(e, iht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C++ ihtab", "StrDelete", sz, n, bench_ns_op([&]{
    ihtab_str_t mc(8);
    for (int i = 0; i < n; i++) { ihtab_str_entry e{skeys[i], i}; ihtab_str_entry *r; if (!mc.perform(e, iht::INSERT, &r)) *r = e; }
    for (int i = 0; i < n; i++) { ihtab_str_entry e{skeys[i], 0}; ihtab_str_entry *r; mc.perform(e, iht::DELETE, &r); }
    do_not_optimize = mc.els_count();
  }, n));

  record("C++ ihtab", "StrIteration", sz, n, bench_ns_op([&]{
    int s = 0; for (auto &e : im_s) s += e.value;
    do_not_optimize = s;
  }, n));

  // --- C ixhtab str ---
  record("C ixhtab", "StrInsert", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_insert(skeys.data(), n);
  }, n));
  void *cxs = c_ixhtab_str_build(skeys.data(), n);
  record("C ixhtab", "StrLookup", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_lookup(cxs, skeys.data(), n);
  }, n));
  record("C ixhtab", "StrDelete", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_delete(skeys.data(), n);
  }, n));
  record("C ixhtab", "StrIteration", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_iterate(cxs);
  }, n));
  c_ixhtab_str_free(cxs);

  // --- C ihtab str ---
  record("C ihtab", "StrInsert", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_insert(skeys.data(), n);
  }, n));
  void *cis = c_ihtab_str_build(skeys.data(), n);
  record("C ihtab", "StrLookup", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_lookup(cis, skeys.data(), n);
  }, n));
  record("C ihtab", "StrDelete", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_delete(skeys.data(), n);
  }, n));
  record("C ihtab", "StrIteration", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_iterate(cis);
  }, n));
  c_ihtab_str_free(cis);
}

// ===== MixedOps: 70% insert, 20% lookup, 10% delete on str keys =====

static void bench_mixed(int n, const char *sz) {
  if (!include_string) return;
  auto keys = gen_str_keys(n);

  record("absl", "MixedOps", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> m;
    for (int j = 0; j < n; j++) {
      int op = j % 10;
      if (op < 7) m[keys[j]] = j;
      else if (op < 9) { auto it = m.find(keys[j]); if (it != m.end()) do_not_optimize = it->second; }
      else m.erase(keys[j]);
    }
    do_not_optimize = m.size();
  }, n));

  record("C++ ixhtab", "MixedOps", sz, n, bench_ns_op([&]{
    ixhtab_str_t m(8);
    for (int j = 0; j < n; j++) {
      int op = j % 10;
      ixhtab_str_entry e{keys[j], j}; ixhtab_str_entry *r;
      if (op < 7) { if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
      else if (op < 9) { if (m.perform(e, ixht::FIND, &r)) do_not_optimize = r->value; }
      else m.perform(e, ixht::DELETE, &r);
    }
    do_not_optimize = m.els_count();
  }, n));

  record("C++ ihtab", "MixedOps", sz, n, bench_ns_op([&]{
    ihtab_str_t m(8);
    for (int j = 0; j < n; j++) {
      int op = j % 10;
      ihtab_str_entry e{keys[j], j}; ihtab_str_entry *r;
      if (op < 7) { if (!m.perform(e, iht::INSERT, &r)) *r = e; }
      else if (op < 9) { if (m.perform(e, iht::FIND, &r)) do_not_optimize = r->value; }
      else m.perform(e, iht::DELETE, &r);
    }
    do_not_optimize = m.els_count();
  }, n));

  record("C ixhtab", "MixedOps", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_mixed(keys.data(), n);
  }, n));
  record("C ihtab", "MixedOps", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_mixed(keys.data(), n);
  }, n));
}

// ===== RandomAccess: lookup with random-pattern keys =====

static void bench_random_access(int n, const char *sz) {
  if (!include_string) return;
  auto keys = gen_random_str_keys(n);

  absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> am;
  for (int i = 0; i < n; i++) am[keys[i]] = i;
  record("absl", "RandomAccess", sz, n, bench_ns_op([&]{
    int s = 0; for (int i = 0; i < n; i++) { auto it = am.find(keys[i]); if (it != am.end()) s += it->second; }
    do_not_optimize = s;
  }, n));

  ixhtab_str_t xm(8);
  for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], i}; ixhtab_str_entry *r; if (!xm.perform(e, ixht::INSERT, &r)) *r = e; }
  record("C++ ixhtab", "RandomAccess", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], 0}; ixhtab_str_entry *r; if (xm.perform(e, ixht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  ihtab_str_t im(8);
  for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], i}; ihtab_str_entry *r; if (!im.perform(e, iht::INSERT, &r)) *r = e; }
  record("C++ ihtab", "RandomAccess", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], 0}; ihtab_str_entry *r; if (im.perform(e, iht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  void *cxra = c_ixhtab_str_build(keys.data(), n);
  record("C ixhtab", "RandomAccess", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_lookup(cxra, keys.data(), n);
  }, n));
  c_ixhtab_str_free(cxra);

  void *cira = c_ihtab_str_build(keys.data(), n);
  record("C ihtab", "RandomAccess", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_lookup(cira, keys.data(), n);
  }, n));
  c_ihtab_str_free(cira);
}

// ===== CacheMiss: lookup keys guaranteed absent (int keys, bit flip) =====

static void bench_cache_miss(int n, const char *sz) {
  auto keys = gen_int_keys(n);
  for (int i = 0; i < n; i++) keys[i] |= 1;
  std::vector<uint64_t> missing(n);
  for (int i = 0; i < n; i++) missing[i] = keys[i] ^ 1;

  absl::flat_hash_map<uint64_t, int, int_hash> am;
  for (int i = 0; i < n; i++) am[keys[i]] = i;
  record("absl", "CacheMiss", sz, n, bench_ns_op([&]{
    int s = 0; for (int i = 0; i < n; i++) { auto it = am.find(missing[i]); if (it != am.end()) s += it->second; }
    do_not_optimize = s;
  }, n));

  ixhtab_int_t xm(8);
  for (int i = 0; i < n; i++) { ixhtab_int_entry e{keys[i], i}; ixhtab_int_entry *r; if (!xm.perform(e, ixht::INSERT, &r)) *r = e; }
  record("C++ ixhtab", "CacheMiss", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_int_entry e{missing[i], 0}; ixhtab_int_entry *r; if (xm.perform(e, ixht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  ihtab_int_t im(8);
  for (int i = 0; i < n; i++) { ihtab_int_entry e{keys[i], i}; ihtab_int_entry *r; if (!im.perform(e, iht::INSERT, &r)) *r = e; }
  record("C++ ihtab", "CacheMiss", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_int_entry e{missing[i], 0}; ihtab_int_entry *r; if (im.perform(e, iht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  void *cxcm = c_ixhtab_int_build(keys.data(), n);
  record("C ixhtab", "CacheMiss", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_int_lookup(cxcm, missing.data(), n);
  }, n));
  c_ixhtab_int_free(cxcm);

  void *cicm = c_ihtab_int_build(keys.data(), n);
  record("C ihtab", "CacheMiss", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_int_lookup(cicm, missing.data(), n);
  }, n));
  c_ihtab_int_free(cicm);
}

// ===== HashCollisions: collision-prone str keys (insert + lookup) =====

static void bench_collisions(int n, const char *sz) {
  if (!include_string) return;
  auto keys = gen_collision_keys(n);

  record("absl", "HashCollision", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> m;
    for (int i = 0; i < n; i++) m[keys[i]] = i;
    int s = 0; for (int i = 0; i < n; i++) { auto it = m.find(keys[i]); if (it != m.end()) s += it->second; }
    do_not_optimize = s;
  }, n));

  record("C++ ixhtab", "HashCollision", sz, n, bench_ns_op([&]{
    ixhtab_str_t m(8);
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], i}; ixhtab_str_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], 0}; ixhtab_str_entry *r; if (m.perform(e, ixht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C++ ihtab", "HashCollision", sz, n, bench_ns_op([&]{
    ihtab_str_t m(8);
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], i}; ihtab_str_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], 0}; ihtab_str_entry *r; if (m.perform(e, iht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C ixhtab", "HashCollision", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_collision(keys.data(), n);
  }, n));
  record("C ihtab", "HashCollision", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_collision(keys.data(), n);
  }, n));
}

// ===== LargeKey: insert + lookup with 200-400 char keys =====

static void bench_large_key(int n, const char *sz) {
  if (!include_string) return;
  auto keys = gen_large_keys(n);

  record("absl", "LargeKeyIns", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> m;
    for (int i = 0; i < n; i++) m[keys[i]] = i;
    do_not_optimize = m.size();
  }, n));

  absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> am;
  for (int i = 0; i < n; i++) am[keys[i]] = i;
  record("absl", "LargeKeyGet", sz, n, bench_ns_op([&]{
    int s = 0; for (int i = 0; i < n; i++) { auto it = am.find(keys[i]); if (it != am.end()) s += it->second; }
    do_not_optimize = s;
  }, n));

  record("C++ ixhtab", "LargeKeyIns", sz, n, bench_ns_op([&]{
    ixhtab_str_t m(8);
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], i}; ixhtab_str_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ixhtab_str_t xm(8);
  for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], i}; ixhtab_str_entry *r; if (!xm.perform(e, ixht::INSERT, &r)) *r = e; }
  record("C++ ixhtab", "LargeKeyGet", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], 0}; ixhtab_str_entry *r; if (xm.perform(e, ixht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C++ ihtab", "LargeKeyIns", sz, n, bench_ns_op([&]{
    ihtab_str_t m(8);
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], i}; ihtab_str_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ihtab_str_t im(8);
  for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], i}; ihtab_str_entry *r; if (!im.perform(e, iht::INSERT, &r)) *r = e; }
  record("C++ ihtab", "LargeKeyGet", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], 0}; ihtab_str_entry *r; if (im.perform(e, iht::FIND, &r)) s += r->value; }
    do_not_optimize = s;
  }, n));

  record("C ixhtab", "LargeKeyIns", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_insert(keys.data(), n);
  }, n));
  void *cxlk = c_ixhtab_str_build(keys.data(), n);
  record("C ixhtab", "LargeKeyGet", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_lookup(cxlk, keys.data(), n);
  }, n));
  c_ixhtab_str_free(cxlk);

  record("C ihtab", "LargeKeyIns", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_insert(keys.data(), n);
  }, n));
  void *cilk = c_ihtab_str_build(keys.data(), n);
  record("C ihtab", "LargeKeyGet", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_lookup(cilk, keys.data(), n);
  }, n));
  c_ihtab_str_free(cilk);
}

// ===== LargeValue: insert + lookup with ~96-byte value struct =====

static void bench_large_value(int n, const char *sz) {
  auto keys = gen_int_keys(n);

  record("absl", "LargeValIns", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<uint64_t, large_value_t, int_hash> m;
    for (int i = 0; i < n; i++) m[keys[i]] = make_large_value(i);
    do_not_optimize = m.size();
  }, n));

  absl::flat_hash_map<uint64_t, large_value_t, int_hash> am;
  for (int i = 0; i < n; i++) am[keys[i]] = make_large_value(i);
  record("absl", "LargeValGet", sz, n, bench_ns_op([&]{
    int s = 0; for (int i = 0; i < n; i++) { auto it = am.find(keys[i]); if (it != am.end()) s += it->second.value; }
    do_not_optimize = s;
  }, n));

  record("C++ ixhtab", "LargeValIns", sz, n, bench_ns_op([&]{
    ixhtab_lv_t m(8);
    for (int i = 0; i < n; i++) { ixhtab_lv_entry e{keys[i], make_large_value(i)}; ixhtab_lv_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ixhtab_lv_t xm(8);
  for (int i = 0; i < n; i++) { ixhtab_lv_entry e{keys[i], make_large_value(i)}; ixhtab_lv_entry *r; if (!xm.perform(e, ixht::INSERT, &r)) *r = e; }
  record("C++ ixhtab", "LargeValGet", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ixhtab_lv_entry e{keys[i], {}}; ixhtab_lv_entry *r; if (xm.perform(e, ixht::FIND, &r)) s += r->value.value; }
    do_not_optimize = s;
  }, n));

  record("C++ ihtab", "LargeValIns", sz, n, bench_ns_op([&]{
    ihtab_lv_t m(8);
    for (int i = 0; i < n; i++) { ihtab_lv_entry e{keys[i], make_large_value(i)}; ihtab_lv_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  ihtab_lv_t im(8);
  for (int i = 0; i < n; i++) { ihtab_lv_entry e{keys[i], make_large_value(i)}; ihtab_lv_entry *r; if (!im.perform(e, iht::INSERT, &r)) *r = e; }
  record("C++ ihtab", "LargeValGet", sz, n, bench_ns_op([&]{
    int s = 0;
    for (int i = 0; i < n; i++) { ihtab_lv_entry e{keys[i], {}}; ihtab_lv_entry *r; if (im.perform(e, iht::FIND, &r)) s += r->value.value; }
    do_not_optimize = s;
  }, n));

  record("C ixhtab", "LargeValIns", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_lv_insert(keys.data(), n);
  }, n));
  void *cxlv = c_ixhtab_lv_build(keys.data(), n);
  record("C ixhtab", "LargeValGet", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_lv_lookup(cxlv, keys.data(), n);
  }, n));
  c_ixhtab_lv_free(cxlv);

  record("C ihtab", "LargeValIns", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_lv_insert(keys.data(), n);
  }, n));
  void *cilv = c_ihtab_lv_build(keys.data(), n);
  record("C ihtab", "LargeValGet", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_lv_lookup(cilv, keys.data(), n);
  }, n));
  c_ihtab_lv_free(cilv);
}

// ===== Growth: insert without pre-allocation =====

static void bench_growth(int n, const char *sz) {
  if (!include_string) return;
  auto keys = gen_str_keys(n);

  record("absl", "GrowthNoPre", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> m;
    for (int i = 0; i < n; i++) m[keys[i]] = i;
    do_not_optimize = m.size();
  }, n));

  record("C++ ixhtab", "GrowthNoPre", sz, n, bench_ns_op([&]{
    ixhtab_str_t m(8);
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], i}; ixhtab_str_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  record("C++ ihtab", "GrowthNoPre", sz, n, bench_ns_op([&]{
    ihtab_str_t m(8);
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], i}; ihtab_str_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  record("absl", "GrowthPrealloc", sz, n, bench_ns_op([&]{
    absl::flat_hash_map<const char *, int, cstr_hash, cstr_eq> m;
    m.reserve(n);
    for (int i = 0; i < n; i++) m[keys[i]] = i;
    do_not_optimize = m.size();
  }, n));

  record("C++ ixhtab", "GrowthPrealloc", sz, n, bench_ns_op([&]{
    ixhtab_str_t m(n);
    for (int i = 0; i < n; i++) { ixhtab_str_entry e{keys[i], i}; ixhtab_str_entry *r; if (!m.perform(e, ixht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  record("C++ ihtab", "GrowthPrealloc", sz, n, bench_ns_op([&]{
    ihtab_str_t m(n);
    for (int i = 0; i < n; i++) { ihtab_str_entry e{keys[i], i}; ihtab_str_entry *r; if (!m.perform(e, iht::INSERT, &r)) *r = e; }
    do_not_optimize = m.els_count();
  }, n));

  record("C ixhtab", "GrowthNoPre", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_growth(keys.data(), n, 0);
  }, n));
  record("C ihtab", "GrowthNoPre", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_growth(keys.data(), n, 0);
  }, n));
  record("C ixhtab", "GrowthPrealloc", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ixhtab_str_growth(keys.data(), n, 1);
  }, n));
  record("C ihtab", "GrowthPrealloc", sz, n, bench_ns_op([&]{
    do_not_optimize = c_ihtab_str_growth(keys.data(), n, 1);
  }, n));
}

static void print_geomean() {
  const char *impls[] = {"absl", "umap", "C++ ixhtab", "C++ ihtab", "C ixhtab", "C ihtab"};
  printf("Geometric mean (ns/op):\n");
  double absl_gm = 0;
  for (auto impl : impls) {
    double sum_log = 0; int cnt = 0;
    for (auto &r : results)
      if (strcmp(r.impl, impl) == 0) { sum_log += log(r.ns_op); cnt++; }
    if (cnt == 0) continue;
    double gm = exp(sum_log / cnt);
    if (strcmp(impl, "absl") == 0) absl_gm = gm;
    if (absl_gm > 0 && strcmp(impl, "absl") != 0)
      printf("  %-11s %6.1f ns/op  (%.3fx vs absl)\n", impl, gm, gm / absl_gm);
    else
      printf("  %-11s %6.1f ns/op\n", impl, gm);
  }
}

// Write CSV.
static void write_csv(const char *path) {
  FILE *f = fopen(path, "w");
  fprintf(f, "Benchmark,Size,Implementation,ns/op\n");
  for (auto &r : results)
    fprintf(f, "%s,%s,%s,%.1f\n", r.benchmark, r.size_name, r.impl, r.ns_op);
  fclose(f);
}

int main(int argc, char *argv[]) {
  bool include_tiny = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--include-tiny") == 0) include_tiny = true;
    else if (strcmp(argv[i], "--include-string") == 0) include_string = true;
    else {
      fprintf(stderr, "Usage: %s [--include-tiny] [--include-string]\n", argv[0]);
      return 1;
    }
  }

  printf("Hash Table Comparison: absl vs umap vs C++/C ihtab/ixhtab\n");
  printf("==========================================================\n");
  printf("Hash: vmum  Sizes:%s Small=100 Medium=10000 Large=1000000\n\n",
         include_tiny ? " Tiny=10" : "");

  struct { int size; const char *name; bool need_tiny; } sizes[] = {
    {10, "Tiny", true}, {100, "Small", false}, {10000, "Medium", false}, {1000000, "Large", false}
  };

  for (auto &s : sizes) {
    if (s.need_tiny && !include_tiny) continue;
    printf("--- %s (n=%d) ---\n", s.name, s.size);
    bench_core(s.size, s.name);
    bench_mixed(s.size, s.name);
    bench_random_access(s.size, s.name);
    bench_cache_miss(s.size, s.name);
    bench_collisions(s.size, s.name);
    bench_large_key(s.size, s.name);
    bench_large_value(s.size, s.name);
    bench_growth(s.size, s.name);
    printf("\n");
  }

  char ts[64];
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H_%M_%S", &tm);

  char csv_path[256];
  snprintf(csv_path, sizeof(csv_path), "result_%s.csv", ts);

  printf("\n");
  print_geomean();
  printf("\n");
  write_csv(csv_path);
  printf("CSV: %s\n", csv_path);
  printf("do_not_optimize = %zu\n", (size_t)do_not_optimize);
  return 0;
}
