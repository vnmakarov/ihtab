#ifndef IXHTAB_H
#define IXHTAB_H

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <iterator>

#ifndef IXHTAB_FORCE_INLINE
#define IXHTAB_FORCE_INLINE __attribute__ ((always_inline)) inline
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif

namespace ixht {

typedef uint16_t ind_t;
typedef size_t hash_t;
typedef unsigned short depth_t;
typedef unsigned int ebin_ind_t;

static constexpr unsigned int GROUP_SIZE = 8;
static constexpr unsigned char EMPTY_H7 = 0x80;
static constexpr unsigned char DELETED_H7 = 0xfe;
static constexpr ind_t ENTRY_DELETED = ~(ind_t) 0;
static constexpr unsigned int MAX_BIN_SIZE_POWER = 15;

static_assert (MAX_BIN_SIZE_POWER <= 15, "bin size must fit in uint16_t entries");

enum action { FIND, DELETE, INSERT, REPLACE };

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

static const bool mask_scale = false;
typedef __m128i group_t;

static IXHTAB_FORCE_INLINE group_t group_load (const unsigned char *p) {
  return _mm_cvtsi64_si128 (*(const long long *) p);
}

static IXHTAB_FORCE_INLINE unsigned int match_mask (group_t g, unsigned char h7_val) {
  __m128i h7_vec = _mm_set1_epi8 ((char) h7_val);
  return (unsigned int) _mm_movemask_epi8 (_mm_cmpeq_epi8 (g, h7_vec)) & 0xff;
}

static IXHTAB_FORCE_INLINE unsigned int match_empty (group_t g) {
  return (unsigned int) _mm_movemask_epi8 (g) & 0xff;
}

#elif defined(__aarch64__) || defined(_M_ARM64)

static const bool mask_scale = false;
typedef uint64_t group_t;

static IXHTAB_FORCE_INLINE group_t group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}

static IXHTAB_FORCE_INLINE unsigned int match_mask (group_t g, unsigned char h7_val) {
  uint8x8_t group = vcreate_u8 (g);
  uint8x8_t match_eq = vceq_u8 (group, vdup_n_u8 (h7_val));
  static const uint8x8_t bit_mask = {1, 2, 4, 8, 16, 32, 64, 128};
  return (unsigned int) vaddv_u8 (vand_u8 (match_eq, bit_mask));
}

static IXHTAB_FORCE_INLINE unsigned int match_empty (group_t g) {
  return match_mask (g, EMPTY_H7);
}

#else

static const bool mask_scale = true;
static constexpr uint64_t SWAR_LSB = 0x0101010101010101ULL;
static constexpr uint64_t SWAR_MSB = 0x8080808080808080ULL;
typedef uint64_t group_t;

static IXHTAB_FORCE_INLINE group_t group_load (const unsigned char *p) {
  return *(const uint64_t *) p;
}

static IXHTAB_FORCE_INLINE uint64_t match_mask (group_t g, unsigned char h7_val) {
  uint64_t cmp = g ^ (SWAR_LSB * h7_val);
  return (cmp - SWAR_LSB) & ~cmp & SWAR_MSB;
}

static IXHTAB_FORCE_INLINE uint64_t match_empty (group_t g) { return g & SWAR_MSB; }

#endif

template <typename El>
struct ebin_t {
  depth_t depth;
  hash_t mask;
  unsigned int els_start, els_bound;
  El *els;
  char *deleted;
  unsigned char *h7;
  ind_t *entries;
  unsigned int groups_mask;
};

template <typename El, typename Hash, typename Eq>
class ixhtab {
  unsigned int els_num;
  depth_t max_depth;
  hash_t bin_mask;
  ebin_ind_t *dir;
  unsigned int dir_capacity;
  ebin_t<El> *bins;
  unsigned int bins_num;
  unsigned int bins_capacity;

  ebin_ind_t create_bin (unsigned int size) {
    if (size < GROUP_SIZE) size = GROUP_SIZE;
    if (bins_num == bins_capacity) {
      bins_capacity = bins_capacity ? bins_capacity * 2 : 4;
      bins = (ebin_t<El> *) std::realloc (bins, bins_capacity * sizeof (ebin_t<El>));
    }
    ebin_ind_t ind = bins_num++;
    auto &b = bins[ind];
    b.els_start = b.els_bound = 0;
    b.els = (El *) std::malloc (size * sizeof (El));
    unsigned int del_bytes = (size + 7) / 8;
    b.deleted = (char *) std::calloc (del_bytes, 1);
    unsigned int entries_size = 2 * size;
    b.h7 = (unsigned char *) std::aligned_alloc (GROUP_SIZE, entries_size);
    std::memset (b.h7, EMPTY_H7, entries_size);
    b.entries = (ind_t *) std::malloc (entries_size * sizeof (ind_t));
    b.groups_mask = entries_size / GROUP_SIZE - 1;
    return ind;
  }

  static void destroy_bin (ebin_t<El> &b) {
    std::free (b.els);
    std::free (b.deleted);
    std::free (b.h7);
    std::free (b.entries);
  }

  static void get_params (size_t size, size_t &bins_num, size_t &bin_power2, size_t &bin_size) {
    bin_size = size;
    bins_num = 1;
    bin_power2 = 0;
    while (bin_size >= (1u << MAX_BIN_SIZE_POWER)) {
      bins_num *= 2;
      bin_size /= 2;
      bin_power2++;
    }
  }

 public:
  ixhtab (unsigned int min_size = 8) {
    unsigned int size;
    for (size = 2; min_size > size; size *= 2);
    els_num = 0;
    bins = nullptr;
    bins_num = 0;
    bins_capacity = 0;
    size_t bn, bin_power2, bin_size;
    get_params (size, bn, bin_power2, bin_size);
    max_depth = (depth_t) bin_power2;
    bin_mask = ((hash_t) 1 << bin_power2) - 1;
    dir = (ebin_ind_t *) std::malloc (bn * sizeof (ebin_ind_t));
    dir_capacity = (unsigned int) bn;
    for (size_t i = 0; i < bn; i++) {
      dir[i] = (ebin_ind_t) i;
      ebin_ind_t ind = create_bin ((unsigned int) bin_size);
      bins[ind].depth = (depth_t) bin_power2;
      bins[ind].mask = (hash_t) i;
    }
  }

  ~ixhtab () {
    for (unsigned int i = 0; i < bins_num; i++) destroy_bin (bins[i]);
    std::free (bins);
    std::free (dir);
  }

 private:
  IXHTAB_FORCE_INLINE bool do_1 (ebin_t<El> &bin, hash_t hash, El &el,
                                 enum action action, El **res) {
    Eq eq_fn;
    unsigned char h7_val = (hash >> (sizeof (hash_t) * 8 - 7)) & 0x7f;
    unsigned int group_ind = (unsigned int) (hash / GROUP_SIZE) & bin.groups_mask;
    unsigned int first_deleted_slot = ~0u;

    for (;;) {
      unsigned char *group_h7 = bin.h7 + group_ind * GROUP_SIZE;
      group_t group = group_load (group_h7);
      auto mmask = match_mask (group, h7_val);
      while (mmask) {
        unsigned int bit = __builtin_ctzll (mmask);
        if (mask_scale) bit /= 8;
        unsigned int slot = group_ind * GROUP_SIZE + bit;
        ind_t el_ind = bin.entries[slot];
        if (el_ind == ENTRY_DELETED) {
          if (first_deleted_slot == ~0u) first_deleted_slot = slot;
        } else if (eq_fn (bin.els[el_ind], el)) {
          if (action != DELETE) {
            *res = &bin.els[el_ind];
          } else {
            els_num--;
            bin.deleted[el_ind / 8] |= 1 << (el_ind % 8);
            bin.entries[slot] = ENTRY_DELETED;
          }
          return true;
        }
        mmask &= mmask - 1;
      }

      auto empty_mask = match_empty (group);
      if (empty_mask) {
        if (action >= INSERT) {
          els_num++;
          unsigned int slot;
          if (first_deleted_slot != ~0u) {
            slot = first_deleted_slot;
          } else {
            unsigned int bit = __builtin_ctzll (empty_mask);
            if (mask_scale) bit /= 8;
            slot = group_ind * GROUP_SIZE + bit;
          }
          bin.h7[slot] = h7_val;
          bin.entries[slot] = (ind_t) bin.els_bound;
          *res = &bin.els[bin.els_bound];
          bin.els_bound++;
        }
        return false;
      }

      group_ind = (group_ind + 1) & bin.groups_mask;
    }
  }

  void split_bin (ebin_ind_t bin_ind) {
    unsigned int size = (bins[bin_ind].groups_mask + 1) * GROUP_SIZE / 2;
    ebin_ind_t new_ind = create_bin (size);
    Hash hash_fn;
    for (;;) {
      auto &new_bin = bins[new_ind];
      auto &bin = bins[bin_ind];
      unsigned int entries_size = (bin.groups_mask + 1) * GROUP_SIZE;
      std::memset (bin.h7, EMPTY_H7, entries_size);
      hash_t split_mask = (hash_t) 1 << bin.depth;
      new_bin.depth = ++bin.depth;
      if (bin.depth > max_depth) {
        max_depth = bin.depth;
        bin_mask = ~(~(hash_t) 0 << max_depth);
        unsigned int len = dir_capacity;
        dir = (ebin_ind_t *) std::realloc (dir, 2 * len * sizeof (ebin_ind_t));
        dir_capacity = 2 * len;
        for (unsigned int j = 0; j < len; j++) dir[j + len] = dir[j];
      }
      new_bin.mask = bin.mask | split_mask;
      dir[new_bin.mask] = new_ind;
      unsigned int start = bin.els_start, bound = bin.els_bound;
      bin.els_start = bin.els_bound = 0;
      unsigned int saved_els_num = els_num;
      bool old_added = false, new_added = false;
      for (unsigned int i = start; i < bound; i++) {
        if (bin.deleted[i / 8] & (1 << (i % 8))) continue;
        hash_t hash = hash_fn (bin.els[i]);
        if (hash == 0) hash = 1;
        bool is_old = (hash & split_mask) == 0;
        if (is_old)
          old_added = true;
        else
          new_added = true;
        El *r;
        do_1 (is_old ? bins[bin_ind] : bins[new_ind], hash, bin.els[i], INSERT, &r);
        *r = bin.els[i];
      }
      els_num = saved_els_num;
      if (!old_added) {
        ebin_ind_t temp = bin_ind;
        bin_ind = new_ind;
        new_ind = temp;
      } else if (new_added) {
        break;
      }
    }
    auto &ob = bins[bin_ind];
    std::memset (ob.deleted, 0, ((ob.groups_mask + 1) * GROUP_SIZE / 2 + 7) / 8);
    auto &nb = bins[new_ind];
    std::memset (nb.deleted, 0, ((nb.groups_mask + 1) * GROUP_SIZE / 2 + 7) / 8);
  }

 public:
  IXHTAB_FORCE_INLINE bool perform (El &el, enum action action, El **res) {
    Hash hash_fn;
    hash_t hash = hash_fn (el);
    if (hash == 0) hash = 1;
    hash_t dir_ind = hash & bin_mask;
    ebin_ind_t bin_ind = dir[dir_ind];
    auto &bin = bins[bin_ind];
    if (action >= INSERT) {
      unsigned int entries_size = (bin.groups_mask + 1) * GROUP_SIZE;
      unsigned int els_size = entries_size / 2;
      if (bin.els_bound == els_size) {
        bool grow = false;
        if (2 * els_num >= entries_size) {
          entries_size *= 2;
          els_size *= 2;
          grow = true;
        }
        if (grow && els_size >= (1u << MAX_BIN_SIZE_POWER)) {
          split_bin (bin_ind);
          bin_ind = dir[hash & bin_mask];
        } else {
          auto &b = bins[bin_ind];
          char *old_deleted = b.deleted;
          unsigned int del_bytes = (els_size + 7) / 8;
          b.deleted = (char *) std::calloc (del_bytes, 1);
          b.els = (El *) std::realloc (b.els, els_size * sizeof (El));
          b.h7 = (unsigned char *) std::realloc (b.h7, entries_size);
          std::memset (b.h7, EMPTY_H7, entries_size);
          b.entries = (ind_t *) std::realloc (b.entries, entries_size * sizeof (ind_t));
          b.groups_mask = entries_size / GROUP_SIZE - 1;
          unsigned int start = b.els_start, bound = b.els_bound;
          b.els_start = b.els_bound = 0;
          unsigned int saved_els_num = els_num;
          els_num = 0;
          for (unsigned int i = start; i < bound; i++) {
            if (old_deleted[i / 8] & (1 << (i % 8))) continue;
            hash_t hash2 = hash_fn (b.els[i]);
            if (hash2 == 0) hash2 = 1;
            El *r;
            do_1 (b, hash2, b.els[i], INSERT, &r);
            *r = b.els[i];
          }
          els_num = saved_els_num;
          std::free (old_deleted);
        }
      }
    }
    return do_1 (bins[bin_ind], hash, el, action, res);
  }

  unsigned int els_count () const { return els_num; }

  struct iter {
    unsigned int bin_idx;
    unsigned int el_idx;
    El *ptr;
  };

  IXHTAB_FORCE_INLINE void iter_advance (iter &it) {
    while (it.bin_idx < bins_num) {
      auto &b = bins[it.bin_idx];
      while (it.el_idx < b.els_bound) {
        if (!(b.deleted[it.el_idx / 8] & (1 << (it.el_idx % 8)))) {
          it.ptr = &b.els[it.el_idx];
          return;
        }
        ++it.el_idx;
      }
      ++it.bin_idx;
      if (it.bin_idx < bins_num) it.el_idx = bins[it.bin_idx].els_start;
    }
    it.ptr = nullptr;
  }

  IXHTAB_FORCE_INLINE iter iter_begin () {
    iter it;
    it.bin_idx = 0;
    it.el_idx = bins_num > 0 ? bins[0].els_start : 0;
    it.ptr = nullptr;
    iter_advance (it);
    return it;
  }

  static IXHTAB_FORCE_INLINE bool iter_valid (iter &it) { return it.ptr != nullptr; }

  IXHTAB_FORCE_INLINE void iter_next (iter &it) {
    ++it.el_idx;
    iter_advance (it);
  }

  struct iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = El;
    using difference_type = std::ptrdiff_t;
    using pointer = El *;
    using reference = El &;

    ixhtab *tab;
    unsigned int bin_idx;
    unsigned int el_idx;

    IXHTAB_FORCE_INLINE void advance () {
      while (bin_idx < tab->bins_num) {
        auto &b = tab->bins[bin_idx];
        while (el_idx < b.els_bound) {
          if (!(b.deleted[el_idx / 8] & (1 << (el_idx % 8)))) return;
          ++el_idx;
        }
        ++bin_idx;
        el_idx = bin_idx < tab->bins_num ? tab->bins[bin_idx].els_start : 0;
      }
    }

    IXHTAB_FORCE_INLINE El &operator* () const { return tab->bins[bin_idx].els[el_idx]; }
    IXHTAB_FORCE_INLINE El *operator->() const { return &tab->bins[bin_idx].els[el_idx]; }
    IXHTAB_FORCE_INLINE iterator &operator++ () {
      ++el_idx;
      advance ();
      return *this;
    }
    IXHTAB_FORCE_INLINE iterator operator++ (int) {
      iterator t = *this;
      ++(*this);
      return t;
    }
    IXHTAB_FORCE_INLINE bool operator== (const iterator &o) const {
      return bin_idx == o.bin_idx && el_idx == o.el_idx;
    }
    IXHTAB_FORCE_INLINE bool operator!= (const iterator &o) const { return !(*this == o); }
  };

  IXHTAB_FORCE_INLINE iterator begin () {
    iterator it{this, 0, bins_num > 0 ? bins[0].els_start : 0};
    it.advance ();
    return it;
  }

  IXHTAB_FORCE_INLINE iterator end () { return {this, bins_num, 0}; }
};

} // namespace ixht

#endif /* #ifndef IXHTAB_H */
