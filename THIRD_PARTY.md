# 本地依赖和来源

| 内容 | 位置 | 来源 |
| --- | --- | --- |
| 生命周期框架 | components/app | 用户 esp_idf_template，保留初始化/回滚/失败保留语义 |
| 菜单、弹簧、ASCII 字体、宠物 | components/core | STM-GameBox Rust 到 C++ 移植，MIT，保留 third_party/STM-GameBox.LICENSE |
| CMSIS-DAP 引擎 | components/debug_probe/vendor | Arm CMSIS-DAP，Apache-2.0，完整 LICENSE |
| SWD/TCP 适配 | components/debug_probe | 参考 m5_sticks3 引擎与校验，重写 C3 引脚、延时与无线生命周期 |
| Super Mini 引脚图 | docs/hardware/pinout.png | 用户 NAS 图片，保留本地副本 |
| 主机工具 | tools | DAP/预览工具使用 Python 标准库；串口测试使用本地 SDK 的 pyserial/esptool |

~~~text
/home/kk/kk_data/esp_project/esp32_idf/esp_idf_template
/home/kk/kk_data/cpp_project/STM-GameBox
/home/kk/kk_data/esp_project/esp32_idf/m5_sticks3/components/debug_probe
smb://nas.local/home/archive/docs/ESP32C3SuperMini-资料/ESP32C3SuperMini引脚定义图.png
~~~

指定模板没有无线调试代码，用户确认后采用相邻 m5_sticks3 的 CMSIS-DAP 来源。
只复制引擎和测试所需源码，不引入 M5Stack、USB、BLE 或 LVGL 组件。

Arm 上游固定提交 12636590eec66fae2d1bba4518749426ad5a4595。
适配修改见 components/debug_probe/vendor/README.md。
菜单相关代码的完整 MIT 声明保存在 third_party/STM-GameBox.LICENSE。

ESP-IDF 5.5.4、FreeRTOS、lwIP、Wi-Fi、NVS、esptool 和 OpenOCD 由本地 SDK 提供。
没有 idf_component.yml、远程 EXTRA_COMPONENT_DIRS、FetchContent 或源码下载。
clang-format 18.1.8 只在 build/format-tool 中，用于开发格式化，不参与固件构建。
