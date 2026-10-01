#ifndef TINYRT_SDK_H
#define TINYRT_SDK_H
#include <stdint.h>
#if defined(__wasm__)
#define TINYRT_IMPORT(name) __attribute__((import_module("tinyrt"), import_name(#name)))
#else
#define TINYRT_IMPORT(name)
#endif
TINYRT_IMPORT(clock_interval) int32_t clock_interval(int32_t ms);
TINYRT_IMPORT(draw_arc) int32_t draw_arc(int32_t cx,int32_t cy,int32_t radius,int32_t thickness,int32_t start_deg,int32_t end_deg,uint32_t rgb);
TINYRT_IMPORT(draw_clear) int32_t draw_clear(uint32_t rgb);
TINYRT_IMPORT(draw_rect) int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb);
TINYRT_IMPORT(draw_rgb565) int32_t draw_rgb565(int32_t x,int32_t y,int32_t w,int32_t h,const uint8_t *pixels,uint32_t length);
TINYRT_IMPORT(draw_round_rect) int32_t draw_round_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t radius,uint32_t rgb);
TINYRT_IMPORT(draw_skip) int32_t draw_skip(void);
TINYRT_IMPORT(draw_text) int32_t draw_text(int32_t x,int32_t y,const char *text,uint32_t length,uint32_t rgb);
TINYRT_IMPORT(draw_text_box) int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *text,uint32_t length,uint32_t rgb,int32_t font_px,int32_t align);
TINYRT_IMPORT(kv_get) int32_t kv_get(uint32_t key,int32_t fallback);
TINYRT_IMPORT(kv_set) int32_t kv_set(uint32_t key,int32_t value);
TINYRT_IMPORT(now_ms) uint32_t now_ms(void);
#define TINYRT_TOUCH_RELEASE 1
#define TINYRT_CLOCK_EVENT 2
/* Export these exact signatures. Return 0 on success. Each host call is bounded;
 * only render may draw, and every drawn frame starts with draw_clear. Text is UTF-8,
 * 1..63 bytes. Storage keys 0..15 contain signed 32-bit values; host scopes to app.
 * Avoid WASI/libc, constructors, unbounded loops, and floating point for this MVP. */
int32_t tinyrt_init(int32_t width,int32_t height);
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg);
int32_t tinyrt_render(void);
/* Optional export. A normal host stop calls this at most once after successful
 * init, under the same instruction budget; absent means no-op. Do not draw.
 * Return 0 to commit changed KV, then the host always destroys the instance.
 * Failed instances and power loss do not call it. This is not a guest exit request. */
#if defined(__wasm__)
__attribute__((export_name("tinyrt_stop")))
#endif
int32_t tinyrt_stop(void);
/* Optional graphics require a core supporting these imports (SDK pins revision).
 * Positive rectangle sizes, radius 0..min(w,h)/2; all bounds in the viewport.
 * Arc radius>0, 0<thickness<=radius; outer radius, 0 degrees right, clockwise,
 * 0<=start<=end<=360. 0..360 is a full circle; equal angles draw nothing.
 * Text boxes are single-line, clipped, vertically centered; font_px=18/24/36/48,
 * align=0 left, 1 center, 2 right. Text remains UTF-8, 1..63 bytes.
 * RGB565: at most one image per frame, 1..256 wide, 1..240 high, inside viewport;
 * length=w*h*2, little-endian packed bytes, copied immediately; no scaling.
 * draw_skip: only as the sole render operation, including no draw_clear; keep
 * the previous displayed frame. Requires DRAW permission like other drawing.
 * clock_interval: CLOCK permission, init/event only, 1..1000 ms, default 100;
 * host schedules one next event after completion, without catch-up bursts. */
#endif
