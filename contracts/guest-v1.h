#ifndef TINYRT_SDK_H
#define TINYRT_SDK_H
#include <stdint.h>
#if defined(__wasm__)
#define TINYRT_IMPORT(name) __attribute__((import_module("tinyrt"), import_name(#name)))
#else
#define TINYRT_IMPORT(name)
#endif
/* Current authenticated resources, init/event only, reads <=4096 bytes. */
TINYRT_IMPORT(asset_read) int32_t asset_read(uint32_t offset,void *destination,uint32_t length);
/* AUDIO=16, init/event only, PCM16 LE mono16000Hz, 2..32000 even bytes; max4 requests. 0 queued,1 busy. */
TINYRT_IMPORT(audio_play) int32_t audio_play(uint32_t resource_offset,uint32_t byte_length,uint32_t sample_rate);
/* CLOCK=8, init/event only, interval1..1000ms. */
TINYRT_IMPORT(clock_interval) int32_t clock_interval(int32_t ms);
TINYRT_IMPORT(draw_arc) int32_t draw_arc(int32_t cx,int32_t cy,int32_t radius,int32_t thickness,int32_t start_deg,int32_t end_deg,uint32_t rgb);
TINYRT_IMPORT(draw_clear) int32_t draw_clear(uint32_t rgb);
TINYRT_IMPORT(draw_rect) int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb);
TINYRT_IMPORT(draw_rgb565) int32_t draw_rgb565(int32_t x,int32_t y,int32_t w,int32_t h,const uint8_t *pixels,uint32_t length);
TINYRT_IMPORT(draw_rgb565_scaled) int32_t draw_rgb565_scaled(int32_t x,int32_t y,int32_t dst_w,int32_t dst_h,int32_t src_w,int32_t src_h,const uint8_t *pixels,uint32_t length);
TINYRT_IMPORT(draw_round_rect) int32_t draw_round_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t radius,uint32_t rgb);
/* Render only, sole operation; retain previous display. */
TINYRT_IMPORT(draw_skip) int32_t draw_skip(void);
TINYRT_IMPORT(draw_text) int32_t draw_text(int32_t x,int32_t y,const char *text,uint32_t length,uint32_t rgb);
TINYRT_IMPORT(draw_text_box) int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *text,uint32_t length,uint32_t rgb,int32_t font_px,int32_t align);
/* Init only, RGB565<=128KiB, integer scale1/2. */
TINYRT_IMPORT(fb_config) int32_t fb_config(uint32_t w,uint32_t h,uint32_t scale);
/* Render only; packed rectangular patch, owned immutable frame snapshot. */
TINYRT_IMPORT(fb_present) int32_t fb_present(const void *pixels,uint32_t length,int32_t x,int32_t y,int32_t w,int32_t h);
/* Optional raster imports; declarations and records also in guest-gfx-v1.h. */
TINYRT_IMPORT(gfx_begin) int32_t gfx_begin(uint32_t flags);
/* Optional CPU capability bitmask, see guest-gfx-v1.h for record structs. */
TINYRT_IMPORT(gfx_caps) uint32_t gfx_caps(void);
TINYRT_IMPORT(gfx_damage) int32_t gfx_damage(int32_t x,int32_t y,int32_t w,int32_t h);
TINYRT_IMPORT(gfx_end) int32_t gfx_end(void);
TINYRT_IMPORT(gfx_pal_upload) int32_t gfx_pal_upload(uint32_t slot,uint32_t first,uint32_t count,const uint16_t *colors);
TINYRT_IMPORT(gfx_submit) int32_t gfx_submit(const void *records,uint32_t length);
TINYRT_IMPORT(gfx_tex_free) int32_t gfx_tex_free(uint32_t slot);
TINYRT_IMPORT(gfx_tex_update_rows) int32_t gfx_tex_update_rows(uint32_t slot,uint32_t y,uint32_t rows,const void *source,uint32_t length);
TINYRT_IMPORT(gfx_tex_upload) int32_t gfx_tex_upload(uint32_t slot,uint32_t format,uint32_t w,uint32_t h,const void *source,uint32_t length,uint32_t flags);
/* INPUT=2, init only, lifecycle0x38/userkey0x40/motion0x80; zero legacy release. */
TINYRT_IMPORT(input_events) int32_t input_events(uint32_t mask);
TINYRT_IMPORT(kv_get) int32_t kv_get(uint32_t key,int32_t fallback);
TINYRT_IMPORT(kv_set) int32_t kv_set(uint32_t key,int32_t value);
TINYRT_IMPORT(now_ms) uint32_t now_ms(void);
/* 0 instruction-metered interpreter,1 deadline-guarded AOT. */
TINYRT_IMPORT(runtime_backend) int32_t runtime_backend(void);
#define TINYRT_TOUCH_RELEASE 1
#define TINYRT_CLOCK_EVENT 2
#define TINYRT_TOUCH_PRESS 3
#define TINYRT_TOUCH_MOVE 4
#define TINYRT_TOUCH_CANCEL 5
#define TINYRT_INPUT_LIFECYCLE_MASK 0x38u
#define TINYRT_USER_KEY_EVENT 6
#define TINYRT_INPUT_USER_KEY_MASK 0x40u
#define TINYRT_EV_MOTION 7
#define TINYRT_IN_MOTION 0x80u
/* MOTION: x=ax, y=ay, arg=az, signed mg in raw sensor axes, -16000..16000.
 * Opt in with INPUT permission; OR 0x80 into any existing subscription mask.
 * Sensor unavailable/stale means no event. Screen-axis mapping belongs to Guest. */
/* USER_KEY_EVENT: x=1, y=1 pressed / 0 released, arg=0. Opt in during init.
 * Accept masks 0, 0x38, 0x40 or 0x78, optionally OR 0x80. CANCEL clears touch and keys. */
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
