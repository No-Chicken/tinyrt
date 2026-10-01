# Optional RGB565, skipped render and clock interval

These are additional optional imports in module `tinyrt`, preserving ABI 1 and
its 100,000-instruction callback ceiling. A guest using them requires a core
revision that implements them; older hosts reject unknown imports. Existing
guests need no changes. The authoritative signatures are in `contracts`.

## Owned pixel frame

`draw_rgb565(x, y, w, h, ptr, len)` has six i32 parameters and an i32 result.
It requires DRAW permission and an active render callback. Width is 1..256,
height 1..240, coordinates are nonnegative, and the complete rectangle must fit
the initialized viewport. Length must be exactly `w*h*2`, at most 122,880 bytes.
Dimensions are bounded before multiplication. The entire guest address range
is checked before copying. Bytes are packed RGB565 little-endian, without
scaling or a stride. A frame permits one image and still starts with draw_clear;
all drawing shares the existing 128-command limit.

Command kind 7 stores x/y/w/h. `tinyrt_frame_t` owns `pixel_bytes` and
`pixels[122880]`; no guest pointer crosses the runtime boundary. Guest changes
after the import cannot alter the host copy. Rendering resets commands, count
and pixel_bytes, but does not clear the pixel payload on each callback.
`pixel_bytes` is authoritative for a drawn frame; bytes after it are unused.

The C frame is 137,224 bytes, including 128 command records. Runtime and manager
own their frames on the heap. Embedding products must place display/queue/frame
copies on suitable memory and avoid automatic task-stack frame variables. A
normal successful render performs bounded deep copies; failed render leaves
the output unchanged and poisons the instance as before.

## Skip without copying pixels

`draw_skip()` requires DRAW permission and render stage, with no earlier
command or skip. No later drawing or repeated skip is allowed. It is the sole
render operation and does not require clear. An ordinary render that does
nothing remains an error.

A successful skipped render changes only output `count` to zero. The manager
also only changes the caller's count, and still applies successful KV
transaction/persistence rules. Consumers must ignore all other frame fields,
including stale pixel_bytes, and leave the previous display intact. A guest
may skip its first render; the host retains its current page/background.

## Bounded scheduling request

`clock_interval(ms)` requires CLOCK permission, accepts 1..1000, and is legal
only during init/event. Render, stop, negative, zero and excessive values fail
the callback. Each new runtime starts at 100 ms. `tinyrt_runtime_clock_interval_ms`
and its manager wrapper return the request for a running instance, otherwise
100 ms for NULL, not initialized, failed or stopped runtimes.

The product computes one next deadline after a completed callback and invokes
one event when due. It must not run a catch-up burst. This is a scheduling
request, not a real-time or frame-rate guarantee; each call retains the original
instruction budget. Guest CPU work, host pixel copying, display bandwidth and
owner-thread load all affect device performance.

## Heap telemetry and evidence

`tinyrt_runtime_memory_used()` counts the bounded runtime allocator's current
bytes. `tinyrt_runtime_memory_peak()` tracks its high-water since the latest
fresh runtime-system initialization, retains it through shutdown, and resets
at the next initialization. The manager's read-only wrappers return zero for
NULL. These values exclude manager/store allocations, allocator headers and
system thread/lock overhead; the limit stays 2 MiB.

Real WAMR tests check known RGB565 bytes, guest mutation after copy, the entire
maximum pixel payload, address/length/dimension overflow, viewport edges,
permission and signature rejection, stage misuse, repeated images, command
limits, skip mixing, clock boundaries/defaults and allocator cleanup. Signed
manager integration checks pixels, no-copy skip sentinels and clock changes.
Host runner JSON reports pixel_bytes, pixel_crc32 and clock_interval_ms without
serializing image bytes. These tests do not substitute for device frame rate,
display byte order, task-stack high-water or BLE installation measurements.
