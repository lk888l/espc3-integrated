# 跨平台连接与桌宠控制

## 第一次连接

1. 在设备主页打开 **Network**。电脑或手机与设备在同一局域网时，用浏览器打开屏上 STA 地址，例如 `http://172.18.6.151/`。STA 地址由路由器分配。
2. 初次配网或路由器不可用时，连接屏上的 **C3GB-xxxxxx** 热点，密码在 **KEY** 行。随后打开 `http://192.168.4.1/`。网页和脚本均随固件提供，可离线使用。
3. 在 **Network** 按一次 **Up**，KEY 行会临时显示 **PAIR CODE**。在网页输入六位数字。可信 USB 控制台的 `wifi pair` 也可生成配对码。
4. 选择“像素猫”或“月兔”会直接在 OLED 打开该造型，点击“跳舞”“进食”等动作会立即作用于当前宠物。网页预览取自设备刚渲染的桌宠帧。退出桌宠回到设备主页，客户端仍能调整显示和 Wi-Fi。

配对码两分钟有效，最多尝试五次，每次成功配对消耗一个码。为另一台客户端再按一次 Up。设备可保留四个独立会话，闲置 30 分钟过期。网页刷新沿用当前标签页的会话。“退出配对”撤销当前会话，Network 页按 Down 或 USB 输入 `wifi revoke` 撤销全部会话。关闭设备 Wi-Fi 或重启后需要重新配对。

控制接口使用本地明文 HTTP，适用于可信局域网或设备的 WPA2 热点。配对码、会话令牌与 Wi-Fi 密码应只填在可信设备上。浏览器直接打开设备提供的网页，网络请求与页面同源。

## 桌面程序与手机

Python 客户端仅依赖标准库，在 Linux、macOS 或 Windows 的 Python 3.7+ 环境运行。下例中的 IP 替换为设备当前地址。命令行会提示输入配对码，完成操作后释放临时会话。

~~~sh
python3 tools/gamebox_client.py discover
python3 tools/gamebox_client.py --host 192.168.4.1 info
python3 tools/gamebox_client.py --host 192.168.4.1 pet --style 1 --action dance
python3 tools/gamebox_client.py --host 192.168.4.1 display --brightness 2
~~~

局域网发现使用 UDP 4444。向子网广播地址发送 UTF-8 字节 `C3GB_DISCOVER/1`，设备单播回复 `{"v":1,"id":"C3GB-4D5B10","http":80}`。客户端使用回包的**源 IP**。Python 客户端会尝试各网卡的广播地址，也可在 discover 后传 `--target 子网广播地址`。路由器隔离或丢弃广播时可手填屏上的 IP。设备 ID 也用于热点名称及 STA 的 DHCP 主机名。

在微信开发者工具中将 [examples/wechat](../examples/wechat) 作为小程序目录导入，使用自己的测试 AppID。在手机上连设备热点或与设备位于同一网段，允许局域网访问，输入设备的数字 IP 和配对码即可切换桌宠。已有小程序可单独复用 [gamebox.js](../examples/wechat/gamebox.js)。局域网通信的地址条件见[微信网络说明](https://developers.weixin.qq.com/miniprogram/dev/framework/ability/network.html)。

原生 App 可直接使用下面的请求和回包，例如用 URLSession 或 OkHttp。目标为设备的局域网 HTTP 地址，需按目标系统配置本地网络访问及明文 HTTP 权限。

设备热点允许四个 Wi-Fi 客户端。默认构建预留四个 HTTP 连接槽，同时给电脑监控、设备发现和 DAP 留出 socket。已有 sdkconfig 若沿用 SDK 的 10 个 socket，HTTP 会使用两个连接槽，空闲连接可被新连接替换。需要四个连接槽时设置 `CONFIG_LWIP_MAX_SOCKETS=12`。

## HTTP v1

| 方法和路径 | 请求和结果 |
| --- | --- |
| GET /api/v1/info | 公开的设备 ID、型号、协议版本与能力列表 |
| POST /api/v1/session | `{"code":"123456"}` 换取 `{"token":"…","idle_seconds":1800}` |
| GET /api/v1/status | 已配对设备的当前页面、宠物、显示设置、STA/AP 地址、RSSI 及配网状态 |
| POST /api/v1/pet | `{"style":1,"action":"dance"}` 原子选择造型并触发动作。仅传 style 打开该造型，仅传 action 操作已打开的宠物。`{"close":true}` 退出。成功返回当前状态 |
| GET /api/v1/pet/frame | 配对后读取当前桌宠帧，Content-Type 为 application/octet-stream。仅在桌宠页且屏幕传输正常时可读 |
| POST /api/v1/display | `{"brightness":2,"motion":1,"header":0}` 按字段更新，成功返回当前状态 |
| POST /api/v1/wifi | `{"ssid":"你的网络","password":"你的密码"}` 试连 2.4 GHz Wi-Fi。SSID 1–32 字节，密码 8–63 字节，开放网络显式传空字符串。`{"reconnect":true}` 立即重试已配置的网络。HTTP 202 表示已接受 |
| DELETE /api/v1/session | 注销请求携带的会话，返回 `{"ok":true}` |

除 info 和创建会话之外，各端点要求 `Authorization: Bearer <token>`。POST 使用 UTF-8、`Content-Type: application/json`，请求体上限 640 字节。造型编号 0–3 分别为灵动双眼、像素猫、月兔、糯米史莱姆。编号 4 使用已安装的自定义 PET1 动画，是否可用见 `pet.custom`。自定义资源可用已有的 [USB 桌宠上传工具](pet-mode.md)安装。

动作名称为 `idle`、`blink`、`happy`、`eat`、`sleep`、`surprised`、`sad`、`dance`。短动作自然结束会返回 idle，设备上的按键也可能改变状态。`pet.sequence` 随动作改变而递增。亮度取 0–3，motion 取 0–2（关闭、简化、完整），header 取 0–3（时间、日期、桌宠、标题）。显示设置和选中的造型经原有存储任务在约 750 ms 静默后保存。

例如，客户端可以在一次配对内连续控制设备。下面代码在工程根目录运行。

~~~python
from getpass import getpass
from tools.gamebox_client import GameBox

box = GameBox("192.168.4.1")
box.pair(getpass("Network 页面上的配对码："))
try:
    state = box.request("pet", "POST", {"style": 1, "action": "dance"})
    print(state["pet"])
    frame = box.request("pet/frame")
finally:
    box.logout()
~~~

画面为 128×64 单色，每帧 1024 字节。坐标 (x,y) 对应字节 `(y // 8) * 128 + x` 的位 `1 << (y % 8)`。网页在可见且桌宠打开时读取画面，后台标签页暂停轮询。

**配网结果**可从 `wifi.provision` 读取。trying 表示试连中，saved 表示新关联已取得 IP 并提交 NVS。失败或约 45 秒超时显示 failed，存储失败显示 storage_error，设备恢复上一组凭据并重连。切换网络时，热点客户端可能需要等待信道切换，原 STA 客户端可能需要用新 IP 重开页面。USB `wifi set` 仍支持在目标网络不在场时预先保存配置。

错误以 `{"error":"原因"}` 返回。400 表示字段或取值无效，401 表示需要配对或配对码失效，409 表示状态冲突（例如宠物未打开、配网已在进行或会话已满），413/415 表示长度或类型不合要求。503 表示 UI 忙。504 或传输中断时先读取状态和动作序号，自动重发动作会再次触发动画。

## 实板回归

在 ESP-IDF 容器的 Python 环境运行。设备停在主页或桌宠页，USB 用于获取配对码和独立核对 UI，操作请求通过真实 Wi-Fi 发送。

~~~sh
python tools/control_smoke.py --port /dev/ttyACM0
python tools/control_smoke.py --port /dev/ttyACM0 --exercise-recovery
~~~

第二条还会重连当前网络、试连临时的无效 SSID、等待恢复、重启设备并验证撤销会话。两条均会还原原显示设置和桌宠造型。在装有 Chromium、Playwright 和 pyserial 的电脑上，可运行以下浏览器测试。

~~~sh
python3 tools/control_browser_smoke.py --port /dev/ttyACM0 --chromium /opt/google/chrome/chrome
~~~
