#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ihtab.h"
#include "ixhtab.h"

typedef struct {
  uint32_t key;
  uint32_t value;
} entry;

static inline iht_hash_t entry_hash (entry e) { return e.key * 0x9E3779B97F4A7C15ULL; }

static inline bool entry_eq (entry a, entry b) { return a.key == b.key; }

DEFINE_IHT (entry, entry_hash, entry_eq)
DEFINE_IXHT (entry, entry_hash, entry_eq)

#define TEST_HTAB(pfx, PFX, name)                                 \
                                                                  \
  static void pfx##_test_insert_find (void) {                     \
    printf ("  " #name " insert/find...");                        \
    struct pfx##_entry t;                                         \
    pfx##_create_entry (&t, 8);                                   \
    entry e = {42, 100}, *res;                                    \
    assert (!pfx##_perform_entry (&t, &e, PFX##_INSERT, &res));   \
    *res = e;                                                     \
    assert (pfx##_els_count_entry (&t) == 1);                     \
    entry q = {42, 0};                                            \
    assert (pfx##_perform_entry (&t, &q, PFX##_FIND, &res));      \
    assert (res->value == 100);                                   \
    entry q2 = {99, 0};                                           \
    assert (!pfx##_perform_entry (&t, &q2, PFX##_FIND, &res));    \
    pfx##_destroy_entry (&t);                                     \
    printf (" ok\n");                                             \
  }                                                               \
                                                                  \
  static void pfx##_test_replace (void) {                         \
    printf ("  " #name " replace...");                            \
    struct pfx##_entry t;                                         \
    pfx##_create_entry (&t, 8);                                   \
    entry e = {42, 100}, *res;                                    \
    assert (!pfx##_perform_entry (&t, &e, PFX##_INSERT, &res));  \
    *res = e;                                                     \
    assert (pfx##_els_count_entry (&t) == 1);                     \
    entry e2 = {42, 200};                                         \
    assert (pfx##_perform_entry (&t, &e2, PFX##_INSERT, &res));  \
    *res = e2;                                                    \
    entry q = {42, 0};                                            \
    assert (pfx##_perform_entry (&t, &q, PFX##_FIND, &res));      \
    assert (res->value == 200);                                   \
    pfx##_destroy_entry (&t);                                     \
    printf (" ok\n");                                             \
  }                                                               \
                                                                  \
  static void pfx##_test_delete (void) {                          \
    printf ("  " #name " delete...");                             \
    struct pfx##_entry t;                                         \
    pfx##_create_entry (&t, 8);                                   \
    entry e = {42, 100}, *res;                                    \
    assert (!pfx##_perform_entry (&t, &e, PFX##_INSERT, &res));   \
    *res = e;                                                     \
    assert (pfx##_els_count_entry (&t) == 1);                     \
    entry q = {42, 0};                                            \
    assert (pfx##_perform_entry (&t, &q, PFX##_DELETE, &res));    \
    assert (pfx##_els_count_entry (&t) == 0);                     \
    assert (!pfx##_perform_entry (&t, &q, PFX##_FIND, &res));     \
    pfx##_destroy_entry (&t);                                     \
    printf (" ok\n");                                             \
  }                                                               \
                                                                  \
  static void pfx##_test_duplicate (void) {                       \
    printf ("  " #name " duplicate insert...");                   \
    struct pfx##_entry t;                                         \
    pfx##_create_entry (&t, 8);                                   \
    entry e = {42, 100}, *res;                                    \
    assert (!pfx##_perform_entry (&t, &e, PFX##_INSERT, &res));   \
    *res = e;                                                     \
    entry e2 = {42, 200};                                         \
    assert (pfx##_perform_entry (&t, &e2, PFX##_INSERT, &res));   \
    assert (res->value == 100);                                   \
    pfx##_destroy_entry (&t);                                     \
    printf (" ok\n");                                             \
  }                                                               \
                                                                  \
  static void pfx##_test_iterate (void) {                         \
    printf ("  " #name " iterate...");                            \
    const int N = 100;                                            \
    struct pfx##_entry t;                                         \
    pfx##_create_entry (&t, 8);                                   \
    for (int i = 0; i < N; i++) {                                 \
      entry e = {(uint32_t) i, (uint32_t) (i * 10)}, *res;        \
      assert (!pfx##_perform_entry (&t, &e, PFX##_INSERT, &res)); \
      *res = e;                                                   \
    }                                                             \
    assert ((int) pfx##_els_count_entry (&t) == N);               \
    bool seen[100];                                               \
    memset (seen, 0, sizeof (seen));                              \
    int count = 0;                                                \
    struct pfx##_iter_entry it = pfx##_iter_begin_entry (&t);     \
    while (pfx##_iter_valid_entry (&it)) {                        \
      assert (it.ptr->key < (uint32_t) N);                        \
      assert (!seen[it.ptr->key]);                                \
      seen[it.ptr->key] = true;                                   \
      count++;                                                    \
      pfx##_iter_next_entry (&t, &it);                            \
    }                                                             \
    assert (count == N);                                          \
    pfx##_destroy_entry (&t);                                     \
    printf (" ok\n");                                             \
  }                                                               \
                                                                  \
  static void pfx##_test_large (void) {                           \
    printf ("  " #name " large table...");                        \
    const int N = 50000;                                          \
    struct pfx##_entry t;                                         \
    pfx##_create_entry (&t, 8);                                   \
    entry *res;                                                   \
    for (int i = 0; i < N; i++) {                                 \
      entry e = {(uint32_t) i, (uint32_t) (i * 7)};               \
      assert (!pfx##_perform_entry (&t, &e, PFX##_INSERT, &res)); \
      *res = e;                                                   \
    }                                                             \
    assert ((int) pfx##_els_count_entry (&t) == N);               \
    for (int i = 0; i < N; i++) {                                 \
      entry q = {(uint32_t) i, 0};                                \
      assert (pfx##_perform_entry (&t, &q, PFX##_FIND, &res));    \
      assert (res->value == (uint32_t) (i * 7));                  \
    }                                                             \
    for (int i = 0; i < N / 2; i++) {                             \
      entry q = {(uint32_t) i, 0};                                \
      assert (pfx##_perform_entry (&t, &q, PFX##_DELETE, &res));  \
    }                                                             \
    assert ((int) pfx##_els_count_entry (&t) == N - N / 2);       \
    for (int i = 0; i < N / 2; i++) {                             \
      entry q = {(uint32_t) i, 0};                                \
      assert (!pfx##_perform_entry (&t, &q, PFX##_FIND, &res));   \
    }                                                             \
    for (int i = N / 2; i < N; i++) {                             \
      entry q = {(uint32_t) i, 0};                                \
      assert (pfx##_perform_entry (&t, &q, PFX##_FIND, &res));    \
      assert (res->value == (uint32_t) (i * 7));                  \
    }                                                             \
    pfx##_destroy_entry (&t);                                     \
    printf (" ok\n");                                             \
  }                                                               \
                                                                  \
  static void pfx##_test_all (void) {                             \
    printf (#name ":\n");                                         \
    pfx##_test_insert_find ();                                    \
    pfx##_test_replace ();                                        \
    pfx##_test_delete ();                                         \
    pfx##_test_duplicate ();                                      \
    pfx##_test_iterate ();                                        \
    pfx##_test_large ();                                          \
  }

TEST_HTAB (iht, IHT, ihtab)
TEST_HTAB (ixht, IXHT, ixhtab)

int main () {
  iht_test_all ();
  ixht_test_all ();
  printf ("All tests passed.\n");
  return 0;
}
