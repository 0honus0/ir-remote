# ir-remote

基于 ESPHome、ESP8266 和本地 IRext 目录的空调红外遥控器。固件不依赖
IRremoteESP8266，通过设备类型、品牌、型号三级目录选择协议，并向 Home
Assistant 暴露动态 Climate 能力。

## 架构

- `components/irext_adapter/`：IRext 目录读取、协议解码和原始时序发射。
- `components/shared/`：存储无关的空调状态与目录索引持久化。
- `components/universal_ac_controller/`：三级选择联动、状态同步和发送控制。
- `components/universal_ac_climate/`：Home Assistant Climate 实体。
- `vendor/irext_core/`：IRext 1.5.3 C 核心库。
- `generated/irext/`：由 SQLite 和 bin 包生成的目录索引与 raw flash 载荷。

IRext 索引、品牌、型号和偏移表编进固件；实际 IR bin 载荷保存在
`generated/irext/catalog.bin`，串口烧录时写入 ESP8266 flash 的 `0x300000`
地址。这样主固件保持在 ESP8266 约 1 MiB 的可执行映射窗口内，同时仍保留完整
IRext 数据包。

## 构建与烧录

在仓库根目录执行：

```bash
esphome compile configs/ir.yaml
esphome run configs/ir.yaml
```

一键生成目录、编译、合成整片镜像并串口烧录：

```powershell
.\tools\build_flash.ps1 -Port COM3
```

Bash 环境：

```bash
tools/build_flash.sh --port /dev/ttyUSB0
```

只构建和校验、不烧录：

```powershell
.\tools\build_flash.ps1 -NoFlash
```

```bash
tools/build_flash.sh --no-flash
```

`configs/ir.yaml` 使用 ESP8266 默认 4 MB 分区，不再依赖自定义 ld 文件。
构建完成后会生成：

- `firmware.bin`：常规 ESPHome 固件，约 1 MiB 以内。
- `firmware.with_irext.bin`：已在 `0x300000` 合并 `catalog.bin` 的整片镜像。

推荐首次串口烧录：

```bash
esphome run configs/ir.yaml
```

如果串口上传工具未写入额外 flash 镜像，可手动分段烧录：

```bash
esptool --chip esp8266 --port COMx --baud 460800 write-flash -z \
  --flash-mode dout --flash-freq 40m --flash-size 4MB \
  0x0 configs/.esphome/build/ir-remote/.pioenvs/ir-remote/firmware.bin \
  0x300000 generated/irext/catalog.bin
```

OTA 只更新主固件，不会更新 `catalog.bin`。IRext 数据变化后需要串口或
`esptool` 重新写入 `0x300000`。

## 重新生成目录

数据库以 ZIP 形式保存在 `data/irext/db/`。先解压其中的 `.db` 文件，再执行：

```bash
python tools/export_irext_catalog.py <解压后的数据库.db> \
  --binary-dir data/irext/bin \
  --output-dir generated/irext \
  --emit-header \
  --allow-missing \
  --raw-flash-offset 0x300000
```

当前数据包有 9 条索引缺少对应 bin，`--allow-missing` 会明确跳过这些记录。
生成器会输出 `catalog_index.h` 和 `catalog.bin`。
`catalog_index.h` 编进固件，`catalog.bin` 作为 raw flash 数据读取。

## 已验证

- ESPHome 2026.6.5 配置校验通过。
- `configs/ir.yaml` 完整编译并生成 `firmware.bin` 和 `firmware.with_irext.bin`。
- 构建占用：RAM 39,480 / 81,920 字节（48.2%），Flash 691,249 / 1,044,464 字节（66.2%）。
- `catalog.bin` 大小 553,869 字节，写入 raw flash 地址 `0x300000`。
- 实机发射波形和具体空调型号兼容性仍需硬件验收。
