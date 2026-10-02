# AOT PSRAM 平台层

`tinyrt_esp_exec_memory.c` 替换 WAMR 的 ESP-IDF `espidf_memmap.c`。平台层使用 `esp_heap_caps.h`、`esp_mmu_map.h` 和 `esp_cache.h` 的公开 API；不创建新的 MMU 映射，也不更改内存保护寄存器。

执行区首先申请缓存行对齐的 PSRAM。平台层逐个检查所覆盖 MMU 页及最后一个字节，确认物理地址属于 PSRAM、存在连续的指令别名，并且指令别名能反向转换回原始数据地址。任何检查失败都会释放申请的内存。数据和指令别名指向同一物理分配，只计账一次。

原生数据映射允许保护完整位于请求范围内的子区间，以支持 WAMR 合并的多个只读 section；拒绝越界范围和增加执行权限。执行映射必须从起点按整个请求长度封存。

加载和重定位通过 `os_get_dbus_mirror()` 获取数据别名。封存时先执行数据 cache-to-memory 同步，再失效指令缓存；两个操作都使用对齐范围。成功封存后清空记录中的可写指针，并拒绝再次获取该执行区的数据别名或恢复 RWX 状态。卸载时通过公开 MMU API 临时恢复分配地址，仅用于释放。运行期间要求启动建立的 MMU 映射保持稳定；若卸载时反查失败，立即 `abort()`，避免 WAMR 的 void `os_munmap` 接口返回后释放统一预算而实际内存未释放。

这是一项加载器状态约束，**不构成硬件 W^X**：芯片的既有数据别名仍在地址空间内，具备主机原生地址访问能力的代码仍可能重建它。AOT 信任边界依赖固定编译链、Wasm 边界检查、受信签名和运行库加载策略。默认继续开启 `CONFIG_ESP_SYSTEM_MEMPROT`，是否可正常执行必须由对应固件的板测确认。

`TINYRT_ESP_AOT_MAP_BUDGET` 默认限制全部 AOT 原生映射为 512 KiB，包括代码和原生数据，按较大的指令/数据缓存行粒度计费。当前 ESP32-S3 配置为 64 字节。这项平台上限独立于 Guest 分配预算；统一的应用预算仍需由运行库另外计账。`tinyrt_esp_exec_memory_get_stats()` 返回当前字节数和映射数，用于装卸验收。

接线要求：

- 使用固定上游版本及 `0001-exec-env-cancellation.patch`、`0002-aot-dbus-writes.patch` 等已批准运行库补丁。
- 从平台源列表移除上游 `espidf_memmap.c`，加入本目录 `.c` 和头文件目录。
- ESP-IDF 组件依赖包含 `esp_mm`、`esp_psram`；启用 `WASM_MEM_DUAL_BUS_MIRROR=1`。
- `.text` 拷贝、Xtensa `R_XTENSA_32` 和 `R_XTENSA_SLOT0_OP` 重定位走数据别名。Xtensa PLT 大小为零，不写额外跳板；`0002` 修正末尾填充写入，并使代码/数据保护失败向上传播。
- `0004-esp-idf-native-stack.patch` 通过 IDF 公开 API 获取当前任务最低栈地址，避免 trace 关闭时上游返回 NULL 而使原生栈检查失效。IDF 6 使用 `xTaskGetStackStart(NULL)`，更早版本使用 `pxTaskGetStackStart(NULL)`。Xtensa 栈向低地址增长；WAMR 在基址上加 `WASM_STACK_GUARD_SIZE` 留出处理异常的余量，实际安全余量仍由板测验证。

主机测试在 Visual Studio x64 环境运行：

```bat
python esp32/tinyrt/ports/esp-idf/tests/run_tests.py --output tmp/exec-memory-tests
python esp32/tinyrt/ports/esp-idf/tests/test_stack_boundary.py --source path/to/patched-wamr/core/shared/platform/esp-idf/espidf_platform.c --output tmp/stack-boundary-tests
```

测试覆盖分配失败、溢出、单块和总配额、跨页别名缺失、缓存同步失败、封存状态和 50 次完整装卸。另在子进程中注入封存后 MMU 反查失败，要求在释放预算前终止。`tests/loader/` 使用真实 WAMR AOT 加载器注入 `os_mprotect` 失败，要求拒绝模块且映射计数归零。主机模拟不能替代 MEMPROT 开启时的实际芯片执行验证。

公开依据：[ESP-IDF MMU 内存管理](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/mm.html)、[缓存同步](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/mm_sync.html)、[当前任务栈最低地址](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos_additions.html#_CPPv418xTaskGetStackStart12TaskHandle_t)。实现同时核对当前 IDF 的 `esp_mmu_map.c`、`esp_cache_msync.c` 和 ESP32-S3 MMU HAL。
