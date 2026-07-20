#pragma once

#include <Arduino.h>
#include <Esp.h>
#include <vector>

#include "irext/catalog_index.h"

extern "C" {
#include "ir_decode.h"
}

namespace esphome::irext_adapter {

struct CatalogCode {
  uint16_t brand_index;
  std::string name;
  uint32_t offset;
  uint16_t length;
  uint8_t category_id;
  uint8_t subcategory;
};

class IrextCatalogProvider {
 public:
  size_t type_count() const { return irext_catalog::type_count; }
  size_t brand_count() const { return irext_catalog::brand_count; }
  size_t code_count() const { return irext_catalog::control_code_count; }

  std::string type_name(uint16_t index) const {
    if (index >= this->type_count()) return {};
    irext_catalog::TypeEntry entry{};
    memcpy_P(&entry, &irext_catalog::types[index], sizeof(entry));
    return this->read_string_(entry.name);
  }

  bool brand(uint16_t index, uint8_t &type_index, std::string &name) const {
    if (index >= this->brand_count()) return false;
    irext_catalog::BrandEntry entry{};
    memcpy_P(&entry, &irext_catalog::brands[index], sizeof(entry));
    type_index = entry.type_index;
    name = this->read_string_(entry.name);
    return true;
  }

  bool code(uint16_t index, CatalogCode &result) const {
    if (index >= this->code_count()) return false;
    irext_catalog::ControlCodeEntry entry{};
    memcpy_P(&entry, &irext_catalog::control_codes[index], sizeof(entry));
    result.brand_index = entry.brand_index;
    result.name = this->read_string_(entry.code);
    result.offset = entry.offset;
    result.length = entry.length;
    result.category_id = entry.category_id;
    result.subcategory = entry.subcategory;
    return result.offset + result.length <= irext_catalog::data_size;
  }

  std::vector<uint16_t> brands_for_type(uint16_t type_index) const {
    std::vector<uint16_t> result;
    for (uint16_t index = 0; index < this->brand_count(); index++) {
      irext_catalog::BrandEntry entry{};
      memcpy_P(&entry, &irext_catalog::brands[index], sizeof(entry));
      if (entry.type_index == type_index) result.push_back(index);
      if ((index & 0x3FU) == 0) yield();
    }
    return result;
  }

  std::vector<uint16_t> ac_types() const {
    this->ensure_ac_brand_bits_();
    std::vector<bool> ac_types(this->type_count(), false);
    for (uint16_t brand_index = 0; brand_index < this->brand_count(); brand_index++) {
      if (!this->is_ac_brand_(brand_index)) continue;
      irext_catalog::BrandEntry entry{};
      memcpy_P(&entry, &irext_catalog::brands[brand_index], sizeof(entry));
      if (entry.type_index < ac_types.size()) ac_types[entry.type_index] = true;
      if ((brand_index & 0x3FU) == 0) yield();
    }
    std::vector<uint16_t> result;
    for (uint16_t type_index = 0; type_index < this->type_count(); type_index++) {
      if (ac_types[type_index]) result.push_back(type_index);
    }
    return result;
  }

  std::vector<uint16_t> ac_brands_for_type(uint16_t type_index) const {
    this->ensure_ac_brand_bits_();
    std::vector<uint16_t> result;
    for (uint16_t brand_index = 0; brand_index < this->brand_count(); brand_index++) {
      if (!this->is_ac_brand_(brand_index)) continue;
      irext_catalog::BrandEntry entry{};
      memcpy_P(&entry, &irext_catalog::brands[brand_index], sizeof(entry));
      if (entry.type_index == type_index) result.push_back(brand_index);
      if ((brand_index & 0x3FU) == 0) yield();
    }
    return result;
  }

  std::vector<uint16_t> codes_for_brand(uint16_t brand_index) const {
    std::vector<uint16_t> result;
    for (uint16_t index = 0; index < this->code_count(); index++) {
      CatalogCode entry{};
      if (this->code(index, entry) && entry.brand_index == brand_index) result.push_back(index);
      if ((index & 0x3FU) == 0) yield();
    }
    return result;
  }

  std::vector<uint16_t> ac_codes_for_brand(uint16_t brand_index) const {
    std::vector<uint16_t> result;
    for (uint16_t code_index = 0; code_index < this->code_count(); code_index++) {
      irext_catalog::ControlCodeEntry entry{};
      memcpy_P(&entry, &irext_catalog::control_codes[code_index], sizeof(entry));
      if (entry.brand_index == brand_index && entry.category_id == REMOTE_CATEGORY_AC)
        result.push_back(code_index);
      if ((code_index & 0x3FU) == 0) yield();
    }
    return result;
  }

  bool valid_selection(uint16_t type_index, uint16_t brand_index, uint16_t code_index) const {
    uint8_t parent_type = 0;
    std::string brand_name;
    CatalogCode code_entry{};
    return type_index < this->type_count() && this->brand(brand_index, parent_type, brand_name) &&
           parent_type == type_index && this->code(code_index, code_entry) &&
           code_entry.brand_index == brand_index;
  }

  bool copy_binary(const CatalogCode &entry, std::vector<uint8_t> &binary) const {
    if (entry.offset + entry.length > irext_catalog::data_size || entry.length == 0) return false;
    binary.resize(entry.length);
    return ESP.flashRead(irext_catalog::raw_flash_offset + entry.offset, binary.data(), entry.length);
  }

 protected:
  void ensure_ac_brand_bits_() const {
    if (this->ac_brand_bits_ready_) return;
    const size_t bytes = (this->brand_count() + 7U) / 8U;
    this->ac_brand_bits_.assign(bytes, 0);
    for (uint16_t code_index = 0; code_index < this->code_count(); code_index++) {
      irext_catalog::ControlCodeEntry entry{};
      memcpy_P(&entry, &irext_catalog::control_codes[code_index], sizeof(entry));
      if (entry.category_id == REMOTE_CATEGORY_AC && entry.brand_index < this->brand_count()) {
        this->ac_brand_bits_[entry.brand_index >> 3] |= static_cast<uint8_t>(1U << (entry.brand_index & 7U));
      }
      // Feed the soft WDT while scanning ~6.5k flash index entries.
      if ((code_index & 0x3FU) == 0) yield();
    }
    this->ac_brand_bits_ready_ = true;
  }

  bool is_ac_brand_(uint16_t brand_index) const {
    if (brand_index >= this->brand_count() || this->ac_brand_bits_.empty()) return false;
    return (this->ac_brand_bits_[brand_index >> 3] & static_cast<uint8_t>(1U << (brand_index & 7U))) != 0;
  }

  static std::string read_string_(const char *flash_string) {
    if (flash_string == nullptr) return {};
    const size_t length = strlen_P(flash_string);
    std::string result(length, '\0');
    if (length != 0) memcpy_P(&result[0], flash_string, length);
    return result;
  }

  mutable std::vector<uint8_t> ac_brand_bits_{};
  mutable bool ac_brand_bits_ready_{false};
};

}  // namespace esphome::irext_adapter
