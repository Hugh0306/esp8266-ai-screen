# 安全刷写指南

这份流程只适用于仓库已配置的 ESP8266EX 或 ESP-12S、4 MB Flash 成品板。不同模组、Flash 容量、屏幕接线或启动电路不能直接套用。

> [!CAUTION]
> 刷写前必须核对 `chip_id` 和 `flash_id`，并读取完整备份。本项目只把应用镜像写到 `0x000000`，不执行 `erase_flash`，不写入 `0x300000` 起的 LittleFS 区域。

## 准备环境

使用稳定的数据线与 USB 端口，避免在写入过程中经过容易断连的扩展坞。关闭可能占用串口的监视器和 Bridge。

```bash
cd firmware
python3 -m venv .pio-venv
source .pio-venv/bin/activate
python3 -m pip install platformio "esptool>=4,<5"
```

先编译，不要直接调用通用上传目标：

```bash
pio run -e nodemcuv2
```

后续所有命令中的 `<SERIAL_PORT>` 都要替换为本次插入设备后确认的串口。不要沿用上一次连接留下的端口名。

## 核对芯片与 Flash

```bash
python3 -m esptool --chip esp8266 --port "<SERIAL_PORT>" chip_id
python3 -m esptool --chip esp8266 --port "<SERIAL_PORT>" flash_id
```

继续前确认：

- `chip_id` 能稳定读取，并且两次读取结果一致。
- 当前串口对应目标设备，没有连到其他开发板。
- `flash_id` 检测到的容量是 4 MB。
- 芯片与容量和之前的设备记录一致。

任一项不一致就停止。不要靠试刷来判断板型。

## 读取完整备份

`<BACKUP_FILE>` 应替换为仓库外的私有备份文件。整片备份可能含 Wi-Fi 和天气服务配置，不要上传、分享或提交到 Git。

```bash
python3 -m esptool \
  --chip esp8266 \
  --port "<SERIAL_PORT>" \
  --baud 115200 \
  read_flash 0x000000 0x400000 "<BACKUP_FILE>"
```

检查备份长度和哈希：

```bash
python3 -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]); n=p.stat().st_size; print(n); assert n == 0x400000' "<BACKUP_FILE>"
shasum -a 256 "<BACKUP_FILE>"
```

预期长度为 `4194304` 字节。命令失败、长度不符或读取中断时，不要继续写入。

完整备份用于故障分析和受控恢复。不要把整片备份直接写回，也不要用它覆盖 LittleFS。需要回滚时，优先使用之前保存的应用镜像，并仍然只写 `0x000000`。

## 检查待写镜像

```bash
python3 -c 'import pathlib; p=pathlib.Path(".pio/build/nodemcuv2/firmware.bin"); n=p.stat().st_size; print(n); assert 0 < n <= 0xFEFF0'
shasum -a 256 .pio/build/nodemcuv2/firmware.bin
```

镜像必须存在，且不超过当前 PlatformIO 环境报告的 `1,044,464` 字节应用分区上限。`0x300000` 是 LittleFS 的物理起点，不是应用镜像可使用的容量上限。如果尺寸检查失败，先检查链接脚本和分区配置，不要调整命令绕过检查。

## 只写应用镜像

确认电脑供电稳定后执行：

```bash
python3 -m esptool \
  --chip esp8266 \
  --port "<SERIAL_PORT>" \
  --baud 115200 \
  write_flash \
  --flash_size 4MB \
  0x000000 .pio/build/nodemcuv2/firmware.bin
```

这条命令只有一个地址和一个镜像。不要追加 LittleFS 镜像，不要使用 `erase_flash`、`erase_region` 或 `pio run -t uploadfs`。

写入完成后校验应用区域：

```bash
python3 -m esptool \
  --chip esp8266 \
  --port "<SERIAL_PORT>" \
  --baud 115200 \
  verify_flash 0x000000 .pio/build/nodemcuv2/firmware.bin
```

## 启动检查

让设备正常复位，再以 115200 波特率读取日志：

```bash
pio device monitor -p "<SERIAL_PORT>" -b 115200
```

确认设备可以启动、屏幕点亮，并且 LittleFS 中原有设置仍在。首次启动先观察，不要连续重刷。出现以下情况时应断开写入流程并检查原因：

- `chip_id` 或 `flash_id` 读取不稳定。
- 写入或校验阶段串口断开。
- 镜像尺寸超过 `0xFEFF0`，或 PlatformIO 报告程序区溢出。
- LittleFS 挂载失败或设备配置消失。
- 屏幕接线、驱动型号或 Flash 容量与本仓库配置不同。

不要为了排障执行整片擦除。先保留串口日志、镜像哈希和私有备份，再核对硬件与构建配置。
