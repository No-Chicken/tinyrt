"""Compile the actual upstream stack getter with IDF API mocks, without trace.

Only the getter is isolated; the full ESP-IDF build validates its real includes.
"""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = args.source.read_text(encoding="utf-8")
start = source.index("uint8 *\nos_thread_get_stack_boundary(void)")
brace = source.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
getter = source[start:end]
for major in (5, 6):
    for trace in (False, True):
        prefix = f"""#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef uint8_t uint8;
typedef uint32_t StackType_t;
typedef void *TaskHandle_t;
typedef struct {{ StackType_t *pxStackBase; }} TaskStatus_t;
static StackType_t mock_stack[8192];
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL({major},1,0)
{('#define CONFIG_FREERTOS_USE_TRACE_FACILITY 1' if trace else '')}
#define pdTRUE 1
#define eInvalid 0
TaskHandle_t xTaskGetCurrentTaskHandle(void) {{ return NULL; }}
void vTaskGetInfo(TaskHandle_t t, TaskStatus_t *s, int p, int e)
{{ (void)t; (void)p; (void)e; s->pxStackBase=mock_stack; }}
"""
        api = "StackType_t *xTaskGetStackStart" if major >= 6 else "uint8_t *pxTaskGetStackStart"
        cast = "StackType_t *" if major >= 6 else "uint8_t *"
        prefix += f"{api}(TaskHandle_t t) {{ (void)t; return ({cast})mock_stack; }}\n"
        unit = args.output / f"stack-idf{major}-trace{int(trace)}.c"
        exe = unit.with_suffix(".exe")
        unit.write_text(prefix + getter + """
int main(void) {
    if (os_thread_get_stack_boundary() != (uint8_t *)mock_stack) {
        puts("native stack boundary missing or incorrect"); return 1;
    }
    puts("native stack boundary is the real low address"); return 0;
}
""", encoding="utf-8")
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/utf-8", str(unit.resolve()),
                        "/Fe:" + str(exe.resolve())], cwd=args.output, check=True)
        subprocess.run([str(exe.resolve())], check=True, timeout=15)
print("ESP-IDF stack boundary: versions 5/6, trace off/on passed")
