#pragma once

#include "hub.h"

#include "../subzero_protocol/dispatch.h"
#include "../subzero_protocol/protocol.h"

#ifdef USE_ESP32
#include "../subzero_protocol/dispatch_esphome.h"
#endif

namespace esphome {
namespace subzero_appliance {

#ifdef USE_ESP32
class DishwasherHub : public SubzeroHub {
public:
  DishwasherHub() = default;
  void set_bus(esphome::subzero_protocol::DishwasherBus *bus) { bus_ = bus; }

protected:
  bool parse_and_dispatch_(std::string &msg) override {
    auto s = esphome::subzero_protocol::parse_dishwasher_in_place(
        msg, debug_mode());
    note_response_meta_(s.status, s.lacking_properties);
    if (!s.valid)
      return false;
    note_poll_response_(s.is_poll);
    log_data_keys_(s.data_keys);
    if (s.common.pin_confirmed) {
      on_pin_confirmed_(*s.common.pin_confirmed);
    }
    if (bus_ != nullptr) {
      esphome::subzero_protocol::dispatch_dishwasher(s, *bus_);
    }
    return true;
  }

private:
  esphome::subzero_protocol::DishwasherBus *bus_ = nullptr;
};
#endif

} // namespace subzero_appliance
} // namespace esphome
