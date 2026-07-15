# ESP8266 AI Screen：把一块 240×240 小屏做成桌面信息屏

项目地址：[Hugh0306/esp8266-ai-screen](https://github.com/Hugh0306/esp8266-ai-screen)

这是一套为 ESP8266EX、ESP-12S 和 ST7789 240×240 屏幕维护的桌面信息屏项目，包含设备固件、macOS 菜单栏 Bridge 和设备 Web 控制台。目前版本是 `0.5.10`，可以显示天气时钟、QQQ 与数字货币行情、Claude Code 和 Codex CLI 状态、额度、网速和自定义桌宠。

## 天气时钟

天气页接入和风天气，显示实时温度、湿度、今日高低温、AQI、农历和 NTP 时间。三套主题都有逐秒动画，数字主题保留数字秒和分钟进度，模拟主题保留时针、分针与秒针。

![晨光数字天气时钟](https://raw.githubusercontent.com/Hugh0306/esp8266-ai-screen/main/docs/images/weather-classic.png)

[查看晨光数字原图](https://raw.githubusercontent.com/Hugh0306/esp8266-ai-screen/main/docs/images/weather-classic.png) · [查看港湾表盘原图](https://raw.githubusercontent.com/Hugh0306/esp8266-ai-screen/main/docs/images/weather-minimal.png) · [查看气象仪表原图](https://raw.githubusercontent.com/Hugh0306/esp8266-ai-screen/main/docs/images/weather-dashboard.png)

天气图标来自 QWeather Icons，固件和 Mac 镜像使用同一套 48×48 单色位图。天气位置在项目配置中设置，API Host 和 API Key 由使用者自行填写，仓库不包含个人凭据或真实设备地址。

## QQQ 与数字货币行情

行情页默认显示 `QQQ`、`BTCUSDT`、`ETHUSDT` 和 `ETHBTC`，也可以在 Web 控制台里调整顺序。涨幅用绿色，跌幅用红色，平盘用灰色。

![QQQ 与数字货币行情](https://raw.githubusercontent.com/Hugh0306/esp8266-ai-screen/main/docs/images/market-preview.png)

[查看行情页原图](https://raw.githubusercontent.com/Hugh0306/esp8266-ai-screen/main/docs/images/market-preview.png)

上图使用演示数据，只展示排版和颜色。Bridge 在线时，数字货币读取 OKX 公开现货行情，QQQ 使用腾讯公开行情；Bridge 离线后，设备会改用 Binance Vision 和腾讯接口继续更新。

## 电脑端软件关闭后，屏幕还能继续更新

Bridge 主要负责读取本机 Claude Code、Codex CLI、额度和网速。天气、行情和 NTP 另外做了设备直连链路，电脑端软件退出约 15 秒后，ESP8266 会自行联网更新。公开接口暂时失败时，屏幕保留最后一份有效数据，不会把整页清空。

设备的 LittleFS 会保存行情列表、最后有效数据、亮度、时钟主题和定时熄屏设置。可以设置每天的熄屏时段，夜里自动关屏，也可以从 Web 控制台临时点亮。

## 目前包含的功能

- Claude Code 与 Codex CLI 工作状态、额度窗口和审批提醒。
- 三套天气时钟主题、17 类天气图标、农历和 NTP 自动对时。
- QQQ、BTCUSDT、ETHUSDT、ETHBTC 行情，支持增删和排序。
- 约 56 秒的上传、下载网速曲线。
- Web 控制台切换页面、调整亮度、主题、夜间模式和定时熄屏。
- Claude 与 Codex 独立 GIF 桌宠，支持上传、预览和恢复默认。
- macOS 菜单栏镜像、设备发现、模式切换和 Bridge 服务。

## 硬件与构建

当前 PlatformIO 配置面向 ESP8266EX 或 ESP-12S、4 MB Flash、1.54 英寸 ST7789 240×240 SPI 屏幕和 CH340 系列 USB 转串口。不同成品屏可能使用不同引脚或分区，刷写前需要核对芯片、Flash 容量、屏幕接线和镜像大小。

构建和配置方法见 [README](https://github.com/Hugh0306/esp8266-ai-screen#readme)，刷写前请先阅读 [安全刷写指南](https://github.com/Hugh0306/esp8266-ai-screen/blob/main/docs/FLASHING.md)。项目不会执行整片擦除，也不建议把设备备份、LittleFS 镜像、Wi-Fi 密码或天气凭据提交到 Git。

这个仓库保留了对 [pengchujin/esp8266-ai](https://github.com/pengchujin/esp8266-ai) 的 fork 关系。上游没有仓库级 `LICENSE`，许可边界和第三方图标署名见 [NOTICE](https://github.com/Hugh0306/esp8266-ai-screen/blob/main/NOTICE.md) 与 [THIRD_PARTY_NOTICES](https://github.com/Hugh0306/esp8266-ai-screen/blob/main/docs/THIRD_PARTY_NOTICES.md)。
