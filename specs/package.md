# TinyRT 应用包格式 1

SDK 与固件版本为 0.1.0。当前唯一包格式为 1，使用 `.trpkg`，256 字节头后包含 section 表、Wasm、可选 AOT、resources 与 cover。完整包不得超过目标宿主包库数据区（`TINYRT_STORE_SIZE` 减两个目录扇区），没有单独的单包上限；SDK 只做 16 MiB 格式健全性检查。旧内部直接载荷布局与旧内部 section 格式号不兼容，旧包需用当前 SDK 重新构建。包元数据是受信发布方的声明，不能证明任意机器码具有边界检查或可取消性。

## 头与 section 表

全部整数小端，固定头仍为 256 字节。`magic[8]=TRPKG001`，offset8 的 u16 format=1，offset10 的 u16 header_size=256，offset12 的 u32 total_size 必须等于文件长度。section 表定位字段如下：

| 偏移 | 字段 |
|---|---|
| 16 | u32 table_offset=256 |
| 20 | u32 section_count，1..4 |
| 24 | u32 section_entry_size=16 |
| 28 | u32 reserved=0 |

offset32..151 的字段如下表；字符串 NUL 终止，NUL 后全零。offset152 为 32 字节 SHA-256，覆盖 `package[256:total_size]`，包括 section 表、对齐填充、AOT 元数据和所有 section 内容。offset184..191 仍全零。offset192..255 为规范 low-S P-256 `r32||s32`。

| 偏移 | 字段 |
|---|---|
| 32、36 | u32 app_version>0、abi_version=1 |
| 40 | u32 permissions，draw=1、input=2、storage=4、clock=8、audio=16、haptic=32，其他位为零 |
| 44、48 | u32 memory_pages=1..16、instruction_budget=1..100000 |
| 52 | u32 key_id，仅信息，不参与校验；SDK 固定写 1 |
| 56 | app_id[32]，1..31 字节 ASCII `[a-z0-9._-]` |
| 88 | title[64]，1..63 字节严格 UTF-8，无内嵌 NUL |

偏移 192..255 是签名区，设备读取但不校验。SDK 仍写入 `b"TinyRT-package-v1\0" + package[:192]` 的公开标量 1 ECDSA-P256-SHA256 签名，只为让包能装到旧固件。整包身份为 SHA-256(package)，包含该区域。payload SHA-256 只覆盖 256 字节之后；包头字段没有设备端的独立保护，由整包身份和发布平台比对把关。

每条 section 表项为四个 u32：`kind, flags=0, offset, size`。kind=1 Wasm、2 AOT、3 resources、4 cover。按 kind 严格递增；未知、重复、乱序、空 section、溢出、重叠或非规范间隙均拒绝。至少包含 Wasm 或 AOT。Wasm 大于 8 字节；resources 为空时省略该项。

表后立即开始内容。每段 offset 必须等于上一段结尾向上对齐至 4 字节的位置；最多三个填充字节且必须全零。首段紧接表；最后一段结尾必须恰好等于 total_size，不允许末尾填充。所有边界先用减法验证再相加。

## AOT section

AOT section 由 256 字节元数据头与原始 AOT 模块组成，原始模块不能为空。元数据结构：

| 偏移 | 字段 |
|---|---|
| 0、2 | u16 metadata_version=1、metadata_size=256 |
| 4 | u32 AOT format_version，当前工具链使用 5 |
| 8 | u32 safety_flags，必须为 7：loop_poll=1、bounds_checks=2、stack_checks=4 |
| 12 | reserved[4]=0 |
| 16、32 | target_arch[16]、target_cpu[16]，1..15 字节 `[a-z0-9_-]`，NUL 后全零 |
| 48、68 | wamr_commit[20]、llvm_commit[20]，原始 Git SHA 字节 |
| 88 | patch_sha256[32]，SHA256(raw compiler_patch_set_hash || raw runtime_patch_set_hash) |
| 120 | aot_compat_id[32] |
| 152 | target_options_sha256[32] |
| 184 | compiler_executable_sha256[32] |
| 216 | source_wasm_sha256[32]，编译输入的原始 Wasm |
| 248 | reserved[8]=0 |

提交与哈希不得全零。含 Wasm 回退时必须验证其 SHA-256 等于 source_wasm_sha256；AOT-only 包保留编译输入摘要供来源核查。包解析器不尝试从机器码逆向证明编译选项；原始 AOT 结构、目标、导入、ABI、内存及执行保护由独立 runtime 校验器负责。

## 校验与选择

设备不校验包的来源：key_id、签名和 app_id 命名空间都不影响安装。侧载的边界是 BLE 配对和设备端确认，商店的边界是发布平台的审核与 app_id 归属。同一 app_id 的不同包（升级、同版本重编译、降级）直接替换旧包并保留存档。

完整载荷哈希、section 布局、来源绑定和安全位不合格时拒绝整包。结构合格的 AOT 如遇后端未启用、架构/CPU 不匹配、格式/compat_id 不匹配，则仅在带 Wasm 时回退。回退仍验证 Wasm；同目标 AOT 校验器拒绝时不能悄悄改用 Wasm。IO_ERROR、NO_MEMORY、BUSY、主机 INVALID_ARGUMENT 保持原状态；失败输出全零。

执行种类：NONE=0、WASM=1、AOT=2。回退原因：NONE=0、DISABLED=1、TARGET=2、COMPAT=3。主机 profile 缺失默认禁用 AOT；实际启用仍须满足执行取消与内存保护门禁。

`tinyrt_package_metadata_t` 保留原 wasm/assets 偏移，追加 raw `aot_offset/aot_size`、AOT 元数据、执行种类及回退原因。AOT offset 已越过其 256 字节元数据头。验证回调收到绝对 IO 偏移；返回元数据仍为包内相对偏移。同步校验期间 IO 不得变化，每次读取不超过 1024 字节。

## 编译一致性

AOT 机器码不受设备沙箱约束，bounds、stack 与 loop-poll 检查是编译时写入的，`safety_flags` 只是包内声明。SDK 只用固定编译器和固定选项从 Wasm 生成 AOT，不接受外部 AOT 或调用方自报的安全位。发布平台必须拒绝不含 Wasm 的包，并用 `tinyrt.py validate --rebuild-aot` 确认 AOT 与重新编译的结果逐字节一致，否则审核 Wasm 没有意义。

两份 patch_set_hash 都为 SHA256(按配置顺序拼接每份补丁的原始 SHA256 字节)，即使只有一份补丁也执行聚合。固件构建从 SDK pin 读取这两份摘要，并重新读取实际补丁文件核对。

compat_id 为以下对象按 JSON `sort_keys=True,separators=(',',':')`、UTF-8 编码后的 SHA-256：`schema=1, wamr_commit, llvm_commit, aot_format_version, target_arch, target_cpu, compiler_patch_sha256, runtime_patch_sha256, runtime_abi_revision, target_options_sha256`。不纳入主机编译器二进制哈希，允许不同构建主机产生兼容目标代码；该二进制哈希仍独立包含在 AOT 元数据中。

固定选项依次为 target、cpu、bounds-checks=1、stack-bounds-checks=1、opt-level=3、size-level、disable-simd、disable-ref-types、enable-loop-poll；不能通过参数追加或取消安全选项。

## BLE 协商合同

管理传输版本 1、逻辑消息上限 256 字节、status u16。HELLO 为 8 字节；bit6（64）声明 PACKAGE_SECTIONS 能力，当前包安装和详情查询要求此位。STORAGE 为 92 字节 schema 1，最大包长由 HELLO 与 STORAGE 共同报告，等于宿主包库数据区。PREPARE/info72、QUERY、传输、卸载与原子提交保留既有身份字段。

`APP_INFO=0x1A` 只接受 `u16 schema=1 + identity68`（70 字节）；旧 68 字节请求和 164 字节回复已移除。成功回复不含 status 共 248 字节：

| 偏移 | 字段 |
|---|---|
| 0、2 | u16 schema=1、package_format=1 |
| 4、5 | u8 selected_backend、fallback_reason，数值同上 |
| 6 | u16 section_bits，Wasm=1、AOT=2、resources=4 |
| 8 | info72 |
| 80 | title[64] |
| 144..180 | 十个 u32：key_id、abi、permissions、memory_pages、budget、wasm_size、assets_size、raw_aot_size、aot_format、safety_flags |
| 184、200 | target_arch[16]、target_cpu[16]；无 AOT 时全零 |
| 216 | compat_id[32]；无 AOT 时全零 |

新增 `RUNTIME_INFO=0x1B`，空请求。回复 136 字节：u16 schema=1@0、u16 package_formats（格式1的位=1）@2、u32 backends（Wasm=1/AOT=2，值 4 保留恒为 0）@4、u32 AOT format@8、u32 runtime_abi_revision@12、arch[16]@16、cpu[16]@32、compat_id[32]@48、options_sha256[32]@80、wamr_commit[20]@112、u32 required_safety_flags@132。未启用 AOT 时所有 AOT/profile 字段为零，不宣称尚未启用的能力。

手机端不能继续套用 `256+wasm+assets==package_size` 于当前格式；按 section_bits、16 字节表项、4 字节对齐及 256 字节 AOT 元数据开销验证长度。安装前可显示目标不兼容及 Wasm 回退，实际裁决始终由设备完成。选中 Wasm 回退时上层显示“兼容模式，性能降低”。

## 校验与 IO 边界

UTF-8拒绝过长编码、孤立延续、截断、代理码点和超出U+10FFFF。SHA-256主机使用Windows BCrypt，ESP32使用PSA；portable C处理规范编码、布局和范围。

`tinyrt_package_inspect` 返回包元数据，`tinyrt_package_verify` 可作为store验证器。Wasm验证回调须检查结构、导出签名、import白名单、内存与禁止的start/shared-memory/threads/WASI，并不得执行Guest。同步IO必须是不可变快照；每次读取≤1024字节，无整包分配，不调用program/erase/sync。IO_ERROR、NO_MEMORY、BUSY保留；失败输出全零。

SDK `validate`只检查信封，还必须经过core完整校验和实际运行验证。固件 OTA 签名与应用包无关，不受这里影响。

## 独立打包命令

SDK根目录中执行；资源为空时省略`--assets`。普通入口只封装Wasm/resources：

```powershell
python tools/package.py pack --wasm app.wasm --assets resources.bin --output app.trpkg --app-id demo.counter --title Counter --version 1 --permissions 7 --memory-pages 4 --budget 10000
```

打包结果确定：相同输入得到相同完整包和安装身份；仍仅检查基础Wasm魔数，设备负责完整结构与ABI验证。输出不会覆盖源Wasm或资源。AOT 包用`python tools/tinyrt.py pack app`，详见SDK指南。

## 可选 cover section（kind=4）

开发者在 `app.json` 设置 `"cover": "cover.png"`。输入路径必须位于应用目录中（解析链接后也不得越界）；PNG 最大 65536 字节，必须为单帧 210×210。SDK 在解码前验证 PNG/IHDR 尺寸，随后验证实际解码尺寸；透明像素叠于 `#101418`。Wasm、开发 AOT 和发布 AOT 使用同一转换与验证流程。

设备接收预生成的两档小端 RGB565，无需设备 PNG 解码器。section 总长严格为 133232 字节；32 字节头后是 88200 字节 210×210 像素和 45000 字节 150×150 像素（SDK Lanczos 缩小）。像素逐行排列，没有步幅填充。头格式：

| 偏移 | 字段 |
|---|---|
| 0 | magic[8] = `TRCOV001` |
| 8、10 | u16 schema=1、codec=1 (`RGB565LE_TWO_SIZES`) |
| 12 | u32 header_size=32 |
| 16、18 | u16 main_width=210、main_height=210 |
| 20、22 | u16 side_width=150、side_height=150 |
| 24、28 | u32 main_size=88200、side_size=45000 |

所有字段必须精确匹配；未知 codec、重复 cover、非规范边界或尺寸均拒绝。cover 和 section 表共同受 payload SHA-256 保护，计入整包长度。cover 不属于 Guest `resources`，不会改变 `asset_read` 相对偏移；它只供 Host 大厅与手机展示。没有 cover 时使用默认封面。

`tinyrt_package_metadata_t.cover` 提供 package-relative offset、size、codec 与完整 cover section（包含 32 字节头）的 SHA-256。`tinyrt_manager_cover_query` 按完整应用 identity 查询；无 cover 返回 `TINYRT_NOT_FOUND`。`tinyrt_manager_cover_read` 以 cover-relative offset 精确读取 1..4096 字节，拒绝越界；安装期间返回 `TINYRT_BUSY`。API 遵循 manager 的单线程 owner 约束。
