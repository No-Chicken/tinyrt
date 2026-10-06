#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "platform_api_vmcore.h"
#include "esp_cache.h"
#include "esp_mmu_map.h"
#include "tinyrt_esp_exec_memory.h"

#if TEST_UNIFIED_ADDRESS
#define ALIAS_DELTA ((uintptr_t)0)
#else
#define ALIAS_DELTA ((uintptr_t)0x10000000)
#endif
static unsigned allocations, frees, cache_calls;
static int fail_allocate, fail_alias, fail_cache, fail_unmap_alias;
static void *live_data;
static size_t live_size;
static struct { void *pointer; size_t size; } blocks[64];

void *heap_caps_aligned_alloc(size_t align, size_t size, unsigned caps)
{
    (void)caps;
    if (fail_allocate) return NULL;
    live_data = _aligned_malloc(size, align);
    live_size = size;
    if (live_data) {
        allocations++;
        unsigned i;
        for (i = 0; i < 64; ++i) if (!blocks[i].pointer) break;
        assert(i < 64);
        blocks[i].pointer = live_data;
        blocks[i].size = size;
    }
    return live_data;
}
void heap_caps_free(void *p)
{
    unsigned i;
    for (i = 0; i < 64; ++i) if (blocks[i].pointer == p) break;
    assert(i < 64);
    blocks[i].pointer = NULL;
    frees++;
    _aligned_free(p);
    live_data = NULL;
}
int esp_mmu_vaddr_to_paddr(void *p, esp_paddr_t *pa, mmu_target_t *target)
{
    if (fail_unmap_alias) return -1;
    for (unsigned i = 0; i < 64; ++i) {
        if (!blocks[i].pointer) continue;
        uintptr_t ptr = (uintptr_t)p, base = (uintptr_t)blocks[i].pointer;
        if (ptr >= base + ALIAS_DELTA && ptr < base + ALIAS_DELTA + blocks[i].size) ptr -= ALIAS_DELTA;
        if (ptr < base || ptr >= base + blocks[i].size) continue;
        *pa = (esp_paddr_t)(i * 0x100000 + ptr - base);
        *target = MMU_TARGET_PSRAM0;
        return 0;
    }
    return -1;
}
int esp_mmu_paddr_to_vaddr(esp_paddr_t pa, mmu_target_t target, mmu_vaddr_t type, void **out)
{
    (void)target;
    unsigned i = pa / 0x100000;
    size_t offset = pa % 0x100000;
    if (i >= 64 || !blocks[i].pointer || offset >= blocks[i].size) return -1;
    if (fail_alias && type == MMU_VADDR_INSTRUCTION && offset >= 65536) return -1;
    *out = (void *)((uintptr_t)blocks[i].pointer + offset + (type == MMU_VADDR_INSTRUCTION ? ALIAS_DELTA : 0));
    return 0;
}
int esp_cache_msync(void *p, size_t size, int flags)
{
    assert(((uintptr_t)p & 63) == 0 && (size & 63) == 0);
    assert(!(flags & ESP_CACHE_MSYNC_FLAG_UNALIGNED));
    cache_calls++;
    if (fail_cache) return -1;
    return 0;
}

static void empty(void)
{
    tinyrt_esp_exec_memory_stats_t stats;
    tinyrt_esp_exec_memory_get_stats(&stats);
    assert(stats.mapped_bytes == 0 && stats.mapping_count == 0);
    assert(allocations == frees);
}

int main(int argc, char **argv)
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    void *code, *data;
    const int rwx = MMAP_PROT_READ | MMAP_PROT_WRITE | MMAP_PROT_EXEC;
    if (argc == 2 && strcmp(argv[1], "--broken-unmap-alias") == 0) {
        code = os_mmap(NULL, 64, rwx, 0, -1);
        assert(code && os_mprotect(code, 64, MMAP_PROT_READ | MMAP_PROT_EXEC) == 0);
        fail_unmap_alias = 1;
        puts("injecting sealed mapping ownership failure");
        fflush(stdout);
        os_munmap(code, 64);
        puts("ERROR: execution continued after ownership failure");
        return 0;
    }
    empty();
    assert(!os_mmap(NULL, 0, rwx, 0, -1));
    assert(!os_mmap(NULL, SIZE_MAX, rwx, 0, -1));
    assert(!os_mmap(NULL, TINYRT_ESP_AOT_MAP_BUDGET + 1, rwx, 0, -1));
    assert(!os_mmap(NULL, 64, rwx, MMAP_MAP_FIXED, -1));
    fail_allocate = 1;
    assert(!os_mmap(NULL, 64, rwx, 0, -1));
    fail_allocate = 0;
    empty();
    fail_alias = 1;
    assert(!os_mmap(NULL, 131072, rwx, 0, -1));
    fail_alias = 0;
    empty();
    void *first = os_mmap(NULL, TINYRT_ESP_AOT_MAP_BUDGET / 2, rwx, 0, -1);
    void *second = os_mmap(NULL, TINYRT_ESP_AOT_MAP_BUDGET / 2, rwx, 0, -1);
    assert(first && second && first != second);
    assert(!os_mmap(NULL, 1, rwx, 0, -1));
    os_munmap(first, TINYRT_ESP_AOT_MAP_BUDGET / 2);
    os_munmap(second, TINYRT_ESP_AOT_MAP_BUDGET / 2);
    empty();
    for (unsigned i = 0; i < 50; ++i) {
        code = os_mmap(NULL, 129, rwx, 0, -1);
        assert(code && ((uintptr_t)code & 63) == 0);
        data = os_get_dbus_mirror(code);
        assert(data == live_data && (ALIAS_DELTA ? code != data : code == data));
        for (unsigned j = 0; j < 192; ++j) assert(((unsigned char *)data)[j] == 0);
        assert(os_get_dbus_mirror((char *)code + 128) == (char *)data + 128);
        memset(data, 0xA5, 129);
        os_dcache_flush();
        assert(os_mprotect((char *)code + 64, 65, MMAP_PROT_READ | MMAP_PROT_EXEC) != 0);
        assert(os_mprotect(code, 128, MMAP_PROT_READ | MMAP_PROT_EXEC) != 0);
        assert(os_mprotect(code, 129, MMAP_PROT_READ | MMAP_PROT_EXEC) == 0);
        assert(os_get_dbus_mirror(code) == NULL);
        assert(os_mprotect(code, 129, rwx) != 0);
        os_icache_flush(code, 129);
        os_munmap(code, 129);
        empty();
    }
    code = os_mmap(NULL, 64, rwx, 0, -1);
    assert(code);
    fail_cache = 1;
    assert(os_mprotect(code, 64, MMAP_PROT_READ | MMAP_PROT_EXEC) != 0);
    fail_cache = 0;
    os_munmap(code, 64);
    empty();
    data = os_mmap(NULL, 9, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1);
    assert(data == live_data && os_get_dbus_mirror(data) == data);
    assert(os_mprotect(data, 9, MMAP_PROT_READ) == 0);
    os_munmap(data, 9);
    empty();
    data = os_mmap(NULL, 129, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1);
    assert(data);
    assert(os_mprotect((char *)data + 64, 65, MMAP_PROT_READ) == 0);
    assert(os_mprotect((char *)data + 64, 66, MMAP_PROT_READ) != 0);
    assert(os_mprotect((char *)data + 130, 1, MMAP_PROT_READ) != 0);
    assert(os_mprotect((char *)data + 64, 64, MMAP_PROT_READ | MMAP_PROT_EXEC) != 0);
    os_munmap(data, 129);
    empty();
    assert(cache_calls >= 200);
    puts("exec memory: limits, alias pages, cache failures, sealed aliases, 50 lifetimes passed");
    return 0;
}
