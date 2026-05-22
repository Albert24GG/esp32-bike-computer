#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

namespace hw {
class Ili9341Display {
public:
  Ili9341Display() noexcept = default;

  Ili9341Display(const Ili9341Display &) = delete;
  Ili9341Display &operator=(const Ili9341Display &) = delete;

  ~Ili9341Display() {
    if (panel_ != nullptr) {
      esp_lcd_panel_del(panel_);
    }

    if (io_ != nullptr) {
      esp_lcd_panel_io_del(io_);
    }
  }

  [[nodiscard]] esp_err_t init() noexcept;

  [[nodiscard]] esp_err_t set_backlight_brightness(uint8_t percent) noexcept;

  [[nodiscard]] esp_lcd_panel_handle_t panel() const noexcept { return panel_; }

  [[nodiscard]] esp_lcd_panel_io_handle_t io() const noexcept { return io_; }

private:
  static constexpr auto tag_ = "Ili9341Display";

  esp_lcd_panel_io_handle_t io_{nullptr};
  esp_lcd_panel_handle_t panel_{nullptr};
  bool initialized_{false};
  bool backlight_pwm_initialized_{false};
};
} // namespace hw
