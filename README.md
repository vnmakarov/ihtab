# ihtab / ixhtab — single-header indexed hash tables

Two C++20 open-addressing hash tables with separate element storage.
Both live in a single header; no build system required to use them.

## Design

Standard Swiss tables store key-value pairs directly in the probe array.
At high load that means frequent cache misses and branch mispredictions on
failed lookups.

Both tables here take a different path: a compact array of 8-bit tags
(7-bit hash fingerprint + 1 empty/deleted bit) is probed with SIMD; the
actual elements live in a **separate dense array** and are referenced by
small integer indices.  At 50 % load the expected probe count is ~2 groups
vs ~8 for a 7/8-loaded Swiss table.  Iteration walks the dense element
array with a deleted-bit check — no empty-slot gaps.

### ihtab

`ihtab.hpp` — flat indexed hash table.

- Single contiguous element array, 32-bit slot indices.
- 50 % load factor; rebuilds the whole table when full.
- Fastest average lookup; suits general-purpose use.

### ixhtab

`ixhtab.hpp` — indexed **extendible** hash table.

- Directory of bins, each bin is an ihtab with 16-bit indices (max 2¹⁵
  elements per bin).
- Only the full bin is split on overflow; the rest of the table is
  untouched.
- Trades a directory indirection for bounded rebuild latency; suits large
  tables or latency-sensitive workloads.

## Usage

Both tables follow the same API.  Define an entry type that bundles key and
value, plus a hash functor and equality functor, then instantiate.

```cpp
#include "ihtab.hpp"

struct Entry { uint64_t key; int value; };
struct Hash  { ihtab_hash_t operator()(const Entry &e) const { return e.key * 11400714819323198485ULL; } };
struct Eq    { bool operator()(const Entry &a, const Entry &b) const { return a.key == b.key; } };

using Tab = ihtab<Entry, Hash, Eq>;

Tab t(64);  // min_size=64; default is 8

// Insert
Entry e{42, 100};
Entry *slot;
if (!t.perform(e, IHTAB_INSERT, &slot))
    *slot = e;           // new element — write it

// Lookup
Entry key{42, 0};
if (t.perform(key, IHTAB_FIND, &slot))
    printf("%d\n", slot->value);

// Delete
t.perform(key, IHTAB_DELETE, &slot);

// Iterate
for (auto it = t.iter_begin(); Tab::iter_valid(it); t.iter_next(it))
    printf("%llu -> %d\n", it.ptr->key, it.ptr->value);

// t destroyed automatically at end of scope
```

Replace `ihtab_t` / `IHTAB_*` with `ixhtab_t` / `IXHTAB_*` to use ixhtab.

`do_` returns `true` when the element was found (FIND/DELETE) or already
existed (INSERT/REPLACE), `false` on a new insertion.  On `false` from
INSERT, `*slot` points to uninitialised memory — you must write the element.

## Requirements

C++20 compiler.  x86/x86-64 uses SSE2; AArch64 uses NEON; other
architectures fall back to portable SWAR bit tricks.

## Install

```sh
make install            # copies ihtab.hpp and ixhtab.hpp to /usr/local/include
make install PREFIX=~/.local
make uninstall
```

## Benchmark

```sh
make test               # build and run vs absl::flat_hash_map
```

Benchmarks run against `absl::flat_hash_map` with the vmum hash on
sizes Small (100), Medium (10 000), Large (1 000 000).

Geometric mean on AMD 9900x across all benchmarks (lower is better):

| Implementation | ns/op | vs absl |
|----------------|------:|--------:|
| absl           |   6.4 |  1.000x |
| ixhtab         |   6.1 |  0.957x |
| ihtab          |   4.3 |  0.677x |

`ihtab` is ~49 % faster than absl on average; `ixhtab` is ~5 % faster.

Geometric mean on Intel 270K+ across all benchmarks:

| Implementation | ns/op | vs absl |
|----------------|------:|--------:|
| absl           |   8.8 |  1.000x |
| ixhtab         |   7.4 |  0.849x |
| ihtab          |   5.1 |  0.581x |

`ihtab` is ~73 % faster than absl on average; `ixhtab` is ~19 % faster.
  
Geometric mean on Apple M4 across all benchmarks:

| Implementation | ns/op | vs absl |
|----------------|------:|--------:|
| absl           |   6.2 |  1.000x |
| ixhtab         |   5.3 |  0.854x |
| ihtab          |   3.9 |  0.625x |

`ihtab` is ~59 % faster than absl on average; `ixhtab` is ~17 % faster.
