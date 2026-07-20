#pragma once

#include <algorithm>
#include <vector>

#include "catalog_provider.h"
#include "ir_transmitter.h"
#include "esphome/core/log.h"

extern "C" {
#include "ir_decode.h"
}

namespace esphome::irext_adapter {

struct AcCapabilities {
  uint8_t modes{0x1F};
  uint8_t wind_speeds{0x0F};
  uint8_t swing{0x02};
  uint8_t fixed_directions{0};
  int8_t minimum_temperature{AC_TEMP_16};
  int8_t maximum_temperature{AC_TEMP_30};
  bool valid{false};
};

class IrextAc {
 public:
  void set_transmitter(remote_base::RemoteTransmitterBase *transmitter) {
    this->transmitter_.set_transmitter(transmitter);
  }

  bool select_code(uint16_t code_index) {
    CatalogCode entry{};
    if (!this->catalog_.code(code_index, entry)) {
      this->status_ = "遥控器码不存在";
      this->capabilities_ = AcCapabilities{};
      return false;
    }
    this->code_index_ = code_index;
    this->entry_ = entry;
    if (entry.category_id != REMOTE_CATEGORY_AC) {
      this->status_ = "所选型号不是空调协议";
      this->capabilities_ = AcCapabilities{};
      return true;
    }
    return this->load_capabilities_();
  }

  bool select_mode(t_ac_mode mode) {
    if (!this->capabilities_.valid || (this->capabilities_.modes & (1U << mode)) == 0) return false;
    if (!this->open_()) return false;
    this->load_mode_capabilities_(mode);
    ir_close();
    return true;
  }

  bool send(const t_remote_ac_status &status, uint8_t key_code) {
    if (this->entry_.category_id != REMOTE_CATEGORY_AC) {
      this->status_ = "所选型号不支持空调控制";
      return false;
    }
    if (!this->open_()) return false;
    t_remote_ac_status mutable_status = status;
    const uint16_t count = ir_decode(key_code, this->timings_, &mutable_status);
    ir_close();
    if (count == 0 || count > USER_DATA_SIZE) {
      this->status_ = "IRext 无法生成该指令";
      return false;
    }
    const bool sent = this->transmitter_.send_raw(this->timings_, count);
    if (sent) this->send_sequence_++;
    this->status_ = sent ? "红外指令发送成功" : "红外发射器不可用";
    return sent;
  }

  const AcCapabilities &capabilities() const { return this->capabilities_; }
  const std::string &status() const { return this->status_; }
  uint32_t send_sequence() const { return this->send_sequence_; }
  uint16_t code_index() const { return this->code_index_; }
  IrextCatalogProvider &catalog() { return this->catalog_; }
  const IrextCatalogProvider &catalog() const { return this->catalog_; }

 protected:
  bool open_() {
    if (!this->catalog_.copy_binary(this->entry_, this->binary_)) {
      this->status_ = "无法读取遥控器码";
      return false;
    }
    if (ir_binary_open(this->entry_.category_id, this->entry_.subcategory, this->binary_.data(),
                       this->binary_.size()) != IR_DECODE_SUCCEEDED) {
      ir_close();
      this->status_ = "IRext 协议解析失败";
      return false;
    }
    return true;
  }

  bool load_capabilities_() {
    this->capabilities_ = AcCapabilities{};
    if (!this->open_()) return false;
    auto &capabilities = this->capabilities_;
    const bool modes_ok = get_supported_mode(&capabilities.modes) == IR_DECODE_SUCCEEDED;
    uint8_t mode = AC_MODE_COOL;
    for (uint8_t candidate = 0; candidate < AC_MODE_MAX; candidate++) {
      if ((capabilities.modes & (1U << candidate)) != 0) {
        mode = candidate;
        break;
      }
    }
    if (get_supported_wind_direction(&capabilities.fixed_directions) != IR_DECODE_SUCCEEDED)
      capabilities.fixed_directions = 0;
    this->load_mode_capabilities_(static_cast<t_ac_mode>(mode));
    capabilities.valid = modes_ok;
    ir_close();
    this->status_ = capabilities.valid ? "遥控器码已加载" : "遥控器能力读取失败";
    return capabilities.valid;
  }

  void load_mode_capabilities_(t_ac_mode mode) {
    auto &capabilities = this->capabilities_;
    if (get_supported_wind_speed(mode, &capabilities.wind_speeds) != IR_DECODE_SUCCEEDED ||
        capabilities.wind_speeds == 0) capabilities.wind_speeds = 0x0F;
    if (get_supported_swing(mode, &capabilities.swing) != IR_DECODE_SUCCEEDED)
      capabilities.swing = 0x02;
    if (get_temperature_range(mode, &capabilities.minimum_temperature,
                              &capabilities.maximum_temperature) != IR_DECODE_SUCCEEDED ||
        capabilities.minimum_temperature < 0 || capabilities.maximum_temperature < 0) {
      capabilities.minimum_temperature = AC_TEMP_16;
      capabilities.maximum_temperature = AC_TEMP_30;
    }
  }

  IrextCatalogProvider catalog_;
  IrTransmitter transmitter_;
  CatalogCode entry_{};
  std::vector<uint8_t> binary_;
  uint16_t timings_[USER_DATA_SIZE]{};
  AcCapabilities capabilities_{};
  std::string status_{"等待选择遥控器码"};
  uint32_t send_sequence_{0};
  uint16_t code_index_{0};
};

}  // namespace esphome::irext_adapter