#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

extern "C" {
#include "ir_ac_control.h"
}

namespace esphome::shared {

constexpr uint32_t AC_STATE_MAGIC = 0x41435354U;
constexpr uint16_t AC_STATE_VERSION = 2;
constexpr uint16_t AC_STATE_FLAG_POWER = 0x0001U;

struct AcPersistentState {
  uint32_t magic{AC_STATE_MAGIC};
  uint16_t version{AC_STATE_VERSION};
  uint16_t flags{0};
  uint16_t catalog_type{0};
  uint16_t catalog_brand{0};
  uint16_t catalog_code{0};
  uint8_t ac_mode{AC_MODE_COOL};
  uint8_t ac_temp{AC_TEMP_26};
  uint8_t ac_wind_speed{AC_WS_AUTO};
  uint8_t ac_wind_dir{AC_SWING_OFF};
  uint8_t ac_display{0};
  uint8_t ac_sleep{0};
  uint8_t ac_timer{0};
  uint8_t reserved{0};
};

static_assert(std::is_trivially_copyable<AcPersistentState>::value,
              "AcPersistentState must be compatible with binary storage");

class AcStateStore {
 public:
  virtual ~AcStateStore() = default;
  virtual bool load(AcPersistentState &state) = 0;
  virtual bool save(const AcPersistentState &state) = 0;
};

class AcStateManager {
 public:
  explicit AcStateManager(AcStateStore &store) : store_(store) {}

  bool begin() {
    AcPersistentState restored{};
    if (this->store_.load(restored) && this->validate_(restored)) {
      this->state_ = restored;
      return true;
    }
    this->state_ = AcPersistentState{};
    this->persist_();
    return false;
  }

  const AcPersistentState &state() const { return this->state_; }

  bool power() const { return (this->state_.flags & AC_STATE_FLAG_POWER) != 0; }
  t_ac_mode mode() const { return static_cast<t_ac_mode>(this->state_.ac_mode); }
  t_ac_temperature temperature() const { return static_cast<t_ac_temperature>(this->state_.ac_temp); }
  t_ac_wind_speed wind_speed() const { return static_cast<t_ac_wind_speed>(this->state_.ac_wind_speed); }
  t_ac_swing swing() const { return static_cast<t_ac_swing>(this->state_.ac_wind_dir); }

  void set_catalog(uint16_t type, uint16_t brand, uint16_t code) {
    this->state_.catalog_type = type;
    this->state_.catalog_brand = brand;
    this->state_.catalog_code = code;
    this->persist_();
  }

  void set_power(bool power) {
    if (power) this->state_.flags |= AC_STATE_FLAG_POWER;
    else this->state_.flags &= ~AC_STATE_FLAG_POWER;
    this->persist_();
  }

  void set_mode(t_ac_mode mode) {
    if (mode >= AC_MODE_COOL && mode < AC_MODE_MAX) {
      this->state_.ac_mode = static_cast<uint8_t>(mode);
      this->persist_();
    }
  }

  void set_temperature(t_ac_temperature temperature) {
    if (temperature >= AC_TEMP_16 && temperature < AC_TEMP_MAX) {
      this->state_.ac_temp = static_cast<uint8_t>(temperature);
      this->persist_();
    }
  }

  void set_wind_speed(t_ac_wind_speed speed) {
    if (speed >= AC_WS_AUTO && speed < AC_WS_MAX) {
      this->state_.ac_wind_speed = static_cast<uint8_t>(speed);
      this->persist_();
    }
  }

  void set_swing(t_ac_swing swing) {
    if (swing >= AC_SWING_ON && swing < AC_SWING_MAX) {
      this->state_.ac_wind_dir = static_cast<uint8_t>(swing);
      this->persist_();
    }
  }

  void replace(const AcPersistentState &state) {
    if (!this->validate_(state)) return;
    this->state_ = state;
    this->persist_();
  }

 protected:
  static bool validate_(const AcPersistentState &state) {
    if (state.magic != AC_STATE_MAGIC || state.version != AC_STATE_VERSION) return false;
    if ((state.flags & ~AC_STATE_FLAG_POWER) != 0) return false;
    if (state.ac_mode >= AC_MODE_MAX || state.ac_temp >= AC_TEMP_MAX) return false;
    if (state.ac_wind_speed >= AC_WS_MAX || state.ac_wind_dir >= AC_SWING_MAX) return false;
    return true;
  }

  void persist_() { this->store_.save(this->state_); }

  AcStateStore &store_;
  AcPersistentState state_{};
};

}  // namespace esphome::shared