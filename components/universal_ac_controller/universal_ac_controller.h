#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "esphome/components/irext_adapter/irext_ac.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/select/select.h"
#include "esphome/components/shared/ac_state_manager.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::universal_ac_controller {

class EspHomeAcStateStore : public shared::AcStateStore {
 public:
  void setup() {
    this->preference_ = global_preferences->make_preference<shared::AcPersistentState>(
        fnv1_hash("irext_ac_controller.state.v2"), true);
  }

  bool load(shared::AcPersistentState &state) override {
    if (!this->preference_.load(&state)) return false;
    this->last_saved_ = state;
    this->has_last_saved_ = true;
    return true;
  }

  bool save(const shared::AcPersistentState &state) override {
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
  shared::AcPersistentState last_saved_{};
  bool has_last_saved_{false};
  bool dirty_{false};
};

class UniversalAcController : public Component {
 public:
  UniversalAcController() : manager_(this->store_) {}

  void set_transmitter(remote_base::RemoteTransmitterBase *transmitter) { this->ac_.set_transmitter(transmitter); }
  void set_type_control(select::Select *control) { this->type_control_ = control; }
  void set_brand_control(select::Select *control) { this->brand_control_ = control; }
  void set_model_control(select::Select *control) { this->model_control_ = control; }
  void set_fan_control(select::Select *control) { this->fan_control_ = control; }
  void set_swing_control(select::Select *control) { this->swing_control_ = control; }
  void set_power_control(switch_::Switch *control) { this->power_control_ = control; }
  void set_status_control(text_sensor::TextSensor *control) { this->status_control_ = control; }

  template<typename F> void add_on_state_callback(F &&callback) {
    this->state_callbacks_.add(std::forward<F>(callback));
  }

  void setup() override {
    this->store_.setup();
    const bool restored = this->manager_.begin();
    ESP_LOGI("universal_ac", "开始加载一级目录");
    this->configure_type_control_();
    this->reset_brand_control_();
    this->reset_model_control_();
    this->catalog_status_ = "一级目录已加载，请选择设备类型";
    ESP_LOGI("universal_ac", "一级目录加载完成，等待选择设备类型");
    ESP_LOGI("universal_ac", "%s AC settings", restored ? "Restored" : "Initialized");
    this->schedule_flush_();
    this->set_timeout("initial_control_sync", 100, [this]() { this->sync_controls(); });
  }

  void on_shutdown() override {
    if (!this->store_.flush()) ESP_LOGE("universal_ac", "Failed to flush AC settings during shutdown");
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool set_type(const std::string &label) {
    const int index = this->parse_index_(label);
    if (index < 0 || static_cast<size_t>(index) >= this->ac_.catalog().type_count()) return false;
    const auto brands = this->ac_.catalog().ac_brands_for_type(index);
    if (brands.empty()) return false;
    this->selected_type_ = static_cast<uint16_t>(index);
    this->selected_brand_ = INVALID_CATALOG_INDEX;
    this->catalog_ready_ = false;
    ESP_LOGI("universal_ac", "一级目录选择完成，开始加载二级目录");
    this->configure_brand_control_();
    this->reset_model_control_();
    this->catalog_status_ = "二级目录已加载，请选择品牌";
    ESP_LOGI("universal_ac", "二级目录加载完成，等待选择品牌");
    this->sync_controls();
    return true;
  }

  bool set_brand(const std::string &label) {
    const int index = this->parse_index_(label);
    uint8_t type = 0;
    std::string name;
    if (this->selected_type_ == INVALID_CATALOG_INDEX || index < 0 ||
        !this->ac_.catalog().brand(index, type, name) || type != this->selected_type_) return false;
    const auto codes = this->ac_.catalog().ac_codes_for_brand(index);
    if (codes.empty()) return false;
    this->selected_brand_ = static_cast<uint16_t>(index);
    this->catalog_ready_ = false;
    ESP_LOGI("universal_ac", "二级目录选择完成，开始加载三级目录");
    this->configure_model_control_();
    this->catalog_status_ = "三级目录已加载，请选择型号";
    ESP_LOGI("universal_ac", "三级目录加载完成，等待选择型号");
    this->sync_controls();
    return true;
  }

  bool set_model(const std::string &label) {
    const int index = this->parse_index_(label);
    irext_adapter::CatalogCode code{};
    if (this->selected_type_ == INVALID_CATALOG_INDEX || this->selected_brand_ == INVALID_CATALOG_INDEX ||
        index < 0 || !this->ac_.catalog().code(index, code) || code.brand_index != this->selected_brand_ ||
        code.category_id != REMOTE_CATEGORY_AC)
      return false;
    this->manager_.set_catalog(this->selected_type_, this->selected_brand_, index);
    this->select_current_code_();
    this->sanitize_state_();
    this->configure_capability_controls_();
    this->catalog_ready_ = true;
    this->catalog_status_.clear();
    ESP_LOGI("universal_ac", "三级目录选择完成，遥控器协议已加载");
    this->state_changed_(true);
    return true;
  }

  void set_fan(const std::string &fan) {
    t_ac_wind_speed speed = AC_WS_AUTO;
    if (fan == "低") speed = AC_WS_LOW;
    else if (fan == "中") speed = AC_WS_MEDIUM;
    else if (fan == "高") speed = AC_WS_HIGH;
    this->manager_.set_wind_speed(speed);
    this->send_(KEY_AC_WIND_SPEED);
  }

  void set_swing(const std::string &swing) {
    this->manager_.set_swing(swing == "摆风" ? AC_SWING_ON : AC_SWING_OFF);
    this->send_(KEY_AC_WIND_SWING);
  }

  void set_power(bool power) {
    this->manager_.set_power(power);
    if (!power) this->manager_.set_swing(AC_SWING_OFF);
    this->send_(KEY_AC_POWER);
  }

  void apply_climate(bool power, t_ac_mode mode, float temperature, t_ac_wind_speed fan,
                     t_ac_swing swing) {
    const auto previous = this->manager_.state();
    auto next = previous;
    if (power) next.flags |= shared::AC_STATE_FLAG_POWER;
    else next.flags &= ~shared::AC_STATE_FLAG_POWER;
    next.ac_mode = static_cast<uint8_t>(mode);
    const int degrees = std::max(16, std::min(30, static_cast<int>(temperature + 0.5f)));
    next.ac_temp = static_cast<uint8_t>(degrees - 16);
    next.ac_wind_speed = static_cast<uint8_t>(fan);
    next.ac_wind_dir = static_cast<uint8_t>(power ? swing : AC_SWING_OFF);
    this->manager_.replace(next);
    this->sanitize_state_();
    this->configure_capability_controls_();
    const auto &current = this->manager_.state();
    uint8_t key_code = KEY_AC_POWER;
    if (((previous.flags & shared::AC_STATE_FLAG_POWER) != 0) == power) {
      if (previous.ac_mode != current.ac_mode) key_code = KEY_AC_MODE_SWITCH;
      else if (previous.ac_temp < current.ac_temp) key_code = KEY_AC_TEMP_PLUS;
      else if (previous.ac_temp > current.ac_temp) key_code = KEY_AC_TEMP_MINUS;
      else if (previous.ac_wind_speed != current.ac_wind_speed) key_code = KEY_AC_WIND_SPEED;
      else if (previous.ac_wind_dir != current.ac_wind_dir) key_code = KEY_AC_WIND_SWING;
    }
    this->send_(key_code);
  }

  void sync_controls() {
    const auto &state = this->manager_.state();
    const uint16_t type = this->selected_type_ == INVALID_CATALOG_INDEX ? state.catalog_type : this->selected_type_;
    this->publish_select_(this->type_control_, this->type_label_(type));
    if (this->selected_brand_ == INVALID_CATALOG_INDEX) {
      this->publish_select_(this->brand_control_, BRAND_PLACEHOLDER);
      this->publish_select_(this->model_control_, MODEL_PLACEHOLDER);
    } else {
      this->publish_select_(this->brand_control_, this->brand_label_(this->selected_brand_));
      this->publish_select_(this->model_control_, this->catalog_ready_ ? this->model_label_(state.catalog_code)
                                                                       : MODEL_PLACEHOLDER);
    }
    this->publish_select_(this->fan_control_, this->fan_label_(this->manager_.wind_speed()));
    this->publish_select_(this->swing_control_, this->manager_.swing() == AC_SWING_ON ? "摆风" : "关闭");
    this->publish_switch_(this->power_control_, this->manager_.power());
    this->publish_text_(this->status_control_, this->status());
  }

  bool power() const { return this->manager_.power(); }
  t_ac_mode mode() const { return this->manager_.mode(); }
  float temperature() const { return static_cast<float>(this->manager_.temperature()) + 16.0f; }
  t_ac_wind_speed fan() const { return this->manager_.wind_speed(); }
  t_ac_swing swing() const { return this->manager_.swing(); }
  const irext_adapter::AcCapabilities &capabilities() const { return this->ac_.capabilities(); }
  uint32_t send_sequence() const { return this->ac_.send_sequence(); }

  std::string status() const {
    if (!this->catalog_ready_) return this->catalog_status_;
    const auto &state = this->manager_.state();
    return this->brand_name_(state.catalog_brand) + " " + this->model_name_(state.catalog_code) + ": " +
           this->ac_.status();
  }

 protected:
  void validate_catalog_selection_() {
    const auto &state = this->manager_.state();
    irext_adapter::CatalogCode selected{};
    if (this->ac_.catalog().valid_selection(state.catalog_type, state.catalog_brand, state.catalog_code) &&
        this->ac_.catalog().code(state.catalog_code, selected) &&
        selected.category_id == REMOTE_CATEGORY_AC) return;
    const auto types = this->ac_.catalog().ac_types();
    if (types.empty()) return;
    const auto brands = this->ac_.catalog().ac_brands_for_type(types.front());
    if (brands.empty()) return;
    const auto codes = this->ac_.catalog().ac_codes_for_brand(brands.front());
    if (!codes.empty()) this->manager_.set_catalog(types.front(), brands.front(), codes.front());
  }

  void select_current_code_() { this->ac_.select_code(this->manager_.state().catalog_code); }

  void sanitize_state_() {
    const auto &capabilities = this->ac_.capabilities();
    if (!capabilities.valid) return;
    auto state = this->manager_.state();
    if ((capabilities.modes & (1U << state.ac_mode)) == 0) {
      for (uint8_t mode = 0; mode < AC_MODE_MAX; mode++) {
        if ((capabilities.modes & (1U << mode)) != 0) {
          state.ac_mode = mode;
          break;
        }
      }
    }
    this->ac_.select_mode(static_cast<t_ac_mode>(state.ac_mode));
    if ((capabilities.wind_speeds & (1U << state.ac_wind_speed)) == 0) state.ac_wind_speed = AC_WS_AUTO;
    state.ac_temp = std::max<uint8_t>(capabilities.minimum_temperature,
                                      std::min<uint8_t>(capabilities.maximum_temperature, state.ac_temp));
    if ((capabilities.swing & 0x02U) == 0) state.ac_wind_dir = AC_SWING_OFF;
    this->manager_.replace(state);
  }

  t_remote_ac_status build_status_() const {
    const auto &state = this->manager_.state();
    t_remote_ac_status status{};
    status.ac_power = this->manager_.power() ? AC_POWER_ON : AC_POWER_OFF;
    status.ac_temp = static_cast<t_ac_temperature>(state.ac_temp);
    status.ac_mode = static_cast<t_ac_mode>(state.ac_mode);
    status.ac_wind_dir = static_cast<t_ac_swing>(state.ac_wind_dir);
    status.ac_wind_speed = static_cast<t_ac_wind_speed>(state.ac_wind_speed);
    return status;
  }

  void send_(uint8_t key_code) {
    this->ac_.send(this->build_status_(), key_code);
    this->state_changed_(true);
  }

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

  void configure_type_control_() {
    this->type_labels_.clear();
    for (const uint16_t index : this->ac_.catalog().ac_types())
      this->type_labels_.push_back(this->indexed_label_(this->ac_.catalog().type_name(index), index));
    this->set_options_(this->type_control_, this->type_labels_);
  }

  void configure_brand_control_() {
    this->brand_labels_.clear();
    for (const uint16_t index : this->ac_.catalog().ac_brands_for_type(this->selected_type_))
      this->brand_labels_.push_back(this->brand_label_(index));
    this->set_options_(this->brand_control_, this->brand_labels_);
  }

  void configure_model_control_() {
    this->model_labels_.clear();
    for (const uint16_t index : this->ac_.catalog().ac_codes_for_brand(this->selected_brand_))
      this->model_labels_.push_back(this->model_label_(index));
    this->set_options_(this->model_control_, this->model_labels_);
  }

  void reset_brand_control_() {
    this->brand_labels_ = {BRAND_PLACEHOLDER};
    this->set_options_(this->brand_control_, this->brand_labels_);
  }

  void reset_model_control_() {
    this->model_labels_ = {MODEL_PLACEHOLDER};
    this->set_options_(this->model_control_, this->model_labels_);
  }

  void configure_capability_controls_() {
    const auto &capabilities = this->ac_.capabilities();
    this->fan_labels_.clear();
    for (uint8_t speed = AC_WS_AUTO; speed < AC_WS_MAX; speed++) {
      if (!capabilities.valid || (capabilities.wind_speeds & (1U << speed)) != 0)
        this->fan_labels_.push_back(this->fan_label_(static_cast<t_ac_wind_speed>(speed)));
    }
    this->swing_labels_ = {"关闭"};
    if (!capabilities.valid || (capabilities.swing & 0x02U) != 0) this->swing_labels_.push_back("摆风");
    this->set_options_(this->fan_control_, this->fan_labels_);
    this->set_options_(this->swing_control_, this->swing_labels_);
  }

  static void set_options_(select::Select *control, const std::vector<std::string> &labels) {
    if (control == nullptr) return;
    FixedVector<const char *> options;
    options.init(labels.size());
    for (const auto &label : labels) options.push_back(label.c_str());
    control->traits.set_options(options);
  }

  std::string type_label_(uint16_t index) const {
    return this->indexed_label_(this->ac_.catalog().type_name(index), index);
  }

  std::string brand_name_(uint16_t index) const {
    uint8_t type = 0;
    std::string name;
    return this->ac_.catalog().brand(index, type, name) ? name : "未知品牌";
  }

  std::string brand_label_(uint16_t index) const { return this->indexed_label_(this->brand_name_(index), index); }

  std::string model_name_(uint16_t index) const {
    irext_adapter::CatalogCode code{};
    return this->ac_.catalog().code(index, code) ? code.name : "未知型号";
  }

  std::string model_label_(uint16_t index) const { return this->indexed_label_(this->model_name_(index), index); }

  static std::string indexed_label_(const std::string &name, uint16_t index) {
    return name + " [" + std::to_string(index) + "]";
  }

  static int parse_index_(const std::string &label) {
    const size_t opening = label.rfind('[');
    if (opening == std::string::npos || label.empty() || label.back() != ']') return -1;
    return atoi(label.substr(opening + 1, label.size() - opening - 2).c_str());
  }

  static const char *fan_label_(t_ac_wind_speed fan) {
    switch (fan) {
      case AC_WS_LOW: return "低";
      case AC_WS_MEDIUM: return "中";
      case AC_WS_HIGH: return "高";
      default: return "自动";
    }
  }

  static void publish_select_(select::Select *control, const std::string &value) {
    if (control != nullptr && control->current_option() != value) control->publish_state(value);
  }

  static void publish_switch_(switch_::Switch *control, bool value) {
    if (control != nullptr && control->state != value) control->publish_state(value);
  }

  static void publish_text_(text_sensor::TextSensor *control, const std::string &value) {
    if (control != nullptr && control->state != value) control->publish_state(value);
  }

  irext_adapter::IrextAc ac_;
  EspHomeAcStateStore store_;
  shared::AcStateManager manager_;
  select::Select *type_control_{nullptr};
  select::Select *brand_control_{nullptr};
  select::Select *model_control_{nullptr};
  select::Select *fan_control_{nullptr};
  select::Select *swing_control_{nullptr};
  switch_::Switch *power_control_{nullptr};
  text_sensor::TextSensor *status_control_{nullptr};
  std::vector<std::string> type_labels_;
  std::vector<std::string> brand_labels_;
  std::vector<std::string> model_labels_;
  std::vector<std::string> fan_labels_;
  std::vector<std::string> swing_labels_;
  LazyCallbackManager<void()> state_callbacks_;
  static constexpr uint16_t INVALID_CATALOG_INDEX = std::numeric_limits<uint16_t>::max();
  static constexpr const char *BRAND_PLACEHOLDER = "请先选择设备类型";
  static constexpr const char *MODEL_PLACEHOLDER = "请先选择品牌";
  uint16_t selected_type_{INVALID_CATALOG_INDEX};
  uint16_t selected_brand_{INVALID_CATALOG_INDEX};
  bool catalog_ready_{false};
  std::string catalog_status_{"等待加载目录"};
};

}  // namespace esphome::universal_ac_controller
