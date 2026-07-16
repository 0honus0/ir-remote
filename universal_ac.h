#pragma once

#include <IRac.h>
#include <IRutils.h>
#include <utility>

class UniversalAc {
 public:
  UniversalAc() : ir_(14) {}

  void begin() {
    brand_ = "格力";
    protocol_ = "GREE";
    model_ = 1;
    selection_ = "格力_GREE_YAW1F";
    status_ = "就绪";
    update_profile_();
  }

  bool set_protocol(const std::string &protocol) {
    const size_t first_separator = protocol.find('_');
    const size_t last_separator = protocol.rfind('_');
    if (first_separator == std::string::npos || first_separator == last_separator) {
      status_ = "协议配置格式错误";
      return false;
    }
    const std::string brand = protocol.substr(0, first_separator);
    const std::string protocol_code = protocol.substr(first_separator + 1, last_separator - first_separator - 1);
    const std::string model_code = protocol.substr(last_separator + 1);
    const auto type = strToDecodeType(protocol_code.c_str());
    if (!IRac::isProtocolSupported(type)) {
      status_ = "当前固件不支持该协议";
      return false;
    }
    int16_t model = default_model_(type);
    if (model_code != "自动") {
      model = IRac::strToModel(model_code.c_str(), -1);
      if (model < 0 || irutils::modelToStr(type, model) == "Unknown") {
        status_ = "该型号不适用于当前协议";
        return false;
      }
    }
    brand_ = brand;
    protocol_ = protocol_code;
    model_ = model;
    selection_ = protocol;
    status_ = "已选择空调协议";
    update_profile_();
    return true;
  }

  void set_power(bool value) {
    power_ = value;
    if (!power_) this->clear_power_off_state_();
    send_();
  }
  void set_power_with_mode(bool value, const std::string &mode) {
    if (value) {
      if (mode == "制冷") mode_ = stdAc::opmode_t::kCool;
      else if (mode == "制热") mode_ = stdAc::opmode_t::kHeat;
      else if (mode == "除湿") mode_ = stdAc::opmode_t::kDry;
      else if (mode == "送风") mode_ = stdAc::opmode_t::kFan;
      else mode_ = stdAc::opmode_t::kAuto;
    }
    set_power(value);
  }
  void set_temperature(float value) {
    temperature_ = value < 16 ? 16 : (value > 30 ? 30 : value);
    send_();
  }
  void adjust_temperature(int delta) { set_temperature(temperature_ + delta); }
  void set_mode(const std::string &value) {
    if (value == "制冷") mode_ = stdAc::opmode_t::kCool;
    else if (value == "制热") mode_ = stdAc::opmode_t::kHeat;
    else if (value == "除湿") mode_ = stdAc::opmode_t::kDry;
    else if (value == "送风") mode_ = stdAc::opmode_t::kFan;
    else mode_ = stdAc::opmode_t::kAuto;
    send_();
  }
  void set_fan(const std::string &value) {
    if (value == "最小") fan_ = stdAc::fanspeed_t::kMin;
    else if (value == "低") fan_ = stdAc::fanspeed_t::kLow;
    else if (value == "中") fan_ = stdAc::fanspeed_t::kMedium;
    else if (value == "高") fan_ = stdAc::fanspeed_t::kHigh;
    else if (value == "最大") fan_ = stdAc::fanspeed_t::kMax;
    else fan_ = stdAc::fanspeed_t::kAuto;
    send_();
  }
  void set_swing_v(const std::string &value) {
    if (value == "最高") swing_v_ = stdAc::swingv_t::kHighest;
    else if (value == "偏高") swing_v_ = stdAc::swingv_t::kHigh;
    else if (value == "中间") swing_v_ = stdAc::swingv_t::kMiddle;
    else if (value == "偏低") swing_v_ = stdAc::swingv_t::kLow;
    else if (value == "最低") swing_v_ = stdAc::swingv_t::kLowest;
    else if (value == "中上") swing_v_ = stdAc::swingv_t::kUpperMiddle;
    else if (value == "自动") swing_v_ = stdAc::swingv_t::kAuto;
    else swing_v_ = stdAc::swingv_t::kOff;
    if (power_) send_();
  }
  void set_swing_h(const std::string &value) {
    if (value == "最左") swing_h_ = stdAc::swingh_t::kLeftMax;
    else if (value == "左") swing_h_ = stdAc::swingh_t::kLeft;
    else if (value == "中间") swing_h_ = stdAc::swingh_t::kMiddle;
    else if (value == "右") swing_h_ = stdAc::swingh_t::kRight;
    else if (value == "最右") swing_h_ = stdAc::swingh_t::kRightMax;
    else if (value == "广角") swing_h_ = stdAc::swingh_t::kWide;
    else if (value == "自动") swing_h_ = stdAc::swingh_t::kAuto;
    else swing_h_ = stdAc::swingh_t::kOff;
    send_();
  }
  void set_feature(const std::string &feature, bool value) {
    if (feature == "quiet") quiet_ = value;
    else if (feature == "turbo") turbo_ = value;
    else if (feature == "econo") econo_ = value;
    else if (feature == "light") light_ = value;
    else if (feature == "filter") filter_ = value;
    else if (feature == "clean") clean_ = value;
    else if (feature == "beep") beep_ = value;
    send_();
  }
  void set_sleep(float minutes) {
    if (minutes <= 0) {
      clear_timer_();
      return;
    }
    sleep_ = static_cast<int16_t>(minutes);
    timer_deadline_ms_ = millis() + static_cast<uint32_t>(sleep_) * 60UL * 1000UL;
  }
  void set_special_mode(const std::string &value) {
    turbo_ = value == "强力";
    sleep_mode_ = value == "睡眠";
    clean_ = value == "干燥";
    if (power_) send_();
  }
  void send() { send_(); }

  void apply_climate(bool power, stdAc::opmode_t mode, float temperature, stdAc::fanspeed_t fan,
                     stdAc::swingv_t swing_v, stdAc::swingh_t swing_h, bool quiet, bool turbo, bool econo,
                     int16_t sleep, bool sleep_mode) {
    power_ = power;
    mode_ = mode;
    temperature_ = temperature < 16 ? 16 : (temperature > 30 ? 30 : temperature);
    fan_ = fan;
    swing_v_ = swing_v;
    swing_h_ = swing_h;
    quiet_ = quiet;
    turbo_ = turbo;
    econo_ = econo;
    (void) sleep;
    sleep_mode_ = sleep_mode;
    if (!power_) this->clear_power_off_state_();
    send_();
  }

  bool power() const { return power_; }
  void set_sending_suspended(bool suspended) { sending_suspended_ = suspended; }
  uint32_t send_sequence() const { return send_sequence_; }
  float temperature() const { return temperature_; }
  int16_t sleep() const { return sleep_; }
  bool sleep_mode() const { return sleep_mode_; }
  bool timer_expired() const {
    return timer_deadline_ms_ != 0 && static_cast<int32_t>(millis() - timer_deadline_ms_) >= 0;
  }
  bool quiet() const { return quiet_; }
  bool turbo() const { return turbo_; }
  bool clean() const { return clean_; }
  bool light() const { return light_; }
  bool econo() const { return econo_; }
  const std::string &selection() const { return selection_; }
  stdAc::opmode_t mode() const { return mode_; }
  stdAc::fanspeed_t fan() const { return fan_; }
  stdAc::swingv_t swing_v() const { return swing_v_; }
  stdAc::swingh_t swing_h() const { return swing_h_; }

  bool restore(const std::string &protocol, float temperature, stdAc::opmode_t mode,
               stdAc::fanspeed_t fan, stdAc::swingv_t swing_v, bool light, bool power) {
    if (!set_protocol(protocol)) return false;
    temperature_ = temperature < 16 ? 16 : (temperature > 30 ? 30 : temperature);
    power_ = power;
    light_ = light;
    quiet_ = false;
    econo_ = false;
    filter_ = false;
    beep_ = false;
    fan_ = fan;
    mode_ = mode;

    if (!power_) this->clear_power_off_state_();
    swing_v_ = swing_v;

    status_ = "已恢复已保存的设置";
    update_profile_();
    return true;
  }

  const std::string &status() const { return status_; }
  const std::string &profile() const { return profile_; }

 protected:
  static std::string protocol_code_(const std::string &label) {
    static const std::pair<const char *, const char *> mappings[] = {
        {"格力", "GREE"}, {"美的", "MIDEA"}, {"海尔", "HAIER_AC"}, {"海尔协议_160", "HAIER_AC160"},
        {"海尔协议_176", "HAIER_AC176"}, {"海尔_YRW02", "HAIER_AC_YRW02"}, {"TCL", "TCL112AC"},
        {"科龙", "KELON"}, {"凯尔文", "KELVINATOR"}, {"酷莱斯", "COOLIX"}, {"大金", "DAIKIN"},
        {"大金_2", "DAIKIN2"}, {"大金_64", "DAIKIN64"}, {"大金_128", "DAIKIN128"},
        {"大金_152", "DAIKIN152"}, {"大金_160", "DAIKIN160"}, {"大金_176", "DAIKIN176"},
        {"大金_216", "DAIKIN216"}, {"富士通", "FUJITSU_AC"}, {"日立", "HITACHI_AC"},
        {"日立协议_1", "HITACHI_AC1"}, {"日立_264", "HITACHI_AC264"}, {"日立_296", "HITACHI_AC296"},
        {"日立_344", "HITACHI_AC344"}, {"日立_424", "HITACHI_AC424"}, {"LG", "LG"}, {"LG协议_2", "LG2"},
        {"三菱电机", "MITSUBISHI_AC"}, {"三菱电机_112", "MITSUBISHI112"}, {"三菱电机_136", "MITSUBISHI136"},
        {"三菱重工_88", "MITSUBISHI_HEAVY_88"}, {"三菱重工_152", "MITSUBISHI_HEAVY_152"},
        {"松下", "PANASONIC_AC"}, {"松下_32", "PANASONIC_AC32"}, {"三星", "SAMSUNG_AC"},
        {"夏普", "SHARP_AC"}, {"东芝", "TOSHIBA_AC"}, {"惠而浦", "WHIRLPOOL_AC"}, {"阿尔戈", "ARGO"},
        {"艾尔顿", "AIRTON"}, {"爱尔威", "AIRWELL"}, {"安可", "AMCOR"}, {"博世_144", "BOSCH144"},
        {"开利_64", "CARRIER_AC64"}, {"科罗娜", "CORONA_AC"}, {"德龙", "DELONGHI_AC"},
        {"伊可林", "ECOCLIM"}, {"伊莱克特拉", "ELECTRA_AC"}, {"好天气", "GOODWEATHER"},
        {"米拉奇", "MIRAGE"}, {"新气候", "NEOCLIMA"}, {"瑞斯", "RHOSS"}, {"三洋", "SANYO_AC"},
        {"三洋_88", "SANYO_AC88"}, {"泰克尼贝尔", "TECHNIBEL_AC"}, {"泰科", "TECO"},
        {"泰克诺点", "TEKNOPOINT"}, {"特兰斯科", "TRANSCOLD"}, {"特洛泰克", "TROTEC"},
        {"特洛泰克_3550", "TROTEC_3550"}, {"特鲁玛", "TRUMA"}, {"维斯特", "VESTEL_AC"},
        {"沃尔塔斯", "VOLTAS"}, {"约克", "YORK"}};
    for (const auto &mapping : mappings) {
      if (label == mapping.first) return mapping.second;
    }
    return "";
  }

  static std::string protocol_label_(const std::string &code) {
    static const std::pair<const char *, const char *> mappings[] = {
        {"GREE", "格力"}, {"MIDEA", "美的"}, {"HAIER_AC", "海尔"}, {"HAIER_AC160", "海尔协议_160"},
        {"HAIER_AC176", "海尔协议_176"}, {"HAIER_AC_YRW02", "海尔_YRW02"}, {"TCL112AC", "TCL"},
        {"KELON", "科龙"}, {"KELVINATOR", "凯尔文"}, {"COOLIX", "酷莱斯"}, {"DAIKIN", "大金"},
        {"DAIKIN2", "大金_2"}, {"DAIKIN64", "大金_64"}, {"DAIKIN128", "大金_128"},
        {"DAIKIN152", "大金_152"}, {"DAIKIN160", "大金_160"}, {"DAIKIN176", "大金_176"},
        {"DAIKIN216", "大金_216"}, {"FUJITSU_AC", "富士通"}, {"HITACHI_AC", "日立"},
        {"HITACHI_AC1", "日立协议_1"}, {"HITACHI_AC264", "日立_264"}, {"HITACHI_AC296", "日立_296"},
        {"HITACHI_AC344", "日立_344"}, {"HITACHI_AC424", "日立_424"}, {"LG", "LG"}, {"LG2", "LG协议_2"},
        {"MITSUBISHI_AC", "三菱电机"}, {"MITSUBISHI112", "三菱电机_112"}, {"MITSUBISHI136", "三菱电机_136"},
        {"MITSUBISHI_HEAVY_88", "三菱重工_88"}, {"MITSUBISHI_HEAVY_152", "三菱重工_152"},
        {"PANASONIC_AC", "松下"}, {"PANASONIC_AC32", "松下_32"}, {"SAMSUNG_AC", "三星"},
        {"SHARP_AC", "夏普"}, {"TOSHIBA_AC", "东芝"}, {"WHIRLPOOL_AC", "惠而浦"}, {"ARGO", "阿尔戈"},
        {"AIRTON", "艾尔顿"}, {"AIRWELL", "爱尔威"}, {"AMCOR", "安可"}, {"BOSCH144", "博世_144"},
        {"CARRIER_AC64", "开利_64"}, {"CORONA_AC", "科罗娜"}, {"DELONGHI_AC", "德龙"},
        {"ECOCLIM", "伊可林"}, {"ELECTRA_AC", "伊莱克特拉"}, {"GOODWEATHER", "好天气"},
        {"MIRAGE", "米拉奇"}, {"NEOCLIMA", "新气候"}, {"RHOSS", "瑞斯"}, {"SANYO_AC", "三洋"},
        {"SANYO_AC88", "三洋_88"}, {"TECHNIBEL_AC", "泰克尼贝尔"}, {"TECO", "泰科"},
        {"TEKNOPOINT", "泰克诺点"}, {"TRANSCOLD", "特兰斯科"}, {"TROTEC", "特洛泰克"},
        {"TROTEC_3550", "特洛泰克_3550"}, {"TRUMA", "特鲁玛"}, {"VESTEL_AC", "维斯特"},
        {"VOLTAS", "沃尔塔斯"}, {"YORK", "约克"}};
    for (const auto &mapping : mappings) {
      if (code == mapping.first) return mapping.second;
    }
    return code;
  }

  int16_t default_model_(decode_type_t protocol) const {
    switch (protocol) {
      case decode_type_t::GREE:
      case decode_type_t::HAIER_AC176:
      case decode_type_t::HITACHI_AC1:
      case decode_type_t::LG:
      case decode_type_t::LG2:
      case decode_type_t::MIRAGE:
      case decode_type_t::PANASONIC_AC:
      case decode_type_t::SHARP_AC:
      case decode_type_t::TCL112AC:
      case decode_type_t::VOLTAS:
      case decode_type_t::WHIRLPOOL_AC:
      case decode_type_t::ARGO:
        return 1;
      default:
        return -1;
    }
  }

  void update_profile_() {
    const String model_name = irutils::modelToStr(strToDecodeType(protocol_.c_str()), model_);
    profile_ = brand_ + "_" + protocol_ + "_";
    profile_ += model_name == "Unknown" ? "自动" : model_name.c_str();
  }

  void clear_timer_() {
    sleep_ = -1;
    timer_deadline_ms_ = 0;
  }

  // Values in this group are one-shot/run-time controls. Vertical airflow is
  // intentionally retained and restored independently by ESPHome.
  void clear_power_off_state_() {
    swing_h_ = stdAc::swingh_t::kOff;
    turbo_ = false;
    sleep_mode_ = false;
    clean_ = false;
    clear_timer_();
  }

  void send_() {
    if (sending_suspended_) return;
    const auto type = strToDecodeType(protocol_.c_str());
    if (!IRac::isProtocolSupported(type)) {
      status_ = "当前固件不支持该协议";
      return;
    }
    const int16_t ir_sleep = sleep_mode_ ? 0 : -1;
    const bool sent = ir_.sendAc(type, model_, power_, mode_, temperature_, true, fan_, swing_v_, swing_h_, quiet_, turbo_,
                                 econo_, light_, filter_, clean_, beep_, ir_sleep);
    if (sent) send_sequence_++;
    status_ = sent ? "红外指令已发送" : "红外指令被协议拒绝";
    update_profile_();
  }

  IRac ir_;
  // Retained state is reconstructed by the storage-agnostic state manager.
  std::string protocol_;
  std::string brand_;
  std::string selection_;
  std::string profile_;
  int16_t model_{1};
  float temperature_{26};
  bool light_{false};
  stdAc::opmode_t mode_{stdAc::opmode_t::kCool};
  stdAc::fanspeed_t fan_{stdAc::fanspeed_t::kAuto};

  // Power-off-cleared runtime state.
  int16_t sleep_{-1};
  uint32_t timer_deadline_ms_{0};
  bool sleep_mode_{false};
  bool power_{false};
  bool turbo_{false};
  bool clean_{false};
  stdAc::swingv_t swing_v_{stdAc::swingv_t::kOff};
  stdAc::swingh_t swing_h_{stdAc::swingh_t::kOff};

  // Other protocol flags and diagnostics.
  std::string status_;
  bool sending_suspended_{false};
  bool quiet_{false};
  bool econo_{false};
  bool filter_{false};
  bool beep_{false};
  uint32_t send_sequence_{0};
};
