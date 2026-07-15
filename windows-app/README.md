# AIClockBridge for Windows

`windows-app/` 是从早期 Bridge 继承下来的 Windows 托盘预览版，保留给需要继续移植和测试的开发者。它不是与 macOS 0.5.10 等价的发行版本。

## 当前状态

现有代码包含以下旧版能力：

- 系统托盘图标和基础设备控制
- Claude、Codex 状态与额度读取
- 本地 `/status`、`/net`、`/stock` 和旧 `/music` 数据接口
- 基础屏幕镜像、网速采样和系统 Now Playing
- petdex 选择器、GIF 上传和带 `sprite_rev` 的恢复默认操作
- 按设备地址发现和连接的旧流程

以下能力尚未与 macOS 0.5.10 同步完成：

- Bridge 关闭后的完整设备独立联网链路
- 当前天气数据、农历和三套天气时钟主题
- 基于 `device_id` 的身份核验、配对和地址变化恢复
- 最新设备设置接口与安全约束的端到端回归

Windows 预览版仍保留旧音乐页和按地址识别设备的逻辑。连接当前固件前，应先检查接口兼容性，不要把它用于固件刷写或无人值守部署。

## 环境

- Windows 10 19041 或更高版本
- [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)

项目使用 WinForms 和 WinRT 媒体接口。第三方依赖 [ImageSharp](https://github.com/SixLabors/ImageSharp) 用于 WebP 解码和 GIF 生成。

## 构建与运行

```powershell
cd windows-app\AIClockBridge
dotnet restore
dotnet build -c Release
dotnet run
```

发布依赖本机 .NET 运行时的单文件构建：

```powershell
dotnet publish -c Release -r win-x64 --self-contained false
```

应用启动后会监听本机 `8765` 端口。Windows 防火墙可能要求确认局域网访问权限，只应在可信网络中允许。

检查本地状态接口：

```powershell
curl.exe -s http://localhost:8765/status
```

设置文件保存在当前用户的应用数据目录中。设备地址和本地状态可能属于私人信息，不要把设置文件、日志或凭据提交到仓库。

## 继续移植时的检查顺序

1. 先让 `DeviceClient` 使用 `device_id` 核验设备身份，避免只按地址配对。
2. 同步当前固件的显示模式、设置和天气接口。
3. 补齐独立联网状态、天气渲染和离线数据的镜像逻辑。
4. 为设备发现、破坏性操作保护和协议兼容添加测试。
5. 完成真机回归后，再把 Windows 版本标记为可发布。

Windows 目录不提供刷写功能。固件构建和安全刷写流程见 [开发指南](../docs/DEVELOPMENT.md) 与 [安全刷写指南](../docs/FLASHING.md)。
