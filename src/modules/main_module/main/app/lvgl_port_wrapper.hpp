#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "hardware/ili9341_display.hpp"
#include "hardware/xpt2046_touch.hpp"

namespace app::graphics {

class LvglPort {
public:
  LvglPort() noexcept = default;

  LvglPort(const LvglPort &) = delete;
  LvglPort &operator=(const LvglPort &) = delete;

  ~LvglPort() noexcept {
    if (display_ != nullptr) {
      lvgl_port_remove_disp(display_);
    }

    if (initialized_) {
      lvgl_port_deinit();
    }
  }

  [[nodiscard]] esp_err_t init() noexcept;

  [[nodiscard]] esp_err_t add_display(hw::Ili9341Display &lcd) noexcept;

  [[nodiscard]] esp_err_t add_touch(hw::Xpt2046Touch &touch) noexcept;

  [[nodiscard]] lv_display_t *display() const { return display_; }

  void lock() const { lvgl_port_lock(0); }

  void unlock() const { lvgl_port_unlock(); }

private:
  static constexpr auto tag_ = "LvglPort";

  bool initialized_{false};
  lv_display_t *display_{nullptr};
};
} // namespace app::graphics