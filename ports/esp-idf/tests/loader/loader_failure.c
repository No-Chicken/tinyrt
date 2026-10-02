#include "wasm_export.h"
#include "platform_api_vmcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned protect_failures;
static unsigned outstanding_mappings;
int real_os_mprotect(void *addr, size_t size, int prot);
void *real_os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file);
void real_os_munmap(void *addr, size_t size);
void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *result = real_os_mmap(hint, size, prot, flags, file);
    if (result) outstanding_mappings++;
    return result;
}
void os_munmap(void *addr, size_t size)
{
    if (addr) outstanding_mappings--;
    real_os_munmap(addr, size);
}
int os_mprotect(void *addr, size_t size, int prot)
{
    if (prot == (MMAP_PROT_READ | MMAP_PROT_EXEC)) { protect_failures++; return -1; }
    return real_os_mprotect(addr, size, prot);
}
int main(int argc, char **argv)
{
    if (argc != 2 || !wasm_runtime_init()) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    unsigned char *buffer = malloc((size_t)length);
    if (!buffer || fread(buffer, 1, (size_t)length, file) != (size_t)length) return 2;
    fclose(file);
    char error[256] = {0};
    wasm_module_t module = wasm_runtime_load(buffer, (uint32_t)length, error, sizeof(error));
    int passed = !module && protect_failures == 1 && strstr(error, "protect AOT code") != NULL;
    if (module) wasm_runtime_unload(module);
    passed = passed && outstanding_mappings == 0;
    free(buffer);
    wasm_runtime_destroy();
    printf("mprotect failures=%u load_rejected=%d outstanding=%u error=%s\n", protect_failures, module == NULL, outstanding_mappings, error);
    return passed ? 0 : 1;
}
