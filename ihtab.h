#ifndef IHT_H
#define IHT_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifndef IHT_FORCE_INLINE
#define IHT_FORCE_INLINE __attribute__ ((always_inline)) inline
#endif

typedef uint32_t iht_ind_t;
typedef unsigned long iht_size_t;
typedef size_t iht_hash_t;

#define IHT_GROUP_SIZE 8
#define IHT_EMPTY_H7 0x80
#define IHT_DELETED_H7 0xfe
#define IHT_ENTRY_DELETED (~(iht_ind_t) 0)
#define IHT_LF_FACTOR 1
#define IHT_LF_DIVISOR 2

enum iht_action { IHT_FIND, IHT_DELETE, IHT_INSERT, IHT_REPLACE };

/* ===== SIMD / SWAR platform selection ===== */

#if !defined(IHT_USE_SWAR) \
  && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))

#include <immintrin.h>
#define IHT_MASK_SCALE 0
typedef __m128i iht_group_t;

static IHT_FORCE_INLINE iht_group_t iht_group_load (const unsigned char *p) {
  return _mm_cvtsi64_si128 (*(const long long *) p);
}
static IHT_FORCE_INLINE uint64_t iht_match_mask (iht_group_t g, unsigned char h7_val) {
  __m128i h7_vec = _mm_set1_epi8 ((char) h7_val);
  return (uint64_t) _mm_movemask_epi8 (_mm_cmpeq_epi8 (g, h7_vec)) & 0xff;
}
static IHT_FORCE_INLINE uint64_t iht_match_empty (iht_group_t g) {
  __m128i mask = _mm_set1_epi8 ((char) 0x80);
  return (uint64_t) _mm_movemask_epi8 (_mm_and_si128 (g, mask)) & 0xff;
}

#elif !defined(IHT_USE_SWAR) && (defined(__aarch64__) || defined(_M_ARM64))

#include <arm_neon.h>
#define IHT_MASK_SCALE 0
typedef uint64_t iht_group_t;

static IHT_FORCE_INLINE iht_group_t iht_group_load (const unsigned char *p) {
  return *(const iht_group_t *) p;
}
static IHT_FORCE_INLINE uint64_t iht_match_mask (iht_group_t g, unsigned char h7_val) {
  uint8x8_t group = vcreate_u8 (g);
  uint8x8_t match_eq = vceq_u8 (group, vdup_n_u8 (h7_val));
  static const uint8x8_t bit_mask = {1, 2, 4, 8, 16, 32, 64, 128};
  return (uint64_t) vaddv_u8 (vand_u8 (match_eq, bit_mask));
}
static IHT_FORCE_INLINE uint64_t iht_match_empty (iht_group_t g) { return iht_match_mask (g, IHT_EMPTY_H7); }

#else

#define IHT_MASK_SCALE 1
#define IHT_SWAR_LSB 0x0101010101010101ULL
#define IHT_SWAR_MSB 0x8080808080808080ULL
typedef uint64_t iht_group_t;

static IHT_FORCE_INLINE iht_group_t iht_group_load (const unsigned char *p) { return *(const uint64_t *) p; }
static IHT_FORCE_INLINE uint64_t iht_match_mask (iht_group_t g, unsigned char h7_val) {
  uint64_t cmp = g ^ (IHT_SWAR_LSB * h7_val);
  return (cmp - IHT_SWAR_LSB) & ~cmp & IHT_SWAR_MSB;
}
static IHT_FORCE_INLINE uint64_t iht_match_empty (iht_group_t g) { return g & IHT_SWAR_MSB; }

#endif

/* ===== Generic macro ===== */

#define DEFINE_IHT(El, Hash, Eq)                                                                          \
                                                                                                            \
  struct hbin_iht_##El {                                                                                    \
    iht_size_t els_bound;                                                                                   \
    El *els;                                                                                                \
    char *deleted;                                                                                          \
    unsigned char *h7;                                                                                      \
    iht_ind_t *entries;                                                                                     \
    iht_size_t groups_mask;                                                                                 \
  };                                                                                                        \
                                                                                                            \
  struct iht_##El {                                                                                         \
    iht_size_t els_num;                                                                                     \
    struct hbin_iht_##El bin;                                                                               \
  };                                                                                                        \
                                                                                                            \
  struct iht_iter_##El {                                                                                    \
    iht_size_t el_idx;                                                                                      \
    El *ptr;                                                                                                \
  };                                                                                                        \
                                                                                                            \
  static IHT_FORCE_INLINE void iht_destroy_bin_##El (struct hbin_iht_##El *b) {                             \
    free (b->els);                                                                                          \
    free (b->deleted);                                                                                      \
    free (b->h7);                                                                                           \
    free (b->entries);                                                                                      \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE bool iht_do_1_##El (struct iht_##El *t, struct hbin_iht_##El *b, El *el,          \
                                              enum iht_action action, El **res) {                           \
    iht_hash_t hash = Hash (*el);                                                                           \
    unsigned char h7_val = (hash >> (sizeof (size_t) * 8 - 7)) & 0x7f;                                      \
    iht_size_t group_ind = (hash / IHT_GROUP_SIZE) & b->groups_mask;                                        \
    iht_size_t first_deleted_slot = ~(iht_size_t) 0;                                                        \
    for (;;) {                                                                                              \
      unsigned char *group_h7 = b->h7 + group_ind * IHT_GROUP_SIZE;                                         \
      iht_group_t group = iht_group_load (group_h7);                                                        \
      uint64_t mmask = iht_match_mask (group, h7_val);                                                      \
      while (mmask) {                                                                                       \
        unsigned int bit = __builtin_ctzll (mmask);                                                         \
        if (IHT_MASK_SCALE) bit /= 8;                                                                       \
        iht_size_t slot = group_ind * IHT_GROUP_SIZE + bit;                                                 \
        iht_ind_t el_ind = b->entries[slot];                                                                \
        if (el_ind == IHT_ENTRY_DELETED) {                                                                  \
          if (first_deleted_slot == ~(iht_size_t) 0) first_deleted_slot = slot;                             \
        } else if (Eq (b->els[el_ind], *el)) {                                                              \
          if (action != IHT_DELETE) {                                                                       \
            *res = &b->els[el_ind];                                                                         \
          } else {                                                                                          \
            t->els_num--;                                                                                   \
            b->deleted[el_ind / 8] |= 1 << (el_ind % 8);                                                    \
            b->entries[slot] = IHT_ENTRY_DELETED;                                                           \
          }                                                                                                 \
          return true;                                                                                      \
        }                                                                                                   \
        mmask &= mmask - 1;                                                                                 \
      }                                                                                                     \
      uint64_t emask = iht_match_empty (group);                                                             \
      if (emask) {                                                                                          \
        if (action >= IHT_INSERT) {                                                                         \
          t->els_num++;                                                                                     \
          iht_size_t slot;                                                                                  \
          if (first_deleted_slot != ~(iht_size_t) 0) {                                                      \
            slot = first_deleted_slot;                                                                      \
          } else {                                                                                          \
            unsigned int bit = __builtin_ctzll (emask);                                                     \
            if (IHT_MASK_SCALE) bit /= 8;                                                                   \
            slot = group_ind * IHT_GROUP_SIZE + bit;                                                        \
          }                                                                                                 \
          b->h7[slot] = h7_val;                                                                             \
          b->entries[slot] = (iht_ind_t) b->els_bound;                                                      \
          *res = &b->els[b->els_bound];                                                                     \
          b->els_bound++;                                                                                   \
        }                                                                                                   \
        return false;                                                                                       \
      }                                                                                                     \
      group_ind = (group_ind + 1) & b->groups_mask;                                                         \
    }                                                                                                       \
  }                                                                                                         \
                                                                                                            \
  static inline void iht_rebuild_##El (struct iht_##El *t) {                                                \
    iht_size_t entries_size = (t->bin.groups_mask + 1) * IHT_GROUP_SIZE;                                    \
    iht_size_t els_size = entries_size * IHT_LF_FACTOR / IHT_LF_DIVISOR;                                    \
    if (2 * IHT_LF_DIVISOR * t->els_num >= IHT_LF_FACTOR * entries_size) {                                  \
      entries_size *= 2;                                                                                    \
      els_size = entries_size * IHT_LF_FACTOR / IHT_LF_DIVISOR;                                             \
    }                                                                                                       \
    struct hbin_iht_##El rb;                                                                                \
    rb.els = (El *) malloc (els_size * sizeof (El));                                                        \
    iht_size_t del_bytes = (els_size + 7) / 8;                                                              \
    rb.deleted = (char *) calloc (del_bytes, 1);                                                            \
    rb.h7 = (unsigned char *) aligned_alloc (IHT_GROUP_SIZE, entries_size);                                 \
    memset (rb.h7, IHT_EMPTY_H7, entries_size);                                                             \
    rb.entries = (iht_ind_t *) malloc (entries_size * sizeof (iht_ind_t));                                  \
    rb.groups_mask = entries_size / IHT_GROUP_SIZE - 1;                                                     \
    rb.els_bound = 0;                                                                                       \
    iht_size_t bound = t->bin.els_bound;                                                                    \
    iht_size_t saved = t->els_num;                                                                          \
    for (iht_size_t i = 0; i < bound; i++)                                                                  \
      if (!(t->bin.deleted[i / 8] & (1 << (i % 8)))) {                                                      \
        El *r;                                                                                              \
        iht_do_1_##El (t, &rb, &t->bin.els[i], IHT_INSERT, &r);                                             \
        *r = t->bin.els[i];                                                                                 \
      }                                                                                                     \
    t->els_num = saved;                                                                                     \
    iht_destroy_bin_##El (&t->bin);                                                                         \
    t->bin = rb;                                                                                            \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE void iht_create_##El (struct iht_##El *t, iht_size_t min_size) {                  \
    iht_size_t entries_size = IHT_GROUP_SIZE;                                                               \
    while (entries_size * IHT_LF_FACTOR / IHT_LF_DIVISOR < min_size) entries_size *= 2;                     \
    iht_size_t els_size = entries_size * IHT_LF_FACTOR / IHT_LF_DIVISOR;                                    \
    t->els_num = 0;                                                                                         \
    t->bin.els_bound = 0;                                                                                   \
    t->bin.els = (El *) malloc (els_size * sizeof (El));                                                    \
    iht_size_t del_bytes = (els_size + 7) / 8;                                                              \
    t->bin.deleted = (char *) calloc (del_bytes, 1);                                                        \
    t->bin.h7 = (unsigned char *) aligned_alloc (IHT_GROUP_SIZE, entries_size);                             \
    memset (t->bin.h7, IHT_EMPTY_H7, entries_size);                                                         \
    t->bin.entries = (iht_ind_t *) malloc (entries_size * sizeof (iht_ind_t));                              \
    t->bin.groups_mask = entries_size / IHT_GROUP_SIZE - 1;                                                 \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE void iht_destroy_##El (struct iht_##El *t) { iht_destroy_bin_##El (&t->bin); }    \
                                                                                                            \
  static IHT_FORCE_INLINE bool iht_perform_##El (struct iht_##El *t, El *el, enum iht_action action,        \
                                                 El **res) {                                                \
    if (action >= IHT_INSERT) {                                                                             \
      iht_size_t entries_size = (t->bin.groups_mask + 1) * IHT_GROUP_SIZE;                                  \
      iht_size_t els_size = entries_size * IHT_LF_FACTOR / IHT_LF_DIVISOR - 1;                              \
      if (__builtin_expect (t->bin.els_bound >= els_size, 0)) iht_rebuild_##El (t);                         \
    }                                                                                                       \
    return iht_do_1_##El (t, &t->bin, el, action, res);                                                     \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE iht_size_t iht_els_count_##El (const struct iht_##El *t) { return t->els_num; }   \
                                                                                                            \
  static IHT_FORCE_INLINE iht_size_t iht_size_##El (const struct iht_##El *t) {                             \
    return (t->bin.groups_mask + 1) * IHT_GROUP_SIZE * IHT_LF_FACTOR / IHT_LF_DIVISOR;                      \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE void iht_iter_advance_##El (const struct iht_##El *t, struct iht_iter_##El *it) { \
    while (it->el_idx < t->bin.els_bound) {                                                                 \
      if (!(t->bin.deleted[it->el_idx / 8] & (1 << (it->el_idx % 8)))) {                                    \
        it->ptr = &t->bin.els[it->el_idx];                                                                  \
        return;                                                                                             \
      }                                                                                                     \
      ++it->el_idx;                                                                                         \
    }                                                                                                       \
    it->ptr = NULL;                                                                                         \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE struct iht_iter_##El iht_iter_begin_##El (const struct iht_##El *t) {             \
    struct iht_iter_##El it;                                                                                \
    it.el_idx = 0;                                                                                          \
    it.ptr = NULL;                                                                                          \
    iht_iter_advance_##El (t, &it);                                                                         \
    return it;                                                                                              \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE bool iht_iter_valid_##El (const struct iht_iter_##El *it) {                       \
    return it->ptr != NULL;                                                                                 \
  }                                                                                                         \
                                                                                                            \
  static IHT_FORCE_INLINE void iht_iter_next_##El (const struct iht_##El *t, struct iht_iter_##El *it) {    \
    ++it->el_idx;                                                                                           \
    iht_iter_advance_##El (t, it);                                                                          \
  }

#endif /* IHT_H */
