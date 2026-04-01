# 气体监测报警小车简易实现

这套原型是根据项目书里提到的核心功能压缩出来的教学/答辩演示版：`甲烷/天然气检测 + CO 检测 + 温湿度补偿 + 超声波避障 + 声光报警 + 数据可视化 + 遥控`。

本目录给了两套可直接落地的版本：

- `cloud_wifi_car/`：小车接入 Wi-Fi，把数据发到服务器，浏览器可远程查看和控制。
- `local_ap_car/`：小车自己开热点，手机/电脑在附近直接连接热点控制，不依赖服务器。
- `cloud_wifi_server/`：云端版配套的 Python 服务器和网页控制台。
- `docs/`：中文 BOM、接线和实现说明。

## 重要说明

- 这是原型/答辩演示方案，不是防爆认证设备，不能直接拿去真正的燃气井下、密闭爆炸性环境、危险化学品仓储现场使用。
- 真正工程化落地还需要做：防爆外壳、本安电源设计、传感器标定、EMC、IP 防护、跌落与震动测试、远程失联保护、冗余急停。
- 项目书中点名了 `Figaro TGS2611`。为了把原型更快做出来，这里推荐用更容易接线和编程的 `Winsen ZC13` 甲烷 UART 模块；答辩时可以写成“`TGS2611/同类 CH4 模块`”，并说明原型阶段选择 UART 成品模块以降低调试难度。

## 推荐开发环境

- Arduino IDE 2.x
- ESP32 Boards 3.x
- Python 3.10+

## Arduino 端需要安装的库

- `ArduinoJson`
- `Adafruit SHT31 Library`

## 快速上手

1. 先看 `docs/hardware_plan_cn.md`。
2. 接线总图看 `docs/esp32_total_wiring_diagram.md`。
3. 答辩单页内容看 `docs/ppt_one_page_cn.md`。
4. 按接线把小车搭起来，注意传感器预热时间。
5. 云端版：
   - 修改 `cloud_wifi_car/cloud_wifi_car.ino` 里的 Wi-Fi 和服务器地址。
   - 在电脑上运行 `cloud_wifi_server/server.py`。
   - 烧录云端版固件。
6. 近程版：
   - 直接烧录 `local_ap_car/local_ap_car.ino`。
   - 手机/电脑连接热点 `GasRobot-01`，访问 `http://192.168.4.1`。

## 文件结构

```text
gas_alarm_robot_demo/
├─ README.md
├─ docs/
│  └─ hardware_plan_cn.md
│  └─ esp32_total_wiring_diagram.md
│  └─ ppt_one_page_cn.md
├─ cloud_wifi_car/
│  └─ cloud_wifi_car.ino
├─ cloud_wifi_server/
│  ├─ requirements.txt
│  └─ server.py
└─ local_ap_car/
   └─ local_ap_car.ino
```
