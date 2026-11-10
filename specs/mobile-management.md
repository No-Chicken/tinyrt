# 手机应用管理：封面与设备确认（schema 1）

所有整数小端。消息继续通过既有 MGMT 分片，每个逻辑回复开头包含 `status:u16`；以下回复长度和偏移均从去掉 status 后的 payload 计算。HELLO capability byte 的 bit7（128）表示 APP_COVER 和异步设备确认都可用。连接必须认证并绑定。

## APP_COVER（0x1c）

请求共 74 字节：identity68（app_id32、version:u32、整包 SHA-256[32]），offset:u32（偏移68），length:u16（偏移72，1..206）。offset 相对于完整 cover section，包含其 32 字节头。

| 回复偏移 | 字段 |
|---|---|
| 0 / 2 | schema:u16=1 / codec:u16=1 |
| 4 / 8 | total:u32=133232 / offset:u32 |
| 12 / 14 | chunk_length:u16 / reserved:u16=0 |
| 16 | 完整 cover section SHA-256[32] |
| 48 | data[chunk_length] |

每次返回请求长度和剩余长度的较小者；offset>=total 拒绝。无 cover 返回 NOT_FOUND(8)。客户端限制 total=133232，核验每段 offset、长度、同一个 SHA-256；完成后核验完整段摘要与 `TRCOV001` 头及固定 210/150 尺寸。codec=1 的字节布局见 [包规格](https://github.com/No-Chicken/tinyrt/blob/main/specs/package.md)。PNG 源文件不会直接传回；手机可以将 RGB565LE 转为自己的图像。SDK Python `app_cover` 和 miniapp TypeScript `appCover` 提供经过完整检查的主图/侧图像素。

APP_INFO 保持 248 字节，section_bits offset6 新增 bit3=cover。该位表示固定 133232 字节，计算整包总长时需将它计入 section 表及四字节对齐；Wasm/AOT/resources 字段偏移不变。

## PREPARE / UNINSTALL 设备确认

PREPARE(0x13) 请求仍为 info72；UNINSTALL(0x14) 仍为 identity68。在用户通过加号手动打开的接收窗口中，成功payload为空，可立即继续；旧固件也沿用空回复。只读管理和由远程确认开启的受限窗口不会自动授权其他应用操作。OTA模式拒绝安装/删除。手机请求触发设备提示时，成功 payload 为 8 字节凭据：schema:u16=1、kind:u8（删除1、安装2）、reserved:u8=0、token:u32（非零）。收到凭据只表示请求已登记。

使用 MOBILE_STATUS(0x1d) 查询同一连接的 token:u32。回复共 12 字节：schema:u16=1、kind:u8、state:u8、token:u32、result:u16、reserved:u16=0。

| state | 处理 |
|---|---|
| 1 WAITING | 等待设备触摸确认，result=BUSY(6) |
| 2 PROCESSING | 已确认正在执行，result=BUSY(6) |
| 3 DONE | result=OK(0) 才算成功，否则传播错误 |
| 4 CANCELLED | 拒绝/断连取消，result=BUSY(6)，停止操作 |
| 5 TIMED_OUT | 15 秒设备确认到期，result=BUSY(6)，停止操作 |

SDK 每250毫秒轮询，客户端20秒上限；schema、kind、token、reserved 和状态必须匹配。token 随断连失效，不可换连接继续轮询。安装只在 DONE/OK 后发送 target2 START；删除只在 DONE/OK 后 QUERY 验证该完整 identity 为 NOT_FOUND。拒绝或超时不得开始传输。Python `confirmed_operation` / TypeScript `confirmedOperation` 同时兼容旧空回复。

封面只在 idle 时读取；安装传输 active 时设备拒绝 cover 请求，以保护带宽。

安装前读取 STORAGE，比较新包占用向上对齐4096字节后的大小与 largest_free_bytes；总空闲不能替代最大连续空闲。同 ID 更新保留旧包，仍需额外一份新包连续空间。不够时先让用户删除应用，不启动传输。设备在 PREPARE 中只能显示尚未认证的 app_id，最终应用标题来自验签后的安装元数据。

确认安装仅授权该次完整identity和包大小，15秒内开始传输；断连撤回本次许可但保留受限连接窗口，重试需要新的token和再次设备确认。受限窗口中LAUNCH/STOP拒绝；后续安装/删除仍需独立确认。
