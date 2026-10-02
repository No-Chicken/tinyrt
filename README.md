# TinyRT core 0.1.0

TinyRT validates signed application packages, commits installations atomically,
and executes bounded Wasm guests behind ABI 1. This repository contains the
portable runtime, contracts and core tests. SDK authoring tools/examples and
EEBadge board, BLE, UI and NVS adapters belong to their own repositories.

## Build and test on Windows

Use an x64 MSVC developer terminal and UTF-8 console (`chcp 65001`). Install
CMake 3.24+, Ninja, Python 3.10+ and `pip install -r requirements-test.txt`.
Fetch the locked dependency with `python scripts/fetch_wamr.py`, or supply an
existing clean checkout of the exact revision in `dependencies.lock.json`.

```sh
cmake -S . -B build -G Ninja -DWAMR_ROOT_DIR=/path/to/wamr -DPython3_EXECUTABLE=/path/to/python
cmake --build build
ctest --test-dir build --output-on-failure
```

Without an override WAMR_ROOT_DIR is `third_party/wamr`. Configuration verifies
both revision and cleanliness. There is no fallback to a machine-specific path.
The full host suite currently uses Windows BCrypt; Linux is not validated and
the root build rejects it explicitly. Store and manager test directories can
also be configured separately. Firmware adapters may compile core C sources
and use the PSA crypto backend, while binding all hardware operations themselves.

## Host runtime integration

For an SDK host harness, include `cmake/HostWamr.cmake` from a C/C++ CMake
project after setting WAMR_ROOT_DIR. It exposes `tinyrt_wamr` and
`tinyrt_runtime`; link the latter for public headers and runtime dependencies.
The root `tinyrt-run` executable validates raw Wasm and accepts bounded stdin
events. Its output is host validation, not package trust or device acceptance.

## Repository boundaries

`contracts/guest-v1.h` and JSON contracts are authoritative. The SDK generates
copies from these contracts. `runtime/` owns package/store/manager/Wasm behavior;
`protocol/` owns portable management framing; `specs/` documents wire formats.
Core tests use independent signed encoders and frozen C/Wasm regression guests,
so building this checkout does not require the SDK, EEBadge or ESP-IDF.
Product-side tests moved to EEBadge `esp32/tests/tinyrt`.

## Optional round-screen drawing and normal stop

ABI 1 now additionally accepts `draw_round_rect`, `draw_arc` and `draw_text_box`.
Existing guests remain valid; new guests require a core revision supporting
their imports, pinned by the SDK contract snapshot. Unknown imports remain a
preflight rejection on older hosts. All drawing stays render-only, requires
DRAW permission, copies values/text, and shares the 128-command limit. The
host frame including the pixel extension requires caller-owned storage sized with `sizeof(tinyrt_frame_t)`. Runtime and manager
write directly into caller-owned unpublished storage and retain no frame between
callbacks. Products must budget their display/queue buffers and publish only a
successful result with a nonzero command count. The core does not render pixels
or choose fonts.
Text boxes are single-line, clipped and vertically centered, with font sizes
18/24/36/48 and left/center/right alignment. See the authoritative guest header
for bounds and arc angle conventions.

Guests may define `int32_t tinyrt_stop(void)`. The guarded export attribute in
the header exports a definition on Wasm without requiring legacy guests to
provide one. Validation checks the optional export's exact `()i` signature.
Normal manager stop invokes it once after successful init, under the usual
instruction budget, saves changed RAM KV only on success, and always destroys
the instance. It does not render. Stop errors are returned and retained for
the product to report; storage adapters must keep failed saves transactional.
Failed callbacks, failed initialization and forced destruction never execute
another guest callback. Old guests without this export stop as a no-op. The
callback responds to host termination; it does not let a guest request exit.

The raw-Wasm runner accepts `stop`; `restart` and clean stdin EOF also stop
normally, while `reboot` forces destruction and resets the clock. Runner KV is
RAM-only and rolls back on a failed stop. JSON frames include the new style
fields. Runtime and signed integration tests execute optional-stop fixtures
on the real pinned WAMR, including budget traps and storage failure injection.

`draw_rgb565` adds one owned RGB565 little-endian image up to 256×240 per frame.
The host copies bytes during render after validating dimensions, viewport,
length and guest memory. `draw_skip` is a sole render operation that keeps the
previous display; a successful skipped output changes only `count` to zero.
Consumers must ignore every other output field in that case. `clock_interval`
allows init/event to request 1..1000 ms scheduling under CLOCK permission,
default 100 ms. The host schedules a single callback after the previous one
completes. No catch-up loop or instruction budget increase is implied. See
[graphics and scheduling](specs/graphics-scheduling-v1.md). The runner reports
pixel byte count/CRC32 and clock interval. Explicit `capture` emits the retained
frame and `pixels_hex` for desktop PNG review; `time` and `pointer` support input scripts.

Current storage is an immutable package store plus sixteen i32 values per app.
`asset_read(offset, ptr, len)` reads authenticated current-package resources in
init/event, at most 4096 bytes per call. There is no private file filesystem yet. The store supports 16 apps and 2 MiB complete packages in
0x4E0000 bytes, with two 4 KiB directory sectors and variable contiguous extents.
Updates require separate free staging space; fragmentation or insufficient
space returns NO_SPACE while preserving the committed version. See
[store format 1](specs/store.md) for the incompatible media format and paging token.
Guest host calls are bounded RAM work; the owning service persists successful
transactions outside guest execution. Physical NVS, BLE and device display
acceptance remain product tests; host success does not establish hardware safety.

## Host protection policies

Trusted keys now optionally restrict `app_id_prefix`: NULL/empty keeps legacy
unrestricted trust, a trailing dot matches nonempty descendants (`demo.`), and
any other value matches one exact app ID. This restriction is host policy; package format 1 records the signing key ID. Independent publishers must receive disjoint scopes. Intentional
overlap permits key rotation and must be authorized by the product trust policy.

The manager coalesces changed KV into at most one ordinary save attempt per
5 seconds, using one timer across guest restarts and app switches. Setters still
update RAM immediately; successful callbacks flush pending data when due even
if that callback changes no keys. Normal stop flushes remaining changes and is
an explicit host-controlled exception to the cadence. Guest faults and save
errors discard unsaved RAM; power loss can also lose pending changes. This is
not a rate guarantee against repeated host stop/start, manager recreation or
power cycling. The host must serialize calls and provide a monotonic uint32
millisecond clock and transactional storage. See [host protection semantics](specs/host-protection.md).

Store recovery first selects the newest structurally valid committed directory,
then quarantines invalid packages individually without writing media. Healthy
apps remain available. Quarantine keeps the original identity, version floor,
capacity usage and extent reservation. Hosts can list quarantined identities,
repair with the exact original package, replace with a higher version, or
uninstall. IO/resource/configuration failures are returned rather than treated
as corruption. A damaged package never causes directory rollback; damaged
directory structure may still require selecting an older valid directory.

## Package and desktop workflow

The only package format is 1: `TRPKG001`, a 256-byte header and canonical
16-byte section records for Wasm, optional trusted AOT, and resources. Store
format and BLE metadata schema are also 1. Firmware/SDK version is 0.1.0.
Older internal package and directory layouts are incompatible and must be
rebuilt/reprovisioned through the product deployment flow. See [package](specs/package.md).

`draw_rgb565_scaled` submits one source up to 256×240 and a destination inside
the viewport. Hosts render it with floor nearest-neighbor scaling. It shares
the one-image-per-frame limit with `draw_rgb565`. `runtime_backend()` returns
0 for classic interpreter and 1 for AOT; guests can choose bounded work batches.
KEY1 is opt-in via `input_events(64)` or combined with touch lifecycle via 120;
event 6 carries x=1, y=0 release/1 press and arg=0. Cancel clears held input.

SDK `tinyrt.py run` drives this real WAMR classic interpreter and exports
466×466 round-screen PNGs. Desktop timings are not device FPS, and desktop fonts
need not match device fonts. The runner does not authenticate a signed package;
installation trust remains in the package/store path.

## Source provenance

This extraction preserves the original source and notices without assigning a
new project license. WAMR is obtained separately at a fixed revision and retains
its upstream license. Test signing scalars 1, 2, 42 and 43 are public test fixtures;
they must never be trusted by production devices. No production keys are stored.
