#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "universal_ac.h"

namespace esphome::universal_ac_climate {

class UniversalAcClimate : public climate::Climate, public PollingComponent {
 public:
  void set_temperature_storage(number::Number *storage) { this->temperature_storage_ = storage; }
  void set_mode_storage(select::Select *storage) { this->mode_storage_ = storage; }
  void set_fan_storage(select::Select *storage) { this->fan_storage_ = storage; }
  void set_swing_storage(select::Select *storage) { this->swing_storage_ = storage; }
  void set_special_storage(select::Select *storage) { this->special_storage_ = storage; }
  void set_power_storage(switch_::Switch *storage) { this->power_storage_ = storage; }
  void set_timer_storage(number::Number *storage) { this->timer_storage_ = storage; }
  void setup() override { this->sync_state_(); }
  void update() override { this->sync_state_(); }

 protected:
  climate::ClimateTraits traits() override {
    climate::ClimateTraits traits;
    traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
    traits.add_supported_mode(climate::CLIMATE_MODE_AUTO);
    traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
    traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
    traits.add_supported_mode(climate::CLIMATE_MODE_DRY);
    traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
    traits.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_VERTICAL);
    traits.set_visual_min_temperature(16);
    traits.set_visual_max_temperature(30);
    traits.set_visual_temperature_step(1);
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    universal_ac.set_sending_suspended(true);
    bool power = universal_ac.power();
    auto mode = universal_ac.mode();
    float temperature = universal_ac.temperature();
    auto fan = universal_ac.fan();
    auto swing_v = universal_ac.swing_v();
    int16_t sleep = universal_ac.sleep();
    const char *stored_mode = nullptr;
    const char *stored_swing = nullptr;

    if (call.get_mode().has_value()) {
      switch (*call.get_mode()) {
        case climate::CLIMATE_MODE_OFF:
          power = false;
          break;
        case climate::CLIMATE_MODE_AUTO:
          power = true;
          mode = stdAc::opmode_t::kAuto;
          stored_mode = "自动";
          break;
        case climate::CLIMATE_MODE_COOL:
          power = true;
          mode = stdAc::opmode_t::kCool;
          stored_mode = "制冷";
          break;
        case climate::CLIMATE_MODE_HEAT:
          power = true;
          mode = stdAc::opmode_t::kHeat;
          stored_mode = "制热";
          break;
        case climate::CLIMATE_MODE_DRY:
          power = true;
          mode = stdAc::opmode_t::kDry;
          stored_mode = "除湿";
          break;
        case climate::CLIMATE_MODE_FAN_ONLY:
          power = true;
          mode = stdAc::opmode_t::kFan;
          stored_mode = "送风";
          break;
        default:
          break;
      }
    }
    if (call.get_target_temperature().has_value()) {
      temperature = *call.get_target_temperature();
      if (this->temperature_storage_ != nullptr)
        this->temperature_storage_->make_call().set_value(temperature).perform();
    }
    if (call.get_fan_mode().has_value()) {
      const char *stored_fan = "自动";
      switch (*call.get_fan_mode()) {
        case climate::CLIMATE_FAN_LOW:
          fan = stdAc::fanspeed_t::kLow;
          stored_fan = "低";
          break;
        case climate::CLIMATE_FAN_MEDIUM:
          fan = stdAc::fanspeed_t::kMedium;
          stored_fan = "中";
          break;
        case climate::CLIMATE_FAN_HIGH:
          fan = stdAc::fanspeed_t::kHigh;
          stored_fan = "高";
          break;
        default:
          fan = stdAc::fanspeed_t::kAuto;
          break;
      }
      if (this->fan_storage_ != nullptr)
        this->fan_storage_->make_call().set_option(stored_fan).perform();
    }
    if (call.get_swing_mode().has_value()) {
      switch (*call.get_swing_mode()) {
        case climate::CLIMATE_SWING_VERTICAL:
          swing_v = stdAc::swingv_t::kAuto;
          stored_swing = "自动";
          break;
        default:
          swing_v = stdAc::swingv_t::kOff;
          stored_swing = "关闭";
          break;
      }
    }
    if (stored_mode != nullptr && this->mode_storage_ != nullptr)
      this->mode_storage_->make_call().set_option(stored_mode).perform();
    if (stored_swing != nullptr && this->swing_storage_ != nullptr)
      this->swing_storage_->make_call().set_option(stored_swing).perform();
    universal_ac.set_sending_suspended(false);
    universal_ac.apply_climate(power, mode, temperature, fan, swing_v,
                               stdAc::swingh_t::kOff, false, universal_ac.turbo(), false,
                               sleep, universal_ac.sleep_mode());
    if (this->power_storage_ != nullptr)
      this->power_storage_->publish_state(power);
    if (!power && this->swing_storage_ != nullptr)
      this->swing_storage_->make_call().set_index(0).perform();
    if (!power && this->special_storage_ != nullptr)
      this->special_storage_->make_call().set_index(0).perform();
    if (!power && this->timer_storage_ != nullptr)
      this->timer_storage_->publish_state(0);
    this->sync_state_();
  }

  void sync_state_() {
    this->target_temperature = universal_ac.temperature();
    this->fan_mode = this->to_fan_mode_();
    this->swing_mode = this->to_swing_mode_();
    if (!universal_ac.power()) {
      this->mode = climate::CLIMATE_MODE_OFF;
      this->action = climate::CLIMATE_ACTION_OFF;
      this->swing_mode = climate::CLIMATE_SWING_OFF;
    } else {
      switch (universal_ac.mode()) {
        case stdAc::opmode_t::kHeat:
          this->mode = climate::CLIMATE_MODE_HEAT;
          this->action = climate::CLIMATE_ACTION_HEATING;
          break;
        case stdAc::opmode_t::kDry:
          this->mode = climate::CLIMATE_MODE_DRY;
          this->action = climate::CLIMATE_ACTION_DRYING;
          break;
        case stdAc::opmode_t::kFan:
          this->mode = climate::CLIMATE_MODE_FAN_ONLY;
          this->action = climate::CLIMATE_ACTION_FAN;
          break;
        case stdAc::opmode_t::kAuto:
          this->mode = climate::CLIMATE_MODE_AUTO;
          this->action = climate::CLIMATE_ACTION_IDLE;
          break;
        default:
          this->mode = climate::CLIMATE_MODE_COOL;
          this->action = climate::CLIMATE_ACTION_COOLING;
          break;
      }
    }
    this->publish_state();
  }

  climate::ClimateSwingMode to_swing_mode_() const {
    return universal_ac.swing_v() == stdAc::swingv_t::kAuto ? climate::CLIMATE_SWING_VERTICAL
                                                             : climate::CLIMATE_SWING_OFF;
  }

  climate::ClimateFanMode to_fan_mode_() const {
    switch (universal_ac.fan()) {
      case stdAc::fanspeed_t::kLow:
      case stdAc::fanspeed_t::kMin:
        return climate::CLIMATE_FAN_LOW;
      case stdAc::fanspeed_t::kMedium:
        return climate::CLIMATE_FAN_MEDIUM;
      case stdAc::fanspeed_t::kHigh:
      case stdAc::fanspeed_t::kMax:
        return climate::CLIMATE_FAN_HIGH;
      default:
        return climate::CLIMATE_FAN_AUTO;
    }
  }

  number::Number *temperature_storage_{nullptr};
  select::Select *mode_storage_{nullptr};
  select::Select *fan_storage_{nullptr};
  select::Select *swing_storage_{nullptr};
  select::Select *special_storage_{nullptr};
  switch_::Switch *power_storage_{nullptr};
  number::Number *timer_storage_{nullptr};
};

}  // namespace esphome::universal_ac_climate
