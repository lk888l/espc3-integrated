# 验证记录

日期：2026-10-08。目标为用户通过 /dev/ttyACM0 连接的 ESP32-C3 Super Mini。
芯片 ESP32-C3 QFN32 rev v0.4，原生 USB Serial/JTAG；
esptool 读出的板载 Flash 为 XMC 4 MiB，晶振 40 MHz。

## 构建与主机检查

| 检查 | 结果 |
| --- | --- |
| 本地 ESP-IDF v5.5.4，ESP32-C3，C++20 | 构建通过；固件使用 SDK GPIO LL 所需的 GNU 扩展 |
| 全新 sdkconfig.defaults 配置 | reconfigure 通过，与用于编译的 sdkconfig 配置一致 |
| USB 主控制台、160 MHz CPU、1 kHz RTOS tick | 默认配置核对通过 |
| core_tests | 按键去抖/手势/组合返回、回绕计时、弹簧、快速导航、渲染、引脚规则通过 |
| lifecycle_tests | 初始化、反序回滚、失败资源保留通过 |
| dap_protocol_tests | 请求/响应边界、帧和发现协议通过 |
| dap_engine_tests | 本地 Arm 引擎、SWD 模拟 GPIO 与取消通过 |
| dap_client_tests | Python 客户端分片、EOF、无效报文与发现解析通过 |
| clang-format 18.1.8 | 自有 C++ 文件格式检查通过 |
| 菜单导出 | 使用实际 C++ 渲染器，PNG 与 GIF 已生成并查看 |

五组 CTest 全部通过。C/C++ 测试启用 ASan/UBSan、警告作为错误和断言；
Python 客户端测试使用标准库。固件构建无需下载组件。

## 实板 USB

最终固件写入及 esptool 哈希校验通过，启动与控制台正常。
运行 tools/serial_smoke.py 通过六组集成检查：

- 四主页与快速跳页，动画期间连续 40 个 Pressed 全部进入模型；
- 秒表启停/清零、倒计时启停；
- 临时修改 Motion/Brightness/Header，重启后读取正确，随后恢复原设置；
- 热点密码重启后保持一致；
- DAP 监听/停止、Wi-Fi 关闭联动停止 DAP、Wi-Fi 再开启；
- 未出现 SPI 驱动错误或按键队列丢事件。

USB status 的 UI 字段来自实际模型快照；注入事件经过同一菜单队列。
这项测试验证软件导航，不代表实体按键边沿、去抖延时或面板显示已实测。

## Wi-Fi 与 CMSIS-DAP

使用用户提供的测试网络，凭据写入设备 NVS。
当前 STA 地址 172.18.6.151，地址可能随 DHCP 租约变化。

| 实测 | 结果 |
| --- | --- |
| STA 连接、重启恢复凭据、获得 DHCP IP | 通过 |
| SNTP 自动校时、手动 ntp 重新同步 | 通过，UTC+8 时间与主机相差 0 秒 |
| UDP 4442 定向发现 | 序列号与 TCP 身份一致 |
| DAP_Info 连续查询 | 300 次通过，9.652 s，平均 32.174 ms/次 |
| TCP 逐字节分片 | 100 次通过 |
| 单客户端与 TransferAbort 无响应 | 通过 |
| 截短请求与非法长度 | 安全拒绝，随后可继续使用/重连 |
| 未完成帧 | 约 2.011 s 后关闭 |
| 无线请求与菜单并发 | 100 次分片查询期间，100 次菜单输入全部处理 |
| 并发统计 | UI 事件增加 100，队列 drops=0，采样 late 未增加，SPI errors=0 |
| OpenOCD 实际 TCP 后端 | init / cmsis-dap info / shutdown 通过 |

探针识别为 C3GameBox / C3 Wi-Fi CMSIS-DAP，CMSIS-DAP 2.1.2，
序列号 0C4EA04D5B10，包大小 64 字节，能力值 33（SWD、时间戳）。
测试使用 OpenOCD v0.12.0-esp32-20251215，配置上限 100 kHz。
没有配置 target，OpenOCD 因此提示不能启动 GDB target 服务；适配器初始化成功。

32.174 ms 是该网络环境中的 DAP_Info 往返测量，不是 SWD 位频率或 Flash 吞吐率。
并发测试时空闲堆 149408 字节；最低堆曾到 130852 字节。
SPI 软件调用样本约 1.1–1.2 ms，包含 IRQ 和抢占，不是面板刷新率。
开机和无线重启过程中 late 计数曾增加，5 ms 调度周期不构成硬实时保证。

实测发现的 DHCP 等待期间重复 connect 已修正：
分别跟踪连接中、已关联、已获得 IP；新版启动日志未再出现该连接报错。
串口脚本也已保留打开时的 DTR/RTS，避免读状态时意外复位。
退出探针时显式禁用输出和内部上下拉。

## 产物

应用 bin：928560 字节；3 MiB 应用分区剩余约 70.5%。
链接器 DRAM 静态占用 129228 字节（40.22%），这是静态占用，运行堆另见上文。

~~~text
build/idf/espc3_integrated.elf
build/idf/espc3_integrated.bin
build/idf/bootloader/bootloader.bin
build/idf/partition_table/partition-table.bin
~~~

应用 bin SHA-256：

~~~text
d3cf9a4809b18591ba35c109ec7dc4ab2e8f75013ca56bd1ad9ca416d2783bf9
~~~

构建/烧录日志：build-flash.log；大小统计：build-size.log。
主机测试在 build/host/；串口、无线、并发和 OpenOCD 记录在 build/hardware/。
测试后设备回到 Tools 主页卡片，Wi-Fi 开启、时间已同步，DAP 服务关闭。

## 尚需接线实测

当前只连接 USB。OLED、实体按键与外部 MCU 均未连接。
因此面板方向/亮度/可见动画、实体按键延迟、SWCLK 电气频率、
外部 MCU 识别/读写/烧录/断点、NRST 和引脚高阻电气状态仍需验证。
步骤见[实板验收清单](hardware-validation.md)及[无线调试说明](wireless-debug.md)。
