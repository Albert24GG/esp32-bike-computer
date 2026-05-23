#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_settings.hpp"
#include "ble_location_service.hpp"
#include "app/lvgl_port_wrapper.hpp"
#include "hardware/ili9341_display.hpp"
#include "hardware/sd_card.hpp"
#include "hardware/spi_bus.hpp"
#include "hardware/xpt2046_touch.hpp"
#include "map_renderer.hpp"
#include "persistent_store.hpp"
#include "ride_metrics.hpp"

#include "constants/board_pins.hpp"
#include "constants/hw_config.hpp"

#include <mutex>

namespace app {

ESP_EVENT_DECLARE_BASE(APP_EVENTS);

class App {
public:
  static App &get_instance() noexcept {
    static App instance;
    return instance;
  }

  App(const App &) = delete;
  App &operator=(const App &) = delete;

  [[nodiscard]] esp_err_t init() noexcept;

  void on_reset_trip() noexcept;
  void on_exit_screen_settings() noexcept;
  void on_ble_start_pairing() noexcept;
  void on_ble_cancel_pairing() noexcept;
  void reset_sleep_timeout() noexcept;

private:
  App() = default;

  [[nodiscard]] esp_err_t init_storage() noexcept;
  [[nodiscard]] esp_err_t init_touch_wakeup() noexcept;
  [[nodiscard]] esp_err_t init_main_event_loop() noexcept;
  [[nodiscard]] esp_err_t init_timeout_timer() noexcept;
  [[nodiscard]] esp_err_t init_persistence_timer() noexcept;
  [[nodiscard]] esp_err_t init_wifi() noexcept;
  [[nodiscard]] esp_err_t init_espnow() noexcept;
  [[nodiscard]] esp_err_t init_ble_location() noexcept;
  [[nodiscard]] esp_err_t init_speed_inactivity_timer() noexcept;
  [[nodiscard]] esp_err_t init_hardware() noexcept;
  [[nodiscard]] esp_err_t init_ui() noexcept;
  [[nodiscard]] esp_err_t init_ui_update_task() noexcept;

  void apply_settings(const Settings &settings) noexcept;
  void refresh_ui_unlocked() noexcept;
  void refresh_ui_from_task() noexcept;
  void save_ride_state_if_needed(bool force) noexcept;
  [[nodiscard]] PersistentRideState persistent_ride_state_unlocked() const noexcept;

  static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                             const uint8_t *data, int data_len) noexcept;
  static void sleep_timeout_timer_cb(void *arg) noexcept;
  static void speed_inactivity_timer_cb(void *arg) noexcept;
  static void persistence_timer_cb(void *arg) noexcept;
  static void ui_update_task(void *arg) noexcept;
  static void lvgl_input_event_cb(lv_event_t *event) noexcept;
  static void ble_location_cb(const LocationCoordinates &location,
                              void *ctx) noexcept;
  static void ble_state_cb(BleLocationState state, void *ctx) noexcept;

  static void espnow_recv_handler(void *event_handler_arg,
                                  esp_event_base_t event_base, int32_t event_id,
                                  void *event_data) noexcept;
  static void sleep_timeout_handler(void *event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data) noexcept;
  static void persistence_save_handler(void *event_handler_arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data) noexcept;
  static void ble_location_handler(void *event_handler_arg,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data) noexcept;
  static void ble_state_handler(void *event_handler_arg,
                                esp_event_base_t event_base, int32_t event_id,
                                void *event_data) noexcept;

  esp_event_loop_handle_t main_event_loop_handle_{};
  esp_timer_handle_t timeout_timer_handle_{};
  esp_timer_handle_t speed_inactivity_timer_handle_{};
  esp_timer_handle_t persistence_timer_handle_{};
  TaskHandle_t ui_update_task_handle_{};
  bool initialized_{false};
  bool ui_ready_{false};
  ride_metrics::RideMetrics ride_metrics_{{}};
  BleLocationService ble_location_{};
  MapRenderer map_renderer_{};
  Settings settings_{};
  PersistentStore persistent_store_{};
  PersistentRideState loaded_ride_state_{};
  LocationCoordinates last_location_{};
  bool has_location_{false};
  mutable std::mutex state_mutex_{};
  uint64_t last_packet_seq_num_{0};
  uint64_t last_wheel_cumulative_rotations_{0};
  uint64_t last_wheel_cumulative_ride_time_us_{0};
  uint64_t unsaved_distance_mm_{0};
  uint64_t unsaved_ride_time_us_{0};
  bool ride_state_dirty_{false};
  struct {
    hw::SpiBus spi_bus{};
    hw::Ili9341Display lcd{};
    hw::Xpt2046Touch touch{};
    hw::SdCard sd{};
  } hardware_{};
  graphics::LvglPort lvgl_{};
};

} // namespace app
