# 开发指南

这份文档面向准备修改固件、Bridge 或设备协议的开发者。项目使用 PlatformIO 构建 ESP8266 固件，macOS Bridge 使用 Swift Package Manager，Windows 目录保留了一份继承的预览实现。

刷写真机前请单独阅读 [FLASHING.md](FLASHING.md)。构建成功不代表镜像适用于任意 ESP8266 开发板。

## 目录

```text
firmware/     ESP8266 固件，包含显示、Web 控制台、设备直连和 LittleFS 数据管理
mac-app/      macOS 菜单栏 Bridge，负责本机状态、行情、天气、镜像和设备管理
windows-app/  继承的 Windows 托盘预览版，功能状态见该目录的 README
tools/        精灵图、天气图标转换和真机回归脚本
docs/         开发、刷写和第三方组件说明
```

固件与 Bridge 通过 HTTP 或 USB 串口交换状态。Bridge 可提供本机 Claude、Codex、行情和天气数据；Bridge 不在线时，固件会按本地设置使用设备直连能力。设备 Web 控制台负责显示模式、主题、亮度、定时熄屏和网络相关设置。

## 固件开发

### 硬件配置

仓库中的 `firmware/platformio.ini` 面向以下硬件：

- ESP8266EX 或 ESP-12S 模组
- 4 MB Flash
- 240 x 240 ST7789 SPI 屏幕
- CH340 系列 USB 转串口

默认屏幕引脚如下。其他板型需要先核对原理图，再修改 `build_flags`。

| 信号 | GPIO | 说明 |
|---|---:|---|
| SCLK | 14 | 硬件 SPI 时钟 |
| MOSI | 13 | 硬件 SPI 数据 |
| CS | 15 | 屏幕片选 |
| DC | 0 | 数据与命令选择 |
| RESET | 2 | 屏幕复位 |
| Backlight | 5 | 低电平点亮，支持 PWM 调光 |

屏幕驱动使用 `ST7789_2_DRIVER`。即使引脚相同，改成普通 `ST7789_DRIVER` 也可能无法点亮这类成品板。

### 构建

需要 Python 3 和 PlatformIO。平台、框架与固件库版本已经写入 `platformio.ini`。

```bash
cd firmware
python3 -m venv .pio-venv
source .pio-venv/bin/activate
python3 -m pip install platformio
pio run -e nodemcuv2
```

应用镜像生成在：

```text
firmware/.pio/build/nodemcuv2/firmware.bin
```

不要把 `.pio/`、固件镜像、Flash 备份或 LittleFS 备份提交到仓库。

### 天气位置示例

公开源码默认使用城市级示例位置。固件侧在 `firmware/include/config.h` 修改 `WEATHER_LOCATION_NAME`、`WEATHER_LOCATION_LABEL`、`QWEATHER_LONGITUDE` 和 `QWEATHER_LATITUDE`；macOS 镜像与 Bridge 侧修改 `mac-app/Sources/AIClockBridge/WeatherMonitor.swift` 中对应的四个常量。两端应保持一致，且只保存城市级坐标。

## macOS Bridge 开发

`mac-app/Package.swift` 要求 Swift 5.9，最低运行系统为 macOS 12。项目只使用系统框架。

```bash
cd mac-app
swift build
swift test
swift run AIClockBridge --self-test-bridge
```

前台运行菜单栏应用：

```bash
swift run AIClockBridge
```

本地状态接口可用于确认 Bridge 已启动：

```bash
curl -s http://localhost:8765/status | python3 -m json.tool
```

打包发布构建：

```bash
./package-app.sh release
```

应用包会生成在 `mac-app/.build/AIClockBridge.app`。修改 Swift 源码或资源后，应重新执行打包脚本，避免误用旧二进制。

### 天气主题回归图

下面的命令会离屏渲染三套 240 x 240 天气主题及边界样例，适合检查字体、图标和布局是否溢出：

```bash
swift run AIClockBridge --render-weather-themes <OUTPUT_DIR>
```

生成结果只用于本地检查。提交图片前应确认其中没有账号、网络或设备信息。

行情分享图使用内置演示数据，不请求网络或连接设备：

```bash
swift run AIClockBridge --render-stock-preview <OUTPUT_FILE>
```

## Windows 预览版

Windows 目录可以用 .NET 8 SDK 构建：

```powershell
cd windows-app\AIClockBridge
dotnet restore
dotnet build -c Release
```

该目录目前是继承的旧版预览，不能按 macOS 0.5.10 的功能状态理解。缺失范围与运行边界见 [windows-app/README.md](../windows-app/README.md)。

## 接口约定

Bridge 默认监听 `8765` 端口。常用读取接口如下：

| 方法 | 路径 | 内容 |
|---|---|---|
| GET | `/status` | Claude 与 Codex 状态及额度窗口 |
| GET | `/net` | 网速采样 |
| GET | `/stock` | 行情数据 |
| GET | `/weather` | 天气数据 |
| GET | `/weather/text.raw` | 设备使用的农历 RGB565 条带 |
| POST | `/event` | 本机 Hook 状态事件 |

设备端常用接口如下：

| 方法 | 路径 | 内容 |
|---|---|---|
| GET | `/api/info` | 设备、显示、网络和数据源状态 |
| GET/POST | `/api/settings` | 时钟、亮度、夜间模式、NTP 与定时熄屏 |
| GET/POST | `/api/stocks` | 设备行情列表与同步状态 |
| POST | `/api/display` | 切换显示模式 |
| POST | `/api/screen` | 临时点亮、熄屏或恢复计划 |
| POST | `/api/time/sync` | 重新发起 NTP 对时 |
| POST | `/api/bridge` | 设置 Bridge 地址 |
| POST | `/sprite/claude`、`/sprite/codex` | 上传 GIF 并由设备解码 |
| POST | `/sprite/claude/reset`、`/sprite/codex/reset` | 按 `sprite_rev` 恢复内置动画 |

新增或修改字段时，需要同时检查固件解析、macOS Bridge、Web 控制台和对应测试。设备端资源有限，外部 HTTP 响应必须限制长度，显示循环中不要加入长时间阻塞操作。

## 真机回归

`tools/smoke_gif_upload.py` 会上传并重置 Codex 动画，也会推进 `sprite_rev`。只有在已确认设备当前没有需要保留的 Codex 自定义动画时才运行：

```bash
python3 tools/smoke_gif_upload.py \
  --host <DEVICE_HOST> \
  --confirm-device-mutation
```

这是有状态的真机测试，不属于普通构建步骤。网络中断或并发修改时，脚本会拒绝覆盖版本已经变化的动画。

## 凭据与网络边界

- 不要把 Wi-Fi 密码、天气服务凭据、OAuth 数据、设备标识、局域网地址或 Flash 备份写入源码、测试夹具和文档。
- 天气服务凭据通过应用或设备 Web 控制台配置，不提供仓库内默认值。
- Bridge 与设备 Web 服务没有面向公网的身份认证，只应运行在可信局域网或 USB 环境中。
- LittleFS 可能保存网络和服务配置，任何 LittleFS 或整片 Flash 备份都按敏感文件处理。
- 测试网络地址使用文档保留地址，测试身份使用明显的虚构值。

## 提交前检查

```bash
(cd firmware && pio run -e nodemcuv2)
(cd mac-app && swift test && swift run AIClockBridge --self-test-bridge)
```

涉及屏幕布局时，再生成天气主题回归图并逐张检查。涉及设备写入时，按 [FLASHING.md](FLASHING.md) 的核验和备份流程执行。

第三方图标、代码和修改说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。项目来源与许可证边界见 [NOTICE.md](../NOTICE.md)。
