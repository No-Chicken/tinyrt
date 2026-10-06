"""Compile the real platform memory source against small Windows IDF mocks."""
from pathlib import Path
import argparse
import os
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--target", choices=("esp32s3","esp32s31"),default="esp32s3")
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
port = Path(__file__).resolve().parents[1]
mocks = {
"platform_api_vmcore.h": '''#pragma once
#include <stddef.h>
typedef int os_file_handle;
enum { MMAP_PROT_READ=1, MMAP_PROT_WRITE=2, MMAP_PROT_EXEC=4, MMAP_MAP_FIXED=2 };
void *os_mmap(void *,size_t,int,int,int);
void os_munmap(void *,size_t);
int os_mprotect(void *,size_t,int);
void *os_get_dbus_mirror(void *);
void os_dcache_flush(void);
void os_icache_flush(void *,size_t);
''',
"esp_heap_caps.h": '''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_aligned_alloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
''',
"esp_mmu_map.h": '''#pragma once
#include <stdint.h>
typedef uint32_t esp_paddr_t;
typedef int mmu_target_t;
typedef int mmu_vaddr_t;
#define MMU_TARGET_PSRAM0 1
#define MMU_VADDR_INSTRUCTION 1
#define MMU_VADDR_DATA 2
#define ESP_OK 0
int esp_mmu_vaddr_to_paddr(void *,esp_paddr_t *,mmu_target_t *);
int esp_mmu_paddr_to_vaddr(esp_paddr_t,mmu_target_t,mmu_vaddr_t,void **);
''',
"esp_cache.h": '''#pragma once
#include <stddef.h>
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 4
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 8
#define ESP_CACHE_MSYNC_FLAG_TYPE_DATA 16
#define ESP_CACHE_MSYNC_FLAG_TYPE_INST 32
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
int esp_cache_msync(void *,size_t,int);
''',
"sdkconfig.h": '''#define CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE 32
#define CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE 64
#define CONFIG_MMU_PAGE_SIZE 65536
''',
"sys/lock.h": '''#pragma once
typedef int _lock_t;
static void _lock_acquire(_lock_t *p) { (void)p; }
static void _lock_release(_lock_t *p) { (void)p; }
''',
}
if args.target=="esp32s31":
    mocks['sdkconfig.h']='#define CONFIG_IDF_TARGET_ESP32S31 1\n#define CONFIG_CACHE_L1_DCACHE_LINE_SIZE 64\n#define CONFIG_MMU_PAGE_SIZE 65536\n'
for name, text in mocks.items():
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
exe = out / "test-exec-memory.exe"
subprocess.run(["cl", "/nologo", "/W4", "/WX", "/utf-8", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                "/DTEST_UNIFIED_ADDRESS="+str(int(args.target=="esp32s31")),
                "/I" + str(out), "/I" + str(port), str(port / "tinyrt_esp_exec_memory.c"),
                str(port / "tests/test_exec_memory.c"), "/Fe:" + str(exe)], cwd=out, check=True)
subprocess.run([str(exe)], check=True, timeout=15)
death = subprocess.run([str(exe), "--broken-unmap-alias"], capture_output=True,
                       text=True, timeout=15)
assert "injecting sealed mapping ownership failure" in death.stdout, death.stdout
assert death.returncode == 3, f"ownership corruption did not abort: {death.returncode}: {death.stdout}"
assert "execution continued" not in death.stdout, death.stdout
print("exec memory: corrupted MMU ownership aborts before budget release")
