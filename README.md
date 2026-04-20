# JCode Buddy — 程序员桌面编码伴侣

JCode Buddy 是一款为程序员设计的桌面智能伴侣设备。3.49 英寸长条形屏幕横向放置在显示器下方，通过 BLE 与电脑连接，实时显示编码状态和消息提醒。屏幕上的像素猫咪会根据你的工作状态做出不同反应——写代码时它在行走，遇到 bug 时它帮你战斗，任务完成时它和你一起庆祝。

---

## 硬件

- **开发板：** Waveshare ESP32-S3-Touch-LCD-3.49
- **屏幕：** 3.49 英寸 QSPI LCD（172 × 640），横向放置
- **连接：** BLE 5.0（NUS 协议）+ Wi-Fi 2.4GHz
- **音频：** ES8311 DAC + 扬声器（BLE 连接/任务完成时播放提示音）
- **传感器：** QMI8658 六轴 IMU、PCF85063 RTC
- **供电：** USB-C / 锂电池

---

## 功能

### 时钟页（默认）

开机后显示时钟界面，包含：
- 大号数字时钟 + 弧形秒针进度环
- 中文日期和星期
- 天气信息（连接 Wi-Fi 后自动获取）

### 蓝牙页（BOOT 键切换）

通过 BLE NUS 与电脑端 CLI 工具（`go-ble/`）配对，设备广播名称 `JCODE-XXXX`。

**像素猫咪动画：**

| 状态 | 命令 | 动画 |
|------|------|------|
| 睡觉 | `sleep` | 闭眼 + Zzz |
| 空闲 | `idle` | 尾巴摆动 + 眨眼 |
| 工作中 | `working` | 行走动画（腿部循环） |
| 注意 | `attention` | 攻击动画（冲刺 + 怒目 + 爪子） |
| 完成 | `complete` | 弹跳 + 笑脸 + 爱心 + 播放提示音 |

**消息列表：** 猫咪右侧显示最近 5 条消息，支持 `{"cmd":"heart","val":"消息内容"}` 格式。

### Wi-Fi 配网

首次启动或 WiFi 连接失败时，自动开启 AP 热点 `LiuGuang-Setup`，手机连接后打开任意网页即可跳转配网页面，选择 Wi-Fi 并输入密码。凭据保存在 NVS 中，下次开机自动连接。

也可通过 BLE 发送 `{"cmd":"wifi","ssid":"xxx","pass":"xxx"}` 配网。

### 音频提示

- BLE 设备连接时：播放连接提示音
- 收到 `complete` 命令时：播放完成提示音

---

## BLE 通信协议

设备使用 Nordic UART Service（NUS），接收 NDJSON 格式指令：

```json
{"cmd":"working","val":"正在编译..."}
{"cmd":"heart","val":"Hello from VS Code"}
{"cmd":"complete","val":"构建成功！"}
{"cmd":"attention","val":"编译错误！"}
{"cmd":"wifi","ssid":"MyWiFi","pass":"12345678"}
```

配套 CLI 工具位于 `go-ble/` 目录。

---

## 项目结构

```
jcode-buddy/
├── main/
│   ├── main.c              # 入口：初始化、任务调度
│   ├── app_ble.c           # BLE NUS 通信 + 音频触发
│   ├── ui_main.c           # 页面布局、页面切换
│   ├── ui_clock.c          # 时钟面板
│   ├── ui_ble.c            # 蓝牙页：像素猫咪 + 消息列表
│   ├── ui_styles.c         # 深色主题样式
│   ├── wifi_prov.c         # AP 配网（HTTP + DNS 劫持）
│   └── asset/              # PCM 音频资源
├── components/
│   ├── audio_bsp/          # 音频驱动（ES8311 + TCA9554 PA）
│   ├── codec_board/        # 编解码器板级支持
│   ├── board_config/       # 开发板引脚配置
│   ├── i2c_bsp/            # I2C 总线
│   ├── i2c_equipment/      # RTC、IMU 驱动
│   ├── lcd_bl_pwm_bsp/     # 背光 PWM 控制
│   └── wifi_bsp/           # Wi-Fi STA/AP 管理
├── go-ble/                 # Go BLE CLI 客户端
├── sdkconfig.defaults      # ESP-IDF 构建默认配置
├── partitions.csv          # 分区表
└── AGENTS.md               # 开发环境参考
```

---

## 开发

```bash
# 编译并烧录
idf.py build && idf.py -p COM7 flash

# 串口监视器
idf.py -p COM7 monitor

# 全量重新编译（修改 sdkconfig.defaults 后）
idf.py fullclean && idf.py build
```

需要 ESP-IDF v6.0，目标芯片 ESP32-S3。
