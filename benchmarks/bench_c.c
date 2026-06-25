#include <stdio.h>
#include <string.h>
#ifdef USE_V0
#include "ihtab-v0.h"
#include "ixhtab-v0.h"
#else
#include "ihtab.h"
#include "ixhtab.h"
#endif
#include "vmum.h"
#include "bench_c.h"

/* ===== Entry types ===== */

typedef struct {
  uint64_t key;
  int value;
} ci_int;
static inline size_t ci_int_h (ci_int e) { return vmum_hash64 (e.key, 0); }
static inline bool ci_int_e (ci_int a, ci_int b) { return a.key == b.key; }

typedef struct {
  const char *key;
  int value;
} ci_str;
static inline size_t ci_str_h (ci_str e) { return vmum_hash (e.key, strlen (e.key), 0); }
static inline bool ci_str_e (ci_str a, ci_str b) { return strcmp (a.key, b.key) == 0; }

typedef struct {
  uint64_t key;
  struct {
    char data[64];
    int value;
    char name[24];
    bool active;
  } value;
} ci_lv;
static inline size_t ci_lv_h (ci_lv e) { return vmum_hash64 (e.key, 0); }
static inline bool ci_lv_e (ci_lv a, ci_lv b) { return a.key == b.key; }

static inline ci_lv make_ci_lv (uint64_t key, int i) {
  ci_lv v;
  v.key = key;
  memset (v.value.data, (char) (i % 256), 64);
  v.value.value = i;
  snprintf (v.value.name, 24, "item_%d", i);
  v.value.active = (i % 2 == 0);
  return v;
}

/* ===== Instantiate tables ===== */

DEFINE_IHT (ci_int, ci_int_h, ci_int_e)
DEFINE_IXHT (ci_int, ci_int_h, ci_int_e)
DEFINE_IHT (ci_str, ci_str_h, ci_str_e)
DEFINE_IXHT (ci_str, ci_str_h, ci_str_e)
DEFINE_IHT (ci_lv, ci_lv_h, ci_lv_e)
DEFINE_IXHT (ci_lv, ci_lv_h, ci_lv_e)

/* ===== Wrapper generator macros ===== */

/* INT wrappers — generates 6 functions per table */
#define GEN_INT(cpfx, tpfx, E, A)                                  \
                                                                   \
  size_t cpfx##_int_insert (const uint64_t *keys, int n) {         \
    struct tpfx##_##E t;                                           \
    tpfx##_create_##E (&t, 8);                                     \
    for (int i = 0; i < n; i++) {                                  \
      E e = {keys[i], i};                                          \
      E *r;                                                        \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;    \
    }                                                              \
    size_t c = tpfx##_els_count_##E (&t);                          \
    tpfx##_destroy_##E (&t);                                       \
    return c;                                                      \
  }                                                                \
                                                                   \
  void *cpfx##_int_build (const uint64_t *keys, int n) {           \
    struct tpfx##_##E *t = malloc (sizeof *t);                     \
    tpfx##_create_##E (t, 8);                                      \
    for (int i = 0; i < n; i++) {                                  \
      E e = {keys[i], i};                                          \
      E *r;                                                        \
      if (!tpfx##_perform_##E (t, &e, A##_INSERT, &r)) *r = e;     \
    }                                                              \
    return t;                                                      \
  }                                                                \
                                                                   \
  int cpfx##_int_lookup (void *h, const uint64_t *keys, int n) {   \
    struct tpfx##_##E *t = h;                                      \
    int s = 0;                                                     \
    for (int i = 0; i < n; i++) {                                  \
      E e = {keys[i], 0};                                          \
      E *r;                                                        \
      if (tpfx##_perform_##E (t, &e, A##_FIND, &r)) s += r->value; \
    }                                                              \
    return s;                                                      \
  }                                                                \
                                                                   \
  size_t cpfx##_int_delete (const uint64_t *keys, int n) {         \
    struct tpfx##_##E t;                                           \
    tpfx##_create_##E (&t, 8);                                     \
    for (int i = 0; i < n; i++) {                                  \
      E e = {keys[i], i};                                          \
      E *r;                                                        \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;    \
    }                                                              \
    for (int i = 0; i < n; i++) {                                  \
      E e = {keys[i], 0};                                          \
      E *r;                                                        \
      tpfx##_perform_##E (&t, &e, A##_DELETE, &r);                 \
    }                                                              \
    size_t c = tpfx##_els_count_##E (&t);                          \
    tpfx##_destroy_##E (&t);                                       \
    return c;                                                      \
  }                                                                \
                                                                   \
  int cpfx##_int_iterate (void *h) {                               \
    struct tpfx##_##E *t = h;                                      \
    int s = 0;                                                     \
    struct tpfx##_iter_##E it = tpfx##_iter_begin_##E (t);         \
    while (tpfx##_iter_valid_##E (&it)) {                          \
      s += it.ptr->value;                                          \
      tpfx##_iter_next_##E (t, &it);                               \
    }                                                              \
    return s;                                                      \
  }                                                                \
                                                                   \
  void cpfx##_int_free (void *h) {                                 \
    struct tpfx##_##E *t = h;                                      \
    tpfx##_destroy_##E (t);                                        \
    free (t);                                                      \
  }

/* STR wrappers — 9 functions per table */
#define GEN_STR(cpfx, tpfx, E, A)                                     \
                                                                      \
  size_t cpfx##_str_insert (char *const *keys, int n) {               \
    struct tpfx##_##E t;                                              \
    tpfx##_create_##E (&t, 8);                                        \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], i};                                             \
      E *r;                                                           \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;       \
    }                                                                 \
    size_t c = tpfx##_els_count_##E (&t);                             \
    tpfx##_destroy_##E (&t);                                          \
    return c;                                                         \
  }                                                                   \
                                                                      \
  void *cpfx##_str_build (char *const *keys, int n) {                 \
    struct tpfx##_##E *t = malloc (sizeof *t);                        \
    tpfx##_create_##E (t, 8);                                         \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], i};                                             \
      E *r;                                                           \
      if (!tpfx##_perform_##E (t, &e, A##_INSERT, &r)) *r = e;        \
    }                                                                 \
    return t;                                                         \
  }                                                                   \
                                                                      \
  int cpfx##_str_lookup (void *h, char *const *keys, int n) {         \
    struct tpfx##_##E *t = h;                                         \
    int s = 0;                                                        \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], 0};                                             \
      E *r;                                                           \
      if (tpfx##_perform_##E (t, &e, A##_FIND, &r)) s += r->value;    \
    }                                                                 \
    return s;                                                         \
  }                                                                   \
                                                                      \
  size_t cpfx##_str_delete (char *const *keys, int n) {               \
    struct tpfx##_##E t;                                              \
    tpfx##_create_##E (&t, 8);                                        \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], i};                                             \
      E *r;                                                           \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;       \
    }                                                                 \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], 0};                                             \
      E *r;                                                           \
      tpfx##_perform_##E (&t, &e, A##_DELETE, &r);                    \
    }                                                                 \
    size_t c = tpfx##_els_count_##E (&t);                             \
    tpfx##_destroy_##E (&t);                                          \
    return c;                                                         \
  }                                                                   \
                                                                      \
  int cpfx##_str_iterate (void *h) {                                  \
    struct tpfx##_##E *t = h;                                         \
    int s = 0;                                                        \
    struct tpfx##_iter_##E it = tpfx##_iter_begin_##E (t);            \
    while (tpfx##_iter_valid_##E (&it)) {                             \
      s += it.ptr->value;                                             \
      tpfx##_iter_next_##E (t, &it);                                  \
    }                                                                 \
    return s;                                                         \
  }                                                                   \
                                                                      \
  void cpfx##_str_free (void *h) {                                    \
    struct tpfx##_##E *t = h;                                         \
    tpfx##_destroy_##E (t);                                           \
    free (t);                                                         \
  }                                                                   \
                                                                      \
  size_t cpfx##_str_mixed (char *const *keys, int n) {                \
    struct tpfx##_##E t;                                              \
    tpfx##_create_##E (&t, 8);                                        \
    for (int j = 0; j < n; j++) {                                     \
      int op = j % 10;                                                \
      E e = {keys[j], j};                                             \
      E *r;                                                           \
      if (op < 7) {                                                   \
        if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;     \
      } else if (op < 9) {                                            \
        tpfx##_perform_##E (&t, &e, A##_FIND, &r);                    \
      } else                                                          \
        tpfx##_perform_##E (&t, &e, A##_DELETE, &r);                  \
    }                                                                 \
    size_t c = tpfx##_els_count_##E (&t);                             \
    tpfx##_destroy_##E (&t);                                          \
    return c;                                                         \
  }                                                                   \
                                                                      \
  int cpfx##_str_collision (char *const *keys, int n) {               \
    struct tpfx##_##E t;                                              \
    tpfx##_create_##E (&t, 8);                                        \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], i};                                             \
      E *r;                                                           \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;       \
    }                                                                 \
    int s = 0;                                                        \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], 0};                                             \
      E *r;                                                           \
      if (tpfx##_perform_##E (&t, &e, A##_FIND, &r)) s += r->value;   \
    }                                                                 \
    tpfx##_destroy_##E (&t);                                          \
    return s;                                                         \
  }                                                                   \
                                                                      \
  size_t cpfx##_str_growth (char *const *keys, int n, int prealloc) { \
    struct tpfx##_##E t;                                              \
    tpfx##_create_##E (&t, prealloc ? (unsigned) n : 8);              \
    for (int i = 0; i < n; i++) {                                     \
      E e = {keys[i], i};                                             \
      E *r;                                                           \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;       \
    }                                                                 \
    size_t c = tpfx##_els_count_##E (&t);                             \
    tpfx##_destroy_##E (&t);                                          \
    return c;                                                         \
  }

/* LV (large-value) wrappers — 4 functions per table */
#define GEN_LV(cpfx, tpfx, E, A)                                         \
                                                                         \
  size_t cpfx##_lv_insert (const uint64_t *keys, int n) {                \
    struct tpfx##_##E t;                                                 \
    tpfx##_create_##E (&t, 8);                                           \
    for (int i = 0; i < n; i++) {                                        \
      E e = make_ci_lv (keys[i], i);                                     \
      E *r;                                                              \
      if (!tpfx##_perform_##E (&t, &e, A##_INSERT, &r)) *r = e;          \
    }                                                                    \
    size_t c = tpfx##_els_count_##E (&t);                                \
    tpfx##_destroy_##E (&t);                                             \
    return c;                                                            \
  }                                                                      \
                                                                         \
  void *cpfx##_lv_build (const uint64_t *keys, int n) {                  \
    struct tpfx##_##E *t = malloc (sizeof *t);                           \
    tpfx##_create_##E (t, 8);                                            \
    for (int i = 0; i < n; i++) {                                        \
      E e = make_ci_lv (keys[i], i);                                     \
      E *r;                                                              \
      if (!tpfx##_perform_##E (t, &e, A##_INSERT, &r)) *r = e;           \
    }                                                                    \
    return t;                                                            \
  }                                                                      \
                                                                         \
  int cpfx##_lv_lookup (void *h, const uint64_t *keys, int n) {          \
    struct tpfx##_##E *t = h;                                            \
    int s = 0;                                                           \
    for (int i = 0; i < n; i++) {                                        \
      E e;                                                               \
      e.key = keys[i];                                                   \
      memset (&e.value, 0, sizeof e.value);                              \
      E *r;                                                              \
      if (tpfx##_perform_##E (t, &e, A##_FIND, &r)) s += r->value.value; \
    }                                                                    \
    return s;                                                            \
  }                                                                      \
                                                                         \
  void cpfx##_lv_free (void *h) {                                        \
    struct tpfx##_##E *t = h;                                            \
    tpfx##_destroy_##E (t);                                              \
    free (t);                                                            \
  }

/* ===== Expand all wrappers ===== */

#ifdef USE_V0
GEN_INT (c_ihtab_v0, iht, ci_int, IHT)
GEN_INT (c_ixhtab_v0, ixht, ci_int, IXHT)
GEN_STR (c_ihtab_v0, iht, ci_str, IHT)
GEN_STR (c_ixhtab_v0, ixht, ci_str, IXHT)
GEN_LV (c_ihtab_v0, iht, ci_lv, IHT)
GEN_LV (c_ixhtab_v0, ixht, ci_lv, IXHT)
#else
GEN_INT (c_ihtab, iht, ci_int, IHT)
GEN_INT (c_ixhtab, ixht, ci_int, IXHT)
GEN_STR (c_ihtab, iht, ci_str, IHT)
GEN_STR (c_ixhtab, ixht, ci_str, IXHT)
GEN_LV (c_ihtab, iht, ci_lv, IHT)
GEN_LV (c_ixhtab, ixht, ci_lv, IXHT)
#endif
