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
    if (this->controller_ != nullptr)
      this->controller_->add_on_state_callback([this]() { this->sync_state_(); });
    this->sync_state_();
  }

 protected:
  climate::ClimateTraits traits() override {
    climate::ClimateTraits traits;
    traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
    if (this->controller_ == nullptr) {
      traits.set_visual_min_temperature(16);
      traits.set_visual_max_temperature(30);
      traits.set_visual_temperature_step(1);
      return traits;
    }
    const auto &capabilities = this->controller_->capabilities();
    if (!capabilities.valid || (capabilities.modes & (1U << AC_MODE_AUTO)) != 0)
      traits.add_supported_mode(climate::CLIMATE_MODE_AUTO);
    if (!capabilities.valid || (capabilities.modes & (1U << AC_MODE_COOL)) != 0)
      traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
    if (!capabilities.valid || (capabilities.modes & (1U << AC_MODE_HEAT)) != 0)
      traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
    if (!capabilities.valid || (capabilities.modes & (1U << AC_MODE_DRY)) != 0)
      traits.add_supported_mode(climate::CLIMATE_MODE_DRY);
    if (!capabilities.valid || (capabilities.modes & (1U << AC_MODE_FAN)) != 0)
      traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
    if (!capabilities.valid || (capabilities.wind_speeds & (1U << AC_WS_AUTO)) != 0)
      traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
    if (!capabilities.valid || (capabilities.wind_speeds & (1U << AC_WS_LOW)) != 0)
      traits.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
    if (!capabilities.valid || (capabilities.wind_speeds & (1U << AC_WS_MEDIUM)) != 0)
      traits.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
    if (!capabilities.valid || (capabilities.wind_speeds & (1U << AC_WS_HIGH)) != 0)
      traits.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
    traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);
    if (!capabilities.valid || (capabilities.swing & 0x02U) != 0)
      traits.add_supported_swing_mode(climate::CLIMATE_SWING_VERTICAL);
    traits.set_visual_min_temperature(capabilities.valid ? capabilities.minimum_temperature + 16 : 16);
    traits.set_visual_max_temperature(capabilities.valid ? capabilities.maximum_temperature + 16 : 30);
    traits.set_visual_temperature_step(1);
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    if (this->controller_ == nullptr) return;

    bool power = this->controller_->power();
    t_ac_mode mode = this->controller_->mode();
    float temperature = this->controller_->temperature();
    t_ac_wind_speed fan = this->controller_->fan();
    t_ac_swing swing = this->controller_->swing();

    if (call.get_mode().has_value()) {
      switch (*call.get_mode()) {
        case climate::CLIMATE_MODE_OFF:
          power = false;
          break;
        case climate::CLIMATE_MODE_AUTO:
          power = true;
          mode = AC_MODE_AUTO;
          break;
        case climate::CLIMATE_MODE_COOL:
          power = true;
          mode = AC_MODE_COOL;
          break;
        case climate::CLIMATE_MODE_HEAT:
          power = true;
          mode = AC_MODE_HEAT;
          break;
        case climate::CLIMATE_MODE_DRY:
          power = true;
          mode = AC_MODE_DRY;
          break;
        case climate::CLIMATE_MODE_FAN_ONLY:
          power = true;
          mode = AC_MODE_FAN;
          break;
        default:
          power = true;
          break;
      }
    }

    if (call.get_target_temperature().has_value()) temperature = *call.get_target_temperature();

    if (call.get_fan_mode().has_value()) {
      switch (*call.get_fan_mode()) {
        case climate::CLIMATE_FAN_LOW:
          fan = AC_WS_LOW;
          break;
        case climate::CLIMATE_FAN_MEDIUM:
          fan = AC_WS_MEDIUM;
          break;
        case climate::CLIMATE_FAN_HIGH:
          fan = AC_WS_HIGH;
          break;
        default:
          fan = AC_WS_AUTO;
          break;
      }
    }

    if (call.get_swing_mode().has_value())
      swing = *call.get_swing_mode() == climate::CLIMATE_SWING_VERTICAL ? AC_SWING_ON : AC_SWING_OFF;

    this->controller_->apply_climate(power, mode, temperature, fan, swing);
  }

  void sync_state_() {
    if (this->controller_ == nullptr) return;

    this->target_temperature = this->controller_->temperature();
    this->fan_mode = this->to_fan_mode_(this->controller_->fan());
    this->swing_mode = this->controller_->swing() == AC_SWING_ON
                           ? climate::CLIMATE_SWING_VERTICAL
                           : climate::CLIMATE_SWING_OFF;

    if (!this->controller_->power()) {
      this->mode = climate::CLIMATE_MODE_OFF;
      this->action = climate::CLIMATE_ACTION_OFF;
    } else {
      switch (this->controller_->mode()) {
        case AC_MODE_HEAT:
          this->mode = climate::CLIMATE_MODE_HEAT;
          this->action = climate::CLIMATE_ACTION_HEATING;
          break;
        case AC_MODE_DRY:
          this->mode = climate::CLIMATE_MODE_DRY;
          this->action = climate::CLIMATE_ACTION_DRYING;
          break;
        case AC_MODE_FAN:
          this->mode = climate::CLIMATE_MODE_FAN_ONLY;
          this->action = climate::CLIMATE_ACTION_FAN;
          break;
        case AC_MODE_AUTO:
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

  static climate::ClimateFanMode to_fan_mode_(t_ac_wind_speed fan) {
    switch (fan) {
      case AC_WS_LOW: return climate::CLIMATE_FAN_LOW;
      case AC_WS_MEDIUM: return climate::CLIMATE_FAN_MEDIUM;
      case AC_WS_HIGH: return climate::CLIMATE_FAN_HIGH;
      default: return climate::CLIMATE_FAN_AUTO;
    }
  }

  universal_ac_controller::UniversalAcController *controller_{nullptr};
};

}  // namespace esphome::universal_ac_climate
