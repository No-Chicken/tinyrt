# TinyRT 应用包 v2

v1 保持原字节格式、签名域、ABI 1、安装身份与校验策略。新包继续使用 `.trpkg`；v2 引入可选 Wasm 回退和带签名来源信息的 AOT section。完整包仍不得超过 2 MiB。包元数据是受信发布方的声明，不能证明任意机器码具有边界检查或可取消性。

## 头与 section 表

全部整数小端，固定头仍为 256 字节。`magic[8]=TRPKG002`，offset8 的 u16 format=2，offset10 的 u16 header_size=256，offset12 的 u32 total_size 必须等于文件长度。以下字段取代 v1 的直接载荷偏移：

| 偏移 | 字段 |
|---|---|
| 16 | u32 table_offset=256 |
| 20 | u32 section_count，1..3 |
| 24 | u32 section_entry_size=16 |
| 28 | u32 reserved=0 |

offset32..151 的 app_version、ABI、权限、内存、预算、key_id、app_id、title 沿用 v1 的位置和规范。offset152 为 32 字节 SHA-256，覆盖 `package[256:total_size]`，包括 section 表、对齐填充、AOT 元数据和所有 section 内容。offset184..191 仍全零。offset192..255 为规范 low-S P-256 `r32||s32`。

签名输入为 `b"TinyRT-package-v2\0" + package[:192]`，ECDSA-P256-SHA256。整包身份仍为 SHA-256(package)，包含签名。

每条 section 表项为四个 u32：`kind, flags=0, offset, size`。kind=1 Wasm、2 AOT、3 resources。按 kind 严格递增；未知、重复、乱序、空 section、溢出、重叠或非规范间隙均拒绝。至少包含 Wasm 或 AOT。Wasm 大于 8 字节；resources 为空时省略该项。

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

## 信任与选择

主机信任记录仍由部署方提供，保留 key_id 和 app_id_prefix，追加 `aot_authority`：NONE=0（旧记录默认，Wasm-only）、RELEASE=1、DEVELOPMENT=2。包不能自行声明授权等级。包含 AOT 的包必须由 RELEASE 签署，或由 DEVELOPMENT 签署且主机显式允许开发 AOT、app_id 为 `demo.` 的非空后代；即使有 Wasm，也不接受无 AOT 权限的签名。

验签、完整载荷哈希、section 布局、来源绑定和安全位不合格时拒绝整包。受信且结构合格的 AOT 如遇后端未启用、架构/CPU 不匹配、格式/compat_id 不匹配，则仅在带 Wasm 时回退。回退仍验证 Wasm；同目标 AOT 校验器拒绝时不能悄悄改用 Wasm。IO_ERROR、NO_MEMORY、BUSY、主机 INVALID_ARGUMENT 保持原状态；失败输出全零。

执行种类：NONE=0、WASM=1、AOT=2。回退原因：NONE=0、DISABLED=1、TARGET=2、COMPAT=3。主机 profile 缺失默认禁用 AOT；实际启用仍须满足执行取消与内存保护门禁。

`tinyrt_package_metadata_t` 保留原 wasm/assets 偏移，追加 raw `aot_offset/aot_size`、已认证 AOT 元数据、执行种类及回退原因。AOT offset 已越过其 256 字节元数据头。验证回调收到绝对 IO 偏移；返回元数据仍为包内相对偏移。同步校验期间 IO 不得变化，每次读取不超过 1024 字节。

当前与上一把发布公钥以两个不同 key_id 同时装入原信任数组，均显式声明 RELEASE。重复 key_id 仍是配置错误。删除旧公钥会使旧签名包在重检时被隔离，因此轮换窗口须覆盖旧包升级期；轮换不修改已存包，不生成或替换发布私钥。

## 可信编译入口

SDK 通用 `package.py pack --format 2` 只封装 Wasm/resources，不接受外部 AOT 或调用方自报的安全位。`release_compile.py` 是本地发布流水线构件：只接受受发布操作方控制的来源锁、编译器 provenance、release profile、按序 runtime 补丁和签名钥；上传应用者不能选择这些配置。

release profile schema1 的全部字段：`schema, provenance_sha256, compiler_patch_sha256, runtime_patch_sha256, runtime_abi_revision, signing_key_id, signing_key_sha256, development`。公钥摘要使用 SEC1 未压缩 65 字节。生产 development=false，并拒绝公开标量 1 测试私钥；development=true 仍限制 `demo.`。配置不内置任何生产密钥。

两份 patch_set_hash 都为 SHA256(按配置顺序拼接每份补丁的原始 SHA256 字节)，即使只有一份补丁也执行聚合。编译器补丁顺序来自 source-lock 与 provenance；runtime 补丁由重复的 `--runtime-patch` 参数给出。所有补丁文件均重新读取验证。

compat_id 为以下对象按 JSON `sort_keys=True,separators=(',',':')`、UTF-8 编码后的 SHA-256：`schema=1, wamr_commit, llvm_commit, aot_format_version, target_arch, target_cpu, compiler_patch_sha256, runtime_patch_sha256, runtime_abi_revision, target_options_sha256`。不纳入主机编译器二进制哈希，允许不同构建主机产生兼容目标代码；该二进制哈希仍独立包含在签名 AOT 元数据中。

入口检查 provenance 的 SHA 与 release profile、source-lock 的 SHA、源码提交、补丁文件、编译器可执行文件和固定选项一致。选项依次为 target、cpu、bounds-checks=1、stack-bounds-checks=1、opt-level=3、size-level=0、disable-simd、disable-ref-types、enable-loop-poll；不能通过参数追加或取消安全选项。工具在新临时目录复制 Wasm、自行运行编译器取得输出，签名前重新核对所有已读取输入和编译器哈希，最后独立验签并原子写包。来源变化或失败不覆盖既有包。

该命令没有部署发布服务器，也不能保护已被攻陷的发布操作系统或泄露的签名钥。运行时安全验收与真实发布基础设施仍需单独完成。

## BLE 协商合同

管理传输保持版本 1、逻辑消息上限 256 字节、status u16。HELLO 仍为 8 字节；bit6（64）声明 PACKAGE_V2 协商。STORAGE 仍为 92 字节 schema1，最大包长仍由 HELLO 与 STORAGE 共同报告，当前为 2097152。PREPARE/info72、QUERY、传输、卸载与原子提交不变。v2 客户端要求 bit6；v1 安装仍按原条件工作。

`APP_INFO=0x1A` 的旧 identity68 请求对 v1 继续返回 164 字节；对 v2 返回 INVALID_ARGUMENT，避免伪造旧式尺寸等式。新请求为 `u16 schema=2 + identity68`（70 字节），可查询 v1/v2。成功回复不含 status 共 248 字节：

| 偏移 | 字段 |
|---|---|
| 0、2 | u16 schema=2、package_format=1/2 |
| 4、5 | u8 selected_backend、fallback_reason，数值同上 |
| 6 | u16 section_bits，Wasm=1、AOT=2、resources=4 |
| 8 | info72 |
| 80 | title[64] |
| 144..180 | 十个 u32：key_id、abi、permissions、memory_pages、budget、wasm_size、assets_size、raw_aot_size、aot_format、safety_flags |
| 184、200 | target_arch[16]、target_cpu[16]；无 AOT 时全零 |
| 216 | compat_id[32]；无 AOT 时全零 |

新增 `RUNTIME_INFO=0x1B`，空请求。回复 136 字节：u16 schema=1@0、u16 package_formats（v1=1/v2=2）@2、u32 backends（Wasm=1/AOT=2/development_AOT=4）@4、u32 AOT format@8、u32 runtime_abi_revision@12、arch[16]@16、cpu[16]@32、compat_id[32]@48、options_sha256[32]@80、wamr_commit[20]@112、u32 required_safety_flags@132。未启用 AOT 时所有 AOT/profile 字段为零，不宣称尚未启用的能力。

手机端不能继续套用 `256+wasm+assets==package_size` 于 v2；按 section_bits、16 字节表项、4 字节对齐及 256 字节 AOT 元数据开销验证长度。安装前可显示目标不兼容及 Wasm 回退，实际裁决始终由设备完成。选中 Wasm 回退时上层显示“兼容模式，性能降低”。
