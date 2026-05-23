#pragma once

#include "esp_err.h"

#include <cstdint>

struct ble_gap_event;
struct ble_gatt_access_ctxt;
struct ble_gatt_register_ctxt;

namespace app {

struct LocationCoordinates {
  double latitude{};
  double longitude{};
  float accuracy_m{};
  uint64_t timestamp_ms{};
};

enum class BleLocationState : uint8_t {
  Idle,
  Advertising,
  Connected,
};

class BleLocationService {
public:
  using LocationCallback = void (*)(const LocationCoordinates &location,
                                    void *ctx);
  using StateCallback = void (*)(BleLocationState state, void *ctx);

  struct Config {
    LocationCallback location_callback{};
    void *location_callback_ctx{};
    StateCallback state_callback{};
    void *state_callback_ctx{};
  };

  [[nodiscard]] esp_err_t init(const Config &config) noexcept;
  [[nodiscard]] esp_err_t start_pairing() noexcept;
  [[nodiscard]] esp_err_t cancel_pairing() noexcept;

  [[nodiscard]] bool is_connected() const noexcept { return connected_; }

  static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            ble_gatt_access_ctxt *ctxt, void *arg) noexcept;
  static void gatt_register_cb(ble_gatt_register_ctxt *ctxt,
                               void *arg) noexcept;

private:
  static void on_stack_reset(int reason) noexcept;
  static void on_stack_sync() noexcept;
  static void nimble_host_task(void *param) noexcept;
  static int gap_event_handler(ble_gap_event *event, void *arg) noexcept;

  [[nodiscard]] esp_err_t start_advertising() noexcept;
  void handle_location_write(const char *payload, uint16_t len) noexcept;
  void notify_status(const char *status) noexcept;
  void notify_state(BleLocationState state) noexcept;

  Config config_{};
  uint8_t own_addr_type_{};
  uint8_t own_addr_[6]{};
  uint16_t conn_handle_{};
  bool initialized_{false};
  bool host_synced_{false};
  bool advertising_requested_{false};
  bool connected_{false};
  bool tx_subscribed_{false};
};

} // namespace app
