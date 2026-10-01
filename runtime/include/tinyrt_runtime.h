#ifndef TINYRT_RUNTIME_H
#define TINYRT_RUNTIME_H
#include "tinyrt_package.h"
#ifdef __cplusplus
extern "C" {
#endif
#define TINYRT_FRAME_MAX_COMMANDS 128u
#define TINYRT_TEXT_MAX_BYTES 63u
#define TINYRT_RUNTIME_HEAP_LIMIT (2u * 1024u * 1024u)
typedef struct tinyrt_runtime tinyrt_runtime_t;
typedef enum { TINYRT_DRAW_CLEAR=1, TINYRT_DRAW_RECT=2, TINYRT_DRAW_TEXT=3 } tinyrt_draw_kind_t;
typedef struct {
    uint32_t kind;
    int32_t x, y, w, h;
    uint32_t rgb;
    char text[64];
} tinyrt_draw_command_t;
typedef struct { uint32_t count; tinyrt_draw_command_t commands[TINYRT_FRAME_MAX_COMMANDS]; } tinyrt_frame_t;
typedef struct {
    void *ctx;
    int32_t (*kv_get)(void *, uint32_t key, int32_t fallback);
    tinyrt_status_t (*kv_set)(void *, uint32_t key, int32_t value);
    uint32_t (*now_ms)(void *);
} tinyrt_runtime_host_t;
/* All APIs are serialized by ONE owner thread. ESP-IDF owner MUST be a
 * pthread, not a raw FreeRTOS task (WAMR port calls pthread_self). Call init
 * once on that thread, destroy every instance before shutdown. Validator uses
 * the same thread and initialized runtime. No API may run from the LVGL task.
 * Host callbacks must be bounded RAM operations, never NVS commit or waits.
 * Guest calls can mutate host RAM; caller checkpoints/rolls back on failure. */
tinyrt_status_t tinyrt_runtime_system_init(void);
void tinyrt_runtime_system_shutdown(void);
/* Tracked allocator bytes, including module copy/frame; excludes system
 * thread/lock overhead and allocator headers. Owner-thread telemetry only. */
size_t tinyrt_runtime_memory_used(void);
tinyrt_status_t tinyrt_runtime_validate_wasm(void *, const tinyrt_store_io_t *,
    uint32_t absolute_wasm_offset, uint32_t wasm_size, const tinyrt_package_policy_t *);
/* Copies bytes; does not execute guest code. Requires ABI exports/signatures,
 * bounded memory and imports; rejects implicit initialization entry points. */
tinyrt_status_t tinyrt_runtime_create(const void *, uint32_t,
    const tinyrt_package_policy_t *, const tinyrt_runtime_host_t *, tinyrt_runtime_t **);
tinyrt_status_t tinyrt_runtime_init(tinyrt_runtime_t *, int32_t width, int32_t height);
tinyrt_status_t tinyrt_runtime_event(tinyrt_runtime_t *, int32_t kind, int32_t x, int32_t y, int32_t arg);
/* Complete deep copy only on success. Failure leaves output unchanged and
 * poisons the instance: only last_error/destroy remain valid. Every render
 * starts a fresh frame; first command must clear it, no retained guest pointer. */
tinyrt_status_t tinyrt_runtime_render(tinyrt_runtime_t *, tinyrt_frame_t *);
const char *tinyrt_runtime_last_error(const tinyrt_runtime_t *);
void tinyrt_runtime_destroy(tinyrt_runtime_t *);
#ifdef __cplusplus
}
#endif
#endif
