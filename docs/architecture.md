# 架构

保留 ESP-IDF 模板的 AppModule / AppManager / AppTask 生命周期。
主入口只做组合，初始化顺序 Storage → Network → Probe → Input → UI → Console，
失败时反序清理。任务不能停止时保留模块及运行时存储，避免释放仍在使用的对象。

## 模式与边界

| 模式 | 使用位置 | 目的 |
| --- | --- | --- |
| 工厂 | create_* | 主入口不依赖具体模块类 |
| 模板方法/生命周期 | AppModule / AppManager | 初始化、回滚、失败保留 |
| 状态机 | ButtonBank / BackChord / Model / TCP | 有界、可重复验证的事件处理 |
| 硬件适配器 | Oled / network / debug_probe | 核心不包含 ESP-IDF API |
| 消息队列 | 按键 64 项、设置/凭据各 1 项 | 单一所有者、非阻塞提交 |
| 快照 | 网络、DAP、UI 状态、Runtime 统计 | UI 和控制台不共享可变业务对象 |
| 仓储 | NVS | 版本检查、提交、写入合并 |

按键采样路径无动态分配。设置按四字节 schema v1 序列化，避免保存 C++ 对象 ABI。
STA 凭据用有版本、定长字段的单个 blob；AP 密码单独保存。
NVS 初始化失败会报告错误，不自动擦除已有配置。

## 任务

| 任务 | 优先级 | 栈字节 | 职责 |
| --- | ---: | ---: | --- |
| buttons | 6 | 3072 | 默认 5 ms，读取 GPIO 并识别事件 |
| oled_ui | 4 | 6144 | 默认 5 ms，唯一 UI/屏幕所有者 |
| network | 3 | 4096 | 25 ms，重连、凭据、SNTP |
| wifi_dap | 2 | 6144 | 2 ms 轮询，唯一 DAP/SWD 所有者 |
| console | 2 | 4096 | 有限时 USB 读取，命令提交请求 |
| settings | 1 | 3072 | 25 ms，750 ms 静默后保存 |

ESP-IDF Wi-Fi 和系统任务采用 SDK 自己的优先级，应用不修改它们。
C3 单核：SWD 不禁用全局中断或跨传输持有临界区，UI 可抢占 SWD。
实际 GPIO 时钟可能停顿，需逻辑分析仪测量。

按键队列给 Pressed/Released/LongPress 预留八个位置，
拥塞时先拒绝 Repeat/Click/DoubleClick，并统计所有提交失败。
满队列仍可能丢事件，Input Lab/status 可查看 drops 和 late。
Up+Down 组合转换成一次 Back，两个键释放后才可再次触发。

## 动画

Q24.8 Spring 移植自 STM-GameBox：
Full 刚度/阻尼 62/184，Reduced 34/206，Off 直接到目标。
每 8 ms 推进固定模拟步长，单轮最多追赶 64 ms，错过调度后从当前重新安排。
5 ms 是 UI 目标调度周期，不是实测可见帧率；面板扫描、渲染、
Wi-Fi、Flash 和 SPI 都会影响表现。

保留 carousel_x、page_x、cursor_y、cursor_width、scroll_y 五个动画值。
动画接受新的 Pressed，Released/Click 不重复确认。
主页保留四卡片，内容适配为 Tools / Network / Clock / Settings。
字体、布局和宠物两帧来自相同参考工程。

## 屏幕

SPI2_HOST、无 MISO、Mode 0、8 MHz、1024 字节 DMA。
同一设备只由 UI 访问，缓冲区分配在内部可 DMA 内存。
相同帧不传输，成功后才记录对比帧；错误时硬复位并退避重试。
1024 字节纯数据时间约 1.024 ms；spi_us 记录软件调用墙钟耗时，
包含命令、复制、IRQ 和抢占。

SSD1306 当前接线没有回读/ACK，SPI 成功不代表屏幕接好，
需实测 DC/RES/CS、供电和模块 SPI 跳线。

## 网络和时间

回调仅更新短快照和标志，耗时操作留给 network 任务。
STA 从 1 s 退避到最多 60 s，AP 保持可访问；RSSI 每秒更新。
分别跟踪连接中、已关联、已获得 IP，避免 DHCP 等待期间重复调用 connect。
关联与 DHCP 分别有 15 s / 30 s 截止时间，过期后断开并退避重试。
SNTP 获得 IP 后启动，回调记录真实同步时间，按 POSIX 时区显示。
AP 本身不提供互联网。关闭 Wi-Fi 会同时关闭 DAP。
打开探针页会请求启用 Wi-Fi，返回/暂停只关闭 DAP。

UI 将页面、选择、设置和计时器状态发布为短临界区快照，
控制台可诊断真实模型，不直接读取或修改 UI 所有者的 Model。
串口测试脚本打开 USB 时保留 DTR/RTS，显式 reset 才改变控制线，
避免查询状态时意外复位并中断无线会话。

core 不依赖 FreeRTOS 或 ESP-IDF，可用于主机测试。
GPIO LL 访问限制在输入与 SWD 适配器，升级 SDK 后需复核。
不需要 LVGL、Arduino、下载字体或组件管理器。
