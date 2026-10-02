#ifndef TINYRT_ESP_EXECUTION_GUARD_H
#define TINYRT_ESP_EXECUTION_GUARD_H
#include "tinyrt_runtime.h"
typedef struct tinyrt_esp_execution_guard tinyrt_esp_execution_guard_t;
#define TINYRT_ESP_RECOVERY_VERSION 1
/* Owner pthread calls create/destroy, outside an esp_timer callback. One guard
 * serves one serialized manager. Requires ESP-IDF 6.1 stop_blocking semantics. */
tinyrt_status_t tinyrt_esp_execution_guard_create(uint32_t timeout_ms,tinyrt_esp_execution_guard_t **out);
const tinyrt_execution_guard_t *tinyrt_esp_execution_guard_config(tinyrt_esp_execution_guard_t *);
/* Optional host-only second deadline, configured while idle. The callback must
 * be bounded and nonblocking, and ctx must outlive the adapter. It must not call
 * guest/runtime APIs or perform storage IO. NULL disables recovery; otherwise
 * grace_ms must be 500..2000. The product must prepare durable recovery state
 * before starting a callback and install its own controlled recovery hook. */
tinyrt_status_t tinyrt_esp_execution_guard_set_recovery(tinyrt_esp_execution_guard_t *,
    void (*recover)(void *),void *ctx,uint32_t grace_ms);
/* Call after manager close. Waits for any already-dispatched cancellation. */
void tinyrt_esp_execution_guard_destroy(tinyrt_esp_execution_guard_t *);
#endif
