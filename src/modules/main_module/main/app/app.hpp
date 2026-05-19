#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_now.h"
#include "esp_timer.h"

#include "app/lvgl_port_wrapper.hpp"
#include "hardware/ili9341_display.hpp"
#include "hardware/sd_card.hpp"
#include "hardware/spi_bus.hpp"
#include "hardware/xpt2046_touch.hpp"
#include "ride_metrics.hpp"

#include "constants/board_pins.hpp"
#include "constants/hw_config.hpp"

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

private:
  App() = default;

  [[nodiscard]] esp_err_t init_touch_wakeup() noexcept;
  [[nodiscard]] esp_err_t init_main_event_loop() noexcept;
  [[nodiscard]] esp_err_t init_timeout_timer() noexcept;
  [[nodiscard]] esp_err_t init_wifi() noexcept;
  [[nodiscard]] esp_err_t init_espnow() noexcept;
  [[nodiscard]] esp_err_t init_speed_inactivity_timer() noexcept;
  [[nodiscard]] esp_err_t init_hardware() noexcept;

  static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                             const uint8_t *data, int data_len) noexcept;
  static void sleep_timeout_timer_cb(void *arg) noexcept;
  static void speed_inactivity_timer_cb(void *arg) noexcept;

  static void espnow_recv_handler(void *event_handler_arg,
                                  esp_event_base_t event_base, int32_t event_id,
                                  void *event_data) noexcept;
  static void sleep_timeout_handler(void *event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data) noexcept;

  esp_event_loop_handle_t main_event_loop_handle_{};
  esp_timer_handle_t timeout_timer_handle_{};
  esp_timer_handle_t speed_inactivity_timer_handle_{};
  bool initialized_{false};
  ride_metrics::RideMetrics ride_metrics_{{}};
  uint64_t last_packet_seq_num_{0};
  struct {
    hw::SpiBus spi_bus{};
    hw::Ili9341Display lcd{};
    hw::Xpt2046Touch touch{};
    hw::SdCard sd{};
  } hardware_{};
  graphics::LvglPort lvgl_{};
};

} // namespace app