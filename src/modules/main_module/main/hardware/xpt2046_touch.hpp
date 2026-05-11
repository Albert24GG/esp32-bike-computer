#pragma once

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace hw {

class Xpt2046Touch {
public:
  struct Config {
    spi_host_device_t host{};
    gpio_num_t cs{};
    gpio_num_t irq{GPIO_NUM_NC};

    int x_max{240};
    int y_max{320};

    bool swap_xy{false};
    bool mirror_x{false};
    bool mirror_y{false};

    int pclk_hz{2 * 1000 * 1000};
  };

  explicit Xpt2046Touch(Config cfg) noexcept : cfg_{cfg} {}

  Xpt2046Touch(const Xpt2046Touch &) = delete;
  Xpt2046Touch &operator=(const Xpt2046Touch &) = delete;

  ~Xpt2046Touch() {
    if (indev_ != nullptr) {
      lvgl_port_remove_touch(indev_);
    }

    if (touch_ != nullptr) {
      esp_lcd_touch_del(touch_);
    }

    if (io_ != nullptr) {
      esp_lcd_panel_io_del(io_);
    }
  }

  [[nodiscard]] esp_err_t init() noexcept;

  [[nodiscard]] esp_err_t add_to_lvgl(lv_display_t *display) noexcept;

  [[nodiscard]] esp_lcd_touch_handle_t handle() const { return touch_; }

private:
  static constexpr auto tag_ = "Xpt2046Touch";

  const Config cfg_{};
  esp_lcd_panel_io_handle_t io_{nullptr};
  esp_lcd_touch_handle_t touch_{nullptr};
  lv_indev_t *indev_{nullptr};
  bool initialized_{false};
};

} // namespace hw