/* Executable PSRAM mappings using the public ESP-IDF MMU/cache APIs. */
#include "platform_api_vmcore.h"
#include "tinyrt_esp_exec_memory.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_mmu_map.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>

#define MAX_MAPPINGS 64u
#if CONFIG_IDF_TARGET_ESP32S31
/* S31 uses a unified PSRAM address window and 64-byte L1 cache lines. */
#define CACHE_ALIGNMENT ((size_t)CONFIG_CACHE_L1_DCACHE_LINE_SIZE)
#else
#define CACHE_ALIGNMENT ((size_t)(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE > CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE \
    ? CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE : CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE))
#endif

typedef struct {
    void *address;
    void *writable;
    size_t requested, allocated;
    bool executable, sealed, cache_failed;
} mapping_t;
static mapping_t s_maps[MAX_MAPPINGS];
static size_t s_bytes, s_count;
/* MMU/cache and heap calls may block; a task mutex covers mapping lifetimes. */
static _lock_t s_lock;

static mapping_t *find_mapping(const void *ptr)
{
    uintptr_t address = (uintptr_t)ptr;
    for (size_t i = 0; i < MAX_MAPPINGS; ++i) {
        uintptr_t base = (uintptr_t)s_maps[i].address;
        if (base && address >= base && address - base < s_maps[i].allocated) return &s_maps[i];
    }
    return NULL;
}

static bool translate(void *ptr, mmu_vaddr_t type, void **out)
{
    esp_paddr_t physical;
    mmu_target_t target;
    return esp_mmu_vaddr_to_paddr(ptr, &physical, &target) == ESP_OK
        && target == MMU_TARGET_PSRAM0
        && esp_mmu_paddr_to_vaddr(physical, target, type, out) == ESP_OK;
}

static bool check_alias(void *data, size_t offset, void **instruction)
{
    void *round_trip, *point = (void *)((uintptr_t)data + offset);
    return translate(point, MMU_VADDR_INSTRUCTION, instruction)
        && translate(*instruction, MMU_VADDR_DATA, &round_trip) && round_trip == point;
}

static bool executable_alias(void *data, size_t size, void **instruction)
{
    void *next;
    if (!check_alias(data, 0, instruction)) return false;
    /* Validate every spanned MMU page, not just the XIP prefix or first page. */
    size_t offset = CONFIG_MMU_PAGE_SIZE - ((uintptr_t)data % CONFIG_MMU_PAGE_SIZE);
    for (; offset < size; offset += CONFIG_MMU_PAGE_SIZE) {
        if (!check_alias(data, offset, &next)
            || (uintptr_t)next != (uintptr_t)*instruction + offset) return false;
    }
    return check_alias(data, size - 1, &next)
        && (uintptr_t)next == (uintptr_t)*instruction + size - 1;
}

static bool synchronize(mapping_t *map)
{
    if (esp_cache_msync(map->writable, map->allocated,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK
        || esp_cache_msync(map->address, map->allocated,
                           ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST) != ESP_OK) {
        map->cache_failed = true;
        return false;
    }
    return true;
}

void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    (void)hint; (void)file;
    if (!size || size > TINYRT_ESP_AOT_MAP_BUDGET
        || size > SIZE_MAX - (CACHE_ALIGNMENT - 1) || (flags & MMAP_MAP_FIXED)) return NULL;
    size_t rounded = (size + CACHE_ALIGNMENT - 1) & ~(CACHE_ALIGNMENT - 1);
    void *result = NULL;
    _lock_acquire(&s_lock);
    if (rounded > TINYRT_ESP_AOT_MAP_BUDGET - s_bytes) goto done;
    mapping_t *map = NULL;
    for (size_t i = 0; i < MAX_MAPPINGS; ++i) {
        if (!s_maps[i].address) { map = &s_maps[i]; break; }
    }
    if (!map) goto done;
    void *data = heap_caps_aligned_alloc(CACHE_ALIGNMENT, rounded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) goto done;
    void *address = data;
    bool executable = (prot & MMAP_PROT_EXEC) != 0;
    if (executable && !executable_alias(data, rounded, &address)) {
        heap_caps_free(data);
        goto done;
    }
    /* All writes, including alignment padding, use the data bus. */
    memset(data, 0, rounded);
    *map = (mapping_t){ address, data, size, rounded, executable, false, false };
    s_bytes += rounded;
    s_count++;
    result = address;
done:
    _lock_release(&s_lock);
    return result;
}

void os_munmap(void *addr, size_t size)
{
    (void)size;
    if (!addr) return;
    _lock_acquire(&s_lock);
    mapping_t *map = find_mapping(addr);
    if (map && map->address == addr) {
        void *allocation = map->writable;
        if (!allocation && !translate(map->address, MMU_VADDR_DATA, &allocation)) {
            /* The loader's void munmap ABI would release its budget on return.
             * Lost ownership must stop execution before that accounting runs. */
            _lock_release(&s_lock);
            abort();
        }
        heap_caps_free(allocation);
        s_bytes -= map->allocated;
        s_count--;
        memset(map, 0, sizeof(*map));
    }
    _lock_release(&s_lock);
}

int os_mprotect(void *addr, size_t size, int prot)
{
    int result = -1;
    _lock_acquire(&s_lock);
    mapping_t *map = find_mapping(addr);
    if (!map) goto done;
    size_t offset = (uintptr_t)addr - (uintptr_t)map->address;
    if (offset > map->requested || size > map->requested - offset) goto done;
    if (!map->executable) { result = (prot & MMAP_PROT_EXEC) ? -1 : 0; goto done; }
    if (offset != 0 || size != map->requested) goto done;
    if (map->sealed) {
        result = (prot == (MMAP_PROT_READ | MMAP_PROT_EXEC)) ? 0 : -1;
        goto done;
    }
    if (prot & MMAP_PROT_WRITE) { result = 0; goto done; }
    if (prot != (MMAP_PROT_READ | MMAP_PROT_EXEC) || map->cache_failed || !synchronize(map)) goto done;
    map->sealed = true;
    map->writable = NULL;
    result = 0;
done:
    _lock_release(&s_lock);
    return result;
}

void *os_get_dbus_mirror(void *ptr)
{
    _lock_acquire(&s_lock);
    mapping_t *map = find_mapping(ptr);
    void *result = ptr;
    if (map && map->executable) result = map->sealed ? NULL
        : (void *)((uintptr_t)map->writable + (uintptr_t)ptr - (uintptr_t)map->address);
    _lock_release(&s_lock);
    return result;
}

void os_dcache_flush(void)
{
    _lock_acquire(&s_lock);
    for (size_t i = 0; i < MAX_MAPPINGS; ++i) {
        mapping_t *map = &s_maps[i];
        if (map->address && map->executable && !map->sealed) synchronize(map);
    }
    _lock_release(&s_lock);
}

void os_icache_flush(void *start, size_t len)
{
    (void)len;
    _lock_acquire(&s_lock);
    mapping_t *map = find_mapping(start);
    /* Sealing already completed the checked cache synchronization. */
    if (map && map->executable && !map->sealed) synchronize(map);
    _lock_release(&s_lock);
}

void *os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    _lock_acquire(&s_lock);
    mapping_t *old_map = find_mapping(old_addr);
    bool permitted = old_map && old_map->address == old_addr && !old_map->executable
        && old_size <= old_map->requested;
    _lock_release(&s_lock);
    if (!permitted) return NULL;
    void *replacement = os_mmap(NULL, new_size, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, 0);
    if (!replacement) return NULL;
    memcpy(replacement, old_addr, old_size < new_size ? old_size : new_size);
    os_munmap(old_addr, old_size);
    return replacement;
}

void tinyrt_esp_exec_memory_get_stats(tinyrt_esp_exec_memory_stats_t *stats)
{
    if (!stats) return;
    _lock_acquire(&s_lock);
    stats->mapped_bytes = s_bytes;
    stats->mapping_count = s_count;
    _lock_release(&s_lock);
}
