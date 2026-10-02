# 固定版本 AOT 工具链

`source-lock.json` 固定 WAMR、Espressif LLVM 和补丁顺序。`build.py` 从这些源码构建 Windows 主机 `wamrc.exe`；不下载预编译 wamrc。运行库必须使用同一 WAMR commit 和对应运行库补丁。

在 Visual Studio x64 Developer Command Prompt 中，将 CMake、Ninja、Git 和 Python 加入 PATH 后执行：

```bat
chcp 65001
python esp32/tinyrt/tools/toolchain/build.py --work tmp/aot-toolchain --jobs 8
```

构建中断后可加 `--resume`。脚本用临时 Git index 对比完整源码和固定补丁，不接受额外源码修改。`--proxy` 只为本次 Git 命令设置代理，不改全局配置。建议预留 5 GB 磁盘和 16 GB 内存；降低 `--jobs` 可减少内存峰值。

输出包括 `wamrc-build/wamrc.exe`、`provenance.json`、构建日志和许可证。清单记录源码 commit、补丁 SHA-256、编译器 SHA-256、实际主机构建工具及编译参数。`target_options_sha256` 是参数数组经 UTF-8 紧凑 JSON 序列化后的 SHA-256，数组顺序参与计算。

```bat
python esp32/tinyrt/tools/toolchain/test_loop_poll.py --wamrc tmp/aot-toolchain/wamrc-build/wamrc.exe --output tmp/aot-verification --example path/to/nes-maze.wasm --objdump path/to/xtensa-esp32s3-elf-objdump.exe
python esp32/tinyrt/tools/toolchain/make_probe.py --wamrc tmp/aot-toolchain/wamrc-build/wamrc.exe --output tmp/aot-board-probe
python esp32/tinyrt/tools/toolchain/compile_fixtures.py --wamrc tmp/aot-toolchain/wamrc-build/wamrc.exe --input path/to/wasm-fixtures --output tmp/aot-board-fixtures
```

验证程序编译普通非共享内存和无内存模块，检查 `br`、`br_if`、`br_table` 和递归中的原子取消读取是否保留到优化后 IR。提供 `--objdump` 时，还检查实际 Xtensa 指令中的内存屏障、标志读取和条件退出。示例连续编译两次，要求 AOT 字节完全相同。这里验证的是 AOT 输出确定性，不承诺不同主机构建得到相同的 wamrc 二进制。

`compile_fixtures.py` 输出每个用例的来源/产物哈希清单，以及 8 字节对齐的纯 C 数组和名称查找表，便于板测直接装载。`test_build_source.py` 可使用已有官方源码检出作为本地 seed，验证首次普通/稀疏检出和已有脏源码拒绝逻辑；测试不创建 Git 提交。

`--enable-loop-poll` 独立于 Guest 多线程：它不设置 AOT 多线程 feature bit，也不要求共享内存。它读取执行环境的既有取消标志，采用 acquire 原子读取；运行库取消请求使用对应的原子写入。运行库仍需在执行结束后将取消状态转为失败，并由回调守卫实施绝对截止时间。

编译参数和元数据只是可信编译链的一部分。产物需要经过运行库加载、板上安全矩阵和签名策略验证，才能成为允许外部安装的发布产物。

依赖许可证与原始版权声明保留在源码及构建输出的 `licenses/`；除顶层许可证外，脚本保留 WAMR core/compiler、LLVM include/lib 中的单独声明文件和带版权信息的原始源码头，覆盖 LLVM Support 的 xxhash、Unicode 转换及 BSD 正则表达式声明。该目录采用保守范围，可能包含本次构建未链接的源码声明；各输出文件的 SHA-256 记录在 provenance 中。本地修改由固定补丁明确记录。
