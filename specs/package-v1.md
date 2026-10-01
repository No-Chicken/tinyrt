# TinyRT 应用包 v1

这是应用包格式与信任边界，不是设备固件格式。固件签名钥与应用包签名钥必须分别管理；模块不内置信任公钥。

## 二进制格式

全部整数小端。256 字节头后依次为原始 Wasm 与可选原始资源。完整包最大 0x4A000（296 KiB），禁止压缩、间隙和尾随数据。

| 偏移 | 字段 |
|---|---|
| 0 | magic[8] = TRPKG001 |
| 8、10 | u16 format=1、header_size=256 |
| 12 | u32 total_size，必须等于实际包长 |
| 16、20 | u32 wasm_offset=256、wasm_size>8 |
| 24、28 | u32 assets_offset=256+wasm_size、assets_size；可为 0 |
| 32、36 | u32 app_version>0、abi_version=1 |
| 40 | u32 permissions，仅 draw=1、input=2、storage=4、clock=8 |
| 44 | u32 max_memory_pages，1..16 |
| 48 | u32 instruction_budget，1..100000 |
| 52 | u32 signing_key_id，必须在部署方信任集内唯一匹配 |
| 56 | app_id[32]，1..31 字节 ASCII [a-z0-9._-]；NUL 终止、剩余字节全零 |
| 88 | title[64]，1..63 字节严格 UTF-8；NUL 终止、剩余字节全零 |
| 152 | payload_sha256[32]，覆盖 Wasm+资源的所有原始字节 |
| 184 | reserved[8]，全零 |
| 192 | signature[64]，P-256 ECDSA raw r32||s32；r、s 均大端 |

UTF-8 不接受过长编码、孤立延续字节、截断序列、代理码点和大于 U+10FFFF 的值。偏移、长度先用减法验证边界，再相加；未通过策略检查的数据不会交给 Wasm 校验器。

## 三个摘要域

1. 签名：对 17 字节 ASCII 域分隔符 `TinyRT-package-v1` 加一个 NUL（共 18 字节），再接原样头的前 192 字节，使用 ECDSA-P256-SHA256。没有额外长度、转义或换行。
2. 载荷摘要：SHA-256(package[256:total_size])。
3. 安装身份摘要：SHA-256(package[0:total_size])，包含完整签名。身份为 app_id + app_version + 此整包摘要。

要求 0 < r < n，0 < s <= floor(n/2)。n 为标准 secp256r1 阶；高 S 签名即使数学上有效也拒绝。SHA-256 与椭圆曲线验签均由成熟后端执行：主机 Windows BCrypt，ESP32 PSA。portable C 仅做字段解析、规范编码与范围检查。

## 接口与读取约束

`tinyrt_package_inspect` 成功返回 `tinyrt_package_metadata_t`：持久身份、标题、包内相对 Wasm/资源偏移、签名 key ID 和资源策略。`tinyrt_package_verify` 直接满足 store 的验证器回调签名。

`tinyrt_package_verifier_t` 的信任集由调用方提供，公钥为 SEC1 未压缩 65 字节（04||X||Y）。没有密钥回退；未知 ID 拒绝，同一个 ID 重复匹配视为配置错误。

每个 `tinyrt_package_trusted_key_t` 可设置 `app_id_prefix`，由宿主保管字符串及其生命周期；`NULL` 或空字符串保留旧版不限 ID 的行为。1～31 字节、字符范围 `[a-z0-9._-]`；以点结尾表示非空后代命名空间，例如 `demo.` 接受 `demo.snake`，拒绝 `demo`、`demo.` 和 `demoevil.snake`。不以点结尾表示精确 ID，例如 `demo.snake` 只接受该 ID。匹配钥的范围配置非法返回 `INVALID_ARGUMENT`，范围不匹配返回 `VERIFY_FAILED`，均不进入 Wasm 验证。

不同钥可以有相同范围，以支持宿主明确授权的换钥。core 不推断发布者关系；独立发布者应由产品配置不重叠范围。任何受信且在范围内的钥都可以签该 ID 的更高版本，因此范围重叠意味着共同更新权限，并不等于发布者隔离。钥移除或范围收窄后，旧包可能进入隔离；撤销策略由宿主管理。本策略不改变包 v1 字节、签名域或 guest ABI。

`validate_wasm` 是必需的独立回调，收到**绝对 Wasm 偏移**、长度和已认证策略。它必须验证 Wasm 结构、ABI 导出、导入白名单、内存上限、无 start section/共享内存/threads/WASI，不能运行 guest。缺回调返回 INVALID_ARGUMENT。包模块主机测试使用明确命名的 stub；不能把这些测试当作真实 Wasm 验证证据。

整个同步校验期间 IO 必须提供不可变快照。包模块每次读取不超过 1024 字节；不调用 program/erase/sync，不分配整包缓冲。输入读取错误保留原状态，IO_ERROR、NO_MEMORY、BUSY 不转成数据损坏；Wasm 验证器因宿主未初始化、配置错误返回的 INVALID_ARGUMENT 同样原样保留，不导致健康包隔离。所有失败输出置零。真实校验失败返回 VERIFY_FAILED，错误 API/歧义信任配置返回 INVALID_ARGUMENT。

## 打包工具

作者工具由独立 `tinyrt-sdk` 仓库提供；本 core 仓库保留包格式、校验实现和独立测试夹具。
普通应用推荐使用 SDK 的 `tools/tinyrt.py build/pack`；以下为 SDK 底层打包入口。

依赖 Python 与 `cryptography>=43`。使用库的确定性 ECDSA，再规范化 low-S，相同输入产生相同完整包及安装身份。

```powershell
python /path/to/tinyrt-sdk/tools/package.py pack --wasm app.wasm --assets resources.bin --output app.trpkg --app-id demo.counter --title 计数器 --version 1 --permissions 7 --memory-pages 4 --budget 10000 --key-id 100 --key application-signing.pem
```

省略 `--assets` 可生成零资源包。成功 stdout 是 JSON，包含 app_id、version、package_size、sha256 和 key_id，便于安装协议使用。错误参数不会覆盖已有输出；输出也不能与 Wasm、资源、签名钥输入指向同一文件。打包工具仅检查 Wasm 魔数/版本；设备仍必须执行完整结构/ABI 校验。

## 公开开发密钥：仅用于测试

`--development-key` 显式启用固定公开标量 **1** 的 P-256 私钥，stderr 明确警告。它是公开、可伪造的测试密钥，绝不可用于生产信任。生产模块没有自动信任它；测试部署需要显式提供其公钥及 ID。

```powershell
python /path/to/tinyrt-sdk/tools/package.py development-public-key --output development-public.sec1
python /path/to/tinyrt-sdk/tools/package.py pack --wasm app.wasm --output demo.trpkg --app-id demo.counter --title Counter --version 1 --permissions 7 --memory-pages 4 --budget 10000 --key-id 1 --development-key
```

独立校验夹具另外使用公开测试标量 2、42、43，均只作测试签名。工具不查找、复制、生成或替换工程的固件签名私钥。
