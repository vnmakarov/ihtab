#ifndef BENCH_C_H
#define BENCH_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Int key benchmarks */
size_t c_ihtab_int_insert (const uint64_t *keys, int n);
void  *c_ihtab_int_build  (const uint64_t *keys, int n);
int    c_ihtab_int_lookup (void *h, const uint64_t *keys, int n);
size_t c_ihtab_int_delete (const uint64_t *keys, int n);
int    c_ihtab_int_iterate(void *h);
void   c_ihtab_int_free   (void *h);

size_t c_ixhtab_int_insert (const uint64_t *keys, int n);
void  *c_ixhtab_int_build  (const uint64_t *keys, int n);
int    c_ixhtab_int_lookup (void *h, const uint64_t *keys, int n);
size_t c_ixhtab_int_delete (const uint64_t *keys, int n);
int    c_ixhtab_int_iterate(void *h);
void   c_ixhtab_int_free   (void *h);

/* Str key benchmarks */
size_t c_ihtab_str_insert   (char *const *keys, int n);
void  *c_ihtab_str_build    (char *const *keys, int n);
int    c_ihtab_str_lookup   (void *h, char *const *keys, int n);
size_t c_ihtab_str_delete   (char *const *keys, int n);
int    c_ihtab_str_iterate  (void *h);
void   c_ihtab_str_free     (void *h);
size_t c_ihtab_str_mixed    (char *const *keys, int n);
int    c_ihtab_str_collision(char *const *keys, int n);
size_t c_ihtab_str_growth   (char *const *keys, int n, int prealloc);

size_t c_ixhtab_str_insert   (char *const *keys, int n);
void  *c_ixhtab_str_build    (char *const *keys, int n);
int    c_ixhtab_str_lookup   (void *h, char *const *keys, int n);
size_t c_ixhtab_str_delete   (char *const *keys, int n);
int    c_ixhtab_str_iterate  (void *h);
void   c_ixhtab_str_free     (void *h);
size_t c_ixhtab_str_mixed    (char *const *keys, int n);
int    c_ixhtab_str_collision(char *const *keys, int n);
size_t c_ixhtab_str_growth   (char *const *keys, int n, int prealloc);

/* Large value benchmarks */
size_t c_ihtab_lv_insert (const uint64_t *keys, int n);
void  *c_ihtab_lv_build  (const uint64_t *keys, int n);
int    c_ihtab_lv_lookup (void *h, const uint64_t *keys, int n);
void   c_ihtab_lv_free   (void *h);

size_t c_ixhtab_lv_insert (const uint64_t *keys, int n);
void  *c_ixhtab_lv_build  (const uint64_t *keys, int n);
int    c_ixhtab_lv_lookup (void *h, const uint64_t *keys, int n);
void   c_ixhtab_lv_free   (void *h);

#ifdef __cplusplus
}
#endif

#endif
