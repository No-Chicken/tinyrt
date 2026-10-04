# Raster graphics extension (ABI 1, core 0.0.2)

This extension is optional. Existing immediate imports, permissions, package version,
AOT safety profile, mandatory initial legacy `draw_clear`, and `draw_skip` semantics
remain unchanged. The authoritative guest declarations are
`contracts/guest-gfx-v1.h`; SDKs copy this contract rather than inventing an encoding.

## Frame and record contract

`gfx_caps()` reports implemented CPU operations, not hypothetical hardware speed.
Call `gfx_begin(flags)`, one or more `gfx_submit(records, bytes)`, optionally
`gfx_damage(x,y,w,h)`, then `gfx_end()` inside render. Each record begins with a
little-endian `u16 op, u16 size_bytes`. Size includes the header, is a multiple of
four, and must match the complete inline payload. Remaining fields are 32-bit
little-endian integers. Coordinates are signed; sizes, colors, IDs and flags are
unsigned. Colors in new records are RGB565. Guest pointers never appear in records.

`flags` allows KEEP_PREVIOUS=1 and logical scale 1, 2, or 3 in bits 8..9 (zero also
means 1). A logical source pixel is replicated by this integer scale after
nearest-neighbor source selection. The host-only RECONSTRUCTIBLE=2 flag cannot be
requested with `gfx_begin`.

| Op | Fields after the four-byte header | Payload / total bytes |
|---|---|---|
| CLEAR=1 | color | 8 |
| RECT=2 | x,y,w,h,color,alpha | 28; alpha 0..255 |
| ROUND_RECT=3 | x,y,w,h,radius,color | 28 |
| ARC=4 | cx,cy,radius,thickness,start,end,color | 32 |
| SPRITE=5 / SOLID_SPRITE=6 / ADD_SPRITE=7 | x,y,w,h,tex,u,v,sw,sh,flags,pal,color | 52 |
| SPRITE_BATCH=8 | tex,sw,sh,flags,pal,count | 28 + count*16; instances x,y,u,v |
| GRID=9 | x,y,cols,rows,cell_w,cell_h,gap,pal,flags | 40 + padded cols*rows bytes |
| TILEMAP=10 | x,y,cols,rows,tile_w,tile_h,tex,scroll_x,scroll_y,flags,pal | 48 + padded cols*rows bytes |
| TEXT=11 | x,y,w,h,font_px,align,color,length | 36 + padded UTF-8 bytes |
| SET_PAL=12 | slot | 8; explicit per-record palettes remain authoritative |
| CLIP=13 | x,y,w,h | 20; w=0 resets the clip |

INDEX8 sprite flags: transparent index zero=1, flip X=2, flip Y=4, clockwise
quarter turns in bits 3..4. Flips act on the original source before rotation.
RGB565 textures have no color key. SOLID_SPRITE uses the same opacity mask but
paints the specified color. ADD_SPRITE saturates native RGB565 channels.
Every INDEX8 record specifies its palette explicitly; SET_PAL validates a resident
palette but does not override those explicit fields.

GRID index zero always skips drawing. Each occupied cell fills the upper-left
`(cell_w-gap)*(cell_h-gap)` pixels. CIRCLE=1 includes cells whose centers lie in
an inscribed circle in physical cell geometry. TILEMAP uses a row-major atlas;
its byte IDs must all reference complete tiles. Scrolling moves the bounded map,
without wraparound. Text uses 18/24/36/48 pixel host fonts, single-line clipping
and vertical centering; align 0/1/2 is left/center/right. The renderer delegates
font pixels to the host callback rather than embedding a platform font.

Rounded shapes evaluate logical pixel centers. ARC angles run clockwise from
right, with 0..360 full and equal endpoints empty. RGB565 alpha blend computes
each channel as `(source*alpha + destination*(255-alpha) + 127)/255`.

Coordinates range -32768..32767, positive dimensions at most 32767. Texture source
rectangles must be fully in bounds; destination rectangles are clipped to the
viewport. Grid/map dimensions are at most 512 each and total extents at most
32767. Text contains 1..63 valid printable UTF-8 bytes. Sprite batches contain
1..1024 instances. Each submission is at most 16KiB, a frame at most 64KiB and
4096 records. Unsupported opcodes return -2 from the portable validator; malformed
records return -1. Runtime imports return -1 and record a zero-based rejection
index. Any bad submission invalidates the entire unpublished raster frame,
including earlier valid submissions. No partial draw is published.

Admission also bounds conservative physical pixel visits to eight viewport areas
per frame, accumulated over all submissions. Clipping can reduce destination
coverage; CLIP itself is deliberately not credited with reducing this estimate.
This bounds overlapping opaque/blended records before execution on the UI thread.
For 466*466, the ceiling is 1,737,248 visits. It is an admission limit, not a frame
rate guarantee. Failed frames retain the previously displayed image.

## Resource ownership and memory

Upload palettes and textures only in init/event, with DRAW permission. There are
32 texture slots, 16 palettes of 256 colors, dimensions at most 512*512, and at
most 256KiB of current texture data per session. Formats are INDEX8=1 and
RGB565 little-endian=2. FROM_ASSET=1 interprets the source argument as an offset
in the current verified package; reads are bounded in 4096-byte chunks. Upload,
row updates, palette updates and free are transactional: failure preserves the
previous resource content.

Publication retains an immutable resource bank. Updates copy changed resources;
queued frames keep the old versions. Atomic reference counts support retirement
on a host UI thread while the runtime owner updates a new bank. Copies exclude
atomic counters. Destroying the runtime releases its ownership but does not
invalidate a retained frame.

A separate process-wide graphics pool has a hard 1152KiB limit including all
texture generations, palettes, resource banks and allocation headers. This is
separate from the existing 2MiB WAMR tracked heap. `tinyrt_gfx_memory_used()` reports
the pool's live charge. Allocation failure is bounded and preserves old resources.
Host frame storage is caller-owned, approximately 200KiB per frame including
legacy pixels and the new 64KiB record buffer. Hosts must account for their own
fixed frame pool and retained display buffer as well; runtime heap telemetry alone
is not total application memory.

Initialize raster frame storage to zero before its first use. Reusing a returned
raster output through `tinyrt_runtime_render` releases its previous snapshot.
`tinyrt_frame_release` releases a retired frame explicitly. A frame copy must retain
its resource bank once, and every owned copy releases once. A transferred frame
must relinquish the source ownership. Release before clearing/freeing any frame
pool. Legacy skip still changes only count. Never overwrite an owned resource
pointer with memset or a shallow copy.

## Partial updates and framebuffer

Without damage declarations, a raster frame has full-screen damage. Damage is in
physical viewport coordinates, positive, fully in bounds, and merges to at most
eight rectangles. Empty record frames skip publication. With KEEP_PREVIOUS, the
host provides previous pixels as the render base. Dropping a dependent incremental
raster frame cannot be repaired by merging damage alone: the host must preserve it
or apply it before superseding it. Complete-scene frames may be superseded.

`fb_config(w,h,scale)` is init-only, scale 1 or 2, with `w*h*2 <= 128KiB` and output
fully inside the viewport. Texture slot 31 is reserved once configured. During
render, `fb_present(pixels,len,x,y,w,h)` copies a packed RGB565 patch. The first
frame reconstructs black background plus the full framebuffer; later frames carry
scaled patch damage. Every published framebuffer frame references the entire
latest resident framebuffer, with RECONSTRUCTIBLE set. Therefore superseded
framebuffer damage can be merged safely and rendered from the latest snapshot.

## Portable renderer and host boundary

`tinyrt_gfx_render_strip` accepts uint16_t pixels with stride measured in pixels,
not bytes. It writes only the requested rows. GRID fills cells directly; sprites
hoist descriptors and use a bounded 512-entry source-offset map. Wider destination
spans use the scalar fallback. `tinyrt_gfx_render_reference_strip` uses scalar
per-pixel RECT/CLEAR/GRID/SPRITE operations. Tests compare fast and reference
outputs for scaling, masks, rotations, clipping and one-row strips.

A raster frame publishes one RASTER command marker (kind 9), then may append
legacy HUD commands after `gfx_end`. Native font callbacks receive internal high
bits for legacy text: align bit31 means top-left, color bit31 preserves original
24-bit ink. Guest TEXT records cannot request those bits. Existing legacy
ROUND_RECT/ARC remain on their established host path for legacy pixel compatibility.

The desktop runner captures actual core-rendered raster pixels through
`raster_pixels_hex`, RGB565 LE at 466*466. It preserves separate legacy overlay
records. Font rendering is a host responsibility, so native raster TEXT without a
font callback is not included in the base pixel capture. CPU capabilities are
portable. No S31 hardware implementation, hardware acceptance or minimum FPS is
claimed by this extension.

## Export timing

Hosts may set a monotonic microsecond clock with
`tinyrt_manager_set_perf_clock` before start and read cumulative
`tinyrt_manager_perf` counters on the owner thread. Measurements surround the
init, event and render exports separately. They include native imports invoked
inside that export and exclude persistence, host raster and panel transfer.
They do not imply that physics belongs to event or composition belongs to render:
a guest may perform composition inside event. Counters are process-local diagnostics.
