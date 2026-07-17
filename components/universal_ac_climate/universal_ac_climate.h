#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/universal_ac_controller/universal_ac_controller.h"
#include "esphome/core/component.h"

namespace esphome::universal_ac_climate {

class UniversalAcClimate : public climate::Climate, public Component {
 public:
  void set_controller(universal_ac_controller::UniversalAcController *controller) {
    this->controller_ = controller;
  }

  void setup() override {
    if (this->controller_ != nullptr) {
      this->controller_->add_on_state_callback([this]() { this->sync_state_(); });
    }
    this->sync_state_();
  }
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
    if (this->controller_ == nullptr) return;

    const auto &ac = this->controller_->ac();
    bool power = ac.power();
    auto mode = ac.mode();
    float temperature = ac.temperature();
    auto fan = ac.fan();
    auto swing_v = ac.swing_v();

    if (call.get_mode().has_value()) {
      switch (*call.get_mode()) {
        case climate::CLIMATE_MODE_OFF:
          power = false;
          break;
        case climate::CLIMATE_MODE_AUTO:
          power = true;
          mode = stdAc::opmode_t::kAuto;
          break;
        case climate::CLIMATE_MODE_COOL:
          power = true;
          mode = stdAc::opmode_t::kCool;
          break;
        case climate::CLIMATE_MODE_HEAT:
          power = true;
          mode = stdAc::opmode_t::kHeat;
          break;
        case climate::CLIMATE_MODE_DRY:
          power = true;
          mode = stdAc::opmode_t::kDry;
          break;
        case climate::CLIMATE_MODE_FAN_ONLY:
          power = true;
          mode = stdAc::opmode_t::kFan;
          break;
        default:
          break;
      }
    }

    if (call.get_target_temperature().has_value()) temperature = *call.get_target_temperature();

    if (call.get_fan_mode().has_value()) {
      switch (*call.get_fan_mode()) {
        case climate::CLIMATE_FAN_LOW:
          fan = stdAc::fanspeed_t::kLow;
          break;
        case climate::CLIMATE_FAN_MEDIUM:
          fan = stdAc::fanspeed_t::kMedium;
          break;
        case climate::CLIMATE_FAN_HIGH:
          fan = stdAc::fanspeed_t::kHigh;
          break;
        default:
          fan = stdAc::fanspeed_t::kAuto;
          break;
      }
    }

    if (call.get_swing_mode().has_value()) {
      swing_v = *call.get_swing_mode() == climate::CLIMATE_SWING_VERTICAL
                    ? stdAc::swingv_t::kAuto
                    : stdAc::swingv_t::kOff;
    }

    this->controller_->apply_climate(power, mode, temperature, fan, swing_v);
  }

  void sync_state_() {
    if (this->controller_ == nullptr) return;

    const auto &ac = this->controller_->ac();
    this->target_temperature = ac.temperature();
    this->fan_mode = this->to_fan_mode_(ac.fan());
    this->swing_mode = ac.swing_v() == stdAc::swingv_t::kAuto
                           ? climate::CLIMATE_SWING_VERTICAL
                           : climate::CLIMATE_SWING_OFF;

    if (!ac.power()) {
      this->mode = climate::CLIMATE_MODE_OFF;
      this->action = climate::CLIMATE_ACTION_OFF;
    } else {
      switch (ac.mode()) {
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

  static climate::ClimateFanMode to_fan_mode_(stdAc::fanspeed_t fan) {
    switch (fan) {
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

  universal_ac_controller::UniversalAcController *controller_{nullptr};
};

}  // namespace esphome::universal_ac_climate
