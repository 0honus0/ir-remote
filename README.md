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
- `generated/irext/`：由 SQLite 和 bin 包生成的 flash 驻留目录。

## 构建与烧录

在仓库根目录执行：

```bash
esphome compile configs/ir_8266.yaml
esphome run configs/ir_8266.yaml
```

`configs/ir_iretx.yaml` 为 4 MB ESP-12E 使用约 1.96 MiB OTA 槽的覆盖配置。
首次使用自定义分区时应通过串口烧录：

```bash
esphome run configs/ir_iretx.yaml
```

## 重新生成目录

数据库以 ZIP 形式保存在 `data/irext/db/`。先解压其中的 `.db` 文件，再执行：

```bash
python tools/export_irext_catalog.py <解压后的数据库.db> \
  --binary-dir data/irext/bin \
  --output-dir generated/irext \
  --emit-header \
  --allow-missing
```

当前数据包有 9 条索引缺少对应 bin，`--allow-missing` 会明确跳过这些记录。
生成器将载荷、索引和目录字符串全部放入 flash，避免占用 ESP8266 DRAM。

## 已验证

- ESPHome 2026.6.5 配置校验通过。
- `configs/ir_iretx.yaml` 完整编译并生成 `firmware.bin`。
- 构建占用：RAM 44,084 / 81,920 字节（53.8%），Flash 1,286,441 / 2,060,272 字节（62.4%）。
- 实机发射波形和具体空调型号兼容性仍需硬件验收。
