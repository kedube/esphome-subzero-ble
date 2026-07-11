#pragma once

// Production BleTransport — wraps the ESPHome BLEClient and forwards
// every operation to the matching `esp_ble_gattc_*` IDF call. Header-
// only because every method is a 1-liner.
//
// On-device only — pulls in ESP-IDF headers that aren't available in
// the host gtest build. Tests inject MockBleTransport instead.

#ifdef USE_ESP32

#include "ble_transport.h"

#include <cstring>
#include <vector>

#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"

#include "esphome/components/ble_client/ble_client.h"

namespace esphome {
namespace subzero_appliance {

class EspIdfTransport : public BleTransport {
public:
  void set_client(esphome::ble_client::BLEClient *c) { client_ = c; }

  bool connected() const override {
    return client_ != nullptr && client_->connected();
  }

  void connect() override {
    if (client_ != nullptr)
      client_->connect();
  }

  void disconnect() override {
    if (client_ != nullptr)
      client_->disconnect();
  }

  void request_mtu() override {
    if (client_ == nullptr)
      return;
    esp_ble_gattc_send_mtu_req(client_->get_gattc_if(), client_->get_conn_id());
  }

  void request_encryption() override {
    if (client_ == nullptr)
      return;
    std::uint8_t addr[6];
    std::memcpy(addr, client_->get_remote_bda(), 6);
    esp_ble_set_encryption(addr, ESP_BLE_SEC_ENCRYPT_MITM);
  }

  void remove_bond() override {
    if (client_ == nullptr)
      return;
    esp_bd_addr_t addr;
    std::memcpy(addr, client_->get_remote_bda(), 6);
    esp_ble_remove_bond_device(addr);
  }

  void cache_refresh() override {
    if (client_ == nullptr)
      return;
    esp_bd_addr_t addr;
    std::memcpy(addr, client_->get_remote_bda(), 6);
    esp_ble_gattc_cache_refresh(addr);
  }

  void cache_clean() override {
    if (client_ == nullptr)
      return;
    esp_bd_addr_t addr;
    std::memcpy(addr, client_->get_remote_bda(), 6);
    esp_ble_gattc_cache_clean(addr);
  }

  void search_service() override {
    if (client_ == nullptr)
      return;
    esp_ble_gattc_search_service(client_->get_gattc_if(),
                                 client_->get_conn_id(), nullptr);
  }

  std::vector<GattDbEntry> read_gatt_db() override {
    std::vector<GattDbEntry> out;
    if (client_ == nullptr)
      return out;
    // Single snapshot read with the element array on the heap — the old
    // fixed esp_gattc_db_elem_t db[64] burned ~2KB of the shared
    // main-loop stack per call. Capacity is 128 (double the old cap) for
    // headroom on large GATT tables.
    //
    // Deliberately NOT sized via esp_ble_gattc_get_attr_count(): the BTA
    // task fills the service cache asynchronously (post cache_refresh /
    // search_service), so a count-then-copy pair can capture a stale
    // smaller count and truncate the copy below the D5 characteristic.
    // One call, one snapshot, matching the original battle-tested
    // behavior — the exact-sizing "optimization" only saves a transient
    // ~4KB heap allocation during discovery, not worth the race window.
    constexpr std::uint16_t kMaxDbEntries = 128;
    std::vector<esp_gattc_db_elem_t> db(kMaxDbEntries);
    std::uint16_t count = kMaxDbEntries;
    esp_ble_gattc_get_db(client_->get_gattc_if(), client_->get_conn_id(),
                         0x0001, 0xFFFF, db.data(), &count);
    out.reserve(count);
    for (std::uint16_t i = 0; i < count; i++) {
      GattDbEntry e;
      switch (db[i].type) {
      case ESP_GATT_DB_PRIMARY_SERVICE:
      case ESP_GATT_DB_SECONDARY_SERVICE:
        e.type = GattDbEntry::kService;
        break;
      case ESP_GATT_DB_CHARACTERISTIC:
        e.type = GattDbEntry::kCharacteristic;
        break;
      case ESP_GATT_DB_DESCRIPTOR:
        e.type = GattDbEntry::kDescriptor;
        break;
      default:
        e.type = GattDbEntry::kOther;
        break;
      }
      e.uuid_is_128bit = (db[i].uuid.len == ESP_UUID_LEN_128);
      e.uuid_first_byte = e.uuid_is_128bit ? db[i].uuid.uuid.uuid128[0] : 0;
      e.handle = db[i].attribute_handle;
      out.push_back(e);
    }
    return out;
  }

  BleResult register_for_notify(std::uint16_t handle) override {
    if (client_ == nullptr)
      return BleResult::kNotConnected;
    esp_err_t err = esp_ble_gattc_register_for_notify(
        client_->get_gattc_if(), client_->get_remote_bda(), handle);
    return err == ESP_OK ? BleResult::kOk : BleResult::kFailed;
  }

  BleResult write(std::uint16_t handle, const std::uint8_t *data,
                  std::size_t len, bool ack_required = true) override {
    if (client_ == nullptr)
      return BleResult::kNotConnected;
    if (handle == 0)
      return BleResult::kInvalidHandle;
    esp_err_t err = esp_ble_gattc_write_char(
        client_->get_gattc_if(), client_->get_conn_id(), handle,
        static_cast<std::uint16_t>(len), const_cast<std::uint8_t *>(data),
        ack_required ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);
    return err == ESP_OK ? BleResult::kOk : BleResult::kFailed;
  }

private:
  esphome::ble_client::BLEClient *client_ = nullptr;
};

} // namespace subzero_appliance
} // namespace esphome

#endif // USE_ESP32
