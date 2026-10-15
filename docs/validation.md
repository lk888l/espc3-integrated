# 验证记录

## 2026-10-15：俄罗斯方块单击延迟缩短，固件 1.1.2

Tetris 的 Enter 单击/双击判定窗口从 280 ms 缩短到 150 ms，
其余页面和按键保持 280 ms。输入任务从 UI 快照选取窗口，首次释放时锁存，
避免切页改变已开始手势的判定时限；单双击保持互斥。

- 七组 ASan/UBSan CTest 全部通过：真实 ButtonBank 在释放后 149 ms 不产生单击、
  150 ms 产生一次单击；覆盖其他按键仍为 280 ms、跨页锁存、双击及计时回绕。
- `/dev/ttyACM0` 烧录及哈希校验通过。
- 新固件 `tetris_smoke.py` 六项串口功能检查通过，单击、双击、长按和继续行为正确，
  drops=0、SPI errors=0，结束回 Games 主卡。串口注入最终手势验证动作，
  判定耗时由主机 ButtonBank 采样序列验证。

应用 bin 为 948528 字节，SHA-256：

~~~text
fd133544b27c725232c38f1083a89e1d347a379cac9efa108072d5bc7b99e394
~~~

日志 `build-tetris-latency-host.log`、`build-tetris-latency.log`、
`build-tetris-latency-flash.log`；实机结果在 `build/hardware/tetris-latency/`。

## 2026-10-15：俄罗斯方块单双击修复，固件 1.1.1

修正 Enter 在 Pressed 时提前旋转、双击硬降夹带两次旋转的问题。
现在单击确认后旋转一次（默认松开后等待 280 ms），双击只硬降并保持朝向，
长按只暂停；开始和继续的尾随点击不会操作方块。

- 完整七组 CTest，ASan/UBSan 全部通过。
- 新增 `tetris_input_tests` 使用真实 ButtonBank 去抖和手势序列：
  旧引擎在双击第一次按下后的画面不变断言失败，新引擎通过；
  覆盖单双击互斥、硬降画面与 Function 基准一致、长按、开始/继续边界、
  等待中的单击与下一次按下/释放交错、连续手势及计时回绕。
- `/dev/ttyACM0` 烧录成功，哈希校验通过。
- `tetris_smoke.py` 六项实机串口检查通过；双击的四次按下/释放均未旋转，
  只在最终 DoubleClick 时硬降；单击、长按及继续的朝向检查通过。
  SPI errors=0、drops=0，结束回 Games 主卡，设置与 Wi-Fi 保持。

应用 bin 为 948352 字节，SHA-256：

~~~text
ef4ec60b423232d314c5fac7a1dd0999cacb70cd3635294a2da25c89dc790d2e
~~~

日志 `build-tetris-host.log`、`build-tetris-fix.log`、`build-tetris-flash.log`，
实机结果 `build/hardware/tetris-smoke.json` 与 `tetris-smoke.log`。

## 2026-10-15：八款游戏，固件 1.1.0

移植 Dino、Snake、Air Raid、Tetris、Pong、Piano，新增 Breakout 和 Flappy。
主页改为 Games / Tools / Network / Clock / Settings。

| 检查 | 结果 |
| --- | --- |
| ESP-IDF Docker 编译、目标 esp32c3 | 通过，原 sdkconfig 错选 esp32s3 已重新生成，旧配置备份在 build/sdkconfig.before-games |
| 完整六组 CTest，ASan/UBSan、警告作为错误 | 全部通过 |
| game_tests 真实玩法 | Dino 跳障碍/碰撞、Snake 吃食/碰墙、Air 射击/三命、Tetris 三块消行、Pong 终局、Piano 音符/时长、Breakout 打砖/三命、Flappy 过管/碰撞均通过 |
| game_tests 稳定性 | 八款共 48,000 次随机输入确定性重放，覆盖 uint32 时间回绕与转场偏移渲染；暂停画面、分数与模拟保持不变 |
| USB 烧录 /dev/ttyACM0 | 成功，esptool 哈希校验通过，芯片 ESP32-C3 |
| 最终镜像与设备 Flash | verify_flash 比对全部 948000 字节，digest matched |
| game_smoke.py | 11 项通过；八款开始、三键输入、暂停、恢复、退出与重进；40 次菜单输入无丢失 |
| serial_smoke.py | 原功能六组通过，计时器、五主页、设置与热点身份重启持久化、Wi-Fi/DAP 启停；原设置已恢复 |
| 画面 | 最终固件相同的 C++ 渲染器导出，游戏及操作提示 PNG 已生成并查看 |

游戏回归的空闲堆采样最低 147472 字节，结束 147816 字节；
UI 任务 8192 字节栈的历史最低余量 4524 字节。
按键队列 drops=0，SPI errors=0，Wi-Fi 与原设置保持。
GameSession / BonusSession 主机占用分别为 356 / 60 字节，游戏状态使用固定数组。

最终应用 bin 为 948000 字节，3 MiB 应用分区约剩 70%；
静态 DRAM 129990 字节（40.46%）。应用 bin SHA-256：

~~~text
0790d15d06052e3159e0544acdff1a2acd41bced722e39bf72a44441dc7322f9
~~~

构建/烧录日志 `build-game-flash.log`，资源统计 `build-game-size.log`，
主机测试 `build-game-host.log`，镜像比对 `build-game-verify.log`，
实机日志及 JSON 在 `build/hardware/`。设备最终停在 Games 主页，DAP 关闭。
操作说明与按键映射见 [README 游戏章节](../README.md#游戏)。
串口回归验证真实设备上的固件模型和驱动提交；实体按键手感、OLED 可见画面与
可选蜂鸣器声音未在本轮直接观察。Piano 默认未配置蜂鸣器，保持无声。

## 2026-10-08：原菜单、网络与无线调试

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
