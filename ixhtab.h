#ifndef IXHTAB_C_H
#define IXHTAB_C_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifndef IXHTAB_FORCE_INLINE
#define IXHTAB_FORCE_INLINE __attribute__ ((always_inline)) inline
#endif

typedef uint16_t ixhtab_ind_t;
typedef size_t ixhtab_hash_t;
typedef unsigned short ixhtab_depth_t;
typedef unsigned int ebin_ixhtab_ind_t;

#define IXHTAB_GROUP_SIZE 8
#define IXHTAB_EMPTY_H7 0x80
#define IXHTAB_DELETED_H7 0xfe
#define IXHTAB_ENTRY_DELETED ((ixhtab_ind_t) ~(unsigned) 0)
#define IXHTAB_MAX_BIN_SIZE_POWER 15

enum ixhtab_action { IXHTAB_FIND, IXHTAB_DELETE, IXHTAB_INSERT, IXHTAB_REPLACE };

/* ===== SIMD / SWAR platform selection ===== */

#if !defined(IXHTAB_USE_SWAR) \
  && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))

#include <immintrin.h>
#define IXHTAB_MASK_SCALE 0
typedef __m128i ixhtab_group_t;

static IXHTAB_FORCE_INLINE ixhtab_group_t ixhtab_group_load (const unsigned char *p) {
  return _mm_cvtsi64_si128 (*(const long long *) p);
}
static IXHTAB_FORCE_INLINE unsigned int ixhtab_match_mask (ixhtab_group_t g, unsigned char h7_val) {
  __m128i h7_vec = _mm_set1_epi8 ((char) h7_val);
  return (unsigned int) _mm_movemask_epi8 (_mm_cmpeq_epi8 (g, h7_vec)) & 0xff;
}
static IXHTAB_FORCE_INLINE unsigned int ixhtab_match_empty (ixhtab_group_t g) {
  return (unsigned int) _mm_movemask_epi8 (g) & 0xff;
}

#elif !defined(IXHTAB_USE_SWAR) && (defined(__aarch64__) || defined(_M_ARM64))

#include <arm_neon.h>
#define IXHTAB_MASK_SCALE 0
typedef uint64_t ixhtab_group_t;

static IXHTAB_FORCE_INLINE ixhtab_group_t ixhtab_group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}
static IXHTAB_FORCE_INLINE unsigned int ixhtab_match_mask (ixhtab_group_t g, unsigned char h7_val) {
  uint8x8_t group = vcreate_u8 (g);
  uint8x8_t match_eq = vceq_u8 (group, vdup_n_u8 (h7_val));
  static const uint8x8_t bit_mask = {1, 2, 4, 8, 16, 32, 64, 128};
  return (unsigned int) vaddv_u8 (vand_u8 (match_eq, bit_mask));
}
static IXHTAB_FORCE_INLINE unsigned int ixhtab_match_empty (ixhtab_group_t g) {
  return ixhtab_match_mask (g, IXHTAB_EMPTY_H7);
}

#else

#define IXHTAB_MASK_SCALE 1
#define IXHTAB_SWAR_LSB 0x0101010101010101ULL
#define IXHTAB_SWAR_MSB 0x8080808080808080ULL
typedef uint64_t ixhtab_group_t;

static IXHTAB_FORCE_INLINE ixhtab_group_t ixhtab_group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}
static IXHTAB_FORCE_INLINE uint64_t ixhtab_match_mask (ixhtab_group_t g, unsigned char h7_val) {
  uint64_t cmp = g ^ (IXHTAB_SWAR_LSB * h7_val);
  return (cmp - IXHTAB_SWAR_LSB) & ~cmp & IXHTAB_SWAR_MSB;
}
static IXHTAB_FORCE_INLINE uint64_t ixhtab_match_empty (ixhtab_group_t g) { return g & IXHTAB_SWAR_MSB; }

#endif

static inline void ixhtab_get_params (size_t size, size_t *bn, size_t *bp2, size_t *bs) {
  *bs = size;
  *bn = 1;
  *bp2 = 0;
  while (*bs >= (1u << IXHTAB_MAX_BIN_SIZE_POWER)) {
    *bn *= 2;
    *bs /= 2;
    (*bp2)++;
  }
}

/* ===== Generic macro ===== */

#define DEFINE_IXHTAB(El, Hash, Eq)                                                                         \
                                                                                                            \
  struct ebin_ixhtab_##El {                                                                                 \
    ixhtab_depth_t depth;                                                                                   \
    ixhtab_hash_t mask;                                                                                     \
    unsigned int els_start, els_bound;                                                                      \
    El *els;                                                                                                \
    char *deleted;                                                                                          \
    unsigned char *h7;                                                                                      \
    ixhtab_ind_t *entries;                                                                                  \
    unsigned int groups_mask;                                                                               \
  };                                                                                                        \
                                                                                                            \
  struct ixhtab_##El {                                                                                      \
    unsigned int els_num;                                                                                   \
    ixhtab_depth_t max_depth;                                                                               \
    ixhtab_hash_t bin_mask;                                                                                 \
    ebin_ixhtab_ind_t *dir;                                                                                 \
    unsigned int dir_capacity;                                                                              \
    struct ebin_ixhtab_##El *bins;                                                                          \
    unsigned int bins_num;                                                                                  \
    unsigned int bins_capacity;                                                                             \
  };                                                                                                        \
                                                                                                            \
  struct ixhtab_iter_##El {                                                                                 \
    unsigned int bin_idx;                                                                                   \
    unsigned int el_idx;                                                                                    \
    El *ptr;                                                                                                \
  };                                                                                                        \
                                                                                                            \
  static IXHTAB_FORCE_INLINE void ixhtab_destroy_bin_##El (struct ebin_ixhtab_##El *b) {                    \
    free (b->els);                                                                                          \
    free (b->deleted);                                                                                      \
    free (b->h7);                                                                                           \
    free (b->entries);                                                                                      \
  }                                                                                                         \
                                                                                                            \
  static inline ebin_ixhtab_ind_t ixhtab_create_bin_##El (struct ixhtab_##El *t, unsigned int size) {       \
    if (size < IXHTAB_GROUP_SIZE) size = IXHTAB_GROUP_SIZE;                                                 \
    if (t->bins_num == t->bins_capacity) {                                                                  \
      t->bins_capacity = t->bins_capacity ? t->bins_capacity * 2 : 4;                                       \
      t->bins = (struct ebin_ixhtab_##El *) realloc (t->bins,                                               \
                                                     t->bins_capacity * sizeof (struct ebin_ixhtab_##El));  \
    }                                                                                                       \
    ebin_ixhtab_ind_t ind = t->bins_num++;                                                                  \
    struct ebin_ixhtab_##El *b = &t->bins[ind];                                                             \
    b->els_start = b->els_bound = 0;                                                                        \
    b->els = (El *) malloc (size * sizeof (El));                                                            \
    unsigned int del_bytes = (size + 7) / 8;                                                                \
    b->deleted = (char *) calloc (del_bytes, 1);                                                            \
    unsigned int entries_size = 2 * size;                                                                   \
    b->h7 = (unsigned char *) aligned_alloc (IXHTAB_GROUP_SIZE, entries_size);                              \
    memset (b->h7, IXHTAB_EMPTY_H7, entries_size);                                                          \
    b->entries = (ixhtab_ind_t *) malloc (entries_size * sizeof (ixhtab_ind_t));                            \
    b->groups_mask = entries_size / IXHTAB_GROUP_SIZE - 1;                                                  \
    return ind;                                                                                             \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE bool ixhtab_do_1_##El (struct ixhtab_##El *t, struct ebin_ixhtab_##El *bin,    \
                                                    ixhtab_hash_t hash, El *el, enum ixhtab_action action,  \
                                                    El **res) {                                             \
    unsigned char h7_val = (hash >> (sizeof (size_t) * 8 - 7)) & 0x7f;                                      \
    unsigned int group_ind = (unsigned int) (hash / IXHTAB_GROUP_SIZE) & bin->groups_mask;                  \
    unsigned int first_deleted_slot = ~0u;                                                                  \
    for (;;) {                                                                                              \
      unsigned char *group_h7 = bin->h7 + group_ind * IXHTAB_GROUP_SIZE;                                    \
      ixhtab_group_t group = ixhtab_group_load (group_h7);                                                  \
      uint64_t mmask = (uint64_t) ixhtab_match_mask (group, h7_val);                                        \
      while (mmask) {                                                                                       \
        unsigned int bit = __builtin_ctzll (mmask);                                                         \
        if (IXHTAB_MASK_SCALE) bit /= 8;                                                                    \
        unsigned int slot = group_ind * IXHTAB_GROUP_SIZE + bit;                                            \
        ixhtab_ind_t el_ind = bin->entries[slot];                                                           \
        if (el_ind == IXHTAB_ENTRY_DELETED) {                                                               \
          if (first_deleted_slot == ~0u) first_deleted_slot = slot;                                         \
        } else if (Eq (bin->els[el_ind], *el)) {                                                            \
          if (action != IXHTAB_DELETE) {                                                                    \
            *res = &bin->els[el_ind];                                                                       \
          } else {                                                                                          \
            t->els_num--;                                                                                   \
            bin->deleted[el_ind / 8] |= 1 << (el_ind % 8);                                                  \
            bin->entries[slot] = IXHTAB_ENTRY_DELETED;                                                      \
          }                                                                                                 \
          return true;                                                                                      \
        }                                                                                                   \
        mmask &= mmask - 1;                                                                                 \
      }                                                                                                     \
      uint64_t emask = (uint64_t) ixhtab_match_empty (group);                                               \
      if (emask) {                                                                                          \
        if (action >= IXHTAB_INSERT) {                                                                      \
          t->els_num++;                                                                                     \
          unsigned int slot;                                                                                \
          if (first_deleted_slot != ~0u) {                                                                  \
            slot = first_deleted_slot;                                                                      \
          } else {                                                                                          \
            unsigned int bit = __builtin_ctzll (emask);                                                     \
            if (IXHTAB_MASK_SCALE) bit /= 8;                                                                \
            slot = group_ind * IXHTAB_GROUP_SIZE + bit;                                                     \
          }                                                                                                 \
          bin->h7[slot] = h7_val;                                                                           \
          bin->entries[slot] = (ixhtab_ind_t) bin->els_bound;                                               \
          *res = &bin->els[bin->els_bound];                                                                 \
          bin->els_bound++;                                                                                 \
        }                                                                                                   \
        return false;                                                                                       \
      }                                                                                                     \
      group_ind = (group_ind + 1) & bin->groups_mask;                                                       \
    }                                                                                                       \
  }                                                                                                         \
                                                                                                            \
  static inline void ixhtab_split_bin_##El (struct ixhtab_##El *t, ebin_ixhtab_ind_t bin_ind) {             \
    unsigned int size = (t->bins[bin_ind].groups_mask + 1) * IXHTAB_GROUP_SIZE / 2;                         \
    ebin_ixhtab_ind_t new_ind = ixhtab_create_bin_##El (t, size);                                           \
    for (;;) {                                                                                              \
      struct ebin_ixhtab_##El *new_bin = &t->bins[new_ind];                                                 \
      struct ebin_ixhtab_##El *bin = &t->bins[bin_ind];                                                     \
      unsigned int entries_size = (bin->groups_mask + 1) * IXHTAB_GROUP_SIZE;                               \
      memset (bin->h7, IXHTAB_EMPTY_H7, entries_size);                                                      \
      ixhtab_hash_t split_mask = (ixhtab_hash_t) 1 << bin->depth;                                           \
      new_bin->depth = ++bin->depth;                                                                        \
      if (bin->depth > t->max_depth) {                                                                      \
        t->max_depth = bin->depth;                                                                          \
        t->bin_mask = ~(~(ixhtab_hash_t) 0 << t->max_depth);                                                \
        unsigned int len = t->dir_capacity;                                                                 \
        t->dir = (ebin_ixhtab_ind_t *) realloc (t->dir, 2 * len * sizeof (ebin_ixhtab_ind_t));              \
        t->dir_capacity = 2 * len;                                                                          \
        for (unsigned int j = 0; j < len; j++) t->dir[j + len] = t->dir[j];                                 \
      }                                                                                                     \
      new_bin->mask = bin->mask | split_mask;                                                               \
      t->dir[new_bin->mask] = new_ind;                                                                      \
      unsigned int start = bin->els_start, bound = bin->els_bound;                                          \
      bin->els_start = bin->els_bound = 0;                                                                  \
      unsigned int saved = t->els_num;                                                                      \
      bool old_added = false, new_added = false;                                                            \
      for (unsigned int i = start; i < bound; i++) {                                                        \
        if (bin->deleted[i / 8] & (1 << (i % 8))) continue;                                                 \
        ixhtab_hash_t hash = Hash (bin->els[i]);                                                            \
        if (hash == 0) hash = 1;                                                                            \
        bool is_old = (hash & split_mask) == 0;                                                             \
        if (is_old)                                                                                         \
          old_added = true;                                                                                 \
        else                                                                                                \
          new_added = true;                                                                                 \
        El *r;                                                                                              \
        ixhtab_do_1_##El (t, is_old ? &t->bins[bin_ind] : &t->bins[new_ind], hash, &bin->els[i],            \
                          IXHTAB_INSERT, &r);                                                               \
        *r = bin->els[i];                                                                                   \
      }                                                                                                     \
      t->els_num = saved;                                                                                   \
      if (!old_added) {                                                                                     \
        ebin_ixhtab_ind_t tmp = bin_ind;                                                                    \
        bin_ind = new_ind;                                                                                  \
        new_ind = tmp;                                                                                      \
      } else if (new_added) {                                                                               \
        break;                                                                                              \
      }                                                                                                     \
    }                                                                                                       \
    struct ebin_ixhtab_##El *ob = &t->bins[bin_ind];                                                        \
    memset (ob->deleted, 0, ((ob->groups_mask + 1) * IXHTAB_GROUP_SIZE / 2 + 7) / 8);                       \
    struct ebin_ixhtab_##El *nb = &t->bins[new_ind];                                                        \
    memset (nb->deleted, 0, ((nb->groups_mask + 1) * IXHTAB_GROUP_SIZE / 2 + 7) / 8);                       \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE void ixhtab_create_##El (struct ixhtab_##El *t, unsigned int min_size) {       \
    unsigned int size;                                                                                      \
    for (size = 2; min_size > size; size *= 2);                                                             \
    t->els_num = 0;                                                                                         \
    t->bins = NULL;                                                                                         \
    t->bins_num = 0;                                                                                        \
    t->bins_capacity = 0;                                                                                   \
    size_t bn, bin_power2, bin_size;                                                                        \
    ixhtab_get_params (size, &bn, &bin_power2, &bin_size);                                                  \
    t->max_depth = (ixhtab_depth_t) bin_power2;                                                             \
    t->bin_mask = ((ixhtab_hash_t) 1 << bin_power2) - 1;                                                    \
    t->dir = (ebin_ixhtab_ind_t *) malloc (bn * sizeof (ebin_ixhtab_ind_t));                                \
    t->dir_capacity = (unsigned int) bn;                                                                    \
    for (size_t i = 0; i < bn; i++) {                                                                       \
      t->dir[i] = (ebin_ixhtab_ind_t) i;                                                                    \
      ebin_ixhtab_ind_t ind = ixhtab_create_bin_##El (t, (unsigned int) bin_size);                          \
      t->bins[ind].depth = (ixhtab_depth_t) bin_power2;                                                     \
      t->bins[ind].mask = (ixhtab_hash_t) i;                                                                \
    }                                                                                                       \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE void ixhtab_destroy_##El (struct ixhtab_##El *t) {                             \
    for (unsigned int i = 0; i < t->bins_num; i++) ixhtab_destroy_bin_##El (&t->bins[i]);                   \
    free (t->bins);                                                                                         \
    free (t->dir);                                                                                          \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE bool ixhtab_perform_##El (struct ixhtab_##El *t, El *el,                       \
                                                       enum ixhtab_action action, El **res) {               \
    ixhtab_hash_t hash = Hash (*el);                                                                        \
    if (hash == 0) hash = 1;                                                                                \
    ixhtab_hash_t dir_ind = hash & t->bin_mask;                                                             \
    ebin_ixhtab_ind_t bin_ind = t->dir[dir_ind];                                                            \
    struct ebin_ixhtab_##El *bin = &t->bins[bin_ind];                                                       \
    if (action >= IXHTAB_INSERT) {                                                                          \
      unsigned int entries_size = (bin->groups_mask + 1) * IXHTAB_GROUP_SIZE;                               \
      unsigned int els_size = entries_size / 2;                                                             \
      if (bin->els_bound == els_size) {                                                                     \
        bool grow = false;                                                                                  \
        if (2 * t->els_num >= entries_size) {                                                               \
          entries_size *= 2;                                                                                \
          els_size *= 2;                                                                                    \
          grow = true;                                                                                      \
        }                                                                                                   \
        if (grow && els_size >= (1u << IXHTAB_MAX_BIN_SIZE_POWER)) {                                        \
          ixhtab_split_bin_##El (t, bin_ind);                                                               \
          bin_ind = t->dir[hash & t->bin_mask];                                                             \
        } else {                                                                                            \
          struct ebin_ixhtab_##El *b = &t->bins[bin_ind];                                                   \
          char *old_deleted = b->deleted;                                                                   \
          unsigned int del_bytes = (els_size + 7) / 8;                                                      \
          b->deleted = (char *) calloc (del_bytes, 1);                                                      \
          b->els = (El *) realloc (b->els, els_size * sizeof (El));                                         \
          b->h7 = (unsigned char *) realloc (b->h7, entries_size);                                          \
          memset (b->h7, IXHTAB_EMPTY_H7, entries_size);                                                    \
          b->entries = (ixhtab_ind_t *) realloc (b->entries, entries_size * sizeof (ixhtab_ind_t));         \
          b->groups_mask = entries_size / IXHTAB_GROUP_SIZE - 1;                                            \
          unsigned int start = b->els_start, bound = b->els_bound;                                          \
          b->els_start = b->els_bound = 0;                                                                  \
          unsigned int saved = t->els_num;                                                                  \
          t->els_num = 0;                                                                                   \
          for (unsigned int i = start; i < bound; i++) {                                                    \
            if (old_deleted[i / 8] & (1 << (i % 8))) continue;                                              \
            ixhtab_hash_t hash2 = Hash (b->els[i]);                                                         \
            if (hash2 == 0) hash2 = 1;                                                                      \
            El *r;                                                                                          \
            ixhtab_do_1_##El (t, b, hash2, &b->els[i], IXHTAB_INSERT, &r);                                  \
            *r = b->els[i];                                                                                 \
          }                                                                                                 \
          t->els_num = saved;                                                                               \
          free (old_deleted);                                                                               \
        }                                                                                                   \
      }                                                                                                     \
    }                                                                                                       \
    return ixhtab_do_1_##El (t, &t->bins[bin_ind], hash, el, action, res);                                  \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE unsigned int ixhtab_els_count_##El (const struct ixhtab_##El *t) {             \
    return t->els_num;                                                                                      \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE void ixhtab_iter_advance_##El (const struct ixhtab_##El *t,                    \
                                                            struct ixhtab_iter_##El *it) {                  \
    while (it->bin_idx < t->bins_num) {                                                                     \
      struct ebin_ixhtab_##El *b = &t->bins[it->bin_idx];                                                   \
      while (it->el_idx < b->els_bound) {                                                                   \
        if (!(b->deleted[it->el_idx / 8] & (1 << (it->el_idx % 8)))) {                                      \
          it->ptr = &b->els[it->el_idx];                                                                    \
          return;                                                                                           \
        }                                                                                                   \
        ++it->el_idx;                                                                                       \
      }                                                                                                     \
      ++it->bin_idx;                                                                                        \
      if (it->bin_idx < t->bins_num) it->el_idx = t->bins[it->bin_idx].els_start;                           \
    }                                                                                                       \
    it->ptr = NULL;                                                                                         \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE struct ixhtab_iter_##El ixhtab_iter_begin_##El (const struct ixhtab_##El *t) { \
    struct ixhtab_iter_##El it;                                                                             \
    it.bin_idx = 0;                                                                                         \
    it.el_idx = t->bins_num > 0 ? t->bins[0].els_start : 0;                                                 \
    it.ptr = NULL;                                                                                          \
    ixhtab_iter_advance_##El (t, &it);                                                                      \
    return it;                                                                                              \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE bool ixhtab_iter_valid_##El (const struct ixhtab_iter_##El *it) {              \
    return it->ptr != NULL;                                                                                 \
  }                                                                                                         \
                                                                                                            \
  static IXHTAB_FORCE_INLINE void ixhtab_iter_next_##El (const struct ixhtab_##El *t,                       \
                                                         struct ixhtab_iter_##El *it) {                     \
    ++it->el_idx;                                                                                           \
    ixhtab_iter_advance_##El (t, it);                                                                       \
  }

#endif /* IXHTAB_C_H */
