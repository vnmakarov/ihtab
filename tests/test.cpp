#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

#include "ihtab.hpp"
#include "ixhtab.hpp"

struct Entry { uint32_t key; uint32_t value; };

struct MyHash {
  size_t operator()(const Entry &e) const {
    return e.key * 0x9E3779B97F4A7C15ULL;
  }
};

struct MyEq {
  bool operator()(const Entry &a, const Entry &b) const {
    return a.key == b.key;
  }
};

using IhtabTable = iht::ihtab<Entry, MyHash, MyEq>;
using IxhtabTable = ixht::ixhtab<Entry, MyHash, MyEq>;

#define TEST_HTAB(name, Table, NS)                                         \
                                                                           \
static void test_##name##_insert_find() {                                  \
  printf("  " #name " insert/find...");                                    \
  Table t;                                                                 \
  Entry e{42, 100}, *res;                                                  \
  assert(!t.perform(e, NS::INSERT, &res));                                 \
  *res = e;                                                                \
  assert(t.els_count() == 1);                                              \
  Entry q{42, 0};                                                          \
  assert(t.perform(q, NS::FIND, &res));                                    \
  assert(res->value == 100);                                               \
  Entry q2{99, 0};                                                         \
  assert(!t.perform(q2, NS::FIND, &res));                                  \
  printf(" ok\n");                                                         \
}                                                                          \
                                                                           \
static void test_##name##_replace() {                                      \
  printf("  " #name " replace...");                                        \
  Table t;                                                                 \
  Entry e{42, 100}, *res;                                                  \
  assert(!t.perform(e, NS::REPLACE, &res));                                \
  *res = e;                                                                \
  assert(t.els_count() == 1);                                              \
  Entry e2{42, 200};                                                       \
  assert(t.perform(e2, NS::REPLACE, &res));                                \
  *res = e2;                                                               \
  Entry q{42, 0};                                                          \
  assert(t.perform(q, NS::FIND, &res));                                    \
  assert(res->value == 200);                                               \
  printf(" ok\n");                                                         \
}                                                                          \
                                                                           \
static void test_##name##_delete() {                                       \
  printf("  " #name " delete...");                                         \
  Table t;                                                                 \
  Entry e{42, 100}, *res;                                                  \
  assert(!t.perform(e, NS::INSERT, &res));                                 \
  *res = e;                                                                \
  assert(t.els_count() == 1);                                              \
  Entry q{42, 0};                                                          \
  assert(t.perform(q, NS::DELETE, &res));                                  \
  assert(t.els_count() == 0);                                              \
  assert(!t.perform(q, NS::FIND, &res));                                   \
  printf(" ok\n");                                                         \
}                                                                          \
                                                                           \
static void test_##name##_duplicate() {                                    \
  printf("  " #name " duplicate insert...");                               \
  Table t;                                                                 \
  Entry e{42, 100}, *res;                                                  \
  assert(!t.perform(e, NS::INSERT, &res));                                 \
  *res = e;                                                                \
  Entry e2{42, 200};                                                       \
  assert(t.perform(e2, NS::INSERT, &res));                                 \
  assert(res->value == 100);                                               \
  printf(" ok\n");                                                         \
}                                                                          \
                                                                           \
static void test_##name##_iterate() {                                      \
  printf("  " #name " iterate...");                                        \
  const int N = 100;                                                       \
  Table t;                                                                 \
  for (int i = 0; i < N; i++) {                                            \
    Entry e{(uint32_t)i, (uint32_t)(i * 10)}, *res;                        \
    assert(!t.perform(e, NS::INSERT, &res));                               \
    *res = e;                                                              \
  }                                                                        \
  assert((int)t.els_count() == N);                                         \
  std::vector<bool> seen(N, false);                                        \
  int count = 0;                                                           \
  for (auto &el : t) {                                                     \
    assert(el.key < (uint32_t)N);                                          \
    assert(!seen[el.key]);                                                 \
    seen[el.key] = true;                                                   \
    count++;                                                               \
  }                                                                        \
  assert(count == N);                                                      \
  std::fill(seen.begin(), seen.end(), false);                              \
  count = 0;                                                               \
  auto it = t.iter_begin();                                                \
  while (Table::iter_valid(it)) {                                          \
    assert(it.ptr->key < (uint32_t)N);                                     \
    assert(!seen[it.ptr->key]);                                            \
    seen[it.ptr->key] = true;                                              \
    count++;                                                               \
    t.iter_next(it);                                                       \
  }                                                                        \
  assert(count == N);                                                      \
  printf(" ok\n");                                                         \
}                                                                          \
                                                                           \
static void test_##name##_large() {                                        \
  printf("  " #name " large table...");                                    \
  const int N = 50000;                                                     \
  Table t;                                                                 \
  Entry *res;                                                              \
  for (int i = 0; i < N; i++) {                                            \
    Entry e{(uint32_t)i, (uint32_t)(i * 7)};                               \
    assert(!t.perform(e, NS::INSERT, &res));                               \
    *res = e;                                                              \
  }                                                                        \
  assert((int)t.els_count() == N);                                         \
  for (int i = 0; i < N; i++) {                                            \
    Entry q{(uint32_t)i, 0};                                               \
    assert(t.perform(q, NS::FIND, &res));                                  \
    assert(res->value == (uint32_t)(i * 7));                               \
  }                                                                        \
  for (int i = 0; i < N / 2; i++) {                                        \
    Entry q{(uint32_t)i, 0};                                               \
    assert(t.perform(q, NS::DELETE, &res));                                \
  }                                                                        \
  assert((int)t.els_count() == N - N / 2);                                 \
  for (int i = 0; i < N / 2; i++) {                                        \
    Entry q{(uint32_t)i, 0};                                               \
    assert(!t.perform(q, NS::FIND, &res));                                 \
  }                                                                        \
  for (int i = N / 2; i < N; i++) {                                        \
    Entry q{(uint32_t)i, 0};                                               \
    assert(t.perform(q, NS::FIND, &res));                                  \
    assert(res->value == (uint32_t)(i * 7));                               \
  }                                                                        \
  printf(" ok\n");                                                         \
}                                                                          \
                                                                           \
static void test_##name##_all() {                                          \
  printf(#name ":\n");                                                     \
  test_##name##_insert_find();                                             \
  test_##name##_replace();                                                 \
  test_##name##_delete();                                                  \
  test_##name##_duplicate();                                               \
  test_##name##_iterate();                                                 \
  test_##name##_large();                                                   \
}

TEST_HTAB(ihtab, IhtabTable, iht)
TEST_HTAB(ixhtab, IxhtabTable, ixht)

int main() {
  test_ihtab_all();
  test_ixhtab_all();
  printf("All tests passed.\n");
  return 0;
}
