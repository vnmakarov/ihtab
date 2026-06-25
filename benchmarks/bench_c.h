#ifndef BENCH_C_H
#define BENCH_C_H

#include <stdint.h>
#include <stddef.h>

#define DECL_BENCH_INT(pfx)                                          \
  size_t pfx##_int_insert (const uint64_t *keys, int n);            \
  void *pfx##_int_build (const uint64_t *keys, int n);              \
  int pfx##_int_lookup (void *h, const uint64_t *keys, int n);      \
  size_t pfx##_int_delete (const uint64_t *keys, int n);            \
  int pfx##_int_iterate (void *h);                                  \
  void pfx##_int_free (void *h);

#define DECL_BENCH_STR(pfx)                                          \
  size_t pfx##_str_insert (char *const *keys, int n);               \
  void *pfx##_str_build (char *const *keys, int n);                 \
  int pfx##_str_lookup (void *h, char *const *keys, int n);         \
  size_t pfx##_str_delete (char *const *keys, int n);               \
  int pfx##_str_iterate (void *h);                                  \
  void pfx##_str_free (void *h);                                    \
  size_t pfx##_str_mixed (char *const *keys, int n);                \
  int pfx##_str_collision (char *const *keys, int n);               \
  size_t pfx##_str_growth (char *const *keys, int n, int prealloc);

#define DECL_BENCH_LV(pfx)                                           \
  size_t pfx##_lv_insert (const uint64_t *keys, int n);             \
  void *pfx##_lv_build (const uint64_t *keys, int n);               \
  int pfx##_lv_lookup (void *h, const uint64_t *keys, int n);       \
  void pfx##_lv_free (void *h);

#define DECL_BENCH_ALL(pfx) \
  DECL_BENCH_INT(pfx)       \
  DECL_BENCH_STR(pfx)       \
  DECL_BENCH_LV(pfx)

#ifdef __cplusplus
extern "C" {
#endif

DECL_BENCH_ALL(c_ihtab)
DECL_BENCH_ALL(c_ixhtab)
DECL_BENCH_ALL(c_ihtab_v0)
DECL_BENCH_ALL(c_ixhtab_v0)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
DECL_BENCH_ALL(cpp_ihtab_v0)
DECL_BENCH_ALL(cpp_ixhtab_v0)
#endif

#endif
