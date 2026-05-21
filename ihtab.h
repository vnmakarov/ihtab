#ifndef IHTAB_C_H
#define IHTAB_C_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifndef IHTAB_FORCE_INLINE
#define IHTAB_FORCE_INLINE __attribute__ ((always_inline)) inline
#endif

typedef uint32_t ihtab_ind_t;
typedef unsigned long ihtab_size_t;
typedef size_t ihtab_hash_t;

#define IHTAB_GROUP_SIZE 8
#define IHTAB_EMPTY_H7 0x80
#define IHTAB_DELETED_H7 0xfe
#define IHTAB_ENTRY_DELETED (~(ihtab_ind_t) 0)
#define IHTAB_LF_FACTOR 1
#define IHTAB_LF_DIVISOR 2

enum ihtab_action { IHTAB_FIND, IHTAB_DELETE, IHTAB_INSERT, IHTAB_REPLACE };

/* ===== SIMD / SWAR platform selection ===== */

#if !defined(IHTAB_USE_SWAR) \
  && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))

#include <immintrin.h>
#define IHTAB_MASK_SCALE 0
typedef __m128i ihtab_group_t;

static IHTAB_FORCE_INLINE ihtab_group_t ihtab_group_load (const unsigned char *p) {
  return _mm_cvtsi64_si128 (*(const long long *) p);
}
static IHTAB_FORCE_INLINE uint64_t ihtab_match_mask (ihtab_group_t g, unsigned char h7_val) {
  __m128i h7_vec = _mm_set1_epi8 ((char) h7_val);
  return (uint64_t) _mm_movemask_epi8 (_mm_cmpeq_epi8 (g, h7_vec)) & 0xff;
}
static IHTAB_FORCE_INLINE uint64_t ihtab_match_empty (ihtab_group_t g) {
  __m128i mask = _mm_set1_epi8 ((char) 0x80);
  return (uint64_t) _mm_movemask_epi8 (_mm_and_si128 (g, mask)) & 0xff;
}

#elif !defined(IHTAB_USE_SWAR) && (defined(__aarch64__) || defined(_M_ARM64))

#include <arm_neon.h>
#define IHTAB_MASK_SCALE 0
typedef uint64_t ihtab_group_t;

static IHTAB_FORCE_INLINE ihtab_group_t ihtab_group_load (const unsigned char *p) {
  return *(const ihtab_group_t *) p;
}
static IHTAB_FORCE_INLINE uint64_t ihtab_match_mask (ihtab_group_t g, unsigned char h7_val) {
  uint8x8_t group = vcreate_u8 (g);
  uint8x8_t match_eq = vceq_u8 (group, vdup_n_u8 (h7_val));
  static const uint8x8_t bit_mask = {1, 2, 4, 8, 16, 32, 64, 128};
  return (uint64_t) vaddv_u8 (vand_u8 (match_eq, bit_mask));
}
static IHTAB_FORCE_INLINE uint64_t ihtab_match_empty (ihtab_group_t g) {
  return ihtab_match_mask (g, IHTAB_EMPTY_H7);
}

#else

#define IHTAB_MASK_SCALE 1
#define IHTAB_SWAR_LSB 0x0101010101010101ULL
#define IHTAB_SWAR_MSB 0x8080808080808080ULL
typedef uint64_t ihtab_group_t;

static IHTAB_FORCE_INLINE ihtab_group_t ihtab_group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}
static IHTAB_FORCE_INLINE uint64_t ihtab_match_mask (ihtab_group_t g, unsigned char h7_val) {
  uint64_t cmp = g ^ (IHTAB_SWAR_LSB * h7_val);
  return (cmp - IHTAB_SWAR_LSB) & ~cmp & IHTAB_SWAR_MSB;
}
static IHTAB_FORCE_INLINE uint64_t ihtab_match_empty (ihtab_group_t g) { return g & IHTAB_SWAR_MSB; }

#endif

/* ===== Generic macro ===== */

#define DEFINE_IHTAB(El, Hash, Eq)                                                                           \
                                                                                                             \
  struct hbin_ihtab_##El {                                                                                   \
    ihtab_size_t els_bound;                                                                                  \
    El *els;                                                                                                 \
    char *deleted;                                                                                           \
    unsigned char *h7;                                                                                       \
    ihtab_ind_t *entries;                                                                                    \
    ihtab_size_t groups_mask;                                                                                \
  };                                                                                                         \
                                                                                                             \
  struct ihtab_##El {                                                                                        \
    ihtab_size_t els_num;                                                                                    \
    struct hbin_ihtab_##El bin;                                                                              \
  };                                                                                                         \
                                                                                                             \
  struct ihtab_iter_##El {                                                                                   \
    ihtab_size_t el_idx;                                                                                     \
    El *ptr;                                                                                                 \
  };                                                                                                         \
                                                                                                             \
  static IHTAB_FORCE_INLINE void ihtab_destroy_bin_##El (struct hbin_ihtab_##El *b) {                        \
    free (b->els);                                                                                           \
    free (b->deleted);                                                                                       \
    free (b->h7);                                                                                            \
    free (b->entries);                                                                                       \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE bool ihtab_do_1_##El (struct ihtab_##El *t, struct hbin_ihtab_##El *b, El *el,   \
                                                  enum ihtab_action action, El **res) {                      \
    ihtab_hash_t hash = Hash (*el);                                                                          \
    unsigned char h7_val = (hash >> (sizeof (size_t) * 8 - 7)) & 0x7f;                                       \
    ihtab_size_t group_ind = (hash / IHTAB_GROUP_SIZE) & b->groups_mask;                                     \
    ihtab_size_t first_deleted_slot = ~(ihtab_size_t) 0;                                                     \
    for (;;) {                                                                                               \
      unsigned char *group_h7 = b->h7 + group_ind * IHTAB_GROUP_SIZE;                                        \
      ihtab_group_t group = ihtab_group_load (group_h7);                                                     \
      uint64_t mmask = ihtab_match_mask (group, h7_val);                                                     \
      while (mmask) {                                                                                        \
        unsigned int bit = __builtin_ctzll (mmask);                                                          \
        if (IHTAB_MASK_SCALE) bit /= 8;                                                                      \
        ihtab_size_t slot = group_ind * IHTAB_GROUP_SIZE + bit;                                              \
        ihtab_ind_t el_ind = b->entries[slot];                                                               \
        if (el_ind == IHTAB_ENTRY_DELETED) {                                                                 \
          if (first_deleted_slot == ~(ihtab_size_t) 0) first_deleted_slot = slot;                            \
        } else if (Eq (b->els[el_ind], *el)) {                                                               \
          if (action != IHTAB_DELETE) {                                                                      \
            *res = &b->els[el_ind];                                                                          \
          } else {                                                                                           \
            t->els_num--;                                                                                    \
            b->deleted[el_ind / 8] |= 1 << (el_ind % 8);                                                     \
            b->entries[slot] = IHTAB_ENTRY_DELETED;                                                          \
          }                                                                                                  \
          return true;                                                                                       \
        }                                                                                                    \
        mmask &= mmask - 1;                                                                                  \
      }                                                                                                      \
      uint64_t emask = ihtab_match_empty (group);                                                            \
      if (emask) {                                                                                           \
        if (action >= IHTAB_INSERT) {                                                                        \
          t->els_num++;                                                                                      \
          ihtab_size_t slot;                                                                                 \
          if (first_deleted_slot != ~(ihtab_size_t) 0) {                                                     \
            slot = first_deleted_slot;                                                                       \
          } else {                                                                                           \
            unsigned int bit = __builtin_ctzll (emask);                                                      \
            if (IHTAB_MASK_SCALE) bit /= 8;                                                                  \
            slot = group_ind * IHTAB_GROUP_SIZE + bit;                                                       \
          }                                                                                                  \
          b->h7[slot] = h7_val;                                                                              \
          b->entries[slot] = (ihtab_ind_t) b->els_bound;                                                     \
          *res = &b->els[b->els_bound];                                                                      \
          b->els_bound++;                                                                                    \
        }                                                                                                    \
        return false;                                                                                        \
      }                                                                                                      \
      group_ind = (group_ind + 1) & b->groups_mask;                                                          \
    }                                                                                                        \
  }                                                                                                          \
                                                                                                             \
  static inline void ihtab_rebuild_##El (struct ihtab_##El *t) {                                             \
    ihtab_size_t entries_size = (t->bin.groups_mask + 1) * IHTAB_GROUP_SIZE;                                 \
    ihtab_size_t els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;                               \
    if (2 * IHTAB_LF_DIVISOR * t->els_num >= IHTAB_LF_FACTOR * entries_size) {                               \
      entries_size *= 2;                                                                                     \
      els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;                                          \
    }                                                                                                        \
    struct hbin_ihtab_##El rb;                                                                               \
    rb.els = (El *) malloc (els_size * sizeof (El));                                                         \
    ihtab_size_t del_bytes = (els_size + 7) / 8;                                                             \
    rb.deleted = (char *) calloc (del_bytes, 1);                                                             \
    rb.h7 = (unsigned char *) aligned_alloc (IHTAB_GROUP_SIZE, entries_size);                                \
    memset (rb.h7, IHTAB_EMPTY_H7, entries_size);                                                            \
    rb.entries = (ihtab_ind_t *) malloc (entries_size * sizeof (ihtab_ind_t));                               \
    rb.groups_mask = entries_size / IHTAB_GROUP_SIZE - 1;                                                    \
    rb.els_bound = 0;                                                                                        \
    ihtab_size_t bound = t->bin.els_bound;                                                                   \
    ihtab_size_t saved = t->els_num;                                                                         \
    for (ihtab_size_t i = 0; i < bound; i++)                                                                 \
      if (!(t->bin.deleted[i / 8] & (1 << (i % 8)))) {                                                       \
        El *r;                                                                                               \
        ihtab_do_1_##El (t, &rb, &t->bin.els[i], IHTAB_INSERT, &r);                                          \
        *r = t->bin.els[i];                                                                                  \
      }                                                                                                      \
    t->els_num = saved;                                                                                      \
    ihtab_destroy_bin_##El (&t->bin);                                                                        \
    t->bin = rb;                                                                                             \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE void ihtab_create_##El (struct ihtab_##El *t, ihtab_size_t min_size) {           \
    ihtab_size_t entries_size = IHTAB_GROUP_SIZE;                                                            \
    while (entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR < min_size) entries_size *= 2;                  \
    ihtab_size_t els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;                               \
    t->els_num = 0;                                                                                          \
    t->bin.els_bound = 0;                                                                                    \
    t->bin.els = (El *) malloc (els_size * sizeof (El));                                                     \
    ihtab_size_t del_bytes = (els_size + 7) / 8;                                                             \
    t->bin.deleted = (char *) calloc (del_bytes, 1);                                                         \
    t->bin.h7 = (unsigned char *) aligned_alloc (IHTAB_GROUP_SIZE, entries_size);                            \
    memset (t->bin.h7, IHTAB_EMPTY_H7, entries_size);                                                        \
    t->bin.entries = (ihtab_ind_t *) malloc (entries_size * sizeof (ihtab_ind_t));                           \
    t->bin.groups_mask = entries_size / IHTAB_GROUP_SIZE - 1;                                                \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE void ihtab_destroy_##El (struct ihtab_##El *t) {                                 \
    ihtab_destroy_bin_##El (&t->bin);                                                                        \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE bool ihtab_perform_##El (struct ihtab_##El *t, El *el, enum ihtab_action action, \
                                                     El **res) {                                             \
    if (action >= IHTAB_INSERT) {                                                                            \
      ihtab_size_t entries_size = (t->bin.groups_mask + 1) * IHTAB_GROUP_SIZE;                               \
      ihtab_size_t els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR - 1;                         \
      if (__builtin_expect (t->bin.els_bound >= els_size, 0)) ihtab_rebuild_##El (t);                        \
    }                                                                                                        \
    return ihtab_do_1_##El (t, &t->bin, el, action, res);                                                    \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE ihtab_size_t ihtab_els_count_##El (const struct ihtab_##El *t) {                 \
    return t->els_num;                                                                                       \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE ihtab_size_t ihtab_size_##El (const struct ihtab_##El *t) {                      \
    return (t->bin.groups_mask + 1) * IHTAB_GROUP_SIZE * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;                 \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE void ihtab_iter_advance_##El (const struct ihtab_##El *t,                        \
                                                          struct ihtab_iter_##El *it) {                      \
    while (it->el_idx < t->bin.els_bound) {                                                                  \
      if (!(t->bin.deleted[it->el_idx / 8] & (1 << (it->el_idx % 8)))) {                                     \
        it->ptr = &t->bin.els[it->el_idx];                                                                   \
        return;                                                                                              \
      }                                                                                                      \
      ++it->el_idx;                                                                                          \
    }                                                                                                        \
    it->ptr = NULL;                                                                                          \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE struct ihtab_iter_##El ihtab_iter_begin_##El (const struct ihtab_##El *t) {      \
    struct ihtab_iter_##El it;                                                                               \
    it.el_idx = 0;                                                                                           \
    it.ptr = NULL;                                                                                           \
    ihtab_iter_advance_##El (t, &it);                                                                        \
    return it;                                                                                               \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE bool ihtab_iter_valid_##El (const struct ihtab_iter_##El *it) {                  \
    return it->ptr != NULL;                                                                                  \
  }                                                                                                          \
                                                                                                             \
  static IHTAB_FORCE_INLINE void ihtab_iter_next_##El (const struct ihtab_##El *t,                           \
                                                       struct ihtab_iter_##El *it) {                         \
    ++it->el_idx;                                                                                            \
    ihtab_iter_advance_##El (t, it);                                                                         \
  }

#endif /* IHTAB_C_H */
