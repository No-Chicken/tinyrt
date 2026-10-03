# TinyRT package store format 1

The store spans `0x4E0000` bytes (4992 KiB). Offsets 0 and 4096 contain two
directory sectors. The remaining 4984 KiB stores immutable signed packages.

The maximum individual package remains 2 MiB. Updates require a separate free
extent while the current package stays committed. This capacity can stage a
second 2 MiB package alongside an installed 2 MiB package, leaving 888 KiB for
other allocations. Other packages and fragmentation can still prevent a large
update; `TINYRT_NO_SPACE` leaves the committed package intact. Applications must
budget a contiguous staging extent when choosing package sizes.
There are at most 16 committed app IDs, including quarantined entries. A
complete package is at most `0x200000` bytes; guest memory and callback budgets
are unchanged.

## Directory encoding

All integers are little-endian. A directory occupies exactly 4096 bytes.

| Offset | Bytes | Value |
|---|---:|---|
| 0 | 8 | `TRDIR001` |
| 8 | 4 | format version 1 |
| 12 | 4 | record count, 0 through 16 |
| 16 | 8 | persistent commit generation, nonzero |
| 24 | 8 | zero |
| 32 | count × 80 | records below |
| after records | through 4087 | erased bytes, 0xFF |
| 4088 | 4 | IEEE CRC32 of bytes 0 through 4087 |
| 4092 | 4 | zero commit marker, programmed last |

| Record offset | Bytes | Value |
|---|---:|---|
| 0 | 32 | canonical NUL-terminated app ID, zero padding |
| 32 | 4 | nonzero app version |
| 36 | 4 | package byte offset relative to store |
| 40 | 4 | complete package size |
| 44 | 32 | complete package SHA-256 |
| 76 | 4 | zero |

Package offsets must be 4096-byte aligned and at least 8192. Each allocation
covers `ceil(package_size / 4096) * 4096` bytes, must fit the store, and must not
overlap any other record. Duplicate IDs are invalid. Decoding verifies every
pair of records. Old internal directory layouts, including the previous format number, return `CORRUPT` without any write or
automatic formatting; changing a product partition table requires a separately
authorized migration or clean installation.

## Allocation and atomic replacement

Allocation selects the first contiguous aligned free interval large enough for
the incoming package. All currently committed intervals remain reserved,
including quarantined packages and the old version of an update. The store
erases the selected staging interval sector by sector, streams the package,
verifies it, then commits a replacement directory in the alternate sector.
Package sync precedes directory publication; the directory body is synced
before its commit marker and the final sync. Staging is never a visible app.

Insufficient contiguous space returns `NO_SPACE` and preserves the old package.
Free bytes split across holes cannot be combined; there is no compaction.
After successful replacement or uninstall, an unreferenced interval becomes
available to the next first-fit allocation. An interrupted publication is
recovered through the existing two-directory commit rules. A bad package in the
newest structurally valid directory is quarantined, not used to roll back that
directory. IO and resource failures do not publish partial recovery state.

## Inventory revision and management paging

`tinyrt_store_generation(store, &revision)` and the manager wrapper return a
status and an opaque uint64 inventory revision. The output is zero on error.
This is a RAM counter for one store lifetime, separate from the persistent
directory generation. An erased empty store starts at zero; opening an existing
committed directory starts at one. Directory content/generation or quarantine
membership changes increment it; unchanged recovery does not. Overflow returns
`CONFLICT` without wrapping or publishing the changed RAM catalog. Reads and
recovery do not write the media. Owners must serialize calls.

`LIST_PAGE` is opcode 0x18, advertised by HELLO capability 0x10. Its request is
10 bytes: kind u8 (0 healthy, 1 quarantined), offset u8, revision u64. Its reply
has revision u64, total u8, offset u8, count u8, followed by up to three standard
72-byte app-info records. Integers are little-endian. The first request uses
offset/revision zero; continuations require the returned revision. A conflict
requires a bounded restart from the first page, including both health lists;
reconnection starts a new listing. Total, offsets, counts and app ID ordering
must be checked by clients. Maximum management message size remains 256 bytes.
The product adapter implements these payloads using the core contract in
`contracts/wire-v1.json`. Legacy LIST and LIST_QUARANTINED return `NO_SPACE` for
more than two entries rather than silently truncating.

## Read-only storage statistics

`tinyrt_store_stats` and `tinyrt_manager_stats` return `tinyrt_store_stats_t`.
`total_bytes` includes the two directory sectors; `data_bytes` excludes them.
`package_bytes` sums exact package lengths, while `allocated_bytes` sums their
4096-byte-rounded extents. Both include quarantined entries. `free_bytes` is
data capacity minus allocated bytes, and `largest_free_bytes` is the largest
currently uncommitted contiguous interval. `installed_count` includes all
committed entries; `quarantined_count` is its quarantined subset. The result
also contains `max_apps`, `max_package_size` and the current inventory revision
in `generation`.

An active install handle, including one awaiting abort after commit, returns
`BUSY` so staging is not reported as usable free space. Dirty state is recovered
first; any error clears the entire output. The operation performs no media
writes and needs no additional catalog allocation. A live guest does not block
clean-state statistics. Protocol opcode `STORAGE` 0x19 and HELLO capability 0x20
use the 92-byte schema-1 payload defined in `contracts/wire-v1.json`. APP_INFO
0x1A returns the 248-byte schema-1 section metadata layout from that same contract.

## Memory placement and verification limits

The manager and its 16-entry metadata catalog and app-info workspace are heap
allocated once. Recovery puts both candidate directories and its 4096-byte
sector buffer on the heap. Initial-directory comparison, commit and uninstall
also allocate their larger scratch buffers on the heap. No 16-entry catalog,
directory pair or sector buffer is added to the caller's task stack. Temporary
allocation failure returns `NO_MEMORY`; the host remains responsible for
allocation placement and sufficient heap. Verification streams hashes and
allocates the Wasm section only, not the package assets.

The storage expansion's MSVC x64 structure probe reports a store of 1,376,
directory of 1,296, and stats result of 48. Recovery's candidate pair is 2,592
bytes plus the sector buffer; first provisioning additionally compares one
4096-byte expected sector. These are heap costs, not measured ESP32 task-stack
high-water values. The host supplies the unpublished frame described in
[graphics and scheduling](graphics-scheduling-v1.md); consumers must budget
that owned pixel payload separately using the current frame type size. The package verifier's existing 1024-byte streaming block
and 256-byte header remain on the stack.

Host tests cover independent format-1 media fixtures, 16-app catalogs, first fit,
fragmentation, maximum packages, malformed intervals, quarantine revision
changes, revision overflow and partial-write recovery. Signed integration runs
a real WAMR guest from a 2 MiB package containing assets. This does not establish
device stack high-water, Flash latency, power-cut behavior or BLE throughput.
