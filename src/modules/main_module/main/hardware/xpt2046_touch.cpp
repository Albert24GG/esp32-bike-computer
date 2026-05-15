#include "xpt2046_touch.hpp"

#include "esp_check.h"

#include "constants/board_pins.hpp"
#include "constants/hw_config.hpp"

namespace hw {

esp_err_t Xpt2046Touch::init() noexcept {
  using namespace constants::hw;

  if (initialized_) {
    ESP_LOGW(tag_, "Touch already initialized");
    return ESP_OK;
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  esp_lcd_panel_io_spi_config_t io_cfg =
      ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(pins::touchscr_cs);
#pragma GCC diagnostic pop

  io_cfg.pclk_hz = touchscr::pclk_hz;

  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(spi::host),
                               &io_cfg, &io_),
      tag_, "touch esp_lcd_new_panel_io_spi failed");

  esp_lcd_touch_config_t touch_cfg = {};
  touch_cfg.x_max = touchscr::x_max;
  touch_cfg.y_max = touchscr::y_max;
  touch_cfg.rst_gpio_num = GPIO_NUM_NC;
  touch_cfg.int_gpio_num = pins::touchscr_irq;

  touch_cfg.flags.swap_xy = touchscr::swap_xy;
  touch_cfg.flags.mirror_x = touchscr::mirror_x;
  touch_cfg.flags.mirror_y = touchscr::mirror_y;

  ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(io_, &touch_cfg, &touch_),
                      tag_, "esp_lcd_touch_new_spi_xpt2046 failed");

  initialized_ = true;
  return ESP_OK;
}

esp_err_t Xpt2046Touch::add_to_lvgl(lv_display_t *display) noexcept {
  lvgl_port_touch_cfg_t touch_cfg = {};
  touch_cfg.disp = display;
  touch_cfg.handle = touch_;

  indev_ = lvgl_port_add_touch(&touch_cfg);
  ESP_RETURN_ON_FALSE(indev_ != nullptr, ESP_FAIL, tag_,
                      "lvgl_port_add_touch failed");

  return ESP_OK;
}

} // namespace hw