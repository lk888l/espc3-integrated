# Wi-Fi CMSIS-DAP / SWD

C3 作为外部 MCU 的 CMSIS-DAP 探针，OpenOCD 提供 Flash 算法和 GDB。
当前为 SWD，不支持 JTAG、ST-Link 私有协议、USB DAP、BLE、SWO 或 UART 桥。

## 接线和启停

| C3 | 目标 |
| --- | --- |
| GPIO20 | SWCLK，STM32 常见 PA14 |
| GPIO21 | SWDIO，STM32 常见 PA13 |
| GND | GND |
| 可配置 NRST | 默认不连接 |

目标自行供电，仅直接连接 3.3 V 逻辑。板上没有 VTref 或电平转换。
用短线、100 kHz 上限开始。已用 GPIO 不可兼作 NRST。
GPIO2 已分配给独立退出键，不能再作 NRST。需要 NRST 时应调整接线，
或核对 GPIO8/9 的启动电平及板载 LED/BOOT 连接后显式允许使用。
全局 `C3_ALLOW_STRAPPING_PINS` 仍默认关闭，仅 GPIO2 作为 Back 输入有专用例外，冲突检查保持有效。

Tools → Wi-Fi DAP 启用服务；主菜单默认不开放 TCP/UDP。
暂停、返回、TCP EOF、帧头错误或请求收发超时释放引脚。
DAP_Connect(SWD) 之前保持输入；NRST 配置为开漏并依赖目标上拉。
USB dap on/off 控制同一服务。

## 主机检查

~~~bash
python3 tools/dap_client.py discover
python3 tools/dap_client.py info --host 192.168.4.1
python3 tools/dap_client.py smoke --host 192.168.4.1 --count 100 --fragment
python3 tools/wifi_smoke.py --host 192.168.4.1
openocd -c "adapter driver cmsis-dap; help cmsis-dap; shutdown"
~~~

Python 命令只发现或查询 DAP_Info，不接触外部目标。
--fragment 逐字节发送请求，检查 TCP 分片。
OpenOCD 必须包含 cmsis-dap backend tcp，本容器有
v0.12.0-esp32-20251215；导出 SDK 环境后使用。
部分发行版旧 OpenOCD 没有 TCP 后端。

## 连接、烧录和 GDB

~~~bash
# 连接检查；替换实际 MCU 的 target。
openocd -c "set DAP_HOST 192.168.4.1" \
  -f tools/openocd/c3-wifi.cfg -f target/stm32f1x.cfg \
  -c "init; targets; shutdown"

# 写入外部目标 Flash，ELF 自带地址。
openocd -c "set DAP_HOST 192.168.4.1" \
  -f tools/openocd/c3-wifi.cfg -f target/stm32f1x.cfg \
  -c "program /path/to/firmware.elf verify reset exit"

# GDB 服务。
openocd -c "set DAP_HOST 192.168.4.1" \
  -f tools/openocd/c3-wifi.cfg -f target/stm32f1x.cfg
arm-none-eabi-gdb /path/to/firmware.elf
~~~

~~~gdb
target extended-remote localhost:3333
monitor reset halt
load
break main
continue
~~~

默认 reset_config none；未接 NRST 时依赖目标的软件复位。
接线并配置 NRST 后，在 target 配置后追加：

~~~text
-c "reset_config srst_only srst_nogate connect_assert_srst"
~~~

目标关闭 SWD、进入低功耗或开启保护时可能需要 NRST 和芯片恢复流程。
芯片识别、调试保护和 Flash 算法由 OpenOCD 决定。

## 时钟和取消

菜单上限 100 / 250 / 500 / 1000 / 2000 kHz，默认 100 kHz。
主机范围 1 kHz–2 MHz，最终配置取请求与上限的较小值。
status 区分 requested/configured/limit；它们不是实测频率。
RISC-V cycle counter 提供延时；GPIO 指令、中断和 UI 抢占会降低实际频率。

命令设置 200 ms 取消截止时间，WAIT/match 循环检查它，
序列和长 idle 时钟也定期检查。最坏退出时间仍包括正在进行的位传输、
抢占和硬件调用，200 ms 不保证是总墙钟上限。
识别工作期间的 DAP_TransferAbort，按标准不返回响应。
未完成 TCP 请求或响应超过 2 s 关闭；静默连接使用 TCP keepalive。

## 协议

TCP 4441 采用 8 字节头：

| 字段 | 长度 | 内容 |
| --- | ---: | --- |
| magic | 4 | ASCII DAP 后跟 NUL |
| length | 2 | 小端，1..64 |
| type | 1 | 请求 1、响应 2 |
| reserved | 1 | 0 |

一名客户端持有目标端口，第二连接立即关闭。
进入 Arm 引擎前验证请求消耗和最坏响应，拒绝原子/JTAG/未知命令。
TransferAbort 无响应，其余返回普通 CMSIS-DAP 数据。

UDP 4442：

- 请求恰好 ASCII C3_DAP_V1?，无 NUL。
- 响应 ASCII C3_DAP_V1! + 12 位大写十六进制序列号 + 两字节大端 TCP 端口。
- 总长度 24 字节，使用源 IP，按序列号去重。

格式依据 [OpenOCD CMSIS-DAP TCP 文档](https://openocd.org/doc/html/Debug-Adapter-Configuration.html)。
TCP DAP 没有 TLS 或应用鉴权，使用设备受密码保护的热点或可信局域网，
不要转发端口到公网。
