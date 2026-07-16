#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "universal_ac.h"

constexpr uint32_t AC_STATE_MAGIC = 0x41435354U;
constexpr uint16_t AC_STATE_VERSION = 1;
constexpr uint16_t AC_STATE_FLAG_POWER = 0x0001U;
constexpr size_t AC_PROTOCOL_CAPACITY = 96;

struct AcPersistentState {
  uint32_t magic{AC_STATE_MAGIC};
  uint16_t version{AC_STATE_VERSION};
  uint16_t flags{0};
  char protocol[AC_PROTOCOL_CAPACITY]{};
  float temperature{26.0f};
  int8_t mode{static_cast<int8_t>(stdAc::opmode_t::kCool)};
  int8_t fan{static_cast<int8_t>(stdAc::fanspeed_t::kAuto)};
  int8_t swing_v{static_cast<int8_t>(stdAc::swingv_t::kOff)};
  uint8_t light{1};
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
  AcStateManager(UniversalAc &ac, AcStateStore &store) : ac_(ac), store_(store) {}

  bool begin() {
    this->ac_.begin();
    this->set_defaults_();

    AcPersistentState restored{};
    if (this->store_.load(restored) && this->validate_(restored) && this->apply_(restored)) {
      this->state_ = restored;
      return true;
    }

    this->apply_(this->state_);
    this->store_.save(this->state_);
    return false;
  }

  bool set_protocol(const std::string &protocol) {
    if (!this->ac_.set_protocol(protocol)) return false;
    this->copy_protocol_(protocol);
    this->persist_();
    return true;
  }

  void set_fan(const std::string &fan) {
    this->ac_.set_fan(fan);
    this->state_.fan = static_cast<int8_t>(this->ac_.fan());
    this->persist_();
  }

  void set_swing_v(const std::string &swing) {
    this->ac_.set_swing_v(swing);
    this->state_.swing_v = static_cast<int8_t>(this->ac_.swing_v());
    this->persist_();
  }

  void set_special_mode(const std::string &mode) { this->ac_.set_special_mode(mode); }

  void set_timer(float minutes) { this->ac_.set_sleep(minutes); }

  void set_light(bool light) {
    this->ac_.set_feature("light", light);
    this->state_.light = light ? 1 : 0;
    this->persist_();
  }

  void set_power(bool power) {
    this->ac_.set_power(power);
    this->set_power_flag_(power);
    this->state_.mode = static_cast<int8_t>(this->ac_.mode());
    this->persist_();
  }

  void apply_climate(bool power, stdAc::opmode_t mode, float temperature, stdAc::fanspeed_t fan,
                     stdAc::swingv_t swing_v) {
    this->ac_.apply_climate(power, mode, temperature, fan, swing_v, stdAc::swingh_t::kOff,
                            false, this->ac_.turbo(), false, this->ac_.sleep(), this->ac_.sleep_mode());
    this->state_.temperature = this->ac_.temperature();
    this->state_.mode = static_cast<int8_t>(this->ac_.mode());
    this->state_.fan = static_cast<int8_t>(this->ac_.fan());
    this->state_.swing_v = static_cast<int8_t>(this->ac_.swing_v());
    this->set_power_flag_(this->ac_.power());
    this->persist_();
  }

  const AcPersistentState &persistent_state() const { return this->state_; }
  UniversalAc &ac() { return this->ac_; }
  const UniversalAc &ac() const { return this->ac_; }

 protected:
  void set_defaults_() {
    this->state_ = AcPersistentState{};
    this->copy_protocol_("格力_GREE_YAW1F");
  }

  void copy_protocol_(const std::string &protocol) {
    std::memset(this->state_.protocol, 0, sizeof(this->state_.protocol));
    std::strncpy(this->state_.protocol, protocol.c_str(), sizeof(this->state_.protocol) - 1);
  }

  bool apply_(const AcPersistentState &state) {
    return this->ac_.restore(state.protocol, state.temperature,
                             static_cast<stdAc::opmode_t>(state.mode),
                             static_cast<stdAc::fanspeed_t>(state.fan),
                             static_cast<stdAc::swingv_t>(state.swing_v), state.light != 0,
                             (state.flags & AC_STATE_FLAG_POWER) != 0);
  }

  bool validate_(const AcPersistentState &state) const {
    if (state.magic != AC_STATE_MAGIC || state.version != AC_STATE_VERSION) return false;
    if ((state.flags & ~AC_STATE_FLAG_POWER) != 0) return false;
    if (state.protocol[0] == '\0' || state.protocol[AC_PROTOCOL_CAPACITY - 1] != '\0') return false;
    if (state.temperature < 16.0f || state.temperature > 30.0f) return false;
    if (!this->valid_mode_(state.mode) || !this->valid_fan_(state.fan) || !this->valid_swing_(state.swing_v))
      return false;
    return state.light <= 1;
  }

  static bool valid_mode_(int8_t mode) {
    switch (static_cast<stdAc::opmode_t>(mode)) {
      case stdAc::opmode_t::kAuto:
      case stdAc::opmode_t::kCool:
      case stdAc::opmode_t::kHeat:
      case stdAc::opmode_t::kDry:
      case stdAc::opmode_t::kFan:
        return true;
      default:
        return false;
    }
  }

  static bool valid_fan_(int8_t fan) {
    switch (static_cast<stdAc::fanspeed_t>(fan)) {
      case stdAc::fanspeed_t::kAuto:
      case stdAc::fanspeed_t::kMin:
      case stdAc::fanspeed_t::kLow:
      case stdAc::fanspeed_t::kMedium:
      case stdAc::fanspeed_t::kHigh:
      case stdAc::fanspeed_t::kMax:
        return true;
      default:
        return false;
    }
  }

  static bool valid_swing_(int8_t swing) {
    switch (static_cast<stdAc::swingv_t>(swing)) {
      case stdAc::swingv_t::kOff:
      case stdAc::swingv_t::kAuto:
      case stdAc::swingv_t::kHighest:
      case stdAc::swingv_t::kHigh:
      case stdAc::swingv_t::kUpperMiddle:
      case stdAc::swingv_t::kMiddle:
      case stdAc::swingv_t::kLow:
      case stdAc::swingv_t::kLowest:
        return true;
      default:
        return false;
    }
  }

  void persist_() { this->store_.save(this->state_); }

  void set_power_flag_(bool power) {
    if (power) {
      this->state_.flags |= AC_STATE_FLAG_POWER;
    } else {
      this->state_.flags &= ~AC_STATE_FLAG_POWER;
    }
  }

  UniversalAc &ac_;
  AcStateStore &store_;
  AcPersistentState state_{};
};
