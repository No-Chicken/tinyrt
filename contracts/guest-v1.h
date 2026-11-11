/* Shared with the native feature extractor. The SDK ships this header standalone. */
#ifndef TINYRT_MIC_ABI_H
#define TINYRT_MIC_ABI_H
#include <stdint.h>
#define TINYRT_EV_MIC_LEVEL 9
#define TINYRT_MIC_PCM 1u
#define TINYRT_MIC_LEVEL 2u
#define TINYRT_MIC_FEATURES 4u
#define TINYRT_MICF_PLAYBACK 0x0001u
#define TINYRT_MICF_CLIPPED 0x0002u
#define TINYRT_MICF_OVERRUN 0x0004u
#define TINYRT_MICF_SETTLING 0x0008u
/* Frozen 32-byte LE record, one per 160 samples (10 ms at 16 kHz). */
typedef struct {
    uint32_t seq, t_ms;                 /* Sequence since open and end-of-frame host clock. */
    int16_t rms_db_q8, floor_db_q8;     /* dBFS x 256, minimum -100 dBFS. */
    uint16_t peak, zcr;
    uint16_t band_q15[4];              /* 50-500, 500-2000, 2000-4000, 4000-7500 Hz. */
    uint16_t flatness_q15, pitch_hz_q4, pitch_conf_q15, flags;
} tinyrt_mic_frame_t;
#endif

#if !defined(TINYRT_SDK_H) && !defined(TINYRT_MIC_ABI_ONLY)
#define TINYRT_SDK_H
#include <stdint.h>
#define TINYRT_API_LEVEL_BASE 1
#define TINYRT_API_LEVEL_SYSTEM 2
#define TINYRT_API_LEVEL_HARDWARE 3
#define TINYRT_API_LEVEL_EXTENDED 4
#define TINYRT_API_LEVEL TINYRT_API_LEVEL_EXTENDED
#define TINYRT_EV_ACC_DATA 13
#define TINYRT_EV_GYRO 10
#define TINYRT_EV_GESTURE 11
#define TINYRT_EV_MINUTE 12
#define TINYRT_IMU_ACCEL 1u
#define TINYRT_IMU_GYRO 2u
#define TINYRT_IMU_GESTURE 4u
#define TINYRT_TIME_MINUTE 1u
typedef struct {int32_t ax,ay,az,gx,gy,gz;uint32_t timestamp_ms;} tinyrt_imu_t;
typedef struct {int32_t year,month,day,hour,minute,second,weekday,utc_offset_min,valid;} tinyrt_datetime_t;
typedef struct {int32_t level_pct,charging,full,voltage_mv;} tinyrt_battery_t;
#define TINYRT_ERR_UNAVAILABLE (-2)
#if defined(__wasm__)
#define TINYRT_IMPORT(name) __attribute__((import_module("tinyrt"), import_name(#name)))
#else
#define TINYRT_IMPORT(name)
#endif
TINYRT_IMPORT(acc_read) int32_t acc_read(void *dst,uint32_t capacity);
TINYRT_IMPORT(acc_send) int32_t acc_send(uint32_t type,const void *src,uint32_t length);
/* Current authenticated resources, init/event only, reads <=4096 bytes. */
TINYRT_IMPORT(asset_read) int32_t asset_read(uint32_t offset,void *destination,uint32_t length);
/* AUDIO=16, init/event only, PCM16 LE mono16000Hz, 2..32000 even bytes; max4 requests. 0 queued,1 busy. */
TINYRT_IMPORT(audio_play) int32_t audio_play(uint32_t resource_offset,uint32_t byte_length,uint32_t sample_rate);
TINYRT_IMPORT(audio_queue_free) int32_t audio_queue_free(void);
TINYRT_IMPORT(audio_tone) int32_t audio_tone(uint32_t hz,uint32_t ms);
TINYRT_IMPORT(audio_write) int32_t audio_write(const void *pcm,uint32_t length);
TINYRT_IMPORT(battery_info) int32_t battery_info(void *dst,uint32_t length);
TINYRT_IMPORT(blob_read) int32_t blob_read(void *dst,uint32_t capacity);
TINYRT_IMPORT(blob_write) int32_t blob_write(const void *src,uint32_t length);
/* CLOCK=8, init/event only, interval1..1000ms. */
TINYRT_IMPORT(clock_interval) int32_t clock_interval(int32_t ms);
/* UTF-8, max 128 bytes, at most 50 messages per second per instance. */
TINYRT_IMPORT(debug_log) int32_t debug_log(const char *text,uint32_t length);
TINYRT_IMPORT(display_brightness) int32_t display_brightness(int32_t percent);
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
TINYRT_IMPORT(haptic_pattern) int32_t haptic_pattern(const uint16_t *steps,uint32_t count);
/* HAPTIC=32, init/event only. Strength1..100%, duration1..2000ms.
 * Nonblocking; new pulse replaces old. 0 accepted,1 unavailable; invalid use traps.
 * Host stops active output when the application ends. */
TINYRT_IMPORT(haptic_pulse) int32_t haptic_pulse(uint32_t strength,uint32_t duration_ms);
TINYRT_IMPORT(haptic_stop) int32_t haptic_stop(void);
TINYRT_IMPORT(imu_config) int32_t imu_config(uint32_t rate_hz,uint32_t mask);
TINYRT_IMPORT(imu_read) int32_t imu_read(void *dst,uint32_t length);
/* INPUT=2, init only, lifecycle0x38/userkey0x40/motion0x80/gamepad0x100; zero legacy release. */
TINYRT_IMPORT(input_events) int32_t input_events(uint32_t mask);
TINYRT_IMPORT(kv_get) int32_t kv_get(uint32_t key,int32_t fallback);
TINYRT_IMPORT(kv_set) int32_t kv_set(uint32_t key,int32_t value);
/* Level 3, MIC=64. Recording is visible to the user and ends on app teardown.
 * Reassess raw PCM permission before adding networking or audio-sized storage.
 * Open in init/event: 16000 Hz, nonempty PCM|LEVEL|FEATURES flags, default gain 24 dB.
 * 0 opened, 1 busy, -2 unavailable. Close in init/event; already closed returns 0.
 * Event-only reads return bytes from a one-second FIFO, oldest items drop on overrun.
 * Read even PCM byte counts or multiples of 32 feature bytes, each <=4096.
 * Gain in init/event: 0..42 dB rounded down to 6 dB steps; 200 ms settling.
 * MIC_LEVEL: peak in x, RMS in y (both 0..32767), flags in arg (low 16 bits). */
TINYRT_IMPORT(mic_close) int32_t mic_close(void);
TINYRT_IMPORT(mic_features) int32_t mic_features(void *dst,uint32_t length);
TINYRT_IMPORT(mic_gain) int32_t mic_gain(int32_t db);
TINYRT_IMPORT(mic_open) int32_t mic_open(uint32_t sample_rate,uint32_t flags);
TINYRT_IMPORT(mic_read) int32_t mic_read(void *dst,uint32_t length);
TINYRT_IMPORT(now_ms) uint32_t now_ms(void);
TINYRT_IMPORT(random_u32) uint32_t random_u32(void);
/* 0 instruction-metered interpreter,1 deadline-guarded AOT. */
TINYRT_IMPORT(runtime_backend) int32_t runtime_backend(void);
typedef struct {
    uint32_t api_level, screen_w, screen_h, safe_x, safe_y, safe_w, safe_h;
    uint32_t mem_pages_max, budget_max;
} tinyrt_sys_info_t;
typedef struct {
    tinyrt_sys_info_t base;
    uint32_t text_bytes_max,image_count_max,commands_max,kv_keys_max;
} tinyrt_sys_info_v4_t;
TINYRT_IMPORT(screen_keep_on) int32_t screen_keep_on(int32_t on);
typedef struct {
    tinyrt_sys_info_v4_t quotas;
    uint32_t log_dropped;
} tinyrt_sys_info_diagnostics_t;
/* Writes the largest complete record that fits (36, 52 or 56 bytes). */
TINYRT_IMPORT(sys_info) int32_t sys_info(void *destination,uint32_t length);
TINYRT_IMPORT(time_local) int32_t time_local(void *dst,uint32_t length);
TINYRT_IMPORT(time_subscribe) int32_t time_subscribe(uint32_t flags);
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
#define TINYRT_EV_GAMEPAD 8
#define TINYRT_IN_GAMEPAD 0x100u
/* GAMEPAD: x=buttons (bit16..23 always 0), y=LX|LY<<8|RX<<16|RY<<24 (int8 each, +X right,
 * +Y down), arg=LT|RT<<8|flags<<16, flags bit0 CONNECTED. Full state per event; accessory
 * HOME is reserved by the host. Older firmware rejects 0x100: retry without it on failure. */
#define TINYRT_PAD_A (1u<<0)
#define TINYRT_PAD_B (1u<<1)
#define TINYRT_PAD_X (1u<<2)
#define TINYRT_PAD_Y (1u<<3)
#define TINYRT_PAD_LB (1u<<4)
#define TINYRT_PAD_RB (1u<<5)
#define TINYRT_PAD_LT (1u<<6)
#define TINYRT_PAD_RT (1u<<7)
#define TINYRT_PAD_SELECT (1u<<8)
#define TINYRT_PAD_START (1u<<9)
#define TINYRT_PAD_L3 (1u<<10)
#define TINYRT_PAD_R3 (1u<<11)
#define TINYRT_PAD_UP (1u<<12)
#define TINYRT_PAD_DOWN (1u<<13)
#define TINYRT_PAD_LEFT (1u<<14)
#define TINYRT_PAD_RIGHT (1u<<15)
#define TINYRT_PAD_FN(n) (1u<<(23+(n)))
#define TINYRT_PAD_LX(y) ((int8_t)((y)&0xff))
#define TINYRT_PAD_LY(y) ((int8_t)(((y)>>8)&0xff))
#define TINYRT_PAD_RX(y) ((int8_t)(((y)>>16)&0xff))
#define TINYRT_PAD_RY(y) ((int8_t)(((uint32_t)(y)>>24)&0xff))
#define TINYRT_PAD_LT_VALUE(a) ((a)&0xff)
#define TINYRT_PAD_RT_VALUE(a) (((a)>>8)&0xff)
#define TINYRT_PAD_CONNECTED(a) (((a)>>16)&1)
/* USER_KEY_EVENT: x=1, y=1 pressed / 0 released, arg=0. Opt in during init.
 * Accept masks 0, 0x38, 0x40 or 0x78, optionally OR 0x80 and/or 0x100. CANCEL clears touch and keys. */
/* Export these exact signatures. Return 0 on success. Each host call is bounded;
 * only render may draw, and every drawn frame starts with draw_clear. Text is UTF-8,
 * 1..63 bytes (Level 4: 95). Storage keys 0..15 (Level 4: 0..23) contain app-scoped i32 values.
 * Avoid WASI, constructors and unbounded loops. S31 accelerates single precision;
 * measure double and hot loops on the device. */
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
 * align=0 left, 1 center, 2 right. Text is UTF-8, 1..63 bytes (Level 4: 95).
 * RGB565: one image per frame (Level 4: two, 122880 total bytes), 1..256 wide, 1..240 high, inside viewport;
 * length=w*h*2, little-endian packed bytes, copied immediately; no scaling.
 * draw_skip: only as the sole render operation, including no draw_clear; keep
 * the previous displayed frame. Requires DRAW permission like other drawing.
 * clock_interval: CLOCK permission, init/event only, 1..1000 ms, default 100;
 * host schedules one next event after completion, without catch-up bursts. */
#endif
