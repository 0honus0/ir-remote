# iretx-version 分支重构规划

## 完成状态

- [x] 数据结构与 EEPROM 持久化迁移到 IRext 字段和稳定目录索引。
- [x] 接入 IRext 1.5.3、flash 目录、协议解码和 ESPHome 原始时序发射。
- [x] 实现设备类型 → 品牌 → 型号三级联动选择。
- [x] 重写 Controller 与 Climate，并按协议能力动态暴露模式、风速和摆风。
- [x] 删除 IRremoteESP8266 生产依赖、旧包装器和不受支持实体。
- [x] 通过 ESPHome 配置校验和完整固件编译，成功生成 `firmware.bin`。
- [ ] 在实体硬件上验证 38 kHz 波形、发射距离和具体空调型号兼容性。

最近验证环境：ESPHome 2026.6.5、ESP8266 ESP-12E、IRext 1.5.3。构建占用
RAM 44,084 / 81,920 字节（53.8%），Flash 1,286,441 / 2,060,272 字节
（62.4%）。

## 现状分析

### master 分支（参考架构模式）
- 11个文件，使用 IRremoteESP8266 库
- 扁平协议列表 ~120项
- 完整3层架构：`UniversalAc` → `AcStateManager` → `Controller` → `Climate`
- 状态持久化 `AcPersistentState` + `EspHomeAcStateStore`

### iretx-version 分支（当前）
- 已接入 IRext vendor 库和完整 flash 驻留目录数据
- 核心组件、Controller、Climate 和持久化均已迁移到 IRext
- 生产固件已移除 IRremoteESP8266 依赖和残余实现

---

## IRext 实际能力确认

> 以下结论来自 `vendor/irext_core/` 源码审查。

### AC 控制能力（实际有效的功能）

| 功能 | IRext 支持 | 枚举/范围 | 说明 |
|------|-----------|-----------|------|
| **开关** | ✅ | `AC_POWER_ON=0, AC_POWER_OFF=1` | 注意：ON=0, OFF=1（反直觉） |
| **模式** | ✅ | `AC_MODE_COOL=0, HEAT=1, AUTO=2, FAN=3, DRY=4` | 5种模式 |
| **温度** | ✅ | `AC_TEMP_16=0 .. AC_TEMP_30=14` | 16~30°C，15档 |
| **风速** | ✅ | `AC_WS_AUTO=0, LOW=1, MEDIUM=2, HIGH=3` | 4档 |
| **摆风** | ✅ | `AC_SWING_ON=0, AC_SWING_OFF=1` | 开/关 |
| **固定风向** | ✅ | `change_wind_direction` 字段 | 在固定位置之间循环 |
| 面板灯 | ❌ | `ac_display` 字段存在但**从未被读取** | 死字段 |
| 睡眠 | ❌ | `ac_sleep` 字段存在但**从未被读取** | 死字段 |
| 定时 | ❌ | `ac_timer` 字段存在但**从未被读取** | 死字段 |
| 强力/静音/ECO/自清洁/健康 | ❌ | 无字段，无 key_code | 不支持 |

### 查询函数（已确认存在且已实现）

| 函数 | 签名 | 返回值 |
|------|------|--------|
| `get_supported_mode` | `(UINT8 *supported_mode)` | 低5位 bitmask：bit0=cool, bit1=heat, bit2=auto, bit3=fan, bit4=dry |
| `get_temperature_range` | `(UINT8 ac_mode, INT8 *temp_min, INT8 *temp_max)` | 每种模式的温度范围（枚举索引 0~14） |
| `get_supported_wind_speed` | `(UINT8 ac_mode, UINT8 *supported_wind_speed)` | 低4位 bitmask：bit0=auto, bit1=low, bit2=medium, bit3=high |
| `get_supported_swing` | `(UINT8 ac_mode, UINT8 *supported_swing)` | `0x03`=swing+fix, `0x02`=仅swing, `0x00`=未指定 |
| `get_supported_wind_direction` | `(UINT8 *supported_wind_direction)` | 固定风向位置数量 |

### IRext 原始结构体（`t_remote_ac_status`）

```c
// vendor/irext_core/include/ir_ac_control.h
typedef struct REMOTE_AC_STATUS {
    t_ac_power       ac_power;              // 实际有效
    t_ac_temperature ac_temp;               // 实际有效
    t_ac_mode        ac_mode;               // 实际有效
    t_ac_swing       ac_wind_dir;           // 实际有效
    t_ac_wind_speed  ac_wind_speed;         // 实际有效
    UINT8            ac_display;            // ⚠️ 死字段
    UINT8            ac_sleep;              // ⚠️ 死字段
    UINT8            ac_timer;              // ⚠️ 死字段
    UINT8            change_wind_direction; // 实际有效（固定风向循环标志）
} t_remote_ac_status;
```

### 核心 API 调用链

```
ir_binary_open(category, sub_category, binary, length)  // 解析 bin 协议文件
    → ir_decode(key_code, user_data, &ac_status)         // 生成 IR timing 数据
    → user_data[] = UINT16 mark/space 微秒时序数组        // 直接发射
    → ir_close()                                          // 释放资源
```

---

## 一、数据存储/加载统一化

**目标**: 参考 master 的 `AcPersistentState` + `AcStateStore` 持久化模式，字段与 `t_remote_ac_status` 对齐，预留兼容字段。

| 阶段 | 任务 | 文件 | 说明 |
|------|------|------|------|
| 1.1 ✅ | 重写 `AcPersistentState` 结构 | `ac_state_manager.h` | 字段与 `t_remote_ac_status` 一一对齐，额外增加目录选择字段（type/brand/code），预留 `ac_display/ac_sleep/ac_timer` 保持结构体兼容 |
| 1.2 ✅ | 重写 `AcStateManager` | `ac_state_manager.h` | setter/getter 只操作实际有效字段（power/mode/temp/wind_speed/wind_dir），预留字段不提供 setter |
| 1.3 ✅ | 保留 `EspHomeAcStateStore` | `universal_ac_controller.h` | EEPROM 持久化机制（dirty-check + debounce flush）沿用 master 的成熟方案，只是存的内容变为 IRext 字段 |

### AcPersistentState 结构（与 `t_remote_ac_status` 对齐）

```cpp
struct AcPersistentState {
    uint32_t magic;            // 0x41435354 "ACST"
    uint16_t version;          // 1
    uint16_t flags;            // bit 0 = power (AC_POWER_ON=0, AC_POWER_OFF=1)
    // ---- IRext 目录选择（持久化） ----
    uint16_t catalog_type;     // 设备类型索引
    uint16_t catalog_brand;    // 品牌索引
    uint16_t catalog_code;     // 遥控器码索引
    // ---- 与 t_remote_ac_status 对齐 ----
    uint8_t  ac_mode;          // t_ac_mode:  COOL=0, HEAT=1, AUTO=2, FAN=3, DRY=4
    uint8_t  ac_temp;          // t_ac_temperature: AC_TEMP_16=0 .. AC_TEMP_30=14
    uint8_t  ac_wind_speed;    // t_ac_wind_speed: AUTO=0, LOW=1, MEDIUM=2, HIGH=3
    uint8_t  ac_wind_dir;      // t_ac_swing: ON=0, OFF=1
    // ---- 预留字段（与 t_remote_ac_status 结构兼容，当前 IRext 未实现） ----
    uint8_t  ac_display;       // 预留：面板灯（IRext 未读取）
    uint8_t  ac_sleep;         // 预留：睡眠模式（IRext 未读取）
    uint8_t  ac_timer;         // 预留：定时器（IRext 未读取）
    uint8_t  reserved;         // 对齐预留
};
```

**设计说明**：
- 有效字段（5个）：`ac_mode`, `ac_temp`, `ac_wind_speed`, `ac_wind_dir`, `flags.bit0`(power)
- 预留字段（3个）：`ac_display`, `ac_sleep`, `ac_timer` — 保持与 `t_remote_ac_status` 结构布局兼容，未来 IRext 库实现后可直接启用
- 目录字段（3个）：`catalog_type`, `catalog_brand`, `catalog_code` — IRext 特有的三级选择持久化

---

## 二、功能模块化（设备→品牌→型号 逐级递进）

**目标**: 基于 IRext catalog 实现3级联动选择，替换现有扁平协议列表。

| 阶段 | 任务 | 文件 | 说明 |
|------|------|------|------|
| 2.1 ✅ | 新建 `IrextCatalogProvider` | 新文件 `components/irext_adapter/catalog_provider.h` | 封装 `catalog_index.h` 的查询：`get_types()` → `get_brands(type)` → `get_codes(brand)`，从 PROGMEM 读取 |
| 2.2 ✅ | 新建 `IrextAc` | 新文件 `components/irext_adapter/irext_ac.h` | 替换 `UniversalAc`，封装 IRext 的 `ir_binary_open()` / `ir_decode()` / `ir_close()`，内部维护 `t_remote_ac_status`，输出原始 timing 数据 |
| 2.3 ✅ | IR 发射封装 | 新文件 `components/irext_adapter/ir_transmitter.h` | 将 `ir_decode()` 返回的 UINT16 timing 数组通过 GPIO 发射（mark/space 微秒时序） |
| 2.4 ✅ | YAML三级Select | `configs/ir_8266.yaml` | 3个联动Select：`ac_device_type`(设备类型)→`ac_brand`(品牌)→`ac_model`(型号) |
| 2.5 ✅ | 删除 IRremoteESP8266 依赖 | `configs/ir_8266.yaml` + `universal_ac.h` | 移除 `platformio_options.lib_deps` 中的 IRremoteESP8266，删除 `universal_ac.h` |

### 用户选择流程

```
设备类型（空调/电视/风扇/...）
  └── 品牌（格力/美的/海尔/大金/...）
        └── 遥控器码（码 1 / 码 2 / 码 3 /...）
```

### IRext 调用流程

```
CatalogProvider.get_binary(type, brand, code)
    → 从 PROGMEM data[] 按 offset+length 读取 bin 数据到 RAM
    → ir_binary_open(REMOTE_CATEGORY_AC, sub_category, binary, length)
    → 填充 t_remote_ac_status {ac_power, ac_mode, ac_temp, ac_wind_speed, ac_wind_dir}
    → ir_decode(KEY_AC_POWER, user_data, &ac_status)
    → user_data[] = UINT16 mark/space 微秒时序（最多 1024 个 UINT16）
    → IrTransmitter.send_raw(user_data, count)  // GPIO 直接发射
    → ir_close()
```

### AC 可用 Key Codes

| Key Code | 常量 | 功能 |
|----------|------|------|
| 0 | `KEY_AC_POWER` | 开/关机 |
| 1 | `KEY_AC_MODE_SWITCH` | 模式切换 |
| 2 | `KEY_AC_TEMP_PLUS` | 温度 + |
| 3 | `KEY_AC_TEMP_MINUS` | 温度 - |
| 9 | `KEY_AC_WIND_SPEED` | 风速切换 |
| 10 | `KEY_AC_WIND_SWING` | 摆风开关 |
| 11 | `KEY_AC_WIND_FIX` | 固定风向 |

---

## 三、对外HA接口/Web接口统一

**目标**: 参考 master 接口命名规范和状态同步模式，实体只暴露 IRext 实际支持的功能。

| 阶段 | 任务 | 文件 | 说明 |
|------|------|------|------|
| 3.1 ✅ | 统一命名约定 | `configs/ir_8266.yaml` | 所有实体名: `"中文名 [english_id]"`，保持 master 规范 |
| 3.2 ✅ | Web分组 | `configs/ir_8266.yaml` | `ac_catalog_group`(设备/品牌/型号, wt:10)、`ac_control_group`(空调控制, wt:20)、`wifi_settings_group`(WiFi, wt:90) |
| 3.3 ✅ | 统一状态回写 | `universal_ac_controller.h` | `sync_controls()` 方法更新所有 HA 实体（Climate + Select + TextSensor），开机时从 EEPROM 恢复并同步 |
| 3.4 ✅ | Climate Traits 动态化 | `universal_ac_climate.h` | 选择遥控器码后调用 `get_supported_mode()` / `get_supported_wind_speed()` / `get_supported_swing()` 查询实际能力，动态设置 Climate traits |
| 3.5 ✅ | `ir_status` 状态文本 | Controller | 发送结果写入 `ir_status` TextSensor，如 `"格力 码1: 制冷 26°C 发送成功"` |

### HA 实体规划

> 注意：IRext AC 只支持 power/mode/temp/fan/swing 五项功能。
> 面板灯/睡眠/定时器保留实体但标注预留状态。

| 类型 | ID | 名称 | Web分组 | 持久化 | 状态 |
|------|----|------|---------|--------|------|
| Climate | `ha_universal_ac` | `空调遥控 [ac_climate]` | — | ✅ | 有效 |
| Select | `ac_device_type` | `设备类型 [ac_device_type]` | `ac_catalog_group` | ✅ | 有效 |
| Select | `ac_brand` | `品牌 [ac_brand]` | `ac_catalog_group` | ✅ | 有效 |
| Select | `ac_model` | `遥控器码 [ac_model]` | `ac_catalog_group` | ✅ | 有效 |
| Select | `ac_fan` | `风速设置 [ac_fan]` | `ac_control_group` | ✅ | 有效 |
| Select | `ac_swing_v` | `送风方向 [ac_swing]` | `ac_control_group` | ❌ | 有效 |
| Switch | `ac_power_toggle` | `空调开关 [ac_power]` | `ac_control_group` | ✅ | 有效 |
| TextSensor | `ir_status` | `红外状态 [ir_status]` | — | ❌ | 有效 |
| TextSensor | `current_wifi_ssid` | `当前 WiFi 名称 [wifi_current]` | — | ❌ | 有效 |
| TextSensor | `firmware_version` | `固件版本 [firmware_version]` | — | ❌ | 有效 |

**移除的实体**（IRext 不支持）：
- ~~`ac_special_mode`~~ — IRext 无 turbo/sleep/dry 等特殊模式 key_code
- ~~`ac_display_light`~~ — `ac_display` 为死字段
- ~~`ac_sleep`(定时器)~~ — `ac_timer` 为死字段（可在 ESPHome 层用软件定时器实现关机，不依赖红外指令）

---

## 四、目录标准化

**目标**: 清晰的项目结构，每个目录职责分明。

```
ir-remote/
├── configs/                          # ESPHome YAML配置
│   ├── ir_8266.yaml
│   └── ir_iretx.yaml
├── components/
│   ├── shared/                       # 共享头文件
│   │   └── ac_state_manager.h        #   持久化状态结构 + 校验
│   ├── irext_adapter/                # IRext 集成
│   │   ├── __init__.py               #   ESPHome 组件注册
│   │   ├── catalog_provider.h        #   目录查询 (type→brand→code)
│   │   ├── irext_ac.h                #   AC 控制核心 (替代 UniversalAc)
│   │   └── ir_transmitter.h          #   IR timing 发射
│   ├── universal_ac_climate/         # HA Climate 实体
│   │   ├── __init__.py
│   │   ├── climate.py
│   │   └── universal_ac_climate.h
│   └── universal_ac_controller/      # ESPHome 控制器组件
│       ├── __init__.py
│       └── universal_ac_controller.h
├── vendor/irext_core/                # 第三方 IRext C 库（不修改）
├── generated/irext/                  # 生成的 PROGMEM 目录数据
│   ├── catalog_index.h
│   └── catalog.bin
├── data/irext/                       # 原始 bin + SQLite 数据
├── tools/                            # 构建/导出工具
│   └── export_irext_catalog.py
├── partitions/
├── docs/
│   └── REFACTORING_PLAN.md           # 本文档
└── README.md
```

### 文件操作表

| 操作 | 文件 | 说明 |
|------|------|------|
| 移动 | `ac_state_manager.h` → `components/shared/` | 共享头文件归组件目录 |
| 重写 | `ac_state_manager.h` | 字段与 `t_remote_ac_status` 对齐 + 目录选择字段 |
| 删除 | `components/universal_ac_climate/universal_ac.h` | IRremoteESP8266 的 `UniversalAc` 类（整个删除） |
| 新建 | `components/irext_adapter/irext_ac.h` | 替代品：基于 IRext 的 AC 控制，内部维护 `t_remote_ac_status` |
| 新建 | `components/irext_adapter/catalog_provider.h` | 目录查询（三级联动数据源） |
| 新建 | `components/irext_adapter/ir_transmitter.h` | 原始 UINT16 timing 数组 → GPIO 发射 |
| 新建 | `components/irext_adapter/__init__.py` | ESPHome 组件注册 |
| 重写 | `components/universal_ac_controller/universal_ac_controller.h` | 依赖从 `UniversalAc` 改为 `IrextAc` |
| 重写 | `components/universal_ac_climate/universal_ac_climate.h` | Climate traits 动态化（只暴露 IRext 实际支持的功能） |
| 重写 | `configs/ir_8266.yaml` | 去掉 IRremoteESP8266 lib_dep，扁平协议列表改为三级Select，移除不支持的实体 |

---

## 实施顺序（建议分3个PR）

### PR1: 目录标准化 + 数据层
1. 目录标准化（移动/删除文件，更新路径引用）
2. 重写 `AcPersistentState`（字段与 `t_remote_ac_status` 对齐 + 目录选择 + 预留字段）
3. 重写 `AcStateManager`（setter/getter 只操作5个有效字段，预留字段不暴露 setter）
4. 保留 `EspHomeAcStateStore` 的 EEPROM 机制

### PR2: IRext 核心接入
1. `CatalogProvider` — PROGMEM 目录查询（三级联动数据源）
2. `IrextAc` — 封装 `ir_binary_open` / `ir_decode` / `ir_close`，内部维护 `t_remote_ac_status`
3. `IrTransmitter` — UINT16 timing 数组 → GPIO 原始发射
4. 重写 `UniversalAcController` 依赖 `IrextAc`
5. 三级 Select 联动（YAML + Controller）
6. 删除 `universal_ac.h`（IRremoteESP8266 包装器）

### PR3: 接口统一 + Climate
1. 重写 `UniversalAcClimate` — 动态 traits（调用 `get_supported_mode()` 等查询函数）
2. HA/Web 命名和分组统一
3. 统一状态回写和 `ir_status` TextSensor
4. 移除 IRremoteESP8266 lib_dep 和所有残余引用
5. 删除不支持的实体（special_mode / display_light 等）

---

## 风险与注意

1. **ESP8266 内存限制**: 已将 IRext 载荷、索引和字符串显式放入 flash；当前完整目录构建占用 RAM 53.8%、约 1.96 MiB 应用槽的 62.4%
2. **IR 发射精度**: `ir_decode()` 返回 UINT16 mark/space 微秒时序数组，通过 ESPHome `remote_transmitter` 以 38 kHz 发送；波形精度和发射距离仍需实机测试
3. **ESPHome 组件注册**: `irext_adapter` 需要正确的 `__init__.py` 注册，并在 YAML 的 `external_components` 中引用
4. **三级联动 UI**: ESPHome 的 `select:` 实体不原生支持动态更新选项列表，需要在 `set_action` 的 lambda 中调用 `traits.set_options()` 刷新下级列表
5. **vendor C 代码编译**: `irext_core` 是纯 C 代码，头文件已有 `extern "C"` 包装。需确认 PlatformIO 的 `build_flags` 包含 `vendor/irext_core/include` 路径
6. **IRext 枚举反直觉**: `AC_POWER_ON=0, AC_POWER_OFF=1` 和 `AC_SWING_ON=0, AC_SWING_OFF=1` 与常规布尔逻辑相反，代码中需特别注意转换
7. **预留字段一致性**: `ac_display/ac_sleep/ac_timer` 虽然持久化存储，但当前 IRext 库不读取它们。在 `IrextAc` 填充 `t_remote_ac_status` 时，这些字段保持默认值 0 即可
