#ifndef TEST_ESP_TIMER_H
#define TEST_ESP_TIMER_H
#include "esp_err.h"
#include <stdint.h>
typedef struct timer_stub *esp_timer_handle_t;
typedef struct { void (*callback)(void *);void *arg;int dispatch_method;const char *name; } esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
esp_err_t esp_timer_start_once_at(esp_timer_handle_t,uint64_t);
esp_err_t esp_timer_stop_blocking(esp_timer_handle_t,uint32_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
#endif
