#pragma once
#include <stddef.h>

/* Native AOT mappings have a separate, bounded PSRAM allocation budget. */
#ifndef TINYRT_ESP_AOT_MAP_BUDGET
#define TINYRT_ESP_AOT_MAP_BUDGET (512u * 1024u)
#endif

typedef struct {
    size_t mapped_bytes;
    size_t mapping_count;
} tinyrt_esp_exec_memory_stats_t;

void tinyrt_esp_exec_memory_get_stats(tinyrt_esp_exec_memory_stats_t *stats);
