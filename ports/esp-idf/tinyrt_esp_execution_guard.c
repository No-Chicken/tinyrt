#include "tinyrt_esp_execution_guard.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stdlib.h>

struct tinyrt_esp_execution_guard {
    tinyrt_execution_guard_t config;
    esp_timer_handle_t timer;
    esp_timer_handle_t recovery_timer;
    void (*cancel)(void *);
    void *cancel_ctx;
    void (*recover)(void *);
    void *recovery_ctx;
    uint32_t recovery_grace_ms;
    bool armed;
};
static void cancel_expired(void *ctx)
{
    tinyrt_esp_execution_guard_t *g=ctx;
    if(g->recover) {
        uint64_t deadline=(uint64_t)esp_timer_get_time()+(uint64_t)g->recovery_grace_ms*1000;
        ESP_ERROR_CHECK(esp_timer_start_once_at(g->recovery_timer,deadline));
    }
    g->cancel(g->cancel_ctx);
}
static void recovery_expired(void *ctx)
{
    tinyrt_esp_execution_guard_t *g=ctx;
    g->recover(g->recovery_ctx);
}
static tinyrt_status_t arm(void *ctx,void (*cancel)(void *),void *arg,uint32_t ms)
{
    tinyrt_esp_execution_guard_t *g=ctx;
    if(g->armed)return TINYRT_BUSY;
    g->cancel=cancel;g->cancel_ctx=arg;
    int64_t deadline=esp_timer_get_time()+(int64_t)ms*1000;
    if(esp_timer_start_once_at(g->timer,(uint64_t)deadline)!=ESP_OK) {
        g->cancel=NULL;g->cancel_ctx=NULL;return TINYRT_IO_ERROR;
    }
    g->armed=true;return TINYRT_OK;
}
static void disarm(void *ctx)
{
    tinyrt_esp_execution_guard_t *g=ctx;
    if(!g->armed)return;
    /* A failed join cannot safely release the borrowed runtime pointer. The
     * adapter requires normal owner-task context and treats API misuse as fatal. */
    ESP_ERROR_CHECK(esp_timer_stop_blocking(g->timer,portMAX_DELAY));
    /* Join the primary first: its in-flight callback may arm recovery. */
    ESP_ERROR_CHECK(esp_timer_stop_blocking(g->recovery_timer,portMAX_DELAY));
    g->armed=false;g->cancel=NULL;g->cancel_ctx=NULL;
}
tinyrt_status_t tinyrt_esp_execution_guard_create(uint32_t ms,tinyrt_esp_execution_guard_t **out)
{
    if(!out)return TINYRT_INVALID_ARGUMENT;
    *out=NULL;
    if(!ms||ms>TINYRT_CALLBACK_TIMEOUT_MAX_MS)return TINYRT_INVALID_ARGUMENT;
    tinyrt_esp_execution_guard_t *g=calloc(1,sizeof(*g));
    if(!g)return TINYRT_NO_MEMORY;
    esp_timer_create_args_t args={.callback=cancel_expired,.arg=g,.dispatch_method=ESP_TIMER_TASK,.name="tinyrt_guard"};
    if(esp_timer_create(&args,&g->timer)!=ESP_OK){free(g);return TINYRT_NO_MEMORY;}
    args.callback=recovery_expired;args.name="tinyrt_recover";
    if(esp_timer_create(&args,&g->recovery_timer)!=ESP_OK) {
        ESP_ERROR_CHECK(esp_timer_delete(g->timer));free(g);return TINYRT_NO_MEMORY;
    }
    g->config=(tinyrt_execution_guard_t){g,arm,disarm,ms};*out=g;return TINYRT_OK;
}
const tinyrt_execution_guard_t *tinyrt_esp_execution_guard_config(tinyrt_esp_execution_guard_t *g)
{
    return g?&g->config:NULL;
}
tinyrt_status_t tinyrt_esp_execution_guard_set_recovery(tinyrt_esp_execution_guard_t *g,
    void (*recover)(void *),void *ctx,uint32_t grace_ms)
{
    if(!g || (recover && (grace_ms<500 || grace_ms>2000)))return TINYRT_INVALID_ARGUMENT;
    if(g->armed)return TINYRT_BUSY;
    g->recover=recover;g->recovery_ctx=ctx;g->recovery_grace_ms=grace_ms;return TINYRT_OK;
}
void tinyrt_esp_execution_guard_destroy(tinyrt_esp_execution_guard_t *g)
{
    if(!g)return;
    disarm(g);ESP_ERROR_CHECK(esp_timer_delete(g->timer));
    ESP_ERROR_CHECK(esp_timer_delete(g->recovery_timer));free(g);
}
