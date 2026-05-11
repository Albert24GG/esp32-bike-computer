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
  struct Config {
    spi_host_device_t host{};
    gpio_num_t cs{};
    gpio_num_t dc{};
    gpio_num_t rst{};
    gpio_num_t backlight{GPIO_NUM_NC};

    int hres{240};
    int vres{320};

    int pclk_hz{40 * 1000 * 1000};

    bool mirror_x{false};
    bool mirror_y{false};
    bool swap_xy{false};
    bool invert_color{false};
  };

  explicit Ili9341Display(Config cfg) noexcept : cfg_{cfg} {}

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

  [[nodiscard]] esp_lcd_panel_handle_t panel() const noexcept { return panel_; }

  [[nodiscard]] esp_lcd_panel_io_handle_t io() const noexcept { return io_; }

  [[nodiscard]] int hres() const noexcept { return cfg_.hres; }

  [[nodiscard]] int vres() const noexcept { return cfg_.vres; }

  [[nodiscard]] const Config &config() const noexcept { return cfg_; }

private:
  static constexpr auto tag_ = "Ili9341Display";

  const Config cfg_{};
  esp_lcd_panel_io_handle_t io_{nullptr};
  esp_lcd_panel_handle_t panel_{nullptr};
  bool initialized_{false};
};
} // namespace hw