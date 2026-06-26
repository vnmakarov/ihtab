![ixht](ixht.png)

# ihtab / ixhtab — single-header indexed hash tables

Two open-addressing hash tables with separate element storage, available
as C++20 templates (`ihtab.hpp`, `ixhtab.hpp`) and C11 macro-generated
headers (`ihtab.h`, `ixhtab.h`).  No build system required to use them.

## Design

These days hash tables based on [swiss
table](https://abseil.io/about/design/swisstables) design have become
very popular.  Standard swiss table is a **direct** open-address hash
table that stores key-value pairs with probing data in one array.  To
decrease memory waste on unused slots for key-value pairs, swiss table
uses a very high load factor (maximum used key-value pairs / allocated
key-value pairs).  This causes frequent cache misses and branch
mispredictions on failed lookups.  Swiss table keeps probing data in
groups and uses SIMD to speed up the search.  Within each group the
probing is linear.

Both tables here take a path different from the swiss table: a compact
array of 8-bit tags (7-bit hash fingerprint) is probed with SIMD; the
actual elements live in a **separate dense array** and are referenced
by small integer indices.  Tags and indices are interleaved in groups
of 8 (8 tag bytes followed by 8 indices) so that a single group lookup
hits one contiguous cache region.  Empty slots use tag `0xc0` (both
top bits set); deleted slots use `0x80` (top bit only); valid tags are
`0x00`–`0x7f`.  For linear probing at 50 % load the expected average
probe count is ~1 probe vs ~4 for a 7/8-loaded Swiss table for
successful search and ~2 vs 32 for unsuccessful search.  Iteration
walks the dense element array with a deleted-bit check — no empty-slot
gaps.

### ihtab

- Single contiguous element array, 32-bit slot indices.
- Interleaved groups: each group is 8 h7 tag bytes + 8 × 4-byte
  indices = 40 bytes.
- 50 % load factor; rebuilds the whole table when full.
- Fastest average lookup; suits general-purpose use.

### ixhtab

- [Extendible hash table](https://en.wikipedia.org/wiki/Extendible_hashing)
- Directory of bins, each bin is an ihtab with 16-bit indices (max 2^15
  elements per bin).
- Interleaved groups: 8 h7 tag bytes + 8 × 2-byte indices = 24 bytes.
- Only the full bin is split on overflow; the rest of the table is
  untouched.
- Trades a directory indirection for bounded rebuild latency; suits large
  tables with latency-sensitive workloads.

### Changes from v0

The original version (v0, kept as `ihtab-v0.h` / `ihtab-v0.hpp` etc.
for benchmarking) stored h7 tags and slot indices in **separate
arrays**.  The current version makes two improvements:

- **Interleaved groups.**  Each group packs 8 h7 bytes followed by 8
  indices into a single contiguous block.  A lookup that matches an h7
  tag can read the corresponding index from the same cache line instead
  of chasing a pointer to a separate array.  At 20M keys this reduces
  cycles by ~30 % vs v0.

- **Tag-based deletion.**  v0 used a sentinel index value
  (`INDEX_DELETED`) to mark deleted slots, requiring an extra compare
  after every h7 match.  The current version encodes deletion in the
  tag itself (`DELETED_H7 = 0x80`) and raises the empty tag to `0xc0`.
  Deleted slots never appear in the h7 match mask, eliminating the
  branch from the hot loop.

More information about the design can be found in this [blog post](https://vnmakarov.github.io/data%20structures/c/c++/open-source/2026/06/23/two-indexed-hash-tables.html).

## C++ Usage

Both C++ tables follow the same API.  Define an entry type that bundles key
and value, plus a hash functor and equality functor, then instantiate.

```cpp
#include "ihtab.hpp"
using namespace iht;

struct Entry { uint64_t key; int value; };
struct Hash  { hash_t operator()(const Entry &e) const { return e.key * 11400714819323198485ULL; } };
struct Eq    { bool operator()(const Entry &a, const Entry &b) const { return a.key == b.key; } };

using Tab = ihtab<Entry, Hash, Eq>;

Tab t(64);  // min_size=64; default is 8

// Insert
Entry e{42, 100};
Entry *slot;
if (!t.perform(e, INSERT, &slot)) *slot = e; // new element — write it

// Lookup
Entry key{42, 0};
if (t.perform(key, FIND, &slot)) printf("%d\n", slot->value);

// Delete
t.perform(key, DELETE, &slot);

// Iterate
for (auto &e : t) printf("%llu -> %d\n", e.key, e.value);
```

For ixhtab, use `#include "ixhtab.hpp"` with `using namespace ixht;` and
replace `ihtab` with `ixhtab`.

`perform` returns `true` when the element was found (FIND/DELETE) or already
existed (INSERT), `false` on a new insertion.  On `false` from
INSERT, `*slot` points to uninitialised memory — you must write the element.

## C Usage

Include the header and invoke the `DEFINE_IHT(El, Hash, Eq)` macro to
generate type-specific structs and functions with an `_El` suffix.
`Hash` and `Eq` are ordinary function names.

```c
#include "ihtab.h"

typedef struct { uint64_t key; int value; } entry;

static inline iht_hash_t entry_hash(entry e) { return e.key * 11400714819323198485ULL; }
static inline bool entry_eq(entry a, entry b) { return a.key == b.key; }

DEFINE_IHT(entry, entry_hash, entry_eq)

// Creates: struct iht_entry, iht_create_entry, iht_perform_entry, etc.

struct iht_entry t;
iht_create_entry(&t, 64);

// Insert
entry e = {42, 100};
entry *slot;
if (!iht_perform_entry(&t, &e, IHT_INSERT, &slot)) *slot = e;

// Lookup
entry key = {42, 0};
if (iht_perform_entry(&t, &key, IHT_FIND, &slot)) printf("%d\n", slot->value);

// Delete
iht_perform_entry(&t, &key, IHT_DELETE, &slot);

// Iterate
struct iht_iter_entry it = iht_iter_begin_entry(&t);
while (iht_iter_valid_entry(&it)) {
    printf("%llu -> %d\n", it.ptr->key, it.ptr->value);
    iht_iter_next_entry(&t, &it);
}

iht_destroy_entry(&t);
```

Replace `iht` / `IHT_*` / `DEFINE_IHT` with `ixht` / `IXHT_*` /
`DEFINE_IXHT` to use ixhtab.

## Requirements

C++20 or C11 compiler.  x86/x86-64 uses SSE2; AArch64 uses NEON; other
architectures fall back to portable SWAR bit tricks.

## Install

```sh
make install            # copies all headers to /usr/local/include
make install PREFIX=~/.local
make uninstall
```

## Test

```sh
make test               # build and run correctness tests
```

Tests cover insert, find, replace, delete, duplicate-key handling,
iteration (both C++ forward iterators and lightweight iterators), and
large tables (50 000 elements with deletions triggering rebuilds/splits).

## Benchmark

```sh
make bench              # build and run vs absl::flat_hash_map
```

Benchmarks run against `absl::flat_hash_map` with the vmum hash on
sizes Small (100), Medium (10 000), Large (1 000 000).

Geometric mean on AMD 9900x across all benchmarks (lower is better):

| Implementation | ns/op | vs absl |
|----------------|------:|--------:|
| absl           |   6.8 |  1.000x |
| C++ ixhtab     |   6.4 |  0.940x |
| C++ ihtab      |   4.2 |  0.628x |
| C ixhtab       |   6.3 |  0.933x |
| C ihtab        |   3.9 |  0.576x |
| C++ ixhtab-v0  |   6.5 |  0.958x |
| C++ ihtab-v0   |   4.0 |  0.597x |
| C ixhtab-v0    |   6.3 |  0.932x |
| C ihtab-v0     |   4.1 |  0.601x |

Geometric mean on Intel 270K+ across all benchmarks:

| Implementation | ns/op | vs absl |
|----------------|------:|--------:|
| absl           |   8.9 |  1.000x |
| C++ ixhtab     |   7.4 |  0.836x |
| C++ ihtab      |   5.3 |  0.601x |
| C ixhtab       |   7.3 |  0.825x |
| C ihtab        |   5.1 |  0.575x |
| C++ ixhtab-v0  |   7.2 |  0.809x |
| C++ ihtab-v0   |   4.8 |  0.544x |
| C ixhtab-v0    |   7.5 |  0.843x |
| C ihtab-v0     |   5.0 |  0.563x |

Geometric mean on Apple M4 across all benchmarks:

| Implementation | ns/op | vs absl |
|----------------|------:|--------:|
| absl           |   6.3 |  1.000x |
| C++ ixhtab     |   6.1 |  0.956x |
| C++ ihtab      |   4.2 |  0.661x |
| C ixhtab       |   5.6 |  0.882x |
| C ihtab        |   3.8 |  0.598x |
| C++ ixhtab-v0  |   5.6 |  0.888x |
| C++ ihtab-v0   |   4.0 |  0.637x |
| C ixhtab-v0    |   5.7 |  0.902x |
| C ihtab-v0     |   4.0 |  0.634x |

## Profile

```sh
make profile            # perf-stat comparison: ihtab vs absl vs ihtab-v0
```

Runs `perf stat` on IntLookup (searching existing keys) for two table sizes (1M and 20M keys)
comparing the current interleaved-group layout against
`absl::flat_hash_map` and the previous v0 layout (separate h7 and
index arrays).  Results on AMD 9900x:

### N=1M (1 000 000 keys, 10 iterations)

| Metric                | absl            | ihtab           | ihtab-v0        | Best                    |
|-----------------------|-----------------|-----------------|-----------------|-------------------------|
| Cycles                | 723.0M          | 265.8M          | 325.5M          | **ihtab** 63% fewer     |
| Instructions          | 519.7M          | 506.3M          | 539.9M          | **ihtab** 6% fewer      |
| IPC                   | 0.72            | 1.90            | 1.66            | **ihtab** 165% higher   |
| L1-dcache miss rate   | 25.0%           | 24.5%           | 15.4%           | **ihtab-v0** 39% fewer  |
| L1-icache miss rate   | 6.5%            | 5.9%            | 5.8%            | **ihtab-v0** 11% fewer  |
| LLC miss rate         | 47.5%           | 49.3%           | 52.5%           | **absl** 10% fewer      |
| Branch misses         | 806.4K (1.79%)  | 446.6K (0.91%)  | 408.8K (0.78%)  | **ihtab-v0** 49% fewer  |
| dTLB miss rate        | 27.4%           | 4.7%            | 2.7%            | **ihtab-v0** 90% fewer  |


### N=20M (20 000 000 keys, 10 iterations)

| Metric                | absl            | ihtab           | ihtab-v0        | Best                    |
|-----------------------|-----------------|-----------------|-----------------|-------------------------|
| Cycles                | 30.1B           | 26.6B           | 38.8B           | **ihtab** 32% fewer     |
| Instructions          | 10.7B           | 10.3B           | 12.0B           | **ihtab** 14% fewer     |
| IPC                   | 0.35            | 0.39            | 0.31            | **ihtab** 25% higher    |
| L1-dcache miss rate   | 19.5%           | 21.1%           | 14.9%           | **ihtab-v0** 30% fewer  |
| L1-icache miss rate   | 9.8%            | 4.7%            | 5.0%            | **ihtab** 52% fewer     |
| LLC miss rate         | 58.6%           | 51.3%           | 55.1%           | **ihtab** 12% fewer     |
| Branch misses         | 12.9M (1.45%)   | 5.1M (0.46%)    | 4.9M (0.45%)    | **ihtab-v0** 62% fewer  |
| dTLB miss rate        | 85.8%           | 92.7%           | 89.4%           | **absl** 7% fewer       |

At 1M keys the table fits in L2 cache — both ihtab variants dominate
on cycles and TLB.  At 20M keys the working set exceeds the LLC and
the interleaved layout (ihtab) wins on cycles and on memory access
because each group lookup touches one contiguous region instead of two
separate arrays.
