#pragma once

#include "esphome/components/remote_base/remote_base.h"

namespace esphome::irext_adapter {

class IrTransmitter {
 public:
  void set_transmitter(remote_base::RemoteTransmitterBase *transmitter) { this->transmitter_ = transmitter; }

  bool send_raw(const uint16_t *timings, uint16_t count) {
    if (this->transmitter_ == nullptr || timings == nullptr || count == 0) return false;
    auto call = this->transmitter_->transmit();
    auto *data = call.get_data();
    data->reserve(count);
    data->set_carrier_frequency(38000);
    for (uint16_t index = 0; index < count; index++) {
      if ((index & 1U) == 0) data->mark(timings[index]);
      else data->space(timings[index]);
    }
    call.perform();
    return true;
  }

 protected:
  remote_base::RemoteTransmitterBase *transmitter_{nullptr};
};

}  // namespace esphome::irext_adapter