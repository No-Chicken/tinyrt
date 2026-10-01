#ifndef TINYRT_SDK_H
#define TINYRT_SDK_H
#include <stdint.h>
#if defined(__wasm__)
#define TINYRT_IMPORT(name) __attribute__((import_module("tinyrt"), import_name(#name)))
#else
#define TINYRT_IMPORT(name)
#endif
TINYRT_IMPORT(draw_clear) int32_t draw_clear(uint32_t rgb);
TINYRT_IMPORT(draw_rect) int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb);
TINYRT_IMPORT(draw_text) int32_t draw_text(int32_t x,int32_t y,const char *text,uint32_t length,uint32_t rgb);
TINYRT_IMPORT(kv_get) int32_t kv_get(uint32_t key,int32_t fallback);
TINYRT_IMPORT(kv_set) int32_t kv_set(uint32_t key,int32_t value);
TINYRT_IMPORT(now_ms) uint32_t now_ms(void);
#define TINYRT_TOUCH_RELEASE 1
#define TINYRT_CLOCK_EVENT 2
/* Export these exact signatures. Return 0 on success. Each host call is bounded;
 * only render may draw, and every frame starts with draw_clear. Text is UTF-8,
 * 1..63 bytes. Storage keys 0..15 contain signed 32-bit values; host scopes to app.
 * Avoid WASI/libc, constructors, unbounded loops, and floating point for this MVP. */
int32_t tinyrt_init(int32_t width,int32_t height);
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg);
int32_t tinyrt_render(void);
#endif
