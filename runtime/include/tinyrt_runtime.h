#ifndef TINYRT_RUNTIME_H
#define TINYRT_RUNTIME_H
#include "tinyrt_package.h"
#ifdef __cplusplus
extern "C" {
#endif
#define TINYRT_FRAME_MAX_COMMANDS 128u
#define TINYRT_TEXT_MAX_BYTES 63u
#define TINYRT_FRAME_MAX_PIXEL_BYTES (256u * 240u * 2u)
#define TINYRT_AUDIO_MAX_BYTES 32000u
#define TINYRT_AUDIO_SAMPLE_RATE 16000u
#define TINYRT_DEFAULT_CLOCK_INTERVAL_MS 100u
#define TINYRT_RUNTIME_HEAP_LIMIT (2u * 1024u * 1024u)
typedef struct tinyrt_runtime tinyrt_runtime_t;
#define TINYRT_EXECUTION_GUARD_VERSION 1
#define TINYRT_CALLBACK_TIMEOUT_MAX_MS 3000u
#define TINYRT_INPUT_EVENTS_MASK 0x38u
typedef struct {
    void *ctx;
    tinyrt_status_t (*arm)(void *, void (*cancel)(void *), void *cancel_ctx, uint32_t timeout_ms);
    void (*disarm)(void *);
    uint32_t timeout_ms;
} tinyrt_execution_guard_t;
#define TINYRT_RUNTIME_AOT_VERSION 1
typedef struct {
    const tinyrt_package_aot_profile_t *profile;
    const tinyrt_execution_guard_t *guard;
} tinyrt_runtime_aot_config_t;
typedef enum { TINYRT_DRAW_CLEAR=1, TINYRT_DRAW_RECT=2, TINYRT_DRAW_TEXT=3,
    TINYRT_DRAW_ROUND_RECT=4, TINYRT_DRAW_ARC=5, TINYRT_DRAW_TEXT_BOX=6,
    TINYRT_DRAW_RGB565=7, TINYRT_DRAW_RGB565_SCALED=8 } tinyrt_draw_kind_t;
typedef struct {
    uint32_t kind;
    int32_t x, y, w, h;
    uint32_t rgb;
    int32_t radius, thickness, start_angle, end_angle, font_px, align;
    int32_t source_w, source_h;
    char text[64];
} tinyrt_draw_command_t;
typedef struct {
    uint32_t count;
    tinyrt_draw_command_t commands[TINYRT_FRAME_MAX_COMMANDS];
    uint32_t pixel_bytes;
    uint8_t pixels[TINYRT_FRAME_MAX_PIXEL_BYTES];
} tinyrt_frame_t;
typedef struct {
    void *ctx;
    int32_t (*kv_get)(void *, uint32_t key, int32_t fallback);
    tinyrt_status_t (*kv_set)(void *, uint32_t key, int32_t value);
    uint32_t (*now_ms)(void *);
    /* Current verified package resources only. At most 4096 bytes per call;
     * no directory lookup, mutation, or unrelated package access. */
    tinyrt_status_t (*asset_read)(void *,uint32_t offset,void *,uint32_t length);
    /* Must validate the current resource range and own any queued copy.
     * BUSY means dropped/unavailable without poisoning the guest. */
    tinyrt_status_t (*audio_play)(void *,uint32_t offset,uint32_t length,uint32_t sample_rate);
} tinyrt_runtime_host_t;
/* All APIs are serialized by ONE owner thread. ESP-IDF owner MUST be a
 * pthread, not a raw FreeRTOS task (WAMR port calls pthread_self). Call init
 * once on that thread, destroy every instance before shutdown. Validator uses
 * the same thread and initialized runtime. No API may run from the LVGL task.
 * Host callbacks must be bounded RAM operations, never NVS commit or waits.
 * Guest calls can mutate host RAM; caller checkpoints/rolls back on failure. */
tinyrt_status_t tinyrt_runtime_system_init(void);
void tinyrt_runtime_system_shutdown(void);
/* Tracked allocator bytes, including module copy, plus AOT mapping reservations
 * rounded conservatively to 64 KiB per mapping. Excludes caller package/prefetch
 * buffers, caller frames, system thread/lock overhead and allocator headers.
 * Owner-thread telemetry only; reservations can exceed physical mapping bytes. */
size_t tinyrt_runtime_memory_used(void);
/* High-water tracked bytes/reservations since the most recent system init;
 * retained after shutdown for diagnostics, reset by the next fresh init. */
size_t tinyrt_runtime_memory_peak(void);
tinyrt_status_t tinyrt_runtime_validate_wasm(void *, const tinyrt_store_io_t *,
    uint32_t absolute_wasm_offset, uint32_t wasm_size, const tinyrt_package_policy_t *);
/* Copies bytes; does not execute guest code. Requires ABI exports/signatures,
 * bounded memory and imports; rejects implicit initialization entry points. */
tinyrt_status_t tinyrt_runtime_create(const void *, uint32_t,
    const tinyrt_package_policy_t *, const tinyrt_runtime_host_t *, tinyrt_runtime_t **);
/* AOT is an explicit build-time opt-in. The host profile and signed metadata
 * must come from the trusted package verifier, never from an unauthenticated
 * caller. Machine code safety depends on that trusted compilation pipeline.
 * Config requires a deadline guard; AOT uses it instead of instruction counts.
 * The validator context is a tinyrt_runtime_aot_config_t; validation does not
 * execute guest code. Creation copies guard configuration and module bytes. */
bool tinyrt_runtime_aot_supported(void);
tinyrt_status_t tinyrt_runtime_validate_aot(void *, const tinyrt_store_io_t *,
    uint32_t absolute_aot_offset, uint32_t aot_size, const tinyrt_package_policy_t *,
    const tinyrt_package_aot_metadata_t *);
tinyrt_status_t tinyrt_runtime_create_aot(const void *, uint32_t,
    const tinyrt_package_policy_t *, const tinyrt_runtime_host_t *,
    const tinyrt_runtime_aot_config_t *, const tinyrt_package_aot_metadata_t *, tinyrt_runtime_t **);
/* Set before init; NULL disables the optional Wasm deadline guard. AOT requires
 * a guard and rejects NULL. A configured
 * guard arms once per callback with an absolute timeout (1..3000 ms), never on
 * host imports. Successful arm must be paired with synchronous disarm that
 * waits for all cancellation callbacks before returning. Failed arm must leave
 * no callback pending. Context must outlive this runtime. */
tinyrt_status_t tinyrt_runtime_set_execution_guard(tinyrt_runtime_t *, const tinyrt_execution_guard_t *);
/* The guard's cancel callback uses this atomic-only operation. A supervising
 * thread may call it only while the owner keeps the runtime alive through the
 * arm/disarm interval. All other runtime APIs remain owner-thread-only. */
void tinyrt_runtime_request_cancel(tinyrt_runtime_t *);
tinyrt_status_t tinyrt_runtime_init(tinyrt_runtime_t *, int32_t width, int32_t height);
tinyrt_status_t tinyrt_runtime_event(tinyrt_runtime_t *, int32_t kind, int32_t x, int32_t y, int32_t arg);
/* Optional ()i export; absent is a no-op. Once successfully initialized, call
 * at most once under the normal callback budget. No drawing; no calls after
 * failure. Owner checkpoints/rolls back RAM KV and must destroy afterwards. */
tinyrt_status_t tinyrt_runtime_stop(tinyrt_runtime_t *);
/* Draws directly into caller-owned, exclusively writable, unpublished storage.
 * The pointer is borrowed only during this call. Publish only TINYRT_OK frames
 * with count > 0; only commands[0..count) and pixels[0..pixel_bytes) are valid.
 * Unused tails are untouched. draw_skip changes only count to zero; retain the
 * previous display. Every failure with non-NULL out also sets count to zero;
 * other output contents are unspecified and are not rolled back. Guest failure
 * poisons the instance: only last_error/destroy remain valid. Every render
 * starts a fresh frame; first command must clear it, no retained guest pointer. */
tinyrt_status_t tinyrt_runtime_render(tinyrt_runtime_t *, tinyrt_frame_t *);
/* Current scheduling request, 1..1000 ms; NULL/not running returns 100 ms.
 * Host schedules one next callback after completion; no catch-up loop. */
uint32_t tinyrt_runtime_clock_interval_ms(const tinyrt_runtime_t *);
/* Optional PRESS=3/MOVE=4/CANCEL=5 subscription bits; zero for legacy, stopped,
 * failed or uninitialized instances. RELEASE=1 and CLOCK=2 remain unchanged. */
uint32_t tinyrt_runtime_input_events(const tinyrt_runtime_t *);
const char *tinyrt_runtime_last_error(const tinyrt_runtime_t *);
void tinyrt_runtime_destroy(tinyrt_runtime_t *);
#ifdef __cplusplus
}
#endif
#endif
