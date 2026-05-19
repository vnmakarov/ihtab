#ifndef IHTAB_H
#define IHTAB_H

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>

#define FORCE_INLINE __attribute__((always_inline)) inline

typedef uint32_t ihtab_ind_t;
typedef unsigned long ihtab_size_t;
typedef size_t ihtab_hash_t;

static constexpr unsigned int IHTAB_GROUP_SIZE = 8;
static constexpr unsigned char IHTAB_EMPTY_H7 = 0x80;
static constexpr unsigned char IHTAB_DELETED_H7 = 0xfe;
static constexpr ihtab_ind_t IHTAB_ENTRY_DELETED = ~(ihtab_ind_t) 0;
static constexpr unsigned int IHTAB_LF_FACTOR = 1;
static constexpr unsigned int IHTAB_LF_DIVISOR = 2;


#if !defined(IHTAB_USE_SWAR) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))

#include <immintrin.h>
static const bool mask_scale = false;
typedef __m128i ihtab_group_t;
static FORCE_INLINE ihtab_group_t ihtab_group_load (const unsigned char *p) {
  return _mm_cvtsi64_si128 (*(const long long *) p);
}
static FORCE_INLINE uint64_t ihtab_match_mask (ihtab_group_t g, unsigned char h7_val) {
  __m128i h7_vec = _mm_set1_epi8 ((char) h7_val);
  return (uint64_t) _mm_movemask_epi8 (_mm_cmpeq_epi8 (g, h7_vec)) & 0xff;
}
static const __m128i IHTAB_EMPTY_MASK = _mm_set1_epi8 ((char) 0x80);
static FORCE_INLINE uint64_t ihtab_match_empty (ihtab_group_t g) {
  return (uint64_t) _mm_movemask_epi8 (_mm_and_si128 (g, IHTAB_EMPTY_MASK)) & 0xff;
}

#elif !defined(IHTAB_USE_SWAR) && (defined(__aarch64__) || defined(_M_ARM64))

#include <arm_neon.h>
static const bool mask_scale = false;
typedef uint64_t ihtab_group_t;
static FORCE_INLINE ihtab_group_t ihtab_group_load (const unsigned char *p) {
  return *(const ihtab_group_t *) p;
}
static FORCE_INLINE uint64_t ihtab_match_mask (ihtab_group_t g, unsigned char h7_val) {
  uint8x8_t group = vcreate_u8 (g);
  uint8x8_t match_eq = vceq_u8 (group, vdup_n_u8 (h7_val));
  static const uint8x8_t bit_mask = {1, 2, 4, 8, 16, 32, 64, 128};
  return (uint64_t) vaddv_u8 (vand_u8 (match_eq, bit_mask));
}
static FORCE_INLINE uint64_t ihtab_match_empty (ihtab_group_t g) {
  return ihtab_match_mask (g, IHTAB_EMPTY_H7);
}

#else

static const bool mask_scale = true;
static constexpr uint64_t IHTAB_SWAR_LSB = 0x0101010101010101ULL;
static constexpr uint64_t IHTAB_SWAR_MSB = 0x8080808080808080ULL;
typedef uint64_t ihtab_group_t;
static FORCE_INLINE ihtab_group_t ihtab_group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}
static FORCE_INLINE uint64_t ihtab_match_mask (ihtab_group_t g, unsigned char h7_val) {
  uint64_t cmp = g ^ (IHTAB_SWAR_LSB * h7_val);
  return (cmp - IHTAB_SWAR_LSB) & ~cmp & IHTAB_SWAR_MSB;
}
static FORCE_INLINE uint64_t ihtab_match_empty (ihtab_group_t g) {
  return g & IHTAB_SWAR_MSB;
}

#endif

enum ihtab_action { IHTAB_FIND, IHTAB_INSERT, IHTAB_REPLACE, IHTAB_DELETE };

template<typename El>
struct hbin_ihtab_t {
  ihtab_size_t els_bound;
  El *els;
  char *deleted;
  unsigned char *h7;
  ihtab_ind_t *entries;
  ihtab_size_t groups_mask;
};

template<typename El, typename Hash, typename Eq>
struct ihtab_t {
  ihtab_size_t els_num;
  hbin_ihtab_t<El> bin;

  static void create (ihtab_t *htab, ihtab_size_t min_size) {
    ihtab_size_t entries_size = IHTAB_GROUP_SIZE;
    while (entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR < min_size) entries_size *= 2;
    ihtab_size_t els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;
    htab->els_num = 0;
    auto &b = htab->bin;
    b.els_bound = 0;
    b.els = (El *) std::malloc (els_size * sizeof (El));
    ihtab_size_t del_bytes = (els_size + 7) / 8;
    b.deleted = (char *) std::calloc (del_bytes, 1);
    b.h7 = (unsigned char *) std::aligned_alloc (IHTAB_GROUP_SIZE, entries_size);
    std::memset (b.h7, IHTAB_EMPTY_H7, entries_size);
    b.entries = (ihtab_ind_t *) std::malloc (entries_size * sizeof (ihtab_ind_t));
    b.groups_mask = entries_size / IHTAB_GROUP_SIZE - 1;
  }

  static void destroy_bin (hbin_ihtab_t<El> &b) {
    std::free (b.els);
    std::free (b.deleted);
    std::free (b.h7);
    std::free (b.entries);
  }

  static void destroy (ihtab_t *htab) {
    destroy_bin (htab->bin);
    htab->els_num = 0;
  }

  static FORCE_INLINE bool do_1 (ihtab_t *htab, hbin_ihtab_t<El> &bin, El &el,
				 enum ihtab_action action, El **res) {
    Hash hash_fn;
    Eq eq_fn;
    ihtab_hash_t hash = hash_fn (el);
    unsigned char h7_val = (hash >> (sizeof (size_t) * 8 - 7)) & 0x7f;
    ihtab_size_t group_ind = (hash / IHTAB_GROUP_SIZE) & bin.groups_mask;
    ihtab_size_t first_deleted_slot = ~(ihtab_size_t) 0;

    for (;;) {
      unsigned char *group_h7 = bin.h7 + group_ind * IHTAB_GROUP_SIZE;
      ihtab_group_t group = ihtab_group_load (group_h7);
      uint64_t match_mask = ihtab_match_mask (group, h7_val);
      while (match_mask) {
        unsigned int bit = __builtin_ctzll (match_mask);
	if (mask_scale) bit /= 8;
        ihtab_size_t slot = group_ind * IHTAB_GROUP_SIZE + bit;
        ihtab_ind_t el_ind = bin.entries[slot];
        if (el_ind == IHTAB_ENTRY_DELETED) {
          if (first_deleted_slot == ~(ihtab_size_t) 0)
            first_deleted_slot = slot;
        } else if (eq_fn (bin.els[el_ind], el)) {
          if (action != IHTAB_DELETE) {
            *res = &bin.els[el_ind];
          } else {
            htab->els_num--;
            bin.deleted[el_ind / 8] |= 1 << (el_ind % 8);
            bin.entries[slot] = IHTAB_ENTRY_DELETED;
          }
          return true;
        }
        match_mask &= match_mask - 1;
      }

      uint64_t empty_mask = ihtab_match_empty (group);
      if (empty_mask) {
        if (action == IHTAB_INSERT || action == IHTAB_REPLACE) {
          htab->els_num++;
          ihtab_size_t slot;
          if (first_deleted_slot != ~(ihtab_size_t) 0) {
            slot = first_deleted_slot;
          } else {
            unsigned int bit = __builtin_ctzll (empty_mask);
	    if (mask_scale) bit /= 8;
            slot = group_ind * IHTAB_GROUP_SIZE + bit;
          }
          bin.h7[slot] = h7_val;
          bin.entries[slot] = (ihtab_ind_t) bin.els_bound;
          *res = &bin.els[bin.els_bound];
          bin.els_bound++;
        }
        return false;
      }

      group_ind = (group_ind + 1) & bin.groups_mask;
    }
  }

  static void rebuild (ihtab_t *htab) {
    ihtab_size_t entries_size = (htab->bin.groups_mask + 1) * IHTAB_GROUP_SIZE;
    ihtab_size_t els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;
    if (2 * IHTAB_LF_DIVISOR * htab->els_num >= IHTAB_LF_FACTOR * entries_size) {
      entries_size *= 2;
      els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;
    }
    hbin_ihtab_t<El> resize_bin;
    resize_bin.els = (El *) std::malloc (els_size * sizeof (El));
    ihtab_size_t del_bytes = (els_size + 7) / 8;
    resize_bin.deleted = (char *) std::calloc (del_bytes, 1);
    resize_bin.h7 = (unsigned char *) std::aligned_alloc (IHTAB_GROUP_SIZE, entries_size);
    std::memset (resize_bin.h7, IHTAB_EMPTY_H7, entries_size);
    resize_bin.entries = (ihtab_ind_t *) std::malloc (entries_size * sizeof (ihtab_ind_t));
    resize_bin.groups_mask = entries_size / IHTAB_GROUP_SIZE - 1;
    resize_bin.els_bound = 0;
    ihtab_size_t bound = htab->bin.els_bound;
    ihtab_size_t saved_els_num = htab->els_num;
    for (ihtab_size_t i = 0; i < bound; i++)
      if (!(htab->bin.deleted[i / 8] & (1 << (i % 8)))) {
        El *r;
        do_1 (htab, resize_bin, htab->bin.els[i], IHTAB_INSERT, &r);
        *r = htab->bin.els[i];
      }
    htab->els_num = saved_els_num;
    destroy_bin (htab->bin);
    htab->bin = resize_bin;
  }

  static FORCE_INLINE bool do_ (ihtab_t *htab, El &el, enum ihtab_action action, El **res) {
    ihtab_size_t entries_size = (htab->bin.groups_mask + 1) * IHTAB_GROUP_SIZE;
    ihtab_size_t els_size = entries_size * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;
    if (action != IHTAB_DELETE && __builtin_expect(htab->bin.els_bound >= els_size, 0))
      rebuild (htab);
    return do_1 (htab, htab->bin, el, action, res);
  }

  static ihtab_size_t size (ihtab_t *htab) {
    return (htab->bin.groups_mask + 1) * IHTAB_GROUP_SIZE * IHTAB_LF_FACTOR / IHTAB_LF_DIVISOR;
  }

  struct iterator {
    ihtab_size_t el_idx;
    El *ptr;
  };

  static FORCE_INLINE void iter_advance (ihtab_t *htab, iterator &it) {
    while (it.el_idx < htab->bin.els_bound) {
      if (!(htab->bin.deleted[it.el_idx / 8] & (1 << (it.el_idx % 8)))) {
        it.ptr = &htab->bin.els[it.el_idx];
        return;
      }
      ++it.el_idx;
    }
    it.ptr = nullptr;
  }

  static FORCE_INLINE iterator iter_begin (ihtab_t *htab) {
    iterator it;
    it.el_idx = 0;
    it.ptr = nullptr;
    iter_advance (htab, it);
    return it;
  }

  static FORCE_INLINE bool iter_valid (iterator &it) {
    return it.ptr != nullptr;
  }

  static FORCE_INLINE void iter_next (ihtab_t *htab, iterator &it) {
    ++it.el_idx;
    iter_advance (htab, it);
  }
};

#endif /* #ifndef IHTAB_H */
