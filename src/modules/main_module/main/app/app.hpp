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

  // [[nodiscard]] esp_err_t signal_timeout() noexcept;

  // TEST ONLY
  float get_current_speed_kmph() const noexcept {
    return ride_metrics_.get_current_speed_kmph();
  }

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
    hw::SpiBus spi_bus{hw::SpiBus::Config{
        .host = constants::hw::lcd::spi_host,
        .mosi = constants::hw::pins::spi_mosi,
        .miso = constants::hw::pins::spi_miso,
        .sclk = constants::hw::pins::spi_sclk,
        .max_transfer_sz = constants::hw::lcd::hres * 80 * sizeof(std::uint16_t),
    }};
    hw::Ili9341Display lcd{hw::Ili9341Display::Config{
      .host = spi_bus.host(),
      .cs = constants::hw::pins::lcd_cs,
      .dc = constants::hw::pins::lcd_dc,
      .rst = constants::hw::pins::lcd_reset,
      .backlight = constants::hw::pins::lcd_backlight,
      .hres = constants::hw::lcd::hres,
      .vres = constants::hw::lcd::vres,
      .pclk_hz = 40 * 1000 * 1000,
      .mirror_x = false,
      .mirror_y = true,
      .swap_xy = false,
      .invert_color = false,
    }};
    hw::Xpt2046Touch touch{hw::Xpt2046Touch::Config{
      .host = spi_bus.host(),
      .cs = constants::hw::pins::touchscr_cs,
      .x_max = constants::hw::lcd::hres,
      .y_max = constants::hw::lcd::vres,
      .swap_xy = false,
      .mirror_x = false,
      .mirror_y = true,
      .pclk_hz = 2 * 1000 * 1000,
    }};
    hw::SdCard sd{hw::SdCard::Config{
      .host = spi_bus.host(),
      .cs = constants::hw::pins::sd_card_cs,
      .mount_point = "/sdcard",
      .max_files = 5,
      .format_if_mount_failed = false,
    }};
  } hardware_{};
  graphics::LvglPort lvgl_{};
};

} // namespace app