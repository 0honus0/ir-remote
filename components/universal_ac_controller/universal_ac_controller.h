#pragma once

#include <cstring>
#include <utility>

#include "ac_state_manager.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::universal_ac_controller {

class EspHomeAcStateStore : public AcStateStore {
 public:
  void setup() {
    this->preference_ = global_preferences->make_preference<AcPersistentState>(
        fnv1_hash("universal_ac_controller.state.v1"), true);
  }

  bool load(AcPersistentState &state) override {
    if (!this->preference_.load(&state)) return false;
    this->last_saved_ = state;
    this->has_last_saved_ = true;
    return true;
  }

  bool save(const AcPersistentState &state) override {
    if (this->has_last_saved_ && std::memcmp(&this->last_saved_, &state, sizeof(state)) == 0) return true;
    if (!this->preference_.save(&state)) return false;
    this->last_saved_ = state;
    this->has_last_saved_ = true;
    this->dirty_ = true;
    return true;
  }

  bool flush() {
    if (!this->dirty_) return true;
    if (!global_preferences->sync()) return false;
    this->dirty_ = false;
    return true;
  }

  bool dirty() const { return this->dirty_; }

 protected:
  ESPPreferenceObject preference_;
  AcPersistentState last_saved_{};
  bool has_last_saved_{false};
  bool dirty_{false};
};

class UniversalAcController : public Component {
 public:
  UniversalAcController() : manager_(this->ac_, this->store_) {}

  void set_protocol_control(select::Select *control) { this->protocol_control_ = control; }
  void set_fan_control(select::Select *control) { this->fan_control_ = control; }
  void set_swing_control(select::Select *control) { this->swing_control_ = control; }
  void set_special_control(select::Select *control) { this->special_control_ = control; }
  void set_timer_control(number::Number *control) { this->timer_control_ = control; }
  void set_light_control(switch_::Switch *control) { this->light_control_ = control; }
  void set_power_control(switch_::Switch *control) { this->power_control_ = control; }
  void set_status_control(text_sensor::TextSensor *control) { this->status_control_ = control; }

  template<typename F> void add_on_state_callback(F &&callback) {
    this->state_callbacks_.add(std::forward<F>(callback));
  }

  void setup() override {
    this->store_.setup();
    const bool restored = this->manager_.begin();
    if (restored) {
      ESP_LOGI("universal_ac", "Restored AC settings from flash");
    } else {
      ESP_LOGI("universal_ac", "Stored default AC settings in flash");
    }
    this->schedule_flush_();
    this->set_timeout("initial_control_sync", 100, [this]() { this->sync_controls(); });
  }

  void on_shutdown() override {
    if (!this->store_.flush()) ESP_LOGE("universal_ac", "Failed to flush AC settings during shutdown");
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool set_protocol(const std::string &protocol) {
    const bool accepted = this->manager_.set_protocol(protocol);
    this->state_changed_(accepted);
    return accepted;
  }

  void set_fan(const std::string &fan) {
    this->manager_.set_fan(fan);
    this->state_changed_(true);
  }

  void set_swing(const std::string &swing) {
    this->manager_.set_swing_v(swing);
    this->state_changed_(true);
  }

  void set_special_mode(const std::string &mode) {
    this->manager_.set_special_mode(mode);
    this->state_changed_(false);
  }

  void set_timer(float minutes) {
    this->manager_.set_timer(minutes);
    this->state_changed_(false);
  }

  void set_light(bool light) {
    this->manager_.set_light(light);
    this->state_changed_(true);
  }

  void set_power(bool power) {
    this->manager_.set_power(power);
    this->state_changed_(true);
  }

  void apply_climate(bool power, stdAc::opmode_t mode, float temperature,
                     stdAc::fanspeed_t fan, stdAc::swingv_t swing_v) {
    this->manager_.apply_climate(power, mode, temperature, fan, swing_v);
    this->state_changed_(true);
  }

  void sync_controls() {
    const auto &ac = this->manager_.ac();
    this->publish_select_(this->protocol_control_, ac.selection());
    this->publish_select_(this->fan_control_, this->fan_label_(ac.fan()));
    this->publish_select_(this->swing_control_, this->swing_label_(ac.swing_v()));
    this->publish_select_(this->special_control_, this->special_label_());
    this->publish_number_(this->timer_control_, ac.sleep() > 0 ? ac.sleep() : 0);
    this->publish_switch_(this->light_control_, ac.light());
    this->publish_switch_(this->power_control_, ac.power());
    this->publish_text_(this->status_control_, ac.status());
  }

  UniversalAc &ac() { return this->manager_.ac(); }
  const UniversalAc &ac() const { return this->manager_.ac(); }
  bool timer_expired() const { return this->ac().timer_expired(); }
  uint32_t send_sequence() const { return this->ac().send_sequence(); }
  const std::string &status() const { return this->ac().status(); }

 protected:
  void state_changed_(bool persistent) {
    this->sync_controls();
    this->state_callbacks_.call();
    if (persistent) this->schedule_flush_();
  }

  void schedule_flush_() {
    if (!this->store_.dirty()) return;
    this->set_timeout("state_flush", 750, [this]() {
      if (!this->store_.flush()) ESP_LOGE("universal_ac", "Failed to flush AC settings to flash");
    });
  }

  static void publish_select_(select::Select *control, const std::string &value) {
    if (control != nullptr && control->current_option() != value) control->publish_state(value);
  }

  static void publish_number_(number::Number *control, float value) {
    if (control != nullptr && control->state != value) control->publish_state(value);
  }

  static void publish_switch_(switch_::Switch *control, bool value) {
    if (control != nullptr && control->state != value) control->publish_state(value);
  }

  static void publish_text_(text_sensor::TextSensor *control, const std::string &value) {
    if (control != nullptr && control->state != value) control->publish_state(value);
  }

  static const char *fan_label_(stdAc::fanspeed_t fan) {
    switch (fan) {
      case stdAc::fanspeed_t::kMin: return "最小";
      case stdAc::fanspeed_t::kLow: return "低";
      case stdAc::fanspeed_t::kMedium: return "中";
      case stdAc::fanspeed_t::kHigh: return "高";
      case stdAc::fanspeed_t::kMax: return "最大";
      default: return "自动";
    }
  }

  static const char *swing_label_(stdAc::swingv_t swing) {
    switch (swing) {
      case stdAc::swingv_t::kHighest: return "最高";
      case stdAc::swingv_t::kHigh: return "偏高";
      case stdAc::swingv_t::kUpperMiddle: return "中上";
      case stdAc::swingv_t::kMiddle: return "中间";
      case stdAc::swingv_t::kLow: return "偏低";
      case stdAc::swingv_t::kLowest: return "最低";
      case stdAc::swingv_t::kAuto: return "自动";
      default: return "关闭";
    }
  }

  const char *special_label_() const {
    if (this->ac().turbo()) return "强力";
    if (this->ac().sleep_mode()) return "睡眠";
    if (this->ac().clean()) return "干燥";
    return "正常";
  }

  UniversalAc ac_;
  EspHomeAcStateStore store_;
  AcStateManager manager_;
  select::Select *protocol_control_{nullptr};
  select::Select *fan_control_{nullptr};
  select::Select *swing_control_{nullptr};
  select::Select *special_control_{nullptr};
  number::Number *timer_control_{nullptr};
  switch_::Switch *light_control_{nullptr};
  switch_::Switch *power_control_{nullptr};
  text_sensor::TextSensor *status_control_{nullptr};
  LazyCallbackManager<void()> state_callbacks_;
};

}  // namespace esphome::universal_ac_controller
