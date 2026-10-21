# 无线电脑监控

电脑通过 wl1-studio 的 Rust 后台采集本机指标，经 Wi-Fi 发送到 ESP32-C3。
支持 Windows / Linux、CPU、内存、NVIDIA GPU 占用率、显存容量和 GPU 温度。
无需自行安装监控驱动；GPU 数据使用已有 NVIDIA 驱动提供的 NVML。
没有 NVIDIA 驱动或某项指标不可用时，CPU / 内存继续发送，该项显示 N/A。

## 使用

1. 让 ESP32-C3 与电脑处于同一局域网。电脑使用有线网络也可以。
   也可让电脑连接设备热点，通过 `192.168.4.1` 通信。
2. 在设备 Network 页面或 USB `status` 命令中查看 STA IP。
   STA 地址来自 DHCP，路由器可能重新分配；连接失败时重新核对地址。
3. 在 `/home/kk/kk_data/rust_project/wl1-studio` 启动桌面应用：

   ```bash
   npm run tauri dev
   ```

4. 打开 **ESP-C3 桌宠 → 电脑监控**，填写设备 IP，选择显卡和更新周期，点击
   **开始监控**。看到“设备已响应”表示 MCU 已确认接收。
5. MCU 进入 **Tools → PC Monitor**。Up / Down 或 Enter 切换两页，Back 返回 Tools。
   第一页为 CPU / RAM / GPU 百分比及进度条，第二页为 RAM / VRAM 容量和 GPU 温度。

电脑端切页、返回产品库和最小化均继续采集，全局状态条可以停止发送。
退出 wl1-studio 会停止任务。首版没有托盘、自启动或关闭窗口后继续运行功能。
浏览器版只能预览配置，采集和发送需要桌面版。无线监控不需要 USB 数据连接，设备仍需供电。

## 数据与生命周期

- Rust 核心位于 wl1-studio 的 `src-tauri/crates/pc-monitor-core`，不依赖 Tauri / React。
  采集、发送和后台任务可供未来独立程序复用。
- 默认周期 500 ms；界面可选 500 / 1000 / 2000 ms。
  CPU 使用差分采样，首包 CPU 为 N/A；RAM 已用按 `total - available` 计算。
  GPU 占用为 NVML GPU 利用率；显存使用 `memory_info.used / total`，与显存带宽利用率区分。
  容量显示 GiB（1024³ 字节）。
- MCU 使用低优先级接收任务，发布固定大小的最新值快照。UI 任务独占屏幕。
  超过 3 秒未更新时提示 `STALE / RECONNECT`，数值显示 N/A。
- 监控接收随 Wi-Fi 启停，菜单和 USB 桌宠会话独立。Wi-Fi 恢复后自动重新接收。
  采样不写 NVS，只保存在 RAM。
- 当前按单台电脑向单台 MCU 发送设计，目标地址手动填写。局域网需允许 UDP 4443，
  设备热点与 STA 都可访问；AP 本身不提供互联网。

## UDP v1

使用 UDP **4443** 单播，报文 UTF-8 JSON，大小严格小于 1024 字节。
现有 DAP TCP 4441 / UDP 4442 不变。每份数据完整描述当前指标，不补发历史数据。

```json
{"v":1,"session":"0123456789abcdef","seq":1,"host":"MY-PC","cpu_pct":23.5,"ram_used":8589934592,"ram_total":34359738368,"gpu_name":"NVIDIA GPU","gpu_pct":42.0,"vram_used":4294967296,"vram_total":12884901888,"gpu_temp_c":55.0}
```

`session` 为最多 32 字节的 ASCII 字母、数字、下划线或连字符；每次启动生成新会话。
`seq` 为 u32，支持回绕；同会话重复或旧序号丢弃。所有字段都需要存在。
`cpu_pct / gpu_pct / gpu_temp_c` 可为 null；显存两个字段同时为 null 表示不可用。
RAM / VRAM 容量为字节；RAM 总量必须大于零，已用量不能超过总量。

有效数据回复：

```json
{"v":1,"session":"0123456789abcdef","seq":1,"ack":true}
```

电脑只接受目标设备发回、与当前会话和已发送序号匹配的回执；3 秒没有有效回执，
界面提示等待响应或响应过期。MCU 拒绝无效版本、范围、字符串、重复字段及超长报文。

## 验证与诊断

固件构建、烧录和主机测试：

```bash
docker exec -w /workspace/espc3-integrated esp-dev bash tools/idf.sh build
docker exec -w /workspace/espc3-integrated esp-dev bash tools/idf.sh -p /dev/ttyACM0 flash
docker exec -w /workspace/espc3-integrated esp-dev bash tools/check.sh
```

专项实板测试会临时切页、运行游戏和开关 Wi-Fi，保留已有配置和 STA 凭据，最后返回主页：

```bash
docker exec -w /workspace/espc3-integrated esp-dev bash -lc \
  'source /opt/esp-idf/export.sh && python tools/pc_monitor_smoke.py --port /dev/ttyACM0 --host 设备STA地址 --duration 30'
```

结果和串口日志在 `build/hardware/pc-monitor/`。`status` 中的 `pc_*` 字段包含
接收计数、异常/乱序计数、数据年龄、字段有效性、socket 错误和任务栈余量。

不启动完整桌面界面也能验证同一轻量核心。在 wl1-studio 目录执行：

```bash
cargo run --manifest-path src-tauri/Cargo.toml -p pc-monitor-core --example send -- 设备STA地址 10
```

该例每秒打印真实快照，持续指定秒数后停止，不会更改桌面应用的配置。
