#ifndef IXHTAB_H
#define IXHTAB_H

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>

#define FORCE_INLINE __attribute__((always_inline)) inline

typedef uint16_t ixhtab_ind_t;
typedef size_t ixhtab_hash_t;
typedef unsigned short ixhtab_depth_t;
typedef unsigned int ebin_ixhtab_ind_t;

static constexpr unsigned int IXHTAB_GROUP_SIZE = 8;
static constexpr unsigned char IXHTAB_EMPTY_H7 = 0x80;
static constexpr unsigned char IXHTAB_DELETED_H7 = 0xfe;
static constexpr ixhtab_ind_t IXHTAB_ENTRY_DELETED = ~(ixhtab_ind_t) 0;
static constexpr unsigned int IXHTAB_MAX_BIN_SIZE_POWER = 15;

static_assert (IXHTAB_MAX_BIN_SIZE_POWER <= 15, "bin size must fit in uint16_t entries");

enum ixhtab_action { IXHTAB_FIND, IXHTAB_INSERT, IXHTAB_REPLACE, IXHTAB_DELETE };

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

#include <immintrin.h>
static const bool ixhtab_mask_scale = false;
typedef __m128i ixhtab_group_t;

static FORCE_INLINE ixhtab_group_t ixhtab_group_load (const unsigned char *p) {
  return _mm_cvtsi64_si128 (*(const long long *) p);
}

static FORCE_INLINE unsigned int ixhtab_match_mask (ixhtab_group_t g,
                                                unsigned char h7_val) {
  __m128i h7_vec = _mm_set1_epi8 ((char) h7_val);
  return (unsigned int) _mm_movemask_epi8 (_mm_cmpeq_epi8 (g, h7_vec)) & 0xff;
}

static FORCE_INLINE unsigned int ixhtab_match_empty (ixhtab_group_t g) {
  return (unsigned int) _mm_movemask_epi8 (g) & 0xff;
}

#elif defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>
static const bool ixhtab_mask_scale = false;
typedef uint64_t ixhtab_group_t;

static FORCE_INLINE ixhtab_group_t ixhtab_group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}

static FORCE_INLINE unsigned int ixhtab_match_mask (ixhtab_group_t g,
                                                unsigned char h7_val) {
  uint8x8_t group = vcreate_u8 (g);
  uint8x8_t match_eq = vceq_u8 (group, vdup_n_u8 (h7_val));
  static const uint8x8_t bit_mask = {1, 2, 4, 8, 16, 32, 64, 128};
  return (unsigned int) vaddv_u8 (vand_u8 (match_eq, bit_mask));
}

static FORCE_INLINE unsigned int ixhtab_match_empty (ixhtab_group_t g) {
  return ixhtab_match_mask (g, IXHTAB_EMPTY_H7);
}

#else

static const bool ixhtab_mask_scale = true;
static constexpr uint64_t IXHTAB_SWAR_LSB = 0x0101010101010101ULL;
static constexpr uint64_t IXHTAB_SWAR_MSB = 0x8080808080808080ULL;
typedef uint64_t ixhtab_group_t;

static FORCE_INLINE ixhtab_group_t ixhtab_group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}

static FORCE_INLINE uint64_t ixhtab_match_mask (ixhtab_group_t g,
                                            unsigned char h7_val) {
  uint64_t cmp = g ^ (IXHTAB_SWAR_LSB * h7_val);
  return (cmp - IXHTAB_SWAR_LSB) & ~cmp & IXHTAB_SWAR_MSB;
}

static FORCE_INLINE uint64_t ixhtab_match_empty (ixhtab_group_t g) {
  return g & IXHTAB_SWAR_MSB;
}

#endif

template<typename El>
struct ebin_ixhtab_t {
  ixhtab_depth_t depth;
  ixhtab_hash_t mask;
  unsigned int els_start, els_bound;
  El *els;
  char *deleted;
  unsigned char *h7;
  ixhtab_ind_t *entries;
  unsigned int groups_mask;
};

template<typename El, typename Hash, typename Eq>
struct ixhtab_t {
  unsigned int els_num;
  ixhtab_depth_t max_depth;
  ixhtab_hash_t bin_mask;
  ebin_ixhtab_ind_t *dir;
  unsigned int dir_capacity;
  ebin_ixhtab_t<El> *bins;
  unsigned int bins_num;
  unsigned int bins_capacity;

  static ebin_ixhtab_ind_t create_bin (ixhtab_t *htab, unsigned int size) {
    if (size < IXHTAB_GROUP_SIZE) size = IXHTAB_GROUP_SIZE;
    if (htab->bins_num == htab->bins_capacity) {
      htab->bins_capacity = htab->bins_capacity ? htab->bins_capacity * 2 : 4;
      htab->bins = (ebin_ixhtab_t<El> *) std::realloc (htab->bins,
                                                      htab->bins_capacity * sizeof (ebin_ixhtab_t<El>));
    }
    ebin_ixhtab_ind_t ind = htab->bins_num++;
    auto &b = htab->bins[ind];
    b.els_start = b.els_bound = 0;
    b.els = (El *) std::malloc (size * sizeof (El));
    unsigned int del_bytes = (size + 7) / 8;
    b.deleted = (char *) std::calloc (del_bytes, 1);
    unsigned int entries_size = 2 * size;
    b.h7 = (unsigned char *) std::aligned_alloc (IXHTAB_GROUP_SIZE, entries_size);
    std::memset (b.h7, IXHTAB_EMPTY_H7, entries_size);
    b.entries = (ixhtab_ind_t *) std::malloc (entries_size * sizeof (ixhtab_ind_t));
    b.groups_mask = entries_size / IXHTAB_GROUP_SIZE - 1;
    return ind;
  }

  static void destroy_bin (ebin_ixhtab_t<El> &b) {
    std::free (b.els);
    std::free (b.deleted);
    std::free (b.h7);
    std::free (b.entries);
  }

  static void get_params (size_t size, size_t &bins_num, size_t &bin_power2, size_t &bin_size) {
    bin_size = size; bins_num = 1; bin_power2 = 0;
    while (bin_size >= (1u << IXHTAB_MAX_BIN_SIZE_POWER)) {
      bins_num *= 2;
      bin_size /= 2;
      bin_power2++;
    }
  }

  static void create (ixhtab_t *htab, unsigned int min_size) {
    unsigned int size;
    for (size = 2; min_size > size; size *= 2);
    htab->els_num = 0;
    htab->bins = nullptr;
    htab->bins_num = 0;
    htab->bins_capacity = 0;
    size_t bins_num, bin_power2, bin_size;
    get_params (size, bins_num, bin_power2, bin_size);
    htab->max_depth = (ixhtab_depth_t) bin_power2;
    htab->bin_mask = ((ixhtab_hash_t) 1 << bin_power2) - 1;
    htab->dir = (ebin_ixhtab_ind_t *) std::malloc (bins_num * sizeof (ebin_ixhtab_ind_t));
    htab->dir_capacity = (unsigned int) bins_num;
    for (size_t i = 0; i < bins_num; i++) {
      htab->dir[i] = (ebin_ixhtab_ind_t) i;
      ebin_ixhtab_ind_t ind = create_bin (htab, (unsigned int) bin_size);
      htab->bins[ind].depth = (ixhtab_depth_t) bin_power2;
      htab->bins[ind].mask = (ixhtab_hash_t) i;
    }
  }

  static void destroy (ixhtab_t *htab) {
    for (unsigned int i = 0; i < htab->bins_num; i++)
      destroy_bin (htab->bins[i]);
    std::free (htab->bins);
    std::free (htab->dir);
    htab->els_num = 0;
  }

  static FORCE_INLINE bool do_1 (ixhtab_t *htab, ebin_ixhtab_t<El> &bin, ixhtab_hash_t hash, El &el,
				 enum ixhtab_action action, El **res) {
    Eq eq_fn;
    unsigned char h7_val = (hash >> (sizeof (size_t) * 8 - 7)) & 0x7f;
    unsigned int group_ind = (unsigned int) (hash / IXHTAB_GROUP_SIZE) & bin.groups_mask;
    unsigned int first_deleted_slot = ~0u;

    for (;;) {
      unsigned char *group_h7 = bin.h7 + group_ind * IXHTAB_GROUP_SIZE;
      ixhtab_group_t group = ixhtab_group_load (group_h7);
      auto match_mask = ixhtab_match_mask (group, h7_val);
      while (match_mask) {
        unsigned int bit = __builtin_ctzll (match_mask);
        if (ixhtab_mask_scale) bit /= 8;
        unsigned int slot = group_ind * IXHTAB_GROUP_SIZE + bit;
        ixhtab_ind_t el_ind = bin.entries[slot];
        if (el_ind == IXHTAB_ENTRY_DELETED) {
          if (first_deleted_slot == ~0u)
            first_deleted_slot = slot;
        } else if (eq_fn (bin.els[el_ind], el)) {
          if (action != IXHTAB_DELETE) {
            *res = &bin.els[el_ind];
          } else {
            htab->els_num--;
            bin.deleted[el_ind / 8] |= 1 << (el_ind % 8);
            bin.entries[slot] = IXHTAB_ENTRY_DELETED;
          }
          return true;
        }
        match_mask &= match_mask - 1;
      }

      auto empty_mask = ixhtab_match_empty (group);
      if (empty_mask) {
        if (action == IXHTAB_INSERT || action == IXHTAB_REPLACE) {
          htab->els_num++;
          unsigned int slot;
          if (first_deleted_slot != ~0u) {
            slot = first_deleted_slot;
          } else {
            unsigned int bit = __builtin_ctzll (empty_mask);
            if (ixhtab_mask_scale) bit /= 8;
            slot = group_ind * IXHTAB_GROUP_SIZE + bit;
          }
          bin.h7[slot] = h7_val;
          bin.entries[slot] = (ixhtab_ind_t) bin.els_bound;
          *res = &bin.els[bin.els_bound];
          bin.els_bound++;
        }
        return false;
      }

      group_ind = (group_ind + 1) & bin.groups_mask;
    }
  }

  static void split_bin (ixhtab_t *htab, ebin_ixhtab_ind_t bin_ind) {
    unsigned int size = (htab->bins[bin_ind].groups_mask + 1) * IXHTAB_GROUP_SIZE / 2;
    ebin_ixhtab_ind_t new_ind = create_bin (htab, size);
    Hash hash_fn;
    for (;;) {
      auto &new_bin = htab->bins[new_ind];
      auto &bin = htab->bins[bin_ind];
      unsigned int entries_size = (bin.groups_mask + 1) * IXHTAB_GROUP_SIZE;
      std::memset (bin.h7, IXHTAB_EMPTY_H7, entries_size);
      ixhtab_hash_t split_mask = (ixhtab_hash_t) 1 << bin.depth;
      new_bin.depth = ++bin.depth;
      if (bin.depth > htab->max_depth) {
        htab->max_depth = bin.depth;
        htab->bin_mask = ~(~(ixhtab_hash_t) 0 << htab->max_depth);
        unsigned int len = htab->dir_capacity;
        htab->dir = (ebin_ixhtab_ind_t *) std::realloc (htab->dir, 2 * len * sizeof (ebin_ixhtab_ind_t));
        htab->dir_capacity = 2 * len;
        for (unsigned int j = 0; j < len; j++) htab->dir[j + len] = htab->dir[j];
      }
      new_bin.mask = bin.mask | split_mask;
      htab->dir[new_bin.mask] = new_ind;
      unsigned int start = bin.els_start, bound = bin.els_bound;
      bin.els_start = bin.els_bound = 0;
      unsigned int els_num = htab->els_num;
      bool old_added = false, new_added = false;
      for (unsigned int i = start; i < bound; i++) {
        if (bin.deleted[i / 8] & (1 << (i % 8))) continue;
        ixhtab_hash_t hash = hash_fn (bin.els[i]);
        if (hash == 0) hash = 1;
        bool is_old = (hash & split_mask) == 0;
        if (is_old) old_added = true; else new_added = true;
        El *r;
        do_1 (htab, is_old ? htab->bins[bin_ind] : htab->bins[new_ind],
              hash, bin.els[i], IXHTAB_INSERT, &r);
        *r = bin.els[i];
      }
      htab->els_num = els_num;
      if (!old_added) {
        ebin_ixhtab_ind_t temp = bin_ind;
        bin_ind = new_ind;
        new_ind = temp;
      } else if (new_added) {
        break;
      }
    }
    auto &ob = htab->bins[bin_ind];
    std::memset (ob.deleted, 0, ((ob.groups_mask + 1) * IXHTAB_GROUP_SIZE / 2 + 7) / 8);
    auto &nb = htab->bins[new_ind];
    std::memset (nb.deleted, 0, ((nb.groups_mask + 1) * IXHTAB_GROUP_SIZE / 2 + 7) / 8);
  }

  static FORCE_INLINE bool do_ (ixhtab_t *htab, El &el, enum ixhtab_action action, El **res) {
    Hash hash_fn;
    ixhtab_hash_t hash = hash_fn (el);
    if (hash == 0) hash = 1;
    ixhtab_hash_t dir_ind = hash & htab->bin_mask;
    ebin_ixhtab_ind_t bin_ind = htab->dir[dir_ind];
    auto &bin = htab->bins[bin_ind];
    unsigned int entries_size = (bin.groups_mask + 1) * IXHTAB_GROUP_SIZE;
    unsigned int els_size = entries_size / 2;
    if ((action == IXHTAB_INSERT || action == IXHTAB_REPLACE) && bin.els_bound == els_size) {
      bool grow = false;
      if (2 * htab->els_num >= entries_size) {
        entries_size *= 2;
        els_size *= 2;
        grow = true;
      }
      if (grow && els_size >= (1u << IXHTAB_MAX_BIN_SIZE_POWER)) {
        split_bin (htab, bin_ind);
        bin_ind = htab->dir[hash & htab->bin_mask];
      } else {
        auto &b = htab->bins[bin_ind];
        char *old_deleted = b.deleted;
        unsigned int del_bytes = (els_size + 7) / 8;
        b.deleted = (char *) std::calloc (del_bytes, 1);
        b.els = (El *) std::realloc (b.els, els_size * sizeof (El));
        b.h7 = (unsigned char *) std::realloc (b.h7, entries_size);
        std::memset (b.h7, IXHTAB_EMPTY_H7, entries_size);
        b.entries = (ixhtab_ind_t *) std::realloc (b.entries,
                                                      entries_size * sizeof (ixhtab_ind_t));
        b.groups_mask = entries_size / IXHTAB_GROUP_SIZE - 1;
        unsigned int start = b.els_start, bound = b.els_bound;
        b.els_start = b.els_bound = 0;
        htab->els_num = 0;
        for (unsigned int i = start; i < bound; i++) {
          if (old_deleted[i / 8] & (1 << (i % 8))) continue;
          ixhtab_hash_t hash2 = hash_fn (b.els[i]);
          if (hash2 == 0) hash2 = 1;
          El *r;
          do_1 (htab, b, hash2, b.els[i], IXHTAB_INSERT, &r);
          *r = b.els[i];
        }
        std::free (old_deleted);
      }
    }
    return do_1 (htab, htab->bins[bin_ind], hash, el, action, res);
  }
  struct iterator {
    unsigned int bin_idx;
    unsigned int el_idx;
    El *ptr;
  };

  static FORCE_INLINE void iter_advance (ixhtab_t *htab, iterator &it) {
    while (it.bin_idx < htab->bins_num) {
      auto &bin = htab->bins[it.bin_idx];
      while (it.el_idx < bin.els_bound) {
        if (!(bin.deleted[it.el_idx / 8] & (1 << (it.el_idx % 8)))) {
          it.ptr = &bin.els[it.el_idx];
          return;
        }
        ++it.el_idx;
      }
      ++it.bin_idx;
      if (it.bin_idx < htab->bins_num)
        it.el_idx = htab->bins[it.bin_idx].els_start;
    }
    it.ptr = nullptr;
  }

  static FORCE_INLINE iterator iter_begin (ixhtab_t *htab) {
    iterator it;
    it.bin_idx = 0;
    it.el_idx = htab->bins_num > 0 ? htab->bins[0].els_start : 0;
    it.ptr = nullptr;
    iter_advance (htab, it);
    return it;
  }

  static FORCE_INLINE bool iter_valid (iterator &it) {
    return it.ptr != nullptr;
  }

  static FORCE_INLINE void iter_next (ixhtab_t *htab, iterator &it) {
    ++it.el_idx;
    iter_advance (htab, it);
  }
};

#endif /* #ifndef IXHTAB_H */
